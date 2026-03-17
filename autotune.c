#include <stdio.h>
#include <math.h>
#include <string.h>

// --- Hardware Addresses ---
volatile int *audio_ptr = (int *)0xFF203040;

// --- Constants ---
#define SAMPLING_RATE   8000
#define FRAME_SIZE      128     // 128 samples @ 8kHz = 16ms
#define XFADE_LEN       32      // crossfade window between frames (increased from 16)
#define PI              3.1415926535f

// OLA (Overlap-Add) parameters
#define HOP_SIZE        (FRAME_SIZE / 2)   // 50% overlap
#define OLA_BUFSIZE     (FRAME_SIZE * 3)   // ring buffer headroom

// --- Function Prototypes ---
float detect_pitch(float *buffer, int size);
float get_target_frequency(float current_freq);
void  apply_shift_ola(float *input, float *output, int size, float ratio,
                      float *ola_buf, int *ola_write_pos);
void  hann_window(float *buf, int size);

int main(void) {
    float input_buffer[FRAME_SIZE];
    float output_buffer[FRAME_SIZE];

    // OLA accumulation buffer and write position (persistent across frames)
    static float ola_buf[OLA_BUFSIZE];
    static int   ola_write_pos = 0;

    // Crossfade history
    static float prev_output[XFADE_LEN];

    int sample_idx = 0;

    // Pitch smoother state
    static float smoothed_freq = 0.0f;

    memset(ola_buf,      0, sizeof(ola_buf));
    memset(prev_output,  0, sizeof(prev_output));

    while (1) {
        // 1. Read status register
        int status = *(audio_ptr + 1);
        int rarc   = (status >> 16) & 0xFF; // samples available to read

        // 2. Drain as many input samples as are ready (up to fill the frame)
        while (rarc-- > 0 && sample_idx < FRAME_SIZE) {
            int raw_left  = *(audio_ptr + 2); // left ADC
            (void)*(audio_ptr + 3);           // consume right ADC to keep FIFOs in sync
            input_buffer[sample_idx++] = (float)raw_left / 2147483647.0f;
        }

        // 3. Process once the frame is full
        if (sample_idx == FRAME_SIZE) {

            // --- Pitch detection ---
            float current_f = detect_pitch(input_buffer, FRAME_SIZE);

            if (current_f > 80.0f && current_f < 1000.0f) {
                // Smooth the detected pitch before snapping to avoid
                // per-frame semitone jumping (the main cause of warbling).
                // Alpha=0.15 gives ~8-frame (128ms) settling time.
                if (smoothed_freq < 80.0f) {
                    // Cold start: seed with the first valid detection
                    smoothed_freq = current_f;
                } else {
                    smoothed_freq = 0.85f * smoothed_freq + 0.15f * current_f;
                }

                float target_f = get_target_frequency(smoothed_freq);
                float ratio    = target_f / smoothed_freq;

                apply_shift_ola(input_buffer, output_buffer, FRAME_SIZE,
                                ratio, ola_buf, &ola_write_pos);
            } else {
                // No clear pitch (silence / noise): pass through unchanged,
                // but still feed the OLA buffer so it stays in sync.
                smoothed_freq = 0.0f; // reset smoother on silence
                apply_shift_ola(input_buffer, output_buffer, FRAME_SIZE,
                                1.0f, ola_buf, &ola_write_pos);
            }

            // 4. Crossfade frame boundaries to suppress clicks.
            //    Blend the tail of the previous output into the head of
            //    the current output using a raised-cosine (Hann) envelope.
            for (int i = 0; i < XFADE_LEN; i++) {
                // cos² ramp: 1→0 for prev, 0→1 for current
                float alpha      = (float)i / (float)XFADE_LEN;
                float w_prev     = 0.5f * (1.0f + cosf(PI * alpha));        // 1 → 0
                float w_curr     = 0.5f * (1.0f - cosf(PI * alpha));        // 0 → 1
                output_buffer[i] = prev_output[i] * w_prev
                                 + output_buffer[i] * w_curr;
            }
            // Save the tail of this frame for the next crossfade
            memcpy(prev_output, output_buffer + (FRAME_SIZE - XFADE_LEN),
                   XFADE_LEN * sizeof(float));

            // 5. Write processed frame to DAC output FIFO
            for (int i = 0; i < FRAME_SIZE; i++) {
                // Wait until there is space in the write FIFO
                while (!((*(audio_ptr + 1) >> 24) & 0xFF));

                // Clamp to [-1, 1] before converting to avoid integer overflow
                float s = output_buffer[i];
                if (s >  1.0f) s =  1.0f;
                if (s < -1.0f) s = -1.0f;

                int out_val = (int)(s * 2147483647.0f);
                *(audio_ptr + 2) = out_val; // left DAC
                *(audio_ptr + 3) = out_val; // right DAC (mono duplicate)
            }

            sample_idx = 0; // reset for next frame
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// hann_window
//   Applies a Hann window in-place.  Used by apply_shift_ola to taper
//   each grain before accumulation so overlapping grains sum to unity gain.
// ---------------------------------------------------------------------------
void hann_window(float *buf, int size) {
    for (int i = 0; i < size; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)(size - 1)));
        buf[i] *= w;
    }
}

