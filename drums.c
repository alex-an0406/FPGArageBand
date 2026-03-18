/* drum_machine.c — DE1-SoC NiosV/RISC-V
 * Uses KEY PIO edge capture register for reliable press detection.
 * KEY[0] = Bass drum, KEY[1] = Snare, KEY[2] = Hi-hat
 * No math.h — pure integer fixed-point arithmetic.
 */

#include "address_map.h"

#define SAMPLE_RATE 8000
#define BASS_LEN    2000
#define SNARE_LEN   1600
#define HIHAT_LEN    400

/* ── Fixed-point (16.16) ─────────────────────────────────────────────── */
#define FP_ONE  (1 << 16)
#define FP_PI   205887
#define FP_2PI  411775

static inline int fp_mul(int a, int b) {
    return (int)(((long long)a * b) >> 16);
}

static int fp_sin(int angle) {
    while (angle < 0)       angle += FP_2PI;
    while (angle >= FP_2PI) angle -= FP_2PI;
    int sign = 1;
    if (angle >= FP_PI) { angle -= FP_PI; sign = -1; }
    long long x = angle, p = FP_PI;
    long long num = 16 * x * (p - x);
    long long den = 5 * p * p - 4 * x * (p - x);
    return sign * (int)((num << 16) / den);
}

static unsigned int lcg_state = 12345;
static inline int lcg_rand(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (int)lcg_state;
}

/* ── Drum buffers ────────────────────────────────────────────────────── */
static int bass_buf [BASS_LEN];
static int snare_buf[SNARE_LEN];
static int hihat_buf[HIHAT_LEN];

static void synth_bass(void) {
    int phase = 0, delta = 6168, d_step = (6168 - 2056) / BASS_LEN;
    int env = FP_ONE, env_dec = 65438;
    for (int i = 0; i < BASS_LEN; i++) {
        int s = fp_mul(env, fp_sin(phase));
        if (i < 40) s += fp_mul(FP_ONE - (i * FP_ONE / 40), FP_ONE * 2 / 5);
        bass_buf[i] = fp_mul(s, 0x7FFF) << 8;
        phase += delta;
        if (phase >= FP_2PI) phase -= FP_2PI;
        delta -= d_step;
        env = fp_mul(env, env_dec);
    }
}

static void synth_snare(void) {
    int phase = 0, delta = 10280;
    int env = FP_ONE, env_dec = 65372;
    for (int i = 0; i < SNARE_LEN; i++) {
        int tone  = fp_mul(env, fp_sin(phase)) >> 1;
        int noise = fp_mul(env, lcg_rand() >> 8);
        snare_buf[i] = fp_mul(tone + noise, 0x3FFF) << 8;
        phase += delta;
        if (phase >= FP_2PI) phase -= FP_2PI;
        env = fp_mul(env, env_dec);
    }
}

static void synth_hihat(void) {
    int env = FP_ONE, env_dec = 64882, prev = 0;
    for (int i = 0; i < HIHAT_LEN; i++) {
        int raw = lcg_rand() >> 8;
        int hp  = raw - fp_mul(prev, 55705);
        prev    = raw;
        hihat_buf[i] = fp_mul(fp_mul(env, hp), 0x3FFF) << 8;
        env = fp_mul(env, env_dec);
    }
}

/* ── Main ────────────────────────────────────────────────────────────── */
int main(void) {
    volatile int *audio_ptr = (int *)AUDIO_BASE;
    volatile int *key_ptr   = (int *)KEY_BASE;

    /* audio_ptr offsets:
     *   +0 = control
     *   +1 = FIFO space
     *   +2 = left data
     *   +3 = right data
     *
     * key_ptr offsets:
     *   +0 = data
     *   +1 = direction
     *   +2 = interrupt mask
     *   +3 = edge capture (set on press, write to clear)
     */

    /* Reset audio FIFOs */
    *(audio_ptr + 0) = 0xC;
    *(audio_ptr + 0) = 0x0;

    /* Clear any stale edge captures */
    *(key_ptr + 3) = 0xF;

    /* Pre-synthesize */
    synth_bass();
    synth_snare();
    synth_hihat();

    int  playing    = 0;
    int  play_index = 0;
    int  play_len   = 0;
    int *play_buf   = 0;

    while (1) {
        /* Wait for FIFO space — rate-locks loop to 8000 Hz */
        int fifos;
        do {
            fifos = *(audio_ptr + 1);
        } while (((fifos >> 16) & 0xFF) < 1 || ((fifos >> 24) & 0xFF) < 1);

        /* Check edge capture — hardware latches the press for us */
        if (!playing) {
            int edge = *(key_ptr + 3);
            if (edge & 0x1) {
                *(key_ptr + 3) = 0xF;
                play_buf = bass_buf;  play_len = BASS_LEN;
                play_index = 0; playing = 1;
            } else if (edge & 0x2) {
                *(key_ptr + 3) = 0xF;
                play_buf = snare_buf; play_len = SNARE_LEN;
                play_index = 0; playing = 1;
            } else if (edge & 0x4) {
                *(key_ptr + 3) = 0xF;
                play_buf = hihat_buf; play_len = HIHAT_LEN;
                play_index = 0; playing = 1;
            }
        }

        /* Output one sample */
        int s = 0;
        if (playing) {
            s = play_buf[play_index++];
            if (play_index >= play_len) {
                playing = 0;
                play_index = 0;
            }
        }

        *(audio_ptr + 2) = s;
        *(audio_ptr + 3) = s;
    }

    return 0;
}
