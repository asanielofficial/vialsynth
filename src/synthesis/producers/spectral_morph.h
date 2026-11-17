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

#include "synth_constants.h"
#include "fourier_transform.h"
#include "futils.h"
#include "wavetable.h"
#include "fx_tables.h"
#include <cmath>
#include "line_generator.h"

namespace vital {
  static constexpr int kNumHarmonics = WaveFrame::kWaveformSize / 2 + 1;
  static constexpr mono_float kMaxFormantShift = 1.0f;
  static constexpr mono_float kMaxEvenOddFormantShift = 2.0f;
  static constexpr mono_float kMaxHarmonicScale = 4.0f;
  static constexpr mono_float kMaxInharmonicScale = 12.0f;
  static constexpr int kMaxSplitScale = 2;
  static constexpr mono_float kMaxSplitShift = 24.0f;
  static constexpr int kRandomAmplitudeStages = 16;
  static constexpr mono_float kPhaseDisperseScale = 0.05f;
  static constexpr mono_float kSkewScale = 16.0f;
  static constexpr int kMaxPolyIndex = WaveFrame::kWaveformSize / poly_float::kSize;

  static force_inline void transformAndWrapBuffer(FourierTransform* transform, mono_float* buffer) {
    transform->transformRealInverse(buffer + poly_float::kSize);

    for (int i = 0; i < poly_float::kSize; ++i) {
      buffer[i] = buffer[i + Wavetable::kWaveformSize];
      buffer[i + Wavetable::kWaveformSize + poly_float::kSize] = buffer[i + poly_float::kSize];
    }

#if DEBUG
    for (int i = 0; i < Wavetable::kWaveformSize + 2 * poly_float::kSize; ++i)
      VITAL_ASSERT(utils::isFinite(buffer[i]));
#endif
  }

  static force_inline void transformAndWrapBuffer(FourierTransform* transform, poly_float* buffer) {
    transformAndWrapBuffer(transform, (mono_float*)buffer);
  }

  static void passthroughMorph(const Wavetable::WavetableData* wavetable_data,
                               int wavetable_index, poly_float* dest, FourierTransform* transform,
                               float shift, int last_harmonic, const poly_float* data_buffer) {
    const poly_float* frequency_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* wave_start = dest + 1;
    int last_index = 2 * last_harmonic / poly_float::kSize;

    for (int i = 0; i <= last_index; ++i)
      wave_start[i] = frequency_amplitudes[i] * normalized_frequencies[i];

    for (int i = last_index + 1; i < kMaxPolyIndex; ++i)
      wave_start[i] = 0.0f;

    transformAndWrapBuffer(transform, dest);
  }

  static void shepardMorph(const Wavetable::WavetableData* wavetable_data,
                           int wavetable_index, poly_float* dest, FourierTransform* transform,
                           float shift, int last_harmonic, const poly_float* data_buffer) {
    static constexpr float kMinAmplitudeRatio = 2.0f;
    static constexpr float kMinAmplitudeAdd = 0.001f;
    const poly_float* poly_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* poly_normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* poly_wave_start = dest + 1;
    int last_index = 2 * last_harmonic / poly_float::kSize;

    float regular_amount = 1.0f - shift;
    for (int i = 0; i <= last_index; ++i) {
      poly_float value = poly_amplitudes[i] * poly_normalized_frequencies[i] * regular_amount;
      poly_wave_start[i] = value & constants::kSecondMask;
    }

    for (int i = last_index + 1; i < kMaxPolyIndex; ++i)
      poly_wave_start[i] = 0.0f;

    const mono_float* frequency_amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    const mono_float* normalized = (const mono_float*)wavetable_data->normalized_frequencies[wavetable_index];
    const mono_float* phases = (const mono_float*)wavetable_data->phases[wavetable_index];
    mono_float* wave_start = (mono_float*)(dest + 1);

    for (int i = 0; i <= last_harmonic; i += 2) {
      int real_index = 2 * i;
      int imag_index = real_index + 1;

      float fundamental_amplitude = frequency_amplitudes[real_index];
      float shepard_amplitude = frequency_amplitudes[i];
      float amplitude = fundamental_amplitude + (shepard_amplitude - fundamental_amplitude) * shift;

      float ratio = (fundamental_amplitude + kMinAmplitudeAdd) / (shepard_amplitude + kMinAmplitudeAdd);
      float real, imag;
      if (ratio < kMinAmplitudeRatio && ratio > (1.0f / kMinAmplitudeRatio)) {
        float fundamental_phase = phases[real_index] * (0.5f / kPi);
        float shepard_phase = phases[i] * (0.5f / kPi);
        float delta_phase = shepard_phase - fundamental_phase;
        int wraps = delta_phase;
        wraps = (wraps + 1) / 2;
        delta_phase -= 2.0f * wraps;

        float phase = fundamental_phase + delta_phase * shift;
        real = futils::sin(utils::mod(phase + 0.75f)[0] - 0.5f);
        imag = futils::sin(utils::mod(phase + 0.5f)[0] - 0.5f);
      }
      else {
        float fundamental_real = normalized[real_index];
        real = (normalized[i] - fundamental_real) * shift + fundamental_real;
        float fundamental_imag = normalized[real_index + 1];
        imag = (normalized[i + 1] - fundamental_imag) * shift + fundamental_imag;
      }

      wave_start[real_index] = amplitude * real;
      wave_start[imag_index] = amplitude * imag;
    }

    transformAndWrapBuffer(transform, dest);
  }

