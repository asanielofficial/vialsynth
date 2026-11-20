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

#include "oscillator_module.h"

#include "synth_oscillator.h"
#include "wavetable.h"

namespace vital {

  OscillatorModule::OscillatorModule(std::string prefix) :
      SynthModule(kNumInputs, kNumOutputs), prefix_(std::move(prefix)), on_(nullptr), distortion_type_(nullptr), random_seed_(nullptr), random_dice_(nullptr) {
    wavetable_ = std::make_shared<Wavetable>(kNumOscillatorWaveFrames);
    was_on_ = std::make_shared<bool>(true);
    last_dice_ = std::make_shared<float>(-1.0f);
  }

  void OscillatorModule::init() {
    oscillator_ = new SynthOscillator(wavetable_.get());

    createBaseControl(prefix_ + "_view_2d");
    on_ = createBaseControl(prefix_ + "_on");
    Value* midi_track = createBaseControl(prefix_ + "_midi_track");
    Value* smooth_interpolation = createBaseControl(prefix_ + "_smooth_interpolation");
    Value* spectral_unison = createBaseControl(prefix_ + "_spectral_unison");
    Value* stack_style = createBaseControl(prefix_ + "_stack_style");
    Value* transpose_quantize = createBaseControl(prefix_ + "_transpose_quantize");

    Input* reset = input(kReset);

    Output* wave_frame = createPolyModControl(prefix_ + "_wave_frame");
    Output* transpose = createPolyModControl(prefix_ + "_transpose", true, false, nullptr, reset);
    Output* tune = createPolyModControl(prefix_ + "_tune", true, false, nullptr, reset);
    Output* unison_voices = createPolyModControl(prefix_ + "_unison_voices");
    Output* unison_detune = createPolyModControl(prefix_ + "_unison_detune");
    Output* detune_power = createPolyModControl(prefix_ + "_detune_power");
    Output* detune_range = createPolyModControl(prefix_ + "_detune_range");
    Output* amplitude = createPolyModControl(prefix_ + "_level", true, true, nullptr, reset);
    Output* pan = createPolyModControl(prefix_ + "_pan");
    Output* phase = createPolyModControl(prefix_ + "_phase", true, true, nullptr, reset);
    Output* distortion_phase = createPolyModControl(prefix_ + "_distortion_phase");
    Output* rand_phase = createPolyModControl(prefix_ + "_random_phase");
    Output* blend = createPolyModControl(prefix_ + "_unison_blend");
    Output* stereo_spread = createPolyModControl(prefix_ + "_stereo_spread");
    Output* frame_spread = createPolyModControl(prefix_ + "_frame_spread");
    Output* distortion_spread = createPolyModControl(prefix_ + "_distortion_spread");
    distortion_type_ = createBaseControl(prefix_ + "_distortion_type");
    Output* distortion_amount = createPolyModControl(prefix_ + "_distortion_amount");
    Output* spectral_morph_spread = createPolyModControl(prefix_ + "_spectral_morph_spread");
    Value* spectral_morph_type = createBaseControl(prefix_ + "_spectral_morph_type");
    Output* spectral_morph_amount = createPolyModControl(prefix_ + "_spectral_morph_amount");
    Output* spectral_position = createPolyModControl(prefix_ + "_spectral_position");
    Output* random_shuffle = createPolyModControl(prefix_ + "_random_shuffle");
    Value* random_seed = createBaseControl(prefix_ + "_random_seed");
    random_seed_ = random_seed;
    // The dice: whenever its value (automation or modulation included) crosses the middle, the pattern is re-rolled.
    random_dice_ = createMonoModControl(prefix_ + "_random_dice");
    Output* spectral_table_mix = createMonoModControl(prefix_ + "_spectral_table_mix");
    Output* spectral_gain = createMonoModControl(prefix_ + "_spectral_gain");
    Output* spectral_density = createMonoModControl(prefix_ + "_spectral_density");
    Output* spectral_resonance = createMonoModControl(prefix_ + "_spectral_resonance");
    Output* spectral_depth = createMonoModControl(prefix_ + "_spectral_depth");
    Output* spectral_phase = createMonoModControl(prefix_ + "_spectral_phase");
    Output* spectral_spread = createMonoModControl(prefix_ + "_spectral_spread");
    Output* table_position = createMonoModControl(prefix_ + "_table_position");
    Output* table_gain = createMonoModControl(prefix_ + "_table_gain");
    Output* table_resonance = createMonoModControl(prefix_ + "_table_resonance");
    Output* distortion_bias = createMonoModControl(prefix_ + "_distortion_bias");
    Output* distortion_smooth = createMonoModControl(prefix_ + "_distortion_smooth");
    Output* distortion_drive = createMonoModControl(prefix_ + "_distortion_drive");
    Value* distortion_dc_filter = createBaseControl(prefix_ + "_distortion_dc_filter");
    Output* distortion_mix = createMonoModControl(prefix_ + "_distortion_mix");

    oscillator_->useInput(input(kReset), SynthOscillator::kReset);
    oscillator_->useInput(input(kRetrigger), SynthOscillator::kRetrigger);
    oscillator_->useInput(input(kActiveVoices), SynthOscillator::kActiveVoices);
    oscillator_->useInput(input(kMidi), SynthOscillator::kMidiNote);
    oscillator_->plug(wave_frame, SynthOscillator::kWaveFrame);
    oscillator_->plug(midi_track, SynthOscillator::kMidiTrack);
    oscillator_->plug(smooth_interpolation, SynthOscillator::kSmoothlyInterpolate);
    oscillator_->plug(spectral_unison, SynthOscillator::kSpectralUnison);
    oscillator_->plug(transpose_quantize, SynthOscillator::kTransposeQuantize);
    oscillator_->plug(transpose, SynthOscillator::kTranspose);
    oscillator_->plug(tune, SynthOscillator::kTune);
    oscillator_->plug(stack_style, SynthOscillator::kStackStyle);
    oscillator_->plug(unison_detune, SynthOscillator::kUnisonDetune);
    oscillator_->plug(unison_voices, SynthOscillator::kUnisonVoices);
    oscillator_->plug(phase, SynthOscillator::kPhase);
    oscillator_->plug(distortion_phase, SynthOscillator::kDistortionPhase);
    oscillator_->plug(rand_phase, SynthOscillator::kRandomPhase);
    oscillator_->plug(blend, SynthOscillator::kBlend);
    oscillator_->plug(amplitude, SynthOscillator::kAmplitude);
    oscillator_->plug(pan, SynthOscillator::kPan);
    oscillator_->plug(detune_power, SynthOscillator::kDetunePower);
    oscillator_->plug(detune_range, SynthOscillator::kDetuneRange);
    oscillator_->plug(stereo_spread, SynthOscillator::kStereoSpread);
    oscillator_->plug(frame_spread, SynthOscillator::kUnisonFrameSpread);
    oscillator_->plug(distortion_spread, SynthOscillator::kUnisonDistortionSpread);
    oscillator_->plug(distortion_type_, SynthOscillator::kDistortionType);
    oscillator_->plug(distortion_amount, SynthOscillator::kDistortionAmount);
    oscillator_->plug(spectral_morph_spread, SynthOscillator::kUnisonSpectralMorphSpread);
    oscillator_->plug(spectral_morph_type, SynthOscillator::kSpectralMorphType);
    oscillator_->plug(spectral_morph_amount, SynthOscillator::kSpectralMorphAmount);
    oscillator_->plug(spectral_position, SynthOscillator::kSpectralPosition);
    oscillator_->plug(random_shuffle, SynthOscillator::kRandomShuffle);
    oscillator_->plug(random_seed, SynthOscillator::kRandomSeed);
    oscillator_->plug(spectral_table_mix, SynthOscillator::kSpectralTableMix);
    oscillator_->plug(spectral_gain, SynthOscillator::kSpectralGain);
    oscillator_->plug(spectral_density, SynthOscillator::kSpectralDensity);
    oscillator_->plug(spectral_resonance, SynthOscillator::kSpectralResonance);
    oscillator_->plug(spectral_depth, SynthOscillator::kSpectralDepth);
    oscillator_->plug(spectral_phase, SynthOscillator::kSpectralPhase);
    oscillator_->plug(spectral_spread, SynthOscillator::kSpectralSpread);
    oscillator_->plug(table_position, SynthOscillator::kTablePosition);
    oscillator_->plug(table_gain, SynthOscillator::kTableGain);
    oscillator_->plug(table_resonance, SynthOscillator::kTableResonance);
    oscillator_->plug(distortion_bias, SynthOscillator::kDistortionBias);
    oscillator_->plug(distortion_smooth, SynthOscillator::kDistortionSmooth);
    oscillator_->plug(distortion_drive, SynthOscillator::kDistortionDrive);
    oscillator_->plug(distortion_dc_filter, SynthOscillator::kDistortionDcFilter);
    oscillator_->plug(distortion_mix, SynthOscillator::kDistortionMix);
    oscillator_->useOutput(output(kRaw), SynthOscillator::kRaw);
    oscillator_->useOutput(output(kLevelled), SynthOscillator::kLevelled);

    addProcessor(oscillator_);
    SynthModule::init();
  }

  void OscillatorModule::process(int num_samples) {
    bool on = on_->value();

    if (random_dice_ && random_seed_) {
      float dice = random_dice_->buffer[0][0] >= 0.5f ? 1.0f : 0.0f;
      if (*last_dice_ >= 0.0f && dice != *last_dice_) {
        // A linear congruential step keeps re-rolls deterministic for a given sequence of flips.
        uint32_t seed = static_cast<uint32_t>(random_seed_->value() + 0.5f);
        seed = (seed * 1664525u + 1013904223u) % 100000u;
        random_seed_->set(static_cast<float>(seed));
      }
      *last_dice_ = dice;
    }

    if (on)
      SynthModule::process(num_samples);
    else if (*was_on_) {
      output(kRaw)->clearBuffer();
      output(kLevelled)->clearBuffer();
    }

    *was_on_ = on;
  }
} // namespace vital
