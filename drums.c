/* drum_machine.c — DE1-SoC, bare-metal RISC-V
 * No math.h — all synthesis uses integer/fixed-point arithmetic only.
 * Sample rate: 8000 Hz
 * KEY[0] = Bass drum, KEY[1] = Snare, KEY[2] = Hi-hat
 */

#include "address_map.h"

#define SAMPLE_RATE 8000

/* Buffer lengths */
#define BASS_LEN  2000   /* 0.25 s */
#define SNARE_LEN 1600   /* 0.20 s */
#define HIHAT_LEN  400   /* 0.05 s */

/* ── Fixed-point helpers (16.16) ─────────────────────────────────────────
 * All trig/exp done with integer-only approximations so no libm needed.
 * ──────────────────────────────────────────────────────────────────────── */
#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)            /* 1.0 in 16.16 */
#define FP_PI    205887                      /* pi  in 16.16  (3.14159 * 65536) */
#define FP_2PI   411775                      /* 2pi in 16.16  */

/* Integer multiply keeping 16.16 format */
static inline int fp_mul(int a, int b) {
    return (int)(((long long)a * b) >> FP_SHIFT);
}

/* ── Sine approximation (Bhaskara I, input in 16.16 radians 0..2pi) ──── */
static int fp_sin(int angle) {
    /* Normalise to 0..2pi */
    while (angle < 0)       angle += FP_2PI;
    while (angle >= FP_2PI) angle -= FP_2PI;

    int sign = 1;
    if (angle >= FP_PI) { angle -= FP_PI; sign = -1; }

    /* Bhaskara I: sin(x) ≈ 16x(pi-x) / (5pi^2 - 4x(pi-x))  for x in [0,pi] */
    /* Work in 16.16; scale to avoid overflow using long long */
    long long x  = angle;                        /* 16.16 */
    long long p  = FP_PI;
    long long num = 16 * x * (p - x);            /* needs >>32 to normalise */
    long long den = 5 * p * p - 4 * x * (p - x);
    int result = (int)((num << FP_SHIFT) / den); /* back to 16.16 */
    return sign * result;
}

/* ── Exponential decay: returns FP approximation of e^(-k * i / SAMPLE_RATE)
 *    Implemented as repeated multiply: env[i] = env[i-1] * decay_factor
 *    decay_factor = (1 - k/SAMPLE_RATE) ≈ e^(-k/SAMPLE_RATE) for small k/SR
 *    For larger k we use: factor = FP_ONE - (k * FP_ONE / SAMPLE_RATE)
 * ──────────────────────────────────────────────────────────────────────── */

/* LCG noise (no stdlib rand needed) */
static unsigned int lcg_state = 12345;
static inline int lcg_rand(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (int)lcg_state;
}

/* ── Drum synthesis ──────────────────────────────────────────────────── */

static int bass_buf[BASS_LEN];
static int snare_buf[SNARE_LEN];
static int hihat_buf[HIHAT_LEN];

static void synth_bass(void) {
    /* Pitch-swept sine: starts ~120 Hz, sweeps to ~40 Hz
     * Phase accumulator approach: phase += delta each sample
     * delta_start = 2pi*120/8000, delta_end = 2pi*40/8000 (in 16.16)
     * delta_start = 6168, delta_end = 2056  (16.16 units)
     * Sweep: delta decreases linearly over BASS_LEN samples.
     * Envelope: multiplicative decay, factor per sample for ~12 Np/s decay:
     *   factor = FP_ONE - 12*FP_ONE/SAMPLE_RATE = 65536 - 98 = 65438
     */
    int phase    = 0;
    int delta    = 6168;                      /* 2pi*120/8000 in 16.16 */
    int d_step   = (6168 - 2056) / BASS_LEN; /* linear sweep step */
    int env      = FP_ONE;
    int env_dec  = 65438;                     /* per-sample decay factor */

    for (int i = 0; i < BASS_LEN; i++) {
        int s = fp_mul(env, fp_sin(phase));

        /* Click transient: add a short pulse at the very start */
        if (i < 40) {
            int click = fp_mul(FP_ONE - (i * FP_ONE / 40), FP_ONE * 2 / 5);
            s += click;
        }

        /* Scale to 32-bit audio range */
        bass_buf[i] = fp_mul(s, 0x7FFF) << 8;

        phase += delta;
        if (phase >= FP_2PI) phase -= FP_2PI;
        delta -= d_step;
        env = fp_mul(env, env_dec);
    }
}

