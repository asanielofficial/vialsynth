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

#pragma once

#include "circular_queue.h"
#include "synth_module.h"
#include "note_handler.h"
#include "keytrack_voices.h"
#include "line_generator.h"

#include <memory>

class Tuning;

namespace vital {
  class Decimator;
  class PeakMeter;
  class Sample;
  class FxEffectChain;
  class StereoMemory;
  class SynthVoiceHandler;
  class SynthLfo;
  class Value;
  class ValueSwitch;
  class WaveFrame;
  class Wavetable;

  class SoundEngine : public SynthModule, public NoteHandler {
    public:
      static constexpr int kDefaultOversamplingAmount = 2;
      static constexpr int kDefaultSampleRate = 44100;

      SoundEngine();
      virtual ~SoundEngine();

      void init() override;
      void process(int num_samples) override;
      void correctToTime(double seconds) override;

      int getNumPressedNotes();
      void connectModulation(const modulation_change& change);
      void disconnectModulation(const modulation_change& change);
      int getNumActiveVoices();
      ModulationConnectionBank& getModulationBank();
      mono_float getLastActiveNote() const;

      void setTuning(const Tuning* tuning);

      void allSoundsOff() override;
      void allNotesOff(int sample) override;
      void allNotesOff(int sample, int channel) override;
      void allNotesOffRange(int sample, int from_channel, int to_channel);

      void noteOn(int note, mono_float velocity, int sample, int channel) override;
      void noteOff(int note, mono_float lift, int sample, int channel) override;
      void setModWheel(mono_float value, int channel);
      void setModWheelAllChannels(mono_float value);
      void setPitchWheel(mono_float value, int channel);
      void setZonedPitchWheel(mono_float value, int from_channel, int to_channel);
      void disableUnnecessaryModSources();
      void enableModSource(const std::string& source);
      void disableModSource(const std::string& source);
      bool isModSourceEnabled(const std::string& source);
      const StereoMemory* getEqualizerMemory(int instance = 0);

      // The effect chain: slots hold 0 or 1 + effectId (see FxEffectChain). Safe to call while audio runs.
      void setEffectChain(const int* slots, int num_slots);
      int getEffectChain(int* slots) const;
      bool morphInChain(int instance) const;

      // Drawn spectral curves of the Morph effects (lanes 2 .. 9, copy n is lane 2 + n) and of the Custom model of the
      // Other filters (lane 10 and 11: filter 1 and 2; lanes 12 .. 19: the copies of the Filter effect). Lanes 0 and 1 are
      // unused. Saved with the preset; the curves are not parameters.
      static constexpr int kNumSpectralCurves = 3;
      static constexpr int kFirstFilterLane = 2 + kMaxEffectInstances;
      static constexpr int kNumFilterCurveSets = 2 + kMaxEffectInstances;
      static constexpr int kNumDrawingLanes = kFirstFilterLane + kNumFilterCurveSets;
      static int morphLane(int instance) { return 2 + instance; }
      static int filterLane(int filter) { return kFirstFilterLane + filter; }
      static int filterFxLane(int instance) { return kFirstFilterLane + 2 + instance; }
      static bool isMorphLane(int lane) { return lane >= 2 && lane < kFirstFilterLane; }
      // Whether copy n of the Morph effect is in the effect chain (its tables are only saved with the preset then).
      bool isMorphInUse(int instance) const { return morphInChain(instance); }
      LineGenerator* getSpectralCurve(int lane, int index);
      void resetSpectralCurve(int lane, int index);
      void resetSpectralDrawings(int lane);
      void setRandomSeed(int lane, uint32_t seed);
      uint32_t getRandomSeed(int lane) const;

      // Modulation of the points of drawn lines. Every spectral curve and every LFO shape has an id; a point's x or
      // y is bound to one of the kNumPointMods slots (a control named pointmod_<slot + 1> of the modulation matrix).
      static constexpr int kNumPointMods = 32;  // = LineGenerator::kMaxPointSlots
      static constexpr int kNumPointModCurves = kNumDrawingLanes * kNumSpectralCurves + kNumLfos;
      LineGenerator* getPointModCurve(int curve_id);
      // Binds a point's axis to a free slot (or returns the slot it already has); -1 when none is left.
      int bindPointMod(int curve_id, int point, int axis);
      // Binds to a given slot (loading a preset); false if the slot is taken or the point does not exist.
      bool bindPointModToSlot(int slot, int curve_id, int point, int axis);
      void unbindPointMod(int slot);
      void unbindAllPointMods();
      int pointModCurve(int slot) const { return point_mod_curve_[slot]; }
      static std::string pointModName(int slot) { return "pointmod_" + std::to_string(slot + 1); }
      // The delay (host samples) the effects in the chain add: the Morph effects' FFT latency.
      int getLatencySamples() const;

      void setBpm(mono_float bpm);
      void setAftertouch(mono_float note, mono_float value, int sample, int channel);
      void setChannelAftertouch(int channel, mono_float value, int sample);
      void setChannelRangeAftertouch(int from_channel, int to_channel, mono_float value, int sample);
      void setChannelSlide(int channel, mono_float value, int sample);
      void setChannelRangeSlide(int from_channel, int to_channel, mono_float value, int sample);
      Wavetable* getWavetable(int index);
      Sample* getSample();
      LineGenerator* getLfoSource(int index);

      void sustainOn(int channel);
      void sustainOff(int sample, int channel);
      void sostenutoOn(int channel);
      void sostenutoOff(int sample, int channel);

      void sustainOnRange(int from_channel, int to_channel);
      void sustainOffRange(int sample, int from_channel, int to_channel);
      void sostenutoOnRange(int from_channel, int to_channel);
      void sostenutoOffRange(int sample, int from_channel, int to_channel);
      force_inline int getOversamplingAmount() const { return last_oversampling_amount_; }

      void checkOversampling();

    private:
      void applyPointModulation();
      int point_mod_curve_[kNumPointMods];
      bool point_mod_dirty_[kNumPointModCurves] = {};
      bool had_point_modulation_ = false;
      Output* point_mod_outputs_[kNumPointMods] = {};
      std::unique_ptr<Wavetable> fx_tables_[kNumFxTables];
      std::unique_ptr<LineGenerator> spectral_curves_[kNumDrawingLanes][kNumSpectralCurves];

      void setOversamplingAmount(int oversampling_amount, int sample_rate);
    
      SynthVoiceHandler* voice_handler_;
      FxEffectChain* effect_chain_;
      KeytrackNotes keytrack_notes_;
      Value* fft_size_ = nullptr;
      // The last played note, for the effects that follow it.
      Value* midi_current_ = nullptr;
      Value* midi_bent_ = nullptr;
      Add* output_total_;

      int last_oversampling_amount_;
      int last_sample_rate_;
      Value* oversampling_;
      Value* bps_;
      Value* legato_;
      Decimator* decimator_;
      PeakMeter* peak_meter_;

      CircularQueue<Processor*> modulation_processors_;

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundEngine)
  };
} // namespace vital