  static void wavetableSkewMorph(const Wavetable::WavetableData* wavetable_data,
                                 int wavetable_index, poly_float* dest, FourierTransform* transform,
                                 float shift, int last_harmonic, const poly_float* data_buffer) {
    mono_float* wave_start = (mono_float*)(dest + 1);

    int num_frames = wavetable_data->num_frames;
    if (num_frames <= 1) {
      passthroughMorph(wavetable_data, wavetable_index, dest, transform, shift, last_harmonic, data_buffer);
      return;
    }

    float dc_amplitude = wavetable_data->frequency_amplitudes[wavetable_index][0][0];
    float dc_real = wavetable_data->normalized_frequencies[wavetable_index][0][0];
    float dc_imag = wavetable_data->normalized_frequencies[wavetable_index][0][1];
    wave_start[0] = dc_amplitude * dc_real;
    wave_start[1] = dc_amplitude * dc_imag;

    float max_frame = kNumOscillatorWaveFrames - 1;
    float base_wavetable_t = wavetable_index / max_frame;
    for (int i = 1; i <= last_harmonic; ++i) {
      float shift_scale = futils::log2(i) / Wavetable::kFrequencyBins;
      poly_float base_value = poly_float(1.0f) - utils::mod((base_wavetable_t + shift * shift_scale) * 0.5f) * 2.0f;
      float shifted_index = (1.0f - poly_float::abs(base_value)[0]) * max_frame;
      int from_index = std::min<int>(shifted_index, num_frames - 2);
      float t = std::min(1.0f, shifted_index - from_index);
      int to_index = from_index + 1;

      int real_index = 2 * i;
      int imaginary_index = real_index + 1;
      const mono_float* from_amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[from_index];
      const mono_float* to_amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[to_index];
      float amplitude = utils::interpolate(from_amplitudes[real_index], to_amplitudes[real_index], t);

      const mono_float* from_normalized = (const mono_float*)wavetable_data->normalized_frequencies[from_index];
      const mono_float* to_normalized = (const mono_float*)wavetable_data->normalized_frequencies[to_index];
      float real = utils::interpolate(from_normalized[real_index], to_normalized[real_index], t);
      float imag = utils::interpolate(from_normalized[imaginary_index], to_normalized[imaginary_index], t);

      wave_start[real_index] = amplitude * real;
      wave_start[imaginary_index] = amplitude * imag;
    }

    for (int i = 2 * (last_harmonic + 1); i < 2 * WaveFrame::kWaveformSize; ++i)
      wave_start[i] = 0.0f;

    transformAndWrapBuffer(transform, dest);
  }