static void synth_snare(void) {
    /* 200 Hz tone + noise, fast decay
     * delta = 2pi*200/8000 = 10280 (16.16)
     * env decay factor for ~20 Np/s: FP_ONE - 20*FP_ONE/8000 = 65372
     */
    int phase   = 0;
    int delta   = 10280;
    int env     = FP_ONE;
    int env_dec = 65372;

    for (int i = 0; i < SNARE_LEN; i++) {
        int tone  = fp_mul(env, fp_sin(phase)) >> 1;
        int noise = fp_mul(env, lcg_rand() >> 8);   /* scaled noise */
        snare_buf[i] = fp_mul(tone + noise, 0x3FFF) << 8;

        phase += delta;
        if (phase >= FP_2PI) phase -= FP_2PI;
        env = fp_mul(env, env_dec);
    }
}

static void synth_hihat(void) {
    /* Short high-passed noise burst
     * env decay factor for ~80 Np/s: FP_ONE - 80*FP_ONE/8000 = 64882
     */
    int env     = FP_ONE;
    int env_dec = 64882;
    int prev    = 0;

    for (int i = 0; i < HIHAT_LEN; i++) {
        int raw  = lcg_rand() >> 8;
        int hp   = raw - fp_mul(prev, 55705);  /* 0.85 * FP_ONE = 55705 */
        prev     = raw;
        int s    = fp_mul(env, hp);
        hihat_buf[i] = fp_mul(s, 0x3FFF) << 8;
        env = fp_mul(env, env_dec);
    }
}

/* ── Main ──────────────────────────────────────────────────────────────── */
int main(void) {
    volatile int *audio_ptr = (int *)AUDIO_BASE;
    volatile int *key_ptr   = (int *)KEY_BASE;

    /* Reset audio FIFOs */
    *(audio_ptr)     = 0xC;
    *(audio_ptr)     = 0x0;

    /* Pre-compute all drum sounds */
    synth_bass();
    synth_snare();
    synth_hihat();

    /* Playback state */
    int  playing    = 0;
    int  play_index = 0;
    int  play_len   = 0;
    int *play_buf   = 0;

    unsigned int prev_keys = 0xF;

    while (1) {
        /* Edge-detect key presses (active-low) */
        unsigned int keys    = *key_ptr & 0xF;
        unsigned int pressed = (~keys) & prev_keys;
        prev_keys = keys;

        if (pressed & 0x1) {
            playing = 1; play_index = 0;
            play_len = BASS_LEN;  play_buf = bass_buf;
        } else if (pressed & 0x2) {
            playing = 1; play_index = 0;
            play_len = SNARE_LEN; play_buf = snare_buf;
        } else if (pressed & 0x4) {
            playing = 1; play_index = 0;
            play_len = HIHAT_LEN; play_buf = hihat_buf;
        }

        /* Check FIFO write space (same as part4.c) */
        int fifos       = *(audio_ptr + 1);
        int left_write  = (fifos >> 16) & 0xFF;
        int right_write = (fifos >> 24) & 0xFF;

        if (left_write < 1 || right_write < 1)
            continue;

        if (playing && play_index < play_len) {
            int s = play_buf[play_index++];
            *(audio_ptr + 2) = s;
            *(audio_ptr + 3) = s;
            if (play_index >= play_len) playing = 0;
        } else {
            *(audio_ptr + 2) = 0;
            *(audio_ptr + 3) = 0;
        }
    }

    return 0;
}
