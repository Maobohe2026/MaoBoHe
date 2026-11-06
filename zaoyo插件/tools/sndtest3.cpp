/* sndtest3.cpp —— 离线对比打分方式（正确版，不进插件）
 *
 * 用法: sndtest3.exe <wav> <tpl.bin> <mode>
 *   mode 1: 窗长=模板长，整窗 RMS 归一化（现有算法但窗长收窄）
 *   mode 2: 窗长=模板长，逐滞后 NCC（按候选段自身能量归一化）
 *   mode 3: 窗长=模板长，PHAT（互功率谱按幅度归一，只留相位）
 * 输出: 每帧 "时间 分数"（供 eval 脚本解析）
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR 32000
#define PI 3.14159265358979323846

/* ---------- WAV ---------- */
static float *readWav(const char *path, int *outN, int *outRate)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
    fclose(f);
    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) { free(buf); return NULL; }
    int ch = 0, bits = 0, rate = 0, fmt = 0;
    unsigned char *data = NULL; long dataLen = 0; long off = 12;
    while (off + 8 <= sz) {
        unsigned char *ck = buf + off;
        long body = off + 8;
        unsigned long csz = *(unsigned long *)(buf + off + 4);
        if (body + (long)csz > sz) { if (memcmp(ck, "data", 4) == 0) csz = sz - body; else break; }
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned short *p = (unsigned short *)(buf + body);
            fmt = p[0]; ch = p[1]; rate = *(long *)(buf + body + 4); bits = p[7];
            if (fmt == 0xFFFE && csz >= 40) fmt = *(unsigned short *)(buf + body + 24);
        } else if (!memcmp(ck, "data", 4)) { data = buf + body; dataLen = csz; }
        off = body + csz + (csz & 1);
    }
    if (!data || !ch || !bits || !rate) { free(buf); return NULL; }
    int bytes = bits / 8;
    long frames = dataLen / (bytes * ch);
    float *mono = (float *)malloc(sizeof(float) * frames);
    long i;
    for (i = 0; i < frames; i++) {
        double s = 0.0; int c;
        for (c = 0; c < ch; c++) {
            const unsigned char *p = data + (size_t)(i * ch + c) * bytes;
            double v = 0.0;
            if (fmt == 3) v = (bytes == 4) ? *(const float *)p : 0.0;
            else if (fmt == 1) { if (bytes == 2) v = (*(const short *)p) / 32768.0; else if (bytes == 1) v = (p[0] - 128) / 128.0; }
            s += v;
        }
        mono[i] = (float)(s / ch);
    }
    free(buf);
    *outN = (int)frames; *outRate = rate;
    return mono;
}

static float *resampleTo32k(const float *in, int n, int rate, int *outN)
{
    if (rate == SR) { float *o = (float *)malloc(sizeof(float) * n); memcpy(o, in, sizeof(float) * n); *outN = n; return o; }
    double ratio = (double)rate / SR;
    int m = (int)(n / ratio);
    float *o = (float *)malloc(sizeof(float) * m);
    int i;
    for (i = 0; i < m; i++) {
        double x = i * ratio; int i0 = (int)x; double fr = x - i0;
        int i1 = (i0 + 1 < n) ? i0 + 1 : n - 1;
        o[i] = (float)(in[i0] * (1 - fr) + in[i1] * fr);
    }
    *outN = m;
    return o;
}