  static void phaseMorph(const Wavetable::WavetableData* wavetable_data,
                         int wavetable_index, poly_float* dest, FourierTransform* transform,
                         float phase_shift, int last_harmonic, const poly_float* data_buffer) {
    static constexpr float kCenterMorph = 24.0f;

    const poly_float* frequency_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* wave_start = dest + 1;
    int last_index = 2 * last_harmonic / poly_float::kSize;

    float offset = -(kCenterMorph - 1.0f) * (kCenterMorph - 1.0f) * phase_shift;
    poly_float value_offset(0.0f, 0.0f, 1.0f, 1.0f);
    poly_float phase_offset(0.25f, 0.0f, 0.25f, 0.0f);
    poly_float scale = 0.5f / kPi;
    for (int i = 0; i <= last_index; ++i) {
      poly_float amplitude = frequency_amplitudes[i];
      poly_float normalized = normalized_frequencies[i];
      poly_float index = value_offset + 2.0f * i;

      poly_float delta_center = (index - kCenterMorph) * (index - kCenterMorph) * phase_shift + offset;
      poly_float phase = utils::mod(delta_center * scale + phase_offset);
      poly_float shift = futils::sin1(phase);

      poly_float match_mult = normalized * shift;
      poly_float switch_mult = utils::swapStereo(normalized) * shift;
      poly_float real = match_mult - utils::swapStereo(match_mult);
      poly_float imag = switch_mult + utils::swapStereo(switch_mult);

      wave_start[i] = amplitude * utils::maskLoad(imag, real, constants::kLeftMask);
    }
    for (int i = last_index + 1; i < kMaxPolyIndex; ++i)
      wave_start[i] = 0.0f;

    transformAndWrapBuffer(transform, dest);
  }

  static void smearMorph(const Wavetable::WavetableData* wavetable_data,
                         int wavetable_index, poly_float* dest, FourierTransform* transform,
                         float smear, int last_harmonic, const poly_float* data_buffer) {
    const poly_float* frequency_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* wave_start = dest + 1;
    int last_index = 2 * last_harmonic / poly_float::kSize;

    poly_float amplitude = frequency_amplitudes[0] * (1.0f - smear);
    wave_start[0] = amplitude * normalized_frequencies[0];

    for (int i = 1; i <= last_index; ++i) {
      poly_float original_amplitude = frequency_amplitudes[i];
      amplitude = utils::interpolate(original_amplitude, amplitude, smear);

      wave_start[i] = amplitude * normalized_frequencies[i];
      amplitude *= (i + 0.25f) / i;
    }

    for (int i = last_index + 1; i < kMaxPolyIndex; ++i)
      wave_start[i] = 0.0f;

    transformAndWrapBuffer(transform, dest);
  }

  static void lowPassMorph(const Wavetable::WavetableData* wavetable_data,
                           int wavetable_index, poly_float* dest, FourierTransform* transform,
                           float cutoff_t, int last_harmonic, const poly_float* data_buffer) {
    const poly_float* frequency_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* wave_start = dest + 1;
    float cutoff = futils::pow(2.0f, (Wavetable::kFrequencyBins - 1) * cutoff_t) + 1.0f;
    int last_index = 2 * last_harmonic / poly_float::kSize;
    float poly_cutoff = std::min(last_index + 1.0f, 2.0f * cutoff / poly_float::kSize);
    last_index = std::min<int>(last_index, poly_cutoff);
    float t = poly_float::kSize * (poly_cutoff - last_index) / 2.0f;

    for (int i = 0; i <= last_index; ++i)
      wave_start[i] = frequency_amplitudes[i] * normalized_frequencies[i];

    for (int i = last_index + 1; i <= kMaxPolyIndex; ++i)
      wave_start[i] = 0.0f;

    poly_float last_mult = 1.0f;
    if (t >= 1.0f)
      last_mult = poly_float(1.0f, 1.0f, t - 1.0f, t - 1.0f);
    else
      last_mult = poly_float(t, t, 0.0f, 0.0f);

    wave_start[last_index] = wave_start[last_index] * last_mult;

    transformAndWrapBuffer(transform, dest);
  }