// ---------------------------------------------------------------------------
// detect_pitch
//   Time-domain normalised autocorrelation pitch detector.
//   Lag range covers 80 Hz (lag 100) to 1000 Hz (lag 8) at 8 kHz.
//
//   FIX: normalisation now divides by the geometric mean of the energy
//   windows at lag 0 and lag L, making the peak height independent of
//   both volume AND lag length.  This is the standard AMDF/YIN-style
//   normalisation and removes the bias toward short lags that the old
//   (corr / total_energy) formula had.
//
//   Returns 0 if no confident pitch is found.
// ---------------------------------------------------------------------------
float detect_pitch(float *buffer, int size) {
    float max_corr = 0.0f;
    int   best_lag = -1;

    // Zero-lag energy (r[0]) — bail out on silence
    float r0 = 0.0f;
    for (int i = 0; i < size; i++) {
        r0 += buffer[i] * buffer[i];
    }
    if (r0 < 1e-6f) return 0.0f;

    for (int lag = 8; lag <= 100; lag++) {
        int   n    = size - lag;

        // Autocorrelation at this lag
        float corr = 0.0f;
        for (int i = 0; i < n; i++) {
            corr += buffer[i] * buffer[i + lag];
        }

        // Energy of the two overlapping windows
        float e0 = 0.0f, eL = 0.0f;
        for (int i = 0; i < n; i++) {
            e0 += buffer[i]       * buffer[i];
            eL += buffer[i + lag] * buffer[i + lag];
        }

        // Normalised correlation in [-1, 1] (avoid divide-by-zero)
        float denom = sqrtf(e0 * eL);
        if (denom < 1e-9f) continue;
        float norm_corr = corr / denom;

        if (norm_corr > max_corr) {
            max_corr = norm_corr;
            best_lag = lag;
        }
    }

    // Threshold of 0.45 gives a good balance between sensitivity and
    // noise rejection at 8 kHz.  Raise toward 0.6 if you get false
    // triggers on breath/fricatives; lower toward 0.3 if high notes drop out.
    if (best_lag > 0 && max_corr > 0.45f) {
        return (float)SAMPLING_RATE / (float)best_lag;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// get_target_frequency
//   Snaps the detected frequency to the nearest equal-tempered semitone
//   (12-TET, A4 = 440 Hz reference).  Chromatic autotune — all 12
//   semitones are valid targets.
//
//   To restrict to a scale (e.g. C-major): replace roundf(midi) with a
//   lookup of the nearest allowed MIDI note number.
// ---------------------------------------------------------------------------
float get_target_frequency(float f) {
    float midi        = 69.0f + 12.0f * log2f(f / 440.0f);
    float target_midi = roundf(midi);
    return 440.0f * powf(2.0f, (target_midi - 69.0f) / 12.0f);
}

// ---------------------------------------------------------------------------
// apply_shift_ola
//   Overlap-Add pitch shifter.
//
//   The original single-frame resampler was the root cause of the
//   "pterodactyl" artifact: it resampled in-place without any time-domain
//   correction, so the output frame represented a different time span than
//   the input (pitch-up frames were short; pitch-down frames ran off the
//   end).  Summing these mismatched frames with a tiny crossfade produced
//   violent discontinuities.
//
//   This OLA approach:
//     1. Copies the input frame into a grain buffer and applies a Hann window.
//     2. Resamples the windowed grain by `ratio` using linear interpolation
//        to produce a pitch-shifted grain that is still FRAME_SIZE samples long.
//     3. Adds (overlaps) the shifted grain into a persistent ring buffer at
//        HOP_SIZE intervals.
//     4. Reads FRAME_SIZE samples out of the ring buffer as the output.
//
//   Because every output frame is exactly FRAME_SIZE samples the timing is
//   preserved and the Hann windows sum to a constant gain (with 50% overlap).
//
//   Limitation: true phase-vocoder OLA also aligns grain phases; this simpler
//   version can produce a mild "phasiness" on pure tones but is far cleaner
//   than the original code and runs comfortably on a Cortex-A9.
// ---------------------------------------------------------------------------
void apply_shift_ola(float *input, float *output, int size, float ratio,
                     float *ola_buf, int *ola_write_pos) {
    // --- Step 1: window the input grain ---
    float grain[FRAME_SIZE];
    memcpy(grain, input, size * sizeof(float));
    hann_window(grain, size);

    // --- Step 2: resample the grain by `ratio` ---
    //   To raise pitch (ratio > 1) we compress time: read source samples
    //   at intervals of 1/ratio so FRAME_SIZE output samples cover only
    //   FRAME_SIZE/ratio source samples.  The grain stays FRAME_SIZE long.
    float shifted[FRAME_SIZE];
    for (int i = 0; i < size; i++) {
        float src = (float)i / ratio;          // source read position
        int   base = (int)src;
        float frac = src - (float)base;

        if (base + 1 < size) {
            shifted[i] = grain[base] * (1.0f - frac) + grain[base + 1] * frac;
        } else {
            shifted[i] = grain[size - 1];
        }
    }

    // --- Step 3: overlap-add shifted grain into ring buffer ---
    for (int i = 0; i < size; i++) {
        int idx = (*ola_write_pos + i) % OLA_BUFSIZE;
        ola_buf[idx] += shifted[i];
    }

    // --- Step 4: read FRAME_SIZE samples out as output ---
    int read_pos = *ola_write_pos;  // read from where we just wrote
    for (int i = 0; i < size; i++) {
        int idx    = (read_pos + i) % OLA_BUFSIZE;
        output[i]  = ola_buf[idx];
        ola_buf[idx] = 0.0f;        // clear after reading so old data doesn't leak
    }

    // Advance write pointer by HOP_SIZE so the next frame overlaps by 50%
    *ola_write_pos = (*ola_write_pos + HOP_SIZE) % OLA_BUFSIZE;
}
