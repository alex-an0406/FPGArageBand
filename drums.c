#include <math.h>

#define SAMPLE_RATE 8000
#define PI 3.14159265358979323846

// Drum sound lengths in samples
#define BASS_LEN   (SAMPLE_RATE / 4)   // 0.25s = 2000 samples
#define SNARE_LEN  (SAMPLE_RATE / 5)   // 0.2s  = 1600 samples
#define HIHAT_LEN  (SAMPLE_RATE / 20)  // 0.05s = 400 samples

// KEY base address (Lightweight HPS-to-FPGA bridge)
#define KEY_BASE 0xFF200050

int main() {
    volatile int *audio_ptr = (int *)0xFF203040;
    volatile int *key_ptr   = (int *)KEY_BASE;

    // ── Reset audio FIFOs ──────────────────────────────────────────
    *(audio_ptr) = 0xC;  // clear read + write FIFOs
    *(audio_ptr) = 0x0;  // release reset

    // ── Pre-synthesize drum sounds into buffers ────────────────────

    // Bass drum: pitch-swept sine 120Hz → 40Hz, exponential decay
    int bass_l[BASS_LEN], bass_r[BASS_LEN];
    for (int i = 0; i < BASS_LEN; i++) {
        double t   = (double)i / SAMPLE_RATE;
        double env = exp(-12.0 * t);
        double f   = 120.0 * exp(log(40.0 / 120.0) * t / 0.25);
        double s   = env * sin(2.0 * PI * f * t);
        // small click transient at start
        if (i < 40) s += (1.0 - (double)i / 40.0) * 0.4;
        int sample = (int)(s * 0x7FFFFF00);
        bass_l[i] = sample;
        bass_r[i] = sample;
    }

    // Snare: 200Hz tone + white noise, fast decay
    int snare_l[SNARE_LEN], snare_r[SNARE_LEN];
    // LCG random for noise (no stdlib rand needed)
    unsigned int seed = 12345;
    for (int i = 0; i < SNARE_LEN; i++) {
        double t    = (double)i / SAMPLE_RATE;
        double env  = exp(-20.0 * t);
        double tone = env * 0.5 * sin(2.0 * PI * 200.0 * t);
        seed = seed * 1664525u + 1013904223u;
        double noise = env * ((double)(int)seed / (double)0x7FFFFFFF);
        int sample = (int)((tone + noise) * 0x3FFFFFF0);
        snare_l[i] = sample;
        snare_r[i] = sample;
    }

    // Hi-hat: high-passed noise, very short
    int hihat_l[HIHAT_LEN], hihat_r[HIHAT_LEN];
    double prev = 0.0;
    for (int i = 0; i < HIHAT_LEN; i++) {
        double t   = (double)i / SAMPLE_RATE;
        double env = exp(-80.0 * t);
        seed = seed * 1664525u + 1013904223u;
        double raw = (double)(int)seed / (double)0x7FFFFFFF;
        double hp  = raw - prev * 0.85;  // 1-pole high-pass
        prev = raw;
        int sample = (int)(hp * env * 0x3FFFFFF0);
        hihat_l[i] = sample;
        hihat_r[i] = sample;
    }

    // ── Playback state ─────────────────────────────────────────────
    int  playing    = 0;
    int  play_index = 0;
    int  play_len   = 0;
    int *play_bufl  = 0;
    int *play_bufr  = 0;

    unsigned int prev_keys = 0xF;  // active-low, all released

    // ── Main loop ──────────────────────────────────────────────────
    while (1) {
        // Key edge detection
        unsigned int keys    = *key_ptr & 0xF;
        unsigned int pressed = (~keys) & prev_keys;  // newly pressed
        prev_keys = keys;

        if (pressed & 0x1) {          // KEY[0] → Bass drum
            playing    = 1;
            play_index = 0;
            play_len   = BASS_LEN;
            play_bufl  = bass_l;
            play_bufr  = bass_r;
        } else if (pressed & 0x2) {   // KEY[1] → Snare
            playing    = 2;
            play_index = 0;
            play_len   = SNARE_LEN;
            play_bufl  = snare_l;
            play_bufr  = snare_r;
        } else if (pressed & 0x4) {   // KEY[2] → Hi-hat
            playing    = 3;
            play_index = 0;
            play_len   = HIHAT_LEN;
            play_bufl  = hihat_l;
            play_bufr  = hihat_r;
        }

        // Check FIFO write space (same as part4.c)
        int fifos       = *(audio_ptr + 1);
        int left_write  = (fifos >> 16) & 0xFF;
        int right_write = (fifos >> 24) & 0xFF;

        if (left_write < 1 || right_write < 1)
            continue;

        // Write audio sample or silence
        if (playing && play_index < play_len) {
            *(audio_ptr + 2) = play_bufl[play_index];
            *(audio_ptr + 3) = play_bufr[play_index];
            play_index++;
            if (play_index >= play_len)
                playing = 0;
        } else {
            *(audio_ptr + 2) = 0;
            *(audio_ptr + 3) = 0;
        }
    }

    return 0;
}