/* ---------- 4 阶 Butterworth 高通 1kHz ---------- */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } BQ;
static void bqHP(BQ *q, double fc, double sr, double Q)
{
    double w0 = 2.0 * PI * fc / sr, cw = cos(w0), sw = sin(w0);
    double alpha = sw / (2.0 * Q), a0 = 1.0 + alpha;
    memset(q, 0, sizeof(*q));
    q->b0 = (float)((1.0 + cw) * 0.5 / a0);
    q->b1 = (float)(-(1.0 + cw) / a0);
    q->b2 = (float)((1.0 + cw) * 0.5 / a0);
    q->a1 = (float)(-2.0 * cw / a0);
    q->a2 = (float)((1.0 - alpha) / a0);
}
static float bqRun(BQ *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

/* ---------- FFT ---------- */
static int nextPow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }
static void fft(float *re, float *im, int n, int inverse)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * PI / len * (inverse ? 1 : -1);
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    if (inverse) for (i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

int main(int argc, char **argv)
{
    int n = 0, rate = 0, n32 = 0, tplN = 0, mode = 2;
    float *raw, *p32, *tpl;
    int L, fftN, hop, pos, i, lag;
    float *xr, *xi, *tr, *ti, *cr, *ci;
    FILE *f;
    long tsz;
    BQ h1, h2;

    if (argc < 4) { printf("usage: sndtest3.exe <wav> <tpl.bin> <1|2|3>\n"); return 1; }
    mode = atoi(argv[3]);

    f = fopen(argv[2], "rb");
    if (!f) { printf("tpl open fail\n"); return 1; }
    fseek(f, 0, SEEK_END); tsz = ftell(f); fseek(f, 0, SEEK_SET);
    tpl = (float *)malloc(tsz);
    if (fread(tpl, 1, tsz, f) != (size_t)tsz) { printf("tpl read fail\n"); return 1; }
    fclose(f);
    tplN = (int)(tsz / 4);
    L = tplN;

    raw = readWav(argv[1], &n, &rate);
    if (!raw) { printf("wav fail\n"); return 1; }
    p32 = resampleTo32k(raw, n, rate, &n32);
    free(raw);

    bqHP(&h1, 1000.0, SR, 0.5412);
    bqHP(&h2, 1000.0, SR, 1.3066);
    for (i = 0; i < n32; i++) p32[i] = bqRun(&h2, bqRun(&h1, p32[i]));

    {   /* 模板同一套高通（离线零相位近似：正反各一次） */
        BQ a, b;
        bqHP(&a, 1000.0, SR, 0.5412);
        bqHP(&b, 1000.0, SR, 1.3066);
        for (i = 0; i < tplN; i++) tpl[i] = bqRun(&b, bqRun(&a, tpl[i]));
        for (i = tplN - 1; i >= 0; i--) tpl[i] = bqRun(&b, bqRun(&a, tpl[i]));  /* 反向一次→近似零相位 */
    }
    {   /* 模板单位范数 */
        double norm = 0.0;
        for (i = 0; i < tplN; i++) norm += (double)tpl[i] * tpl[i];
        norm = sqrt(norm + 1e-12);
        for (i = 0; i < tplN; i++) tpl[i] = (float)(tpl[i] / norm);
    }

    fftN = nextPow2(L + L - 1);
    hop = (int)(SR * 0.08);
    xr = (float *)calloc(fftN, sizeof(float));
    xi = (float *)calloc(fftN, sizeof(float));
    tr = (float *)calloc(fftN, sizeof(float));
    ti = (float *)calloc(fftN, sizeof(float));
    cr = (float *)calloc(fftN, sizeof(float));
    ci = (float *)calloc(fftN, sizeof(float));

    for (i = 0; i < tplN; i++) tr[i] = tpl[i];
    fft(tr, ti, fftN, 0);

    for (pos = L; pos < n32; pos += hop) {
        const float *x = p32 + (pos - L);
        double esum = 0.0, emax = 1e-12;
        float score = 0.0f;

        for (i = 0; i < L; i++) esum += (double)x[i] * x[i];
        if (esum < 1e-10) { printf("%7.3f %8.4f\n", (double)pos / SR, 0.0); continue; }

        /* x 归一化（mode1 用整窗 RMS；mode2/3 用原始幅度的相关性） */
        for (i = 0; i < fftN; i++) { xr[i] = 0.0f; xi[i] = 0.0f; }
        if (mode == 1) {
            double inv = 1.0 / sqrt(esum / L + 1e-9);
            for (i = 0; i < L; i++) xr[i] = (float)(x[i] * inv);
        } else {
            for (i = 0; i < L; i++) xr[i] = x[i];
        }
        fft(xr, xi, fftN, 0);

        for (i = 0; i < fftN; i++) {
            float ar = xr[i], ai = xi[i];
            float br = tr[i], bi = -ti[i];
            cr[i] = ar * br - ai * bi;
            ci[i] = ar * bi + ai * br;
            if (mode == 3) {
                float m = (float)sqrt((double)cr[i] * cr[i] + (double)ci[i] * ci[i]) + 1e-9f;
                cr[i] /= m; ci[i] /= m;
            }
        }
        fft(cr, ci, fftN, 1);

        if (mode == 1) {
            float peak = 0.0f;
            for (lag = 0; lag < L; lag++) if (cr[lag] > peak) peak = cr[lag];
            score = (float)(peak / (double)L);
        } else if (mode == 2) {
            /* 逐滞后：能量用前缀和快速算 */
            for (lag = 0; lag < L; lag++) {
                double e = 0.0; int k;
                for (k = 0; k < L; k++) e += (double)x[lag + k] * x[lag + k];
                if (e < 1e-10) continue;
                {
                    float v = (float)(cr[lag] / sqrt(e));
                    if (v > score) score = v;
                }
            }
        } else {
            float peak = 0.0f;
            for (lag = 0; lag < fftN; lag++) {
                float v = cr[lag] < 0 ? -cr[lag] : cr[lag];
                if (v > peak) peak = v;
            }
            score = peak;      /* PHAT 峰值：完全匹配≈1，噪声远小于 1 */
        }
        (void)emax;
        printf("%7.3f %8.4f\n", (double)pos / SR, score);
    }

    free(xr); free(xi); free(tr); free(ti); free(cr); free(ci);
    free(p32); free(tpl);
    return 0;
}