  static void highPassMorph(const Wavetable::WavetableData* wavetable_data,
                            int wavetable_index, poly_float* dest, FourierTransform* transform,
                            float cutoff_t, int last_harmonic, const poly_float* data_buffer) {
    const poly_float* frequency_amplitudes = wavetable_data->frequency_amplitudes[wavetable_index];
    const poly_float* normalized_frequencies = wavetable_data->normalized_frequencies[wavetable_index];

    poly_float* wave_start = dest + 1;
    float cutoff = futils::pow(2.0f, (Wavetable::kFrequencyBins - 1) * cutoff_t);
    cutoff *= (kNumHarmonics + 1.0f) / kNumHarmonics;
    int last_index = 2 * last_harmonic / poly_float::kSize;
    float poly_cutoff = std::min(last_index + 1.0f, 2.0f * cutoff / poly_float::kSize);
    int start_index = poly_cutoff;
    float t = poly_float::kSize * (poly_cutoff - start_index) / 2.0f;

    for (int i = 0; i < start_index; ++i)
      wave_start[i] = 0.0f;

    for (int i = start_index; i <= last_index; ++i)
      wave_start[i] = frequency_amplitudes[i] * normalized_frequencies[i];

    for (int i = last_index + 1; i <= kMaxPolyIndex; ++i)
      wave_start[i] = 0.0f;

    poly_float last_mult = 1.0f;
    if (t >= 1.0f)
      last_mult = poly_float(0.0f, 0.0f, 2.0f - t, 2.0f - t);
    else
      last_mult = poly_float(1.0f - t, 1.0f - t, 1.0f, 1.0f);

    wave_start[start_index] = wave_start[start_index] * last_mult;

    transformAndWrapBuffer(transform, dest);
  }

  static void evenOddVocodeMorph(const Wavetable::WavetableData* wavetable_data,
                                 int wavetable_index, poly_float* dest, FourierTransform* transform,
                                 float shift, int last_harmonic, const poly_float* data_buffer) {
    mono_float* wave_start = (mono_float*)(dest + 1);
    int last_index = std::min<int>(last_harmonic, WaveFrame::kWaveformSize / (2 * shift));

    const mono_float* amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    const mono_float* normalized = (const mono_float*)wavetable_data->normalized_frequencies[wavetable_index];

    float dc_amplitude = amplitudes[0];
    wave_start[0] = dc_amplitude * normalized[0];
    wave_start[1] = dc_amplitude * normalized[1];

    for (int i = 1; i <= last_index; ++i) {
      float shifted_index = std::max(1.0f, i * shift);
      int index_start = shifted_index;
      index_start = index_start - (i + index_start) % 2;
      VITAL_ASSERT(index_start >= 0 && index_start < kNumHarmonics);

      float t = (shifted_index - index_start) * 0.5f;
      int real_index1 = 2 * index_start;
      int real_index2 = real_index1 + 4;
      float amplitude_from = amplitudes[real_index1];
      float amplitude_to = amplitudes[real_index2];
      float real_from = amplitude_from * normalized[real_index1];
      float real_to = amplitude_to * normalized[real_index2];
      float imag_from = amplitude_from * normalized[real_index1 + 1];
      float imag_to = amplitude_to * normalized[real_index2 + 1];

      VITAL_ASSERT(utils::isFinite(real_from) && utils::isFinite(real_to));
      VITAL_ASSERT(utils::isFinite(imag_from) && utils::isFinite(imag_to));

      int real_index = 2 * i;
      wave_start[real_index] = shift * utils::interpolate(real_from, real_to, t);
      wave_start[real_index + 1] = shift * utils::interpolate(imag_from, imag_to, t);
    }
    for (int i = 2 * (last_index + 1); i < WaveFrame::kWaveformSize; ++i)
      wave_start[i] = 0.0f;

    transformAndWrapBuffer(transform, dest);
  }

  static void harmonicScaleMorph(const Wavetable::WavetableData* wavetable_data,
                                 int wavetable_index, poly_float* dest, FourierTransform* transform,
                                 float shift, int last_harmonic, const poly_float* data_buffer) {
    mono_float* wave_start = (mono_float*)(dest + 1);
    memset(wave_start, 0, 2 * WaveFrame::kWaveformSize * sizeof(mono_float));
    int harmonics = std::min<int>(kNumHarmonics, (last_harmonic - 1) / shift + 1);

    const mono_float* amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    const mono_float* normalized = (const mono_float*)wavetable_data->normalized_frequencies[wavetable_index];

    float dc_amplitude = amplitudes[0];
    wave_start[0] = dc_amplitude * normalized[0];
    wave_start[1] = dc_amplitude * normalized[1];

    for (int i = 1; i <= harmonics; ++i) {
      float shifted_index = std::max(1.0f, (i - 1) * shift + 1);
      int dest_index = shifted_index;
      VITAL_ASSERT(dest_index >= 0 && dest_index <= kNumHarmonics);

      float t = shifted_index - dest_index;
      float real_amount = normalized[2 * i];
      float imag_amount = normalized[2 * i + 1];
      float amplitude = amplitudes[2 * i];
      float amplitude1 = (1.0f - t) * amplitude;
      float amplitude2 = t * amplitude;

      int real_index1 = 2 * dest_index;
      int imaginary_index1 = real_index1 + 1;
      wave_start[real_index1] += amplitude1 * real_amount;
      wave_start[imaginary_index1] += amplitude1 * imag_amount;

      int real_index2 = imaginary_index1 + 1;
      int imaginary_index2 = real_index2 + 1;
      wave_start[real_index2] += amplitude2 * real_amount;
      wave_start[imaginary_index2] += amplitude2 * imag_amount;
    }

    transformAndWrapBuffer(transform, dest);
  }

  static void inharmonicScaleMorph(const Wavetable::WavetableData* wavetable_data,
                                   int wavetable_index, poly_float* dest, FourierTransform* transform,
                                   float mult, int last_harmonic, const poly_float* data_buffer) {
    poly_float* poly_data_start = dest + 2 + kMaxPolyIndex;

    poly_float offset(0.0f, 2.0f, 1.0f, 3.0f);
    for (int i = 0; i <= kMaxPolyIndex / 2; ++i) {
      poly_float index = offset + i * 4;
      poly_float octave = futils::log2(index);
      poly_float power = octave * (1.0f / (Wavetable::kFrequencyBins - 1.0f));
      poly_float shift = futils::pow(mult, power);
      poly_float shifted_index = utils::max(1.0f, shift * (index - 1.0f) + 1.0f);
      poly_data_start[2 * i] = shifted_index;
      poly_data_start[2 * i + 1] = utils::swapStereo(shifted_index);
    }

    const mono_float* amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    const mono_float* normalized = (const mono_float*)wavetable_data->normalized_frequencies[wavetable_index];
    mono_float* wave_start = (mono_float*)(dest + 1);
    mono_float* index_data = (mono_float*)(poly_data_start);
    memset(wave_start, 0, WaveFrame::kWaveformSize * sizeof(mono_float));

    float dc_amplitude = amplitudes[0];
    wave_start[0] = dc_amplitude * normalized[0];
    wave_start[1] = dc_amplitude * normalized[1];

    int processed_index = 1;
    for (; processed_index <= kNumHarmonics; ++processed_index) {
      int index = 2 * processed_index;
      float shifted_index = index_data[index];
      int dest_index = shifted_index;
      if (dest_index > 2 * last_harmonic)
        break;
      VITAL_ASSERT(dest_index >= 0 && dest_index <= kNumHarmonics * 2);

      float t = shifted_index - dest_index;
      float amplitude = amplitudes[index];
      float real = normalized[index];
      float imag = normalized[index + 1];
      VITAL_ASSERT(real < 10000.0f);
      VITAL_ASSERT(imag < 10000.0f);

      int real_index = 2 * dest_index;
      float value1 = (1.0f - t) * amplitude;
      wave_start[real_index] += value1 * real;
      wave_start[real_index + 1] += value1 * imag;
      float value2 = t * amplitude;
      wave_start[real_index + 2] += value2 * real;
      wave_start[real_index + 3] += value2 * imag;
    }

    transformAndWrapBuffer(transform, dest);
  }

  // What the drawable and table modes (and the upgraded Random Amplitudes) of an oscillator read: the oscillator's
  // three drawn curves, its Spectral Table, the position control and the random pattern pair. The harmonics are laid
  // on the same log axis as the effect's frequencies: harmonic 1 is the left edge, harmonic 1024 the right.
  struct OscillatorWarpContext {
    static constexpr int kRandomSize = kNumHarmonics + 1;
    static constexpr float kOctaves = 9.9658f;      // log2(20000 / 20), the span of the drawings and tables
    static constexpr float kMaxRandomGain = 8.0f;

    static float harmonicPosition(int harmonic) {
      return std::max(0.0f, std::min(std::log2(std::max(1.0f, static_cast<float>(harmonic))) * 0.1f, 1.0f));
    }

