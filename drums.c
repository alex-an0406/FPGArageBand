/* drum_machine.c — DE1-SoC NiosV/RISC-V
 * KEY[0..2] trigger drum sounds via hardware interrupt (PIO IRQ → RISC-V mtvec).
 * The audio loop runs uninterrupted; the ISR just sets a pending flag.
 * No math.h — pure integer/fixed-point arithmetic.
 * Sample rate: 8000 Hz
 */

#include "address_map.h"

/* ── KEY PIO interrupt registers (offset from KEY_BASE) ──────────────────
 * KEY_BASE + 0x00  = data        (read key state, active-low)
 * KEY_BASE + 0x08  = interruptmask  (1 = enable IRQ for that bit)
 * KEY_BASE + 0x0C  = edgecapture   (1 = falling edge seen; write 1 to clear)
 * ──────────────────────────────────────────────────────────────────────── */
#define KEY_DATA        (*(volatile int *)(KEY_BASE + 0x00))
#define KEY_MASK        (*(volatile int *)(KEY_BASE + 0x08))
#define KEY_EDGE        (*(volatile int *)(KEY_BASE + 0x0C))

/* ── RISC-V CSR helpers ───────────────────────────────────────────────── */
#define MSTATUS_MIE  (1 << 3)
#define MIE_MEIE     (1 << 11)   /* machine external interrupt enable */

static inline void enable_interrupts(void) {
    /* Enable machine external interrupts in mie, then set MIE in mstatus */
    __asm__ volatile ("csrrs zero, mie,     %0" :: "r"(MIE_MEIE));
    __asm__ volatile ("csrrs zero, mstatus, %0" :: "r"(MSTATUS_MIE));
}

/* ── Drum selection flag — written by ISR, read by main loop ─────────── */
/* Values: 0=none, 1=bass, 2=snare, 3=hihat */
volatile int pending_drum = 0;

/* ── Interrupt handler — must match mtvec (set by crt0/startup) ─────────
 * The Intel FPGA Academy BSP sets mtvec to __interrupt_handler, so we
 * define that symbol here. attribute interrupt saves/restores all regs.
 * ──────────────────────────────────────────────────────────────────────── */
void __attribute__((interrupt)) __interrupt_handler(void) {
    int edge = KEY_EDGE;          /* which keys fired */
    KEY_EDGE = edge;              /* clear edge capture (write-to-clear) */

    if      (edge & 0x1) pending_drum = 1;
    else if (edge & 0x2) pending_drum = 2;
    else if (edge & 0x4) pending_drum = 3;
}

/* ── Fixed-point (16.16) ─────────────────────────────────────────────── */
#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)
#define FP_PI    205887
#define FP_2PI   411775

static inline int fp_mul(int a, int b) {
    return (int)(((long long)a * b) >> FP_SHIFT);
}

static int fp_sin(int angle) {
    while (angle < 0)       angle += FP_2PI;
    while (angle >= FP_2PI) angle -= FP_2PI;
    int sign = 1;
    if (angle >= FP_PI) { angle -= FP_PI; sign = -1; }
    long long x   = angle, p = FP_PI;
    long long num = 16 * x * (p - x);
    long long den = 5 * p * p - 4 * x * (p - x);
    return sign * (int)((num << FP_SHIFT) / den);
}

static unsigned int lcg_state = 12345;
static inline int lcg_rand(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (int)lcg_state;
}

/* ── Drum buffers ────────────────────────────────────────────────────── */
#define SAMPLE_RATE 8000
#define BASS_LEN    2000
#define SNARE_LEN   1600
#define HIHAT_LEN    400

static int bass_buf [BASS_LEN];
static int snare_buf[SNARE_LEN];
static int hihat_buf[HIHAT_LEN];

static void synth_bass(void) {
    int phase = 0, delta = 6168, d_step = (6168-2056)/BASS_LEN;
    int env = FP_ONE, env_dec = 65438;
    for (int i = 0; i < BASS_LEN; i++) {
        int s = fp_mul(env, fp_sin(phase));
        if (i < 40) s += fp_mul(FP_ONE - (i * FP_ONE / 40), FP_ONE * 2 / 5);
        bass_buf[i] = fp_mul(s, 0x7FFF) << 8;
        phase += delta; if (phase >= FP_2PI) phase -= FP_2PI;
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
        phase += delta; if (phase >= FP_2PI) phase -= FP_2PI;
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

    /* Reset audio FIFOs */
    *(audio_ptr) = 0xC;
    *(audio_ptr) = 0x0;

    /* Pre-synthesize drum sounds */
    synth_bass();
    synth_snare();
    synth_hihat();

    /* Configure KEY PIO to interrupt on falling edge for KEY[0..2] */
    KEY_EDGE = 0xF;   /* clear any stale edge captures */
    KEY_MASK = 0x7;   /* enable interrupts for KEY[0], KEY[1], KEY[2] */

    /* Enable RISC-V interrupts */
    enable_interrupts();

    int  playing    = 0;
    int  play_index = 0;
    int  play_len   = 0;
    int *play_buf   = 0;

    while (1) {
        /* Wait until FIFO has space — rate-locks loop to 8000 Hz */
        int fifos;
        do {
            fifos = *(audio_ptr + 1);
        } while (((fifos >> 16) & 0xFF) < 1 || ((fifos >> 24) & 0xFF) < 1);

        /* Check if ISR flagged a new drum hit */
        if (pending_drum) {
            int drum    = pending_drum;
            pending_drum = 0;          /* acknowledge */
            play_index  = 0;
            playing     = 1;
            if      (drum == 1) { play_buf = bass_buf;  play_len = BASS_LEN;  }
            else if (drum == 2) { play_buf = snare_buf; play_len = SNARE_LEN; }
            else                { play_buf = hihat_buf; play_len = HIHAT_LEN; }
        }

        /* Output one sample */
        int s = (playing && play_index < play_len) ? play_buf[play_index++] : 0;
        if (play_index >= play_len) playing = 0;

        *(audio_ptr + 2) = s;
        *(audio_ptr + 3) = s;
    }

    return 0;
}
