#include <stdio.h>
#include <math.h>
#include <string.h>

// --- Hardware Addresses ---
volatile int *audio_ptr = (int *)0xFF203040;

// --- Constants ---
#define SAMPLING_RATE   8000
#define FRAME_SIZE      128     // 128 samples @ 8kHz = 16ms
#define XFADE_LEN       16      // crossfade window between frames
#define PI              3.1415926535f

// --- Function Prototypes ---
float detect_pitch(float *buffer, int size);
float get_target_frequency(float current_freq);
void  apply_shift(float *input, float *output, int size, float ratio);

int main(void) {
    float input_buffer[FRAME_SIZE];
    float output_buffer[FRAME_SIZE];
    static float prev_output[FRAME_SIZE]; // crossfade history
    int sample_idx = 0;

    // Zero the history buffer
    memset(prev_output, 0, sizeof(prev_output));

    while (1) {
        // 1. Read status register
        int status = *(audio_ptr + 1);
        int rarc   = (status >> 16) & 0xFF; // samples available to read
        int wsrc   = (status >> 24) & 0xFF; // space available to write
        (void)wsrc; // wsrc checked per-sample during output

        // 2. Drain as many input samples as are ready (up to fill the frame)
        while (rarc-- > 0 && sample_idx < FRAME_SIZE) {
            int raw_left  = *(audio_ptr + 2); // left ADC
            (void)*(audio_ptr + 3);           // consume right ADC to keep FIFOs in sync
            input_buffer[sample_idx++] = (float)raw_left / 2147483647.0f;
        }

        // 3. Process once the frame is full
        if (sample_idx == FRAME_SIZE) {
            float current_f = detect_pitch(input_buffer, FRAME_SIZE);

            if (current_f > 80.0f && current_f < 1000.0f) {
                // Pitched signal: compute ratio and shift
                float target_f = get_target_frequency(current_f);
                float ratio    = target_f / current_f;
                apply_shift(input_buffer, output_buffer, FRAME_SIZE, ratio);
            } else {
                // No clear pitch (silence / noise): pass through unchanged
                memcpy(output_buffer, input_buffer, FRAME_SIZE * sizeof(float));
            }

            // 4. Crossfade frame boundaries to suppress clicks
            for (int i = 0; i < XFADE_LEN; i++) {
                float alpha       = (float)i / (float)XFADE_LEN;
                output_buffer[i]  = prev_output[FRAME_SIZE - XFADE_LEN + i] * (1.0f - alpha)
                                  + output_buffer[i] * alpha;
            }
            memcpy(prev_output, output_buffer, FRAME_SIZE * sizeof(float));

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
// detect_pitch
//   Time-domain autocorrelation (Yin-style normalised peak search).
//   Lag range covers 85 Hz (lag 94) to 1000 Hz (lag 8) at 8 kHz.
//   Returns 0 if no confident pitch is found.
// ---------------------------------------------------------------------------
float detect_pitch(float *buffer, int size) {
    float max_corr = 0.0f;
    int   best_lag = -1;

    // Normalise the autocorrelation by the zero-lag energy to make the
    // threshold independent of input volume.
    float energy = 0.0f;
    for (int i = 0; i < size; i++) {
        energy += buffer[i] * buffer[i];
    }
    if (energy < 1e-6f) return 0.0f; // silence — nothing to detect

    for (int lag = 8; lag < 94; lag++) {
        float corr = 0.0f;
        for (int i = 0; i < size - lag; i++) {
            corr += buffer[i] * buffer[i + lag];
        }
        // Normalise so the threshold below is volume-independent
        corr /= energy;

        if (corr > max_corr) {
            max_corr = corr;
            best_lag = lag;
        }
    }

    // Only report a pitch when the correlation is strong enough
    // (0.25 rejects most noise / unvoiced consonants)
    if (best_lag > 0 && max_corr > 0.25f) {
        return (float)SAMPLING_RATE / (float)best_lag;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// get_target_frequency
//   Snaps the detected frequency to the nearest equal-tempered semitone
//   (12-TET, A4 = 440 Hz reference).  This is chromatic autotune — all 12
//   semitones are valid targets.  To restrict to a scale (e.g. C-major)
//   add a lookup table of allowed MIDI note numbers and round to the nearest
//   allowed note instead of roundf(midi).
// ---------------------------------------------------------------------------
float get_target_frequency(float f) {
    // Convert Hz → MIDI note number (continuous)
    float midi        = 69.0f + 12.0f * log2f(f / 440.0f);
    // Snap to nearest semitone
    float target_midi = roundf(midi);
    // Convert back to Hz
    return 440.0f * powf(2.0f, (target_midi - 69.0f) / 12.0f);
}

// ---------------------------------------------------------------------------
// apply_shift
//   Resamples `input` into `output` by `ratio` using linear interpolation.
//   ratio > 1  →  pitch up   (reads input faster, fewer source samples used)
//   ratio < 1  →  pitch down (reads input slower, runs past end → clamped)
//
//   Limitation: this is a simple single-frame resampler.  For smoother
//   results on sustained notes, a full overlap-add (OLA) or phase-vocoder
//   approach should replace this function.
// ---------------------------------------------------------------------------
void apply_shift(float *input, float *output, int size, float ratio) {
    for (int i = 0; i < size; i++) {
        float read_idx = (float)i * ratio;
        int   base     = (int)read_idx;
        float frac     = read_idx - (float)base;

        if (base + 1 < size) {
            // Linear interpolation between adjacent samples
            output[i] = input[base] * (1.0f - frac) + input[base + 1] * frac;
        } else {
            // Past the end of the input frame — hold last sample
            output[i] = input[size - 1];
        }
    }
}