    // The drawn curve (0..1; a line generator point is upside down, so the buffer holds the value) at x (0..1).
    float curveAt(int which, float x, float default_value) const {
      const LineGenerator* curve = curves[which];
      if (curve == nullptr)
        return default_value;
      x = std::max(0.0f, std::min(x, 1.0f));
      const float* buffer = curve->getBuffer();
      float position_in_buffer = x * (curve->resolution() - 1);
      int i = std::min(static_cast<int>(position_in_buffer), curve->resolution() - 2);
      float t = position_in_buffer - i;
      return buffer[i] + (buffer[i + 1] - buffer[i]) * t;
    }

    static void fillRandom(float* out, uint32_t seed) {
      uint32_t state = 0x9E3779B9u * (seed + 1u);
      for (int i = 0; i < kRandomSize; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
      }
    }

    static uint32_t scrambleSeed(uint32_t seed) {
      uint32_t x = seed * 747796405u + 2891336453u;
      x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
      return (x >> 22u) ^ x;
    }

    // Seed number 0 is the default pattern; the dice button picks another number, which is scrambled into a seed.
    static uint32_t seedFromNumber(int number) {
      return number <= 0 ? 4u : scrambleSeed(static_cast<uint32_t>(number));
    }

    // Walks the chain of 8 patterns (derived from the seed) with the shuffle control, blending neighbours.
    void updateRandom(int seed_number, float shuffle_amount) {
      float place = std::max(0.0f, std::min(shuffle_amount, 1.0f)) * 8.0f;
      int first = std::min(static_cast<int>(place), 7);
      random_blend = place - first;
      uint32_t seed = seedFromNumber(seed_number);
      if (!random_valid || random_seed != seed || random_first != first) {
        fillRandom(random_a, seed + static_cast<uint32_t>(first) * 0x632BE5ABu);
        fillRandom(random_b, seed + static_cast<uint32_t>(first + 1) * 0x632BE5ABu);
        random_seed = seed;
        random_first = first;
        random_valid = true;
      }
    }

    const LineGenerator* curves[3] = { nullptr, nullptr, nullptr };
    const Wavetable::WavetableData* spectral_table = nullptr;
    float position = 0.5f;
    float table_shift = 0.5f;   // Spectral Table settings (the same as the Morph effect's)
    float table_gain = 0.5f;
    float table_resonance = 0.5f;
    float table_mix = 1.0f;
    float gain = 0.5f;          // the detail controls of Spectral Filter, Phaser and Flanger (as in the Morph effect)
    float density = 0.5f;
    float resonance = 0.5f;
    float depth = 0.5f;
    float comb_phase = 0.0f;
    float random_a[kRandomSize];
    float random_b[kRandomSize];
    float random_blend = 0.0f;
    uint32_t random_seed = 0;
    int random_first = -1;
    bool random_valid = false;
  };

  static force_inline const OscillatorWarpContext* warpContext(const poly_float* data_buffer) {
    return reinterpret_cast<const OscillatorWarpContext*>(data_buffer);
  }

  // Runs a complex gain over the harmonics of a frame: gain(i, amplitude, re, im) fills the multiplier of harmonic i.
  // The DC harmonic passes untouched.
  template<typename Gain>
  static void harmonicGainMorph(const Wavetable::WavetableData* wavetable_data, int wavetable_index,
                                poly_float* dest, FourierTransform* transform, int last_harmonic, Gain gain) {
    mono_float* wave_start = (mono_float*)(dest + 1);
    memset(wave_start, 0, 2 * WaveFrame::kWaveformSize * sizeof(mono_float));
    const mono_float* amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    const mono_float* normalized = (const mono_float*)wavetable_data->normalized_frequencies[wavetable_index];
    last_harmonic = std::min(last_harmonic, kNumHarmonics - 1);

    wave_start[0] = amplitudes[0] * normalized[0];
    wave_start[1] = amplitudes[0] * normalized[1];
    for (int i = 1; i <= last_harmonic; ++i) {
      float amplitude = amplitudes[2 * i];
      float re = amplitude * normalized[2 * i];
      float im = amplitude * normalized[2 * i + 1];
      float g_re = 1.0f, g_im = 0.0f;
      gain(i, amplitude, g_re, g_im);
      wave_start[2 * i] = re * g_re - im * g_im;
      wave_start[2 * i + 1] = re * g_im + im * g_re;
    }

    transformAndWrapBuffer(transform, dest);
  }

