/*
 * KissFFT - simplified real-FFT implementation
 * Based on Mark Borgerding's KissFFT (BSD-3-Clause)
 */
#include "kiss_fft.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int nfft;
    int inverse;
    kiss_fft_cpx* twiddles;
    int* factors;
    kiss_fft_cpx* tmpbuf;
} kiss_fft_state;

typedef struct {
    int nfft;
    int inverse;
    kiss_fft_cpx* twiddles;
    kiss_fft_state* substate;
    kiss_fft_cpx* tmpbuf;
    kiss_fft_cpx* super_twiddles;
} kiss_fftr_state;

#define MAXFACTORS 32

static void kf_bfly2(kiss_fft_cpx* Fout, const size_t fstride,
                     const kiss_fft_cfg st, int m) {
    kiss_fft_cpx* Fout2 = Fout + m;
    const kiss_fft_cpx* tw1 = st->twiddles;
    kiss_fft_cpx t;
    do {
        t.r = Fout2->r * tw1->r - Fout2->i * tw1->i;
        t.i = Fout2->r * tw1->i + Fout2->i * tw1->r;
        tw1 += fstride;
        Fout2->r = Fout->r - t.r;
        Fout2->i = Fout->i - t.i;
        Fout->r += t.r;
        Fout->i += t.i;
        ++Fout2;
        ++Fout;
    } while (--m);
}

static void kf_bfly4(kiss_fft_cpx* Fout, const size_t fstride,
                     const kiss_fft_cfg st, const size_t m) {
    kiss_fft_cpx* Fout2, *Fout3;
    const kiss_fft_cpx* tw1, *tw2, *tw3;
    kiss_fft_cpx scratch[6];
    size_t k;
    const size_t m2 = 2 * m;
    const size_t m3 = 3 * m;
    tw3 = tw2 = tw1 = st->twiddles;
    Fout2 = Fout + m2;
    Fout3 = Fout + m3;

    for (k = m; k--;) {
        scratch[0].r = Fout[m].r  * tw1->r - Fout[m].i  * tw1->i;
        scratch[0].i = Fout[m].r  * tw1->i + Fout[m].i  * tw1->r;
        scratch[1].r = Fout2->r * tw2->r - Fout2->i * tw2->i;
        scratch[1].i = Fout2->r * tw2->i + Fout2->i * tw2->r;
        scratch[2].r = Fout3->r * tw3->r - Fout3->i * tw3->i;
        scratch[2].i = Fout3->r * tw3->i + Fout3->i * tw3->r;

        scratch[5].r = Fout->r - scratch[1].r;
        scratch[5].i = Fout->i - scratch[1].i;
        Fout->r += scratch[1].r;
        Fout->i += scratch[1].i;
        scratch[3].r = scratch[0].r + scratch[2].r;
        scratch[3].i = scratch[0].i + scratch[2].i;
        scratch[4].r = scratch[0].r - scratch[2].r;
        scratch[4].i = scratch[0].i - scratch[2].i;

        Fout2->r = Fout->r - scratch[3].r;
        Fout2->i = Fout->i - scratch[3].i;
        Fout->r += scratch[3].r;
        Fout->i += scratch[3].i;

        Fout[m].r  = scratch[5].r + scratch[4].i;
        Fout[m].i  = scratch[5].i - scratch[4].r;
        Fout3->r = scratch[5].r - scratch[4].i;
        Fout3->i = scratch[5].i + scratch[4].r;

        tw1 += fstride;
        tw2 += fstride * 2;
        tw3 += fstride * 3;
        ++Fout; ++Fout2; ++Fout3;
    }
}

static void kf_work(kiss_fft_cpx* Fout, const kiss_fft_cpx* f,
                    const size_t fstride, int in_stride,
                    int* factors, const kiss_fft_cfg st) {
    kiss_fft_cpx* Fout_beg = Fout;
    const int p = *factors++;
    const int m = *factors++;
    const kiss_fft_cpx* Fout_end = Fout + p * m;

    if (m == 1) {
        do {
            *Fout = *f;
            f += fstride * in_stride;
        } while (++Fout != Fout_end);
    } else {
        do {
            kf_work(Fout, f, fstride * p, in_stride, factors, st);
            f += fstride * in_stride;
        } while ((Fout += m) != Fout_end);
    }

    Fout = Fout_beg;
    if (p == 2) kf_bfly2(Fout, fstride, st, m);
    else if (p == 4) kf_bfly4(Fout, fstride, st, m);
}

static void kf_factor(int n, int* facbuf) {
    int p = 4;
    double floor_sqrt = floor(sqrt((double)n));
    do {
        while (n % p) {
            switch (p) {
                case 4: p = 2; break;
                case 2: p = 3; break;
                default: p += 2; break;
            }
            if (p > floor_sqrt) p = n;
        }
        n /= p;
        *facbuf++ = p;
        *facbuf++ = n;
    } while (n > 1);
}

kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void* mem, size_t* lenmem) {
    kiss_fft_cfg st = NULL;
    size_t memneeded = sizeof(kiss_fft_state)
        + sizeof(kiss_fft_cpx) * nfft
        + sizeof(int) * MAXFACTORS * 2
        + sizeof(kiss_fft_cpx) * nfft;

    if (lenmem == NULL) {
        st = (kiss_fft_cfg)malloc(memneeded);
    } else {
        if (mem != NULL && *lenmem >= memneeded) st = (kiss_fft_cfg)mem;
        *lenmem = memneeded;
    }
    if (st) {
        int i;
        st->nfft = nfft;
        st->inverse = inverse_fft;
        st->twiddles = (kiss_fft_cpx*)((char*)st + sizeof(kiss_fft_state));
        st->factors = (int*)((char*)st->twiddles + sizeof(kiss_fft_cpx) * nfft);
        st->tmpbuf = (kiss_fft_cpx*)((char*)st->factors + sizeof(int) * MAXFACTORS * 2);
        for (i = 0; i < nfft; ++i) {
            const double phase = (inverse_fft ? 1 : -1) * 2 * M_PI * i / nfft;
            st->twiddles[i].r = (kiss_fft_scalar)cos(phase);
            st->twiddles[i].i = (kiss_fft_scalar)sin(phase);
        }
        kf_factor(nfft, st->factors);
    }
    return st;
}

void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx* fin, kiss_fft_cpx* fout) {
    kf_work(fout, fin, 1, 1, cfg->factors, cfg);
}

void kiss_fft_free(kiss_fft_cfg cfg) {
    free(cfg);
}

/* Real FFT */
typedef struct {
    int nfft;
    int inverse;
    kiss_fft_cpx* twiddles;
    kiss_fft_cfg substate;
    kiss_fft_cpx* tmpbuf;
    kiss_fft_cpx* super_twiddles;
} kiss_fftr_state2;

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void* mem, size_t* lenmem) {
    int i;
    kiss_fftr_cfg st = NULL;
    size_t subsize = 0, memneeded;

    kiss_fft_alloc(nfft / 2, inverse_fft, NULL, &subsize);
    memneeded = sizeof(kiss_fftr_state2) + subsize + sizeof(kiss_fft_cpx) * (nfft * 3 / 2);

    if (lenmem == NULL) {
        st = (kiss_fftr_cfg)malloc(memneeded);
    } else {
        if (mem != NULL && *lenmem >= memneeded) st = (kiss_fftr_cfg)mem;
        *lenmem = memneeded;
    }
    if (!st) return NULL;

    st->substate = (kiss_fft_cfg)((char*)st + sizeof(kiss_fftr_state2));
    st->tmpbuf = (kiss_fft_cpx*)((char*)st->substate + subsize);
    st->super_twiddles = st->tmpbuf + nfft / 2;

    kiss_fft_alloc(nfft / 2, inverse_fft, st->substate, &subsize);

    for (i = 0; i < nfft / 2; ++i) {
        double phase = -M_PI * ((double)i / (nfft / 2) + 0.5);
        if (inverse_fft) phase *= -1;
        st->super_twiddles[i].r = (float)cos(phase);
        st->super_twiddles[i].i = (float)sin(phase);
    }
    return st;
}

void kiss_fftr(kiss_fftr_cfg st, const kiss_fft_scalar* timedata, kiss_fft_cpx* freqdata) {
    int k, ncfft;
    kiss_fft_cpx fpnk, fpk, f1k, f2k, tw;
    ncfft = st->substate->nfft;

    kiss_fft(st->substate, (const kiss_fft_cpx*)timedata, st->tmpbuf);

    freqdata[0].r = st->tmpbuf[0].r + st->tmpbuf[0].i;
    freqdata[0].i = 0;
    freqdata[ncfft].r = st->tmpbuf[0].r - st->tmpbuf[0].i;
    freqdata[ncfft].i = 0;

    for (k = 1; k <= ncfft / 2; ++k) {
        fpk  = st->tmpbuf[k];
        fpnk.r = st->tmpbuf[ncfft - k].r;
        fpnk.i = -st->tmpbuf[ncfft - k].i;

        f1k.r = fpk.r + fpnk.r;
        f1k.i = fpk.i + fpnk.i;
        f2k.r = fpk.r - fpnk.r;
        f2k.i = fpk.i - fpnk.i;

        tw.r = f2k.r * st->super_twiddles[k - 1].r - f2k.i * st->super_twiddles[k - 1].i;
        tw.i = f2k.r * st->super_twiddles[k - 1].i + f2k.i * st->super_twiddles[k - 1].r;

        freqdata[k].r = (f1k.r + tw.r) * 0.5f;
        freqdata[k].i = (f1k.i + tw.i) * 0.5f;
        freqdata[ncfft - k].r = (f1k.r - tw.r) * 0.5f;
        freqdata[ncfft - k].i = (tw.i - f1k.i) * 0.5f;
    }
}

void kiss_fftr_free(kiss_fftr_cfg cfg) {
    free(cfg);
}
