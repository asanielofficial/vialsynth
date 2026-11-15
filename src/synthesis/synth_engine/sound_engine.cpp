/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sound_engine.h"

#include "compressor_module.h"
#include "flanger_module.h"
#include "phaser_module.h"
#include "decimator.h"
#include "modulation_connection_processor.h"
#include "synth_constants.h"
#include "synth_voice_handler.h"
#include "peak_meter.h"
#include "operators.h"
#include "fx_effect_chain.h"
#include "fx_tables.h"
#include "morph_module.h"
#include "other_filter.h"
#include "spectral_morph_processor.h"
#include "value_switch.h"

namespace vital {

  SoundEngine::SoundEngine() : SynthModule(0, 1), voice_handler_(nullptr), effect_chain_(nullptr),
                               output_total_(nullptr), last_oversampling_amount_(-1), last_sample_rate_(-1),
                               oversampling_(nullptr), legato_(nullptr), decimator_(nullptr), peak_meter_(nullptr) {
    for (int& curve : point_mod_curve_)
      curve = -1;
    // The wavetables of the Morph effects (slots 4 and up) start as the built-in ones; the editor can replace their
    // contents. They exist before the graph, whose processors read them.
    for (int i = kNumOscillators; i < kNumFxTables; ++i) {
      fx_tables_[i] = std::make_unique<Wavetable>(kNumOscillatorWaveFrames);
      FxTables::fillWavetable(fx_tables_[i].get(), FxTables::kindOfSlot(i));
    }
    // Lanes 0 and 1 (the input and sidechain lanes of the effect-only build) are unused here, but they are saved
    // with the preset like the others, so they exist too.
    for (int lane = 0; lane < kNumDrawingLanes; ++lane) {
      if (lane >= 2 && lane < kFirstOscLane)
        continue;
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        resetSpectralCurve(lane, i);
      }
    }
    // The drawn curves of the Custom model of the Other filters exist before the graph too: every filter finds them
    // through the table that is current while the graph is built.
    const LineGenerator* filter_curve_table[kNumFilterCurveSets * kNumSpectralCurves];
    for (int set = 0; set < kNumFilterCurveSets; ++set) {
      int lane = kFirstFilterLane + set;
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        filter_curve_table[set * kNumSpectralCurves + i] = spectral_curves_[lane][i].get();
        resetSpectralCurve(lane, i);
      }
    }
    OtherFilter::CurveScope curve_scope(filter_curve_table);
    SoundEngine::init();
    bps_ = data_->controls["beats_per_minute"];
    modulation_processors_.reserve(kMaxModulationConnections);
  }

  SoundEngine::~SoundEngine() {
    voice_handler_->prepareDestroy();
  }

  void SoundEngine::init() {
    createBaseControl("bypass");
    createBaseControl("mpe_enabled");
    createBaseControl("view_spectrogram");
    oversampling_ = createBaseControl("oversampling");
    legato_ = createBaseControl("legato");

    Output* stereo_routing = createMonoModControl("stereo_routing");
    Value* stereo_mode = createBaseControl("stereo_mode");
    Output* beats_per_second = createMonoModControl("beats_per_minute");
    cr::LowerBound* beats_per_second_clamped = new cr::LowerBound(0.0f);
    beats_per_second_clamped->plug(beats_per_second);
    addProcessor(beats_per_second_clamped);

    Output* polyphony = createMonoModControl("polyphony");
    Value* voice_priority = createBaseControl("voice_priority");
    Value* voice_override = createBaseControl("voice_override");

    voice_handler_ = new SynthVoiceHandler(beats_per_second_clamped->output());
    addSubmodule(voice_handler_);
    voice_handler_->setPolyphony(vital::kMaxPolyphony);
    voice_handler_->plug(polyphony, VoiceHandler::kPolyphony);
    voice_handler_->plug(voice_priority, VoiceHandler::kVoicePriority);
    voice_handler_->plug(voice_override, VoiceHandler::kVoiceOverride);

    addProcessor(voice_handler_);

    createBaseControl("pitch_wheel");
    createBaseControl("mod_wheel");

    fft_size_ = createBaseControl("fft_size");
    midi_current_ = new Value(60.0f);
    midi_bent_ = new Value(60.0f);
    addProcessor(midi_current_);
    addProcessor(midi_bent_);

    effect_chain_ = new FxEffectChain(beats_per_second, voice_handler_->midi_offset_output(), &keytrack_notes_);
    addSubmodule(effect_chain_);
    addProcessor(effect_chain_);
    effect_chain_->plug(voice_handler_, FxEffectChain::kAudio);

    // The effects' meters and displays read these; every copy of an effect has its own.
    for (int copy = 0; copy < kMaxEffectInstances; ++copy) {
      auto id = [copy](int type) { return FxEffectChain::instanceId(type, copy); };

      SynthModule* compressor = effect_chain_->getEffect(constants::kCompressor, copy);
      std::string compressor_id = id(constants::kCompressor);
      createStatusOutput(compressor_id + "_low_input", compressor->output(CompressorModule::kLowInputMeanSquared));
      createStatusOutput(compressor_id + "_band_input", compressor->output(CompressorModule::kBandInputMeanSquared));
      createStatusOutput(compressor_id + "_high_input", compressor->output(CompressorModule::kHighInputMeanSquared));
      createStatusOutput(compressor_id + "_low_output", compressor->output(CompressorModule::kLowOutputMeanSquared));
      createStatusOutput(compressor_id + "_band_output", compressor->output(CompressorModule::kBandOutputMeanSquared));
      createStatusOutput(compressor_id + "_high_output", compressor->output(CompressorModule::kHighOutputMeanSquared));

      SynthModule* chorus = effect_chain_->getEffect(constants::kChorus, copy);
      for (int i = 0; i < ChorusModule::kMaxDelayPairs; ++i)
        createStatusOutput(id(constants::kChorus) + "_delays" + std::to_string(i + 1), chorus->output(i + 1));

      SynthModule* phaser = effect_chain_->getEffect(constants::kPhaser, copy);
      createStatusOutput(id(constants::kPhaser) + "_cutoff", phaser->output(PhaserModule::kCutoffOutput));

      SynthModule* flanger = effect_chain_->getEffect(constants::kFlanger, copy);
      createStatusOutput(id(constants::kFlanger) + "_delay_frequency",
                         flanger->output(FlangerModule::kFrequencyOutput));
    }

    for (int i = 0; i < kNumPointMods; ++i)
      point_mod_outputs_[i] = createMonoModControl(pointModName(i));
    output_total_ = new Add();
    output_total_->plug(effect_chain_, 0);
    output_total_->plug(voice_handler_->getDirectOutput(), 1);
    addProcessor(output_total_);

    decimator_ = new Decimator(3);
    decimator_->plug(output_total_);
    addProcessor(decimator_);

    StereoEncoder* decoder = new StereoEncoder(true);
    decoder->plug(decimator_, StereoEncoder::kAudio);
    decoder->plug(stereo_routing, StereoEncoder::kEncodingValue);
    decoder->plug(stereo_mode, StereoEncoder::kMode);
    addProcessor(decoder);

    Output* volume = createMonoModControl("volume");
    SmoothVolume* scaled_audio = new SmoothVolume();
    scaled_audio->plug(decoder, SmoothVolume::kAudioRate);
    scaled_audio->plug(volume, SmoothVolume::kDb);

    peak_meter_ = new PeakMeter();
    peak_meter_->plug(scaled_audio, 0);
    createStatusOutput("peak_meter", peak_meter_->output(PeakMeter::kLevel));
    createStatusOutput("peak_meter_memory", peak_meter_->output(PeakMeter::kMemoryPeak));

    Clamp* clamp = new Clamp(-2.1f, 2.1f);
    clamp->plug(scaled_audio);
    
    addProcessor(peak_meter_);
    addProcessor(scaled_audio);

    addProcessor(clamp);
    clamp->useOutput(output());

    SynthModule::init();

    effect_chain_->connectMorph(fft_size_);

    // The Morph effects draw their curves and read their tables from the engine.
    for (int instance = 0; instance < kMaxEffectInstances; ++instance) {
      int lane = morphLane(instance);
      MorphModule* morph = static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, instance));
      for (int i = 0; i < kNumSpectralCurves; ++i) {
        spectral_curves_[lane][i] = std::make_unique<LineGenerator>();
        morph->setCurve(i, spectral_curves_[lane][i].get());
      }
      morph->setTables(fx_tables_[FxTables::morphSlot(instance, false)].get(),
                       fx_tables_[FxTables::morphSlot(instance, true)].get());
      resetSpectralDrawings(lane);
    }

    effect_chain_->connectNote(midi_bent_->output(), midi_current_->output());

    // The oscillators' drawn curves and tables (the new Spectral Filter, Phaser, Flanger, Table and Distort Table modes).
    for (int osc = 0; osc < kNumOscillators; ++osc) {
      const LineGenerator* curves[kNumSpectralCurves];
      for (int i = 0; i < kNumSpectralCurves; ++i)
        curves[i] = spectral_curves_[oscLane(osc)][i].get();
      voice_handler_->setOscillatorWarp(osc, curves, fx_tables_[FxTables::oscSlot(osc, true)].get(),
                                        fx_tables_[FxTables::oscSlot(osc, false)].get());
    }

    disableUnnecessaryModSources();
    setOversamplingAmount(kDefaultOversamplingAmount, kDefaultSampleRate);
  }

  void SoundEngine::connectModulation(const modulation_change& change) {
    change.modulation_processor->plug(change.source, ModulationConnectionProcessor::kModulationInput);
    change.modulation_processor->setDestinationScale(change.destination_scale);
    VITAL_ASSERT(vital::utils::isFinite(change.destination_scale));

    Processor* destination = change.mono_destination;
    bool polyphonic = change.source->owner->isPolyphonic() && change.poly_destination;
    change.modulation_processor->setPolyphonicModulation(polyphonic);
    voice_handler_->enableModulationConnection(change.modulation_processor);
    if (polyphonic) {
      destination = change.poly_destination;
      voice_handler_->setActiveNonaccumulatedOutput(change.poly_destination->output());
    }

    if (!destination->isControlRate() && !change.source->isControlRate()) {
      change.source->owner->setControlRate(false);
      change.modulation_processor->setControlRate(false);
    }
    change.source->owner->enable(true);
    change.modulation_processor->enable(true);
    destination->plugNext(change.modulation_processor);
    change.modulation_processor->process(1);
    destination->process(1);

    change.mono_modulation_switch->set(1);
    if (change.poly_modulation_switch)
      change.poly_modulation_switch->set(1);

    modulation_processors_.push_back(change.modulation_processor);
  }

  int SoundEngine::getNumPressedNotes() {
    return voice_handler_->getNumPressedNotes();
  }

  void SoundEngine::disconnectModulation(const modulation_change& change) {
    change.modulation_processor->setDestinationScale(0.0f);

    Processor* destination = change.mono_destination;
    if (change.source->owner->isPolyphonic() && change.poly_destination)
      destination = change.poly_destination;

    destination->unplug(change.modulation_processor);
    voice_handler_->disableModulationConnection(change.modulation_processor);

    if (change.mono_destination->connectedInputs() == 1 &&
        (change.poly_destination == nullptr || change.poly_destination->connectedInputs() == 0)) {
      change.mono_modulation_switch->set(0);

      if (change.poly_modulation_switch) {
        change.poly_modulation_switch->set(0);
        voice_handler_->setInactiveNonaccumulatedOutput(change.poly_destination->output());
      }
    }

    change.modulation_processor->enable(false);
    change.modulation_processor->setControlRate(true);
    if (change.num_audio_rate == 0)
      change.source->owner->setControlRate(true);

    modulation_processors_.remove(change.modulation_processor);
  }

  int SoundEngine::getNumActiveVoices() {
    return voice_handler_->getNumActiveVoices();
  }

  ModulationConnectionBank& SoundEngine::getModulationBank() {
    return voice_handler_->getModulationBank();
  }

  mono_float SoundEngine::getLastActiveNote() const {
    return voice_handler_->getLastActiveNote();
  }

  void SoundEngine::setTuning(const Tuning* tuning) {
    voice_handler_->setTuning(tuning);
  }

  void SoundEngine::checkOversampling() {
    int oversampling = oversampling_->value();
    int oversampling_amount = 1 << oversampling;
    int sample_rate = getSampleRate();
    if (last_oversampling_amount_ != oversampling_amount || last_sample_rate_ != sample_rate)
      setOversamplingAmount(oversampling_amount, sample_rate);
  }

  void SoundEngine::setOversamplingAmount(int oversampling_amount, int sample_rate) {
    static constexpr int kBaseSampleRate = 44100;
    
    int oversample = oversampling_amount;
    int sample_rate_mult = sample_rate / kBaseSampleRate;
    while (sample_rate_mult > 1 && oversample > 1) {
      sample_rate_mult >>= 1;
      oversample >>= 1;
    }
    voice_handler_->setOversampleAmount(oversample);
    effect_chain_->setOversampleAmount(oversample);
    output_total_->setOversampleAmount(oversample);
    last_oversampling_amount_ = oversampling_amount;
    last_sample_rate_ = sample_rate;
  }

  void SoundEngine::process(int num_samples) {
    VITAL_ASSERT(num_samples <= output()->buffer_size);

    FloatVectorOperations::disableDenormalisedNumberSupport();
    voice_handler_->setLegato(legato_->value());
    midi_current_->set(static_cast<mono_float>(keytrack_notes_.lastNote()));
    midi_bent_->set(static_cast<mono_float>(keytrack_notes_.lastNote()));
    ProcessorRouter::process(num_samples);
    applyPointModulation();

    if (getNumActiveVoices() == 0) {
      CircularQueue<ModulationConnectionProcessor*>& connections = voice_handler_->enabledModulationConnection();
      for (ModulationConnectionProcessor* modulation : connections) {
        if (!modulation->isInputSourcePolyphonic())
          modulation->process(num_samples);
      }
    }

    for (auto& status_source : data_->status_outputs)
      status_source.second->update();
  }

  void SoundEngine::correctToTime(double seconds) {
    voice_handler_->correctToTime(seconds);
    effect_chain_->correctToTime(seconds);
  }

  void SoundEngine::allSoundsOff() {
    keytrack_notes_.allNotesOff();
    voice_handler_->allSoundsOff();
    effect_chain_->hardReset();
    decimator_->hardReset();
  }

  void SoundEngine::allNotesOff(int sample) {
    keytrack_notes_.allNotesOff();
    voice_handler_->allNotesOff(sample);
  }

  void SoundEngine::allNotesOff(int sample, int channel) {
    keytrack_notes_.allNotesOff();
    voice_handler_->allNotesOff(channel);
  }

  void SoundEngine::allNotesOffRange(int sample, int from_channel, int to_channel) {
    voice_handler_->allNotesOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::noteOn(int note, mono_float velocity, int sample, int channel) {
    keytrack_notes_.noteOn(note);
    voice_handler_->noteOn(note, velocity, sample, channel);
  }

  void SoundEngine::noteOff(int note, mono_float lift, int sample, int channel) {
    keytrack_notes_.noteOff(note);
    voice_handler_->noteOff(note, lift, sample, channel);
  }

  void SoundEngine::setModWheel(mono_float value, int channel) {
    voice_handler_->setModWheel(value, channel);
  }

  void SoundEngine::setModWheelAllChannels(mono_float value) {
    voice_handler_->setModWheelAllChannels(value);
  }
  
  void SoundEngine::setPitchWheel(mono_float value, int channel) {
    voice_handler_->setPitchWheel(value, channel);
  }

  void SoundEngine::setZonedPitchWheel(mono_float value, int from_channel, int to_channel) {
    voice_handler_->setZonedPitchWheel(value, from_channel, to_channel);
  }

  void SoundEngine::disableUnnecessaryModSources() {
    voice_handler_->disableUnnecessaryModSources();
  }

  void SoundEngine::enableModSource(const std::string& source) {
    getModulationSource(source)->owner->enable(true);
  }

  void SoundEngine::disableModSource(const std::string& source) {
    voice_handler_->disableModSource(source);
  }

  bool SoundEngine::isModSourceEnabled(const std::string& source) {
    return getModulationSource(source)->owner->enabled();
  }

  const StereoMemory* SoundEngine::getEqualizerMemory(int instance) {
    return effect_chain_->getEqualizerMemory(instance);
  }

  void SoundEngine::setEffectChain(const int* slots, int num_slots) {
    effect_chain_->setChain(slots, num_slots);
  }

  int SoundEngine::getEffectChain(int* slots) const {
    return effect_chain_->getChain(slots);
  }

  bool SoundEngine::morphInChain(int instance) const {
    return effect_chain_ != nullptr && effect_chain_->morphInChain(instance);
  }

  int SoundEngine::getLatencySamples() const {
    if (fft_size_ == nullptr || effect_chain_ == nullptr)
      return 0;

    return effect_chain_->morphLatencySamples(static_cast<int>(fft_size_->value() + 0.5f));
  }

  void SoundEngine::setAftertouch(mono_float note, mono_float value, int sample, int channel) {
    voice_handler_->setAftertouch(note, value, sample, channel);
  }

  void SoundEngine::setChannelAftertouch(int channel, mono_float value, int sample) {
    voice_handler_->setChannelAftertouch(channel, value, sample);
  }

  void SoundEngine::setChannelRangeAftertouch(int from_channel, int to_channel, mono_float value, int sample) {
    voice_handler_->setChannelRangeAftertouch(from_channel, to_channel, value, sample);
  }

  void SoundEngine::setChannelSlide(int channel, mono_float value, int sample) {
    voice_handler_->setChannelSlide(channel, value, sample);
  }

  void SoundEngine::setChannelRangeSlide(int from_channel, int to_channel, mono_float value, int sample) {
    voice_handler_->setChannelRangeSlide(from_channel, to_channel, value, sample);
  }

  void SoundEngine::setBpm(mono_float bpm) {
    mono_float bps = bpm / 60.0f;
    if (bps_->value() != bps)
      bps_->set(bps);
  }

  Wavetable* SoundEngine::getWavetable(int index) {
    if (index >= kNumOscillators) {
      if (index >= kNumFxTables)
        return nullptr;
      return fx_tables_[index].get();
    }
    return voice_handler_->getWavetable(index);
  }

  Sample* SoundEngine::getSample() {
    return voice_handler_->getSample();
  }

  LineGenerator* SoundEngine::getLfoSource(int index) {
    return voice_handler_->getLfoSource(index);
  }

  void SoundEngine::sustainOn(int channel) {
    keytrack_notes_.sustainOn();
    voice_handler_->sustainOn(channel);
  }

  void SoundEngine::sustainOff(int sample, int channel) {
    keytrack_notes_.sustainOff();
    voice_handler_->sustainOff(sample, channel);
  }

  void SoundEngine::sostenutoOn(int channel) {
    voice_handler_->sostenutoOn(channel);
  }

  void SoundEngine::sostenutoOff(int sample, int channel) {
    voice_handler_->sostenutoOff(sample, channel);
  }

  void SoundEngine::sustainOnRange(int from_channel, int to_channel) {
    keytrack_notes_.sustainOn();
    voice_handler_->sustainOnRange(from_channel, to_channel);
  }

  void SoundEngine::sustainOffRange(int sample, int from_channel, int to_channel) {
    keytrack_notes_.sustainOff();
    voice_handler_->sustainOffRange(sample, from_channel, to_channel);
  }

  void SoundEngine::sostenutoOnRange(int from_channel, int to_channel) {
    voice_handler_->sostenutoOnRange(from_channel, to_channel);
  }

  void SoundEngine::sostenutoOffRange(int sample, int from_channel, int to_channel) {
    voice_handler_->sostenutoOffRange(sample, from_channel, to_channel);
  }

  static_assert(SoundEngine::kNumPointMods == LineGenerator::kMaxPointSlots, "slot counts differ");

  LineGenerator* SoundEngine::getPointModCurve(int curve_id) {
    constexpr int kNumSpectral = kNumDrawingLanes * kNumSpectralCurves;
    if (curve_id < 0 || curve_id >= kNumPointModCurves)
      return nullptr;
    if (curve_id < kNumSpectral)
      return getSpectralCurve(curve_id / kNumSpectralCurves, curve_id % kNumSpectralCurves);
    return getLfoSource(curve_id - kNumSpectral);
  }

  int SoundEngine::bindPointMod(int curve_id, int point, int axis) {
    LineGenerator* curve = getPointModCurve(curve_id);
    if (curve == nullptr || point < 0 || point >= curve->getNumPoints())
      return -1;

    int existing = curve->pointSlot(point, axis);
    if (existing >= 0 && point_mod_curve_[existing] == curve_id)
      return existing;

    for (int slot = 0; slot < kNumPointMods; ++slot) {
      if (point_mod_curve_[slot] < 0) {
        point_mod_curve_[slot] = curve_id;
        curve->setPointSlot(point, axis, slot);
        return slot;
      }
    }
    return -1;
  }

  bool SoundEngine::bindPointModToSlot(int slot, int curve_id, int point, int axis) {
    LineGenerator* curve = getPointModCurve(curve_id);
    if (curve == nullptr || slot < 0 || slot >= kNumPointMods || point_mod_curve_[slot] >= 0 || point < 0 ||
        point >= curve->getNumPoints() || axis < 0 || axis >= LineGenerator::kNumAxes ||
        curve->pointSlot(point, axis) >= 0)
      return false;

    point_mod_curve_[slot] = curve_id;
    curve->setPointSlot(point, axis, slot);
    return true;
  }

  void SoundEngine::unbindPointMod(int slot) {
    if (slot < 0 || slot >= kNumPointMods || point_mod_curve_[slot] < 0)
      return;

    LineGenerator* curve = getPointModCurve(point_mod_curve_[slot]);
    point_mod_curve_[slot] = -1;
    int point, axis;
    if (curve && curve->findSlot(slot, point, axis)) {
      curve->setPointSlot(point, axis, -1);
      float zeros[kNumPointMods] = {};
      curve->applySlotValues(zeros);
    }
  }

  void SoundEngine::unbindAllPointMods() {
    for (int slot = 0; slot < kNumPointMods; ++slot)
      point_mod_curve_[slot] = -1;
    for (int id = 0; id < kNumPointModCurves; ++id) {
      if (LineGenerator* curve = getPointModCurve(id))
        curve->clearPointModulation();
    }
  }

  // Once per block, after the modulation matrix has run: moves the bound points by their slot's value.
  void SoundEngine::applyPointModulation() {
    float values[kNumPointMods];
    bool touched[kNumPointModCurves] = {};
    bool any = false;
    for (int slot = 0; slot < kNumPointMods; ++slot) {
      values[slot] = 0.0f;
      int id = point_mod_curve_[slot];
      if (id < 0)
        continue;
      LineGenerator* curve = getPointModCurve(id);
      if (curve == nullptr || !curve->hasSlot(slot)) {
        point_mod_curve_[slot] = -1;  // the point it was bound to is gone
        continue;
      }
      values[slot] = utils::clamp(point_mod_outputs_[slot]->buffer[0][0], -1.0f, 1.0f);
      touched[id] = true;
      any = true;
    }

    if (!any && !had_point_modulation_)
      return;
    for (int id = 0; id < kNumPointModCurves; ++id) {
      if (touched[id] || point_mod_dirty_[id]) {
        getPointModCurve(id)->applySlotValues(values);
        point_mod_dirty_[id] = touched[id];
      }
    }
    had_point_modulation_ = any;
  }

  // Flat gain, a rising phase ramp and a flat (x1) comb delay: the starting point of each drawable mode.
  void SoundEngine::resetSpectralCurve(int lane, int index) {
    LineGenerator* curve = spectral_curves_[lane][index].get();
    curve->initLinear();
    if (isFilterLane(lane)) {
      // Custom filter: A is a low pass, B a band pass, C a high pass (a point's y is upside down: 0 = pass).
      static const float kShapes[3][5][2] = {
        { { 0.0f, 0.0f }, { 0.35f, 0.0f }, { 0.7f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 1.0f }, { 0.35f, 1.0f }, { 0.5f, 0.0f }, { 0.65f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 1.0f }, { 0.3f, 1.0f }, { 0.65f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } } };
      int count = index == 1 ? 5 : 4;
      curve->setNumPoints(count);
      for (int i = 0; i < count; ++i) {
        curve->setPoint(i, { kShapes[index][i][0], kShapes[index][i][1] });
        curve->setPower(i, 0.0f);
      }
      curve->setSmooth(true);
      curve->render();
      return;
    }
    if (index != 1) {
      float level = 0.5f;
      curve->setNumPoints(2);
      // (a line generator point is upside down: its value is 1 - y)
      curve->setPoint(0, { 0.0f, 1.0f - level });
      curve->setPoint(1, { 1.0f, 1.0f - level });
      curve->setPower(0, 0.0f);
      curve->setPower(1, 0.0f);
    }
    curve->render();
  }

  void SoundEngine::resetSpectralDrawings(int lane) {
    for (int i = 0; i < kNumSpectralCurves; ++i)
      resetSpectralCurve(lane, i);
    setRandomSeed(lane, uint32_t(SpectralMorphProcessor::kDefaultSeed));
  }

  uint32_t SoundEngine::getRandomSeed(int lane) const {
    if (lane >= kFirstFilterLane || lane < 2 || effect_chain_ == nullptr)
      return uint32_t(SpectralMorphProcessor::kDefaultSeed);
    return static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, lane - 2))->seed();
  }

  void SoundEngine::setRandomSeed(int lane, uint32_t seed) {
    if (lane >= kFirstFilterLane || lane < 2 || effect_chain_ == nullptr)
      return;
    static_cast<MorphModule*>(effect_chain_->getEffect(constants::kMorph, lane - 2))->setSeed(seed);
  }

  LineGenerator* SoundEngine::getSpectralCurve(int lane, int index) {
    if (lane < 0 || lane >= kNumDrawingLanes || index < 0 || index >= kNumSpectralCurves)
      return nullptr;
    return spectral_curves_[lane][index].get();
  }

} // namespace vital