  // Random Amplitudes: the amount is how hard the pattern bites, the shuffle (in the context) picks the pattern.
  static void randomAmplitudeMorph(const Wavetable::WavetableData* wavetable_data,
                                   int wavetable_index, poly_float* dest, FourierTransform* transform,
                                   float shift, int last_harmonic, const poly_float* data_buffer) {
    const OscillatorWarpContext* context = warpContext(data_buffer);
    float a = std::max(0.0f, std::min(shift, 1.0f));
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      float r = context->random_a[i] + (context->random_b[i] - context->random_a[i]) * context->random_blend;
      float gain = (1.0f + a) * std::max(1.0f - a - a * r, 0.0f);
      const float max_gain = OscillatorWarpContext::kMaxRandomGain;
      re = std::min(gain, max_gain);
    });
  }

  // Spectral Gate: harmonics quieter than a threshold (relative to the loudest, 0 dB .. -70 dB with the amount) go.
  static void spectralGateMorph(const Wavetable::WavetableData* wavetable_data,
                                int wavetable_index, poly_float* dest, FourierTransform* transform,
                                float shift, int last_harmonic, const poly_float* data_buffer) {
    const mono_float* amplitudes = (const mono_float*)wavetable_data->frequency_amplitudes[wavetable_index];
    int last = std::min(last_harmonic, kNumHarmonics - 1);
    float peak = 0.0f;
    for (int i = 1; i <= last; ++i)
      peak = std::max(peak, amplitudes[2 * i]);
    float a = std::max(0.0f, std::min(shift, 1.0f));
    float threshold = peak * std::pow(10.0f, -70.0f * (1.0f - a) / 20.0f) + 1e-20f;
    float inverse = 1.0f / threshold;
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      re = std::max(0.0f, std::min((amplitude * inverse - 0.5f) * 2.0f, 1.0f));
    });
  }

  // Spectral Filter: the amount fades from a resonant peak (+-24 dB around the position's frequency) to the drawn
  // curve (+-48 dB, the middle is flat). The position also slides the drawing up and down the axis (+-4 octaves).
  static void spectralFilterMorph(const Wavetable::WavetableData* wavetable_data,
                                  int wavetable_index, poly_float* dest, FourierTransform* transform,
                                  float shift, int last_harmonic, const poly_float* data_buffer) {
    const OscillatorWarpContext* context = warpContext(data_buffer);
    float morph = std::max(0.0f, std::min(shift, 1.0f));
    float centre_octave = std::log2(120.0f) * context->position + std::log2(5.0f);   // 100 Hz .. 12 kHz over 20 Hz
    float shift_octaves = (context->position - 0.5f) * 8.0f;
    const float width = 0.3f * std::pow(2.0f, (context->density - 0.5f) * 4.0f);
    const float peak_db = 48.0f * context->resonance;
    const float out_gain = 2.0f * context->gain;
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      float x = OscillatorWarpContext::harmonicPosition(i);
      float octave = x * OscillatorWarpContext::kOctaves;
      float octaves = (octave - centre_octave) / width;
      float automatic = 2.0f * peak_db * std::exp(-0.5f * octaves * octaves) - peak_db;
      float drawn_x = (octave - shift_octaves) / OscillatorWarpContext::kOctaves;
      float drawn = (context->curveAt(0, drawn_x, 0.5f) - 0.5f) * 96.0f;
      float db = automatic + (drawn - automatic) * morph;
      re = std::min(std::pow(10.0f, db / 20.0f), 64.0f) * out_gain;
    });
  }

  // Spectral Table: the amount picks a frame of the oscillator's table of filter responses (frame 0 is flat).
  static void spectralTableMorph(const Wavetable::WavetableData* wavetable_data,
                                 int wavetable_index, poly_float* dest, FourierTransform* transform,
                                 float shift, int last_harmonic, const poly_float* data_buffer) {
    const OscillatorWarpContext* context = warpContext(data_buffer);
    float table_position = std::max(0.0f, std::min(shift, 1.0f));
    float shift_octaves = (context->table_shift - 0.5f) * 8.0f;
    float out_gain = 2.0f * context->table_gain;
    float depth = 2.0f * context->table_resonance;
    float mix = context->table_mix;
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      float octave = OscillatorWarpContext::harmonicPosition(i) * OscillatorWarpContext::kOctaves - shift_octaves;
      float db = FxTables::spectralDb(context->spectral_table, octave, table_position) * depth;
      float gain = std::min(std::pow(10.0f, db / 20.0f), 64.0f) * out_gain;
      re = 1.0f + (gain - 1.0f) * mix;
    });
  }

  // Spectral Phaser: every harmonic goes through H = (1 + e^(j phi)) / (1 - r e^(j phi)), a phase-rotating feedback
  // loop: notches where phi is half a turn, resonant peaks where it is a whole one. phi follows a curve over the
  // axis: the amount fades it from a plain log ramp (a classic phaser) to the drawing, the position is the sweep.
  static void spectralPhaserMorph(const Wavetable::WavetableData* wavetable_data,
                                  int wavetable_index, poly_float* dest, FourierTransform* transform,
                                  float shift, int last_harmonic, const poly_float* data_buffer) {
    const OscillatorWarpContext* context = warpContext(data_buffer);
    float morph = std::max(0.0f, std::min(shift, 1.0f));
    const float turns = 6.0f * std::pow(2.0f, (context->density - 0.5f) * 4.0f);
    const float feedback = std::min(0.95f, 1.4f * context->resonance);
    const float norm = std::sqrt((1.0f - feedback * feedback) * 0.5f) * 2.0f * context->gain;
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      float x = OscillatorWarpContext::harmonicPosition(i);
      float drawn = context->curveAt(1, x, x);
      float curve = x + (drawn - x) * morph;
      float phi = 2.0f * kPi * (turns * curve + context->position);
      float c = std::cos(phi), sn = std::sin(phi);
      float num_re = 1.0f + c, num_im = sn;
      float den_re = 1.0f - feedback * c, den_im = -feedback * sn;
      float den_norm = norm / (den_re * den_re + den_im * den_im);
      re = (num_re * den_re + num_im * den_im) * den_norm;
      im = (num_im * den_re - num_re * den_im) * den_norm;
    });
  }

  // Spectral Flanger: a lattice of regenerating combs across the harmonics. The position sets the spacing of the
  // notches (from a few per octave to every other harmonic), the drawing bends it along the axis, the amount says how
  // much of the drawing is used.
  static void spectralFlangerMorph(const Wavetable::WavetableData* wavetable_data,
                                   int wavetable_index, poly_float* dest, FourierTransform* transform,
                                   float shift, int last_harmonic, const poly_float* data_buffer) {
    const OscillatorWarpContext* context = warpContext(data_buffer);
    float morph = std::max(0.0f, std::min(shift, 1.0f));
    const float depth = std::min(1.0f, 1.6f * context->depth);
    const float resonance = std::min(0.95f, 1.6f * context->resonance) * depth;
    const int taps = 1 + std::min(3, static_cast<int>(context->density * 4.0f));
    const float theta_offset = 2.0f * kPi * context->comb_phase;
    const float out_gain = 2.0f * context->gain;
    const float stage_norm = std::sqrt((1.0f - resonance * resonance) / (1.0f + depth * depth));
    static const float kTapRatio[4] = { 1.0f, 1.37f, 1.83f, 2.41f };
    const float base_delay = 0.0033f * std::pow(150.0f, context->position);   // in cycles per harmonic
    harmonicGainMorph(wavetable_data, wavetable_index, dest, transform, last_harmonic,
                      [&](int i, float amplitude, float& re, float& im) {
      float x = OscillatorWarpContext::harmonicPosition(i);
      float delay = base_delay * std::exp2((context->curveAt(2, x, 0.5f) - 0.5f) * 4.0f * morph);
      float h_re = out_gain, h_im = 0.0f;
      for (int tap = 0; tap < taps; ++tap) {
        float angle = -2.0f * kPi * i * delay * kTapRatio[tap] + theta_offset;
        float cs = std::cos(angle), sn = std::sin(angle);
        float num_re = 1.0f + depth * cs, num_im = depth * sn;
        float den_re = 1.0f - resonance * cs, den_im = -resonance * sn;
        float stage = stage_norm / (den_re * den_re + den_im * den_im);
        float f_re = (num_re * den_re + num_im * den_im) * stage;
        float f_im = (num_im * den_re - num_re * den_im) * stage;
        float next_re = h_re * f_re - h_im * f_im;
        h_im = h_re * f_im + h_im * f_re;
        h_re = next_re;
      }
      re = h_re;
      im = h_im;
    });
  }
} // namespace vital
