#include "vc6compat.h"
/* sound.cpp - 音效波形匹配引擎（见 sound.h 说明） */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================ .npy 读取 ============================ */

static unsigned char* slurp(const wchar_t* path, int* outSize)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD hi = 0, lo = GetFileSize(h, &hi);
    if (lo == INVALID_FILE_SIZE || lo < 16 || hi != 0) { CloseHandle(h); return NULL; }
    unsigned char* buf = (unsigned char*)malloc(lo);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, lo, &got, NULL);
    CloseHandle(h);
    if (!ok || got != lo) { free(buf); return NULL; }
    *outSize = (int)lo;
    return buf;
}

/* 解析 npy 头，返回数据指针与样本数；仅支持 1 维实数数组 */
static const unsigned char* npyData(const unsigned char* buf, int size,
                                    int* outCount, int* outIsFloat64)
{
    if (size < 16) return NULL;
    if (memcmp(buf, "\x93NUMPY", 6) != 0) return NULL;
    int major = buf[6];
    int hlen, off;
    if (major == 1) {
        hlen = buf[8] | (buf[9] << 8);
        off = 10;
    } else {
        hlen = buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24);
        off = 12;
    }
    if (hlen <= 0 || off + hlen > size) return NULL;

    char hdr[512];
    int n = hlen < (int)sizeof(hdr) - 1 ? hlen : (int)sizeof(hdr) - 1;
    memcpy(hdr, buf + off, n);
    hdr[n] = 0;

    int isF8 = 0;
    const char* d = strstr(hdr, "'descr':");
    if (!d) return NULL;
    {
        const char* q = strchr(d, 'f');
        if (!q) return NULL;
        if (q[1] == '8') isF8 = 1;
        else if (q[1] == '4') isF8 = 0;
        else return NULL;
    }
    if (strstr(hdr, "'fortran_order': True")) return NULL;

    const char* sp = strstr(hdr, "'shape':");
    if (!sp) return NULL;
    const char* lp = strchr(sp, '(');
    const char* rp = strchr(sp, ')');
    if (!lp || !rp) return NULL;
    long count = 0;
    for (const char* p = lp + 1; p < rp; p++) {
        if (*p >= '0' && *p <= '9') count = count * 10 + (*p - '0');
        else if (*p == ',') { if (count > 0) break; }
    }
    if (count <= 0) return NULL;

    const unsigned char* data = buf + off + hlen;
    int bytes = isF8 ? 8 : 4;
    if ((data - buf) + (long)count * bytes > size) return NULL;

    *outCount = (int)count;
    *outIsFloat64 = isF8;
    return data;
}

struct SndTpl {
    float* ref;
    int    len;
    float  peak;
};

static HSNDTPL tplFromSamples(const float* src, int count)
{
    if (!src || count < 64) return NULL;
    SndTpl* t = (SndTpl*)calloc(1, sizeof(SndTpl));
    if (!t) return NULL;
    float* ref = (float*)malloc(sizeof(float) * count);
    if (!ref) { free(t); return NULL; }

    float peak = 0.0f;
    for (int i_r20 = 0; i_r20 < count; i_r20++) {
        ref[i_r20] = src[i_r20];
        if (fabsf(src[i_r20]) > peak) peak = fabsf(src[i_r20]);
    }
    /* RMS 归一化（对应 Python 的 _norm） */
    double s = 0.0;
    for (int i_r19 = 0; i_r19 < count; i_r19++) s += (double)ref[i_r19] * ref[i_r19];
    double rms = sqrt(s / count + 1e-6);
    float inv = (float)(1.0 / rms);
    for (int i_r18 = 0; i_r18 < count; i_r18++) ref[i_r18] *= inv;

    t->ref = ref;
    t->len = count;
    t->peak = peak;
    return (HSNDTPL)t;
}

HSNDTPL SndTplLoad(const wchar_t* path)
{
    int size = 0;
    unsigned char* buf = slurp(path, &size);
    if (!buf) return NULL;
    int count = 0, isF8 = 0;
    const unsigned char* d = npyData(buf, size, &count, &isF8);
    if (!d) { free(buf); return NULL; }

    float* tmp = (float*)malloc(sizeof(float) * count);
    if (!tmp) { free(buf); return NULL; }
    if (isF8) {
        const double* p = (const double*)d;
        for (int i_r17 = 0; i_r17 < count; i_r17++) tmp[i_r17] = (float)p[i_r17];
    } else {
        const float* p = (const float*)d;
        for (int i_r16 = 0; i_r16 < count; i_r16++) tmp[i_r16] = p[i_r16];
    }
    HSNDTPL t = tplFromSamples(tmp, count);
    free(tmp);
    free(buf);
    return t;
}

/* 从内置资源（float32 裸数组）建模板 */
HSNDTPL SndTplLoadF32(const void* data, int bytes)
{
    if (!data || bytes < 256) return NULL;
    return tplFromSamples((const float*)data, bytes / (int)sizeof(float));
}

void SndTplFree(HSNDTPL h)
{
    SndTpl* t = (SndTpl*)h;
    if (!t) return;
    free(t->ref);
    free(t);
}

int SndTplSamples(HSNDTPL h) { SndTpl* t = (SndTpl*)h; return t ? t->len : 0; }
float SndTplPeak(HSNDTPL h) { SndTpl* t = (SndTpl*)h; return t ? t->peak : 0.0f; }

/* ============================ FFT ============================ */

static void fftCore(float* re, float* im, int n,
                    const float* twRe, const float* twIm, int inv)
{
    /* 位反转重排 */
    for (int i_r15 = 1, j = 0; i_r15 < n; i_r15++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i_r15 < j) {
            float t = re[i_r15]; re[i_r15] = re[j]; re[j] = t;
            t = im[i_r15]; im[i_r15] = im[j]; im[j] = t;
        }
    }
    for (int len_r14 = 2; len_r14 <= n; len_r14 <<= 1) {
        int half = len_r14 >> 1, step = n / len_r14;
        for (int i_r13 = 0; i_r13 < n; i_r13 += len_r14) {
            for (int j_r12 = 0; j_r12 < half; j_r12++) {
                int k = j_r12 * step;
                float wr = twRe[k];
                float wi = inv ? -twIm[k] : twIm[k];
                int a = i_r13 + j_r12, b = a + half;
                float xr = re[b] * wr - im[b] * wi;
                float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
    if (inv) {
        float s = 1.0f / (float)n;
        for (int i_r11 = 0; i_r11 < n; i_r11++) { re[i_r11] *= s; im[i_r11] *= s; }
    }
}

/* ============================ 检测器 ============================ */

struct SndDet {
    const float* ref;
    int   refLen;
    int   win;          /* 滑动窗长度 */
    float* buf;         /* 环形缓冲，长度 win */
    int   pos, filled;
    int   fftN;
    float* refRe;       /* 预计算 FFT(ref) 的共轭 */
    float* refIm;
    float* segRe;
    float* segIm;
    float* work;        /* 拼接后的窗口 */
    float* twRe;
    float* twIm;
    float  thresh;
    float  cooldown;
    DWORD  lastFire;
    float  lastScore;
    int    hits;
    int    consec;      /* 连续超阈帧数（抑制单帧误报）*/
};

static int nextPow2(int v)
{
    int n = 1;
    while (n < v) n <<= 1;
    return n;
}

HSNDDET SndDetCreate(HSNDTPL h, float thresh, float cooldownSec)
{
    SndTpl* t = (SndTpl*)h;
    if (!t) return NULL;

    SndDet* d = (SndDet*)calloc(1, sizeof(SndDet));
    if (!d) return NULL;
    d->ref = t->ref;
    d->refLen = t->len;
    d->win = t->len;      /* 窗长=模板长度：0.5s 窗口会把无关声音算进归一化，混战时摊薄分数 */
    int n = d->win + d->refLen - 1;
    d->fftN = nextPow2(n > d->win ? n : d->win);

    d->buf   = (float*)calloc(d->win, sizeof(float));
    d->work  = (float*)calloc(d->win, sizeof(float));
    d->refRe = (float*)calloc(d->fftN, sizeof(float));
    d->refIm = (float*)calloc(d->fftN, sizeof(float));
    d->segRe = (float*)calloc(d->fftN, sizeof(float));
    d->segIm = (float*)calloc(d->fftN, sizeof(float));
    d->twRe  = (float*)calloc(d->fftN / 2, sizeof(float));
    d->twIm  = (float*)calloc(d->fftN / 2, sizeof(float));
    if (!d->buf || !d->work || !d->refRe || !d->refIm || !d->segRe || !d->segIm ||
        !d->twRe || !d->twIm) {
        SndDetFree(d);
        return NULL;
    }

    for (int i_r10 = 0; i_r10 < d->fftN / 2; i_r10++) {
        double a = 2.0 * M_PI * i_r10 / d->fftN;
        d->twRe[i_r10] = (float)cos(a);
        d->twIm[i_r10] = (float)(-sin(a));
    }

    for (int i_r9 = 0; i_r9 < d->refLen; i_r9++) d->refRe[i_r9] = d->ref[i_r9];
    fftCore(d->refRe, d->refIm, d->fftN, d->twRe, d->twIm, 0);
    /* 共轭，便于与 seg 频谱直接相乘 */
    for (int i_r8 = 0; i_r8 < d->fftN; i_r8++) d->refIm[i_r8] = -d->refIm[i_r8];

    d->thresh = thresh > 0.0f ? thresh : 0.13f;
    d->cooldown = cooldownSec >= 0.0f ? cooldownSec : 0.5f;
    d->lastFire = 0;
    d->lastScore = 0.0f;
    d->hits = 0;
    return (HSNDDET)d;
}

void SndDetFree(HSNDDET h)
{
    SndDet* d = (SndDet*)h;
    if (!d) return;
    free(d->buf); free(d->work);
    free(d->refRe); free(d->refIm);
    free(d->segRe); free(d->segIm);
    free(d->twRe); free(d->twIm);
    free(d);
}

void SndDetReset(HSNDDET h)
{
    SndDet* d = (SndDet*)h;
    if (!d) return;
    memset(d->buf, 0, sizeof(float) * d->win);
    memset(d->work, 0, sizeof(float) * d->win);
    d->pos = 0;
    d->filled = 0;
    d->lastScore = 0.0f;
}

void SndDetSetThresh(HSNDDET h, float thresh)
{
    SndDet* d = (SndDet*)h;
    if (d && thresh > 0.0f) d->thresh = thresh;
}

float SndDetScore(HSNDDET h) { SndDet* d = (SndDet*)h; return d ? d->lastScore : 0.0f; }
int   SndDetHits(HSNDDET h)  { SndDet* d = (SndDet*)h; return d ? d->hits : 0; }

static float detCompute(SndDet* d)
{
    int filled = d->filled;
    if (filled <= 0) return 0.0f;

    /* 拼出时间顺序的窗口 */
    int first = d->win - d->pos;
    if (first > filled) first = filled;
    memcpy(d->work, d->buf + d->pos, sizeof(float) * first);
    if (filled > first) memcpy(d->work + first, d->buf, sizeof(float) * (filled - first));

    /* PHAT（相位变换）打分：
       R = X*conj(T) / (|X*conj(T)| + eps)，只保留相位结构，再 IFFT。
       好处：对整体音量变化、宽带噪声都不敏感 —— 混战中弱音效也能浮出来。
       得分 = IFFT 峰值（完全匹配≈1，纯噪声远小于 1）。
       以前是"整窗 RMS 归一化 + 峰值/窗长"，战斗音一多就会把模板那一段压小。 */
    for (int i_r6 = 0; i_r6 < d->fftN; i_r6++) { d->segRe[i_r6] = 0.0f; d->segIm[i_r6] = 0.0f; }
    for (int i_r5 = 0; i_r5 < filled; i_r5++) d->segRe[i_r5] = d->work[i_r5];

    fftCore(d->segRe, d->segIm, d->fftN, d->twRe, d->twIm, 0);
    for (int i_r4 = 0; i_r4 < d->fftN; i_r4++) {
        float ar = d->segRe[i_r4], ai = d->segIm[i_r4];
        float br = d->refRe[i_r4], bi = d->refIm[i_r4];
        float rr = ar * br - ai * bi;
        float ri = ar * bi + ai * br;
        float mag = (float)sqrt((double)rr * rr + (double)ri * ri) + 1e-9f;
        d->segRe[i_r4] = rr / mag;
        d->segIm[i_r4] = ri / mag;
    }
    fftCore(d->segRe, d->segIm, d->fftN, d->twRe, d->twIm, 1);

    float best = 0.0f;
    for (int i_r3 = 0; i_r3 < d->fftN; i_r3++) {
        float v = d->segRe[i_r3];
        if (v < 0) v = -v;
        if (v > best) best = v;
    }
    return best;
}

float SndDetFeed(HSNDDET h, const float* x, int n, int* fired)
{
    SndDet* d = (SndDet*)h;
    if (fired) *fired = 0;
    if (!d || !x || n <= 0) return d ? d->lastScore : 0.0f;

    if (n >= d->win) {
        /* 一次喂满，取最后 win 个 */
        memcpy(d->buf, x + (n - d->win), sizeof(float) * d->win);
        d->pos = 0;
        d->filled = d->win;
    } else {
        int end = d->pos + n;
        if (end <= d->win) {
            memcpy(d->buf + d->pos, x, sizeof(float) * n);
        } else {
            int p1 = d->win - d->pos;
            memcpy(d->buf + d->pos, x, sizeof(float) * p1);
            memcpy(d->buf, x + p1, sizeof(float) * (end - d->win));
        }
        d->pos = (d->pos + n) % d->win;
        d->filled = d->filled + n < d->win ? d->filled + n : d->win;
    }

    float score = detCompute(d);
    d->lastScore = score;

    /* 连续 2 帧超阈才触发：混战里单帧的偶发高分是误报，真实音效会持续 2 帧以上（80ms/帧） */
    DWORD now = GetTickCount();
    if (score >= d->thresh) {
        d->consec++;
        if (d->consec >= 2) {
            if (d->lastFire == 0 || (DWORD)(now - d->lastFire) >= (DWORD)(d->cooldown * 1000.0f)) {
                d->lastFire = now ? now : 1;
                d->hits++;
                if (fired) *fired = 1;
            }
        }
    } else {
        d->consec = 0;
    }
    return score;
}

/* ============================ 高通滤波器 ============================ */

struct Biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
};

struct SndHp {
    Biquad s1, s2;
};

static void bqHighpass(Biquad* q, double fc, double sr, double Q)
{
    double w0 = 2.0 * M_PI * fc / sr;
    double cw = cos(w0), sw = sin(w0);
    double alpha = sw / (2.0 * Q);
    double a0 = 1.0 + alpha;
    memset(q, 0, sizeof(*q));
    q->b0 = (float)((1.0 + cw) * 0.5 / a0);
    q->b1 = (float)(-(1.0 + cw) / a0);
    q->b2 = (float)((1.0 + cw) * 0.5 / a0);
    q->a1 = (float)(-2.0 * cw / a0);
    q->a2 = (float)((1.0 - alpha) / a0);
}

static float bqRun(Biquad* q, float x)
{
    /* transposed direct form II */
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x;
    q->y2 = q->y1; q->y1 = y;
    return y;
}

SndHp* SndHpCreate(float fc, int sr)
{
    if (sr <= 0) sr = SND_SR;
    if (fc <= 0.0f) fc = 1000.0f;
    SndHp* f = (SndHp*)calloc(1, sizeof(SndHp));
    if (!f) return NULL;
    /* 4 阶 Butterworth = 两级 2 阶（Q 值固定） */
    bqHighpass(&f->s1, fc, sr, 0.5411961001461969);
    bqHighpass(&f->s2, fc, sr, 1.3065629648763766);
    return f;
}

void SndHpFree(SndHp* f) { free(f); }

void SndHpRun(SndHp* f, float* x, int n)
{
    if (!f) return;
    for (int i_r2 = 0; i_r2 < n; i_r2++) {
        float v = bqRun(&f->s1, x[i_r2]);
        x[i_r2] = bqRun(&f->s2, v);
    }
}

/* ============================ 重采样 ============================ */

struct SndRs {
    int    src, dst;
    double step;      /* src/dst */
    double cut;       /* 归一化截止（相对输入 Nyquist） */
    int    half;      /* 单边抽头数 */
    float* buf;
    int    cap, bufN;
    double t;         /* 当前位置，相对于 buf 起点 */
};

HSNTRS SndRsCreate(int srcRate)
{
    if (srcRate <= 0) srcRate = 48000;
    SndRs* r = (SndRs*)calloc(1, sizeof(SndRs));
    if (!r) return NULL;
    r->src = srcRate;
    r->dst = SND_SR;
    r->step = (double)srcRate / (double)SND_SR;
    r->cut = srcRate > SND_SR ? (double)SND_SR / (double)srcRate : 1.0;
    r->half = 8;
    r->cap = 8192;
    r->buf = (float*)calloc(r->cap, sizeof(float));
    if (!r->buf) { free(r); return NULL; }
    r->bufN = 0;
    r->t = 0.0;
    return (HSNTRS)r;
}

void SndRsFree(HSNTRS h)
{
    SndRs* r = (SndRs*)h;
    if (!r) return;
    free(r->buf);
    free(r);
}

static int rsEnsure(SndRs* r, int need)
{
    if (need <= r->cap) return 1;
    int cap = r->cap;
    while (cap < need) cap <<= 1;
    float* p = (float*)realloc(r->buf, sizeof(float) * cap);
    if (!p) return 0;
    r->buf = p;
    r->cap = cap;
    return 1;
}

int SndRsRun(HSNTRS h, const float* in, int inN, float* out)
{
    SndRs* r = (SndRs*)h;
    if (!r || !out) return 0;
    if (inN <= 0) return 0;
    if (!rsEnsure(r, r->bufN + inN)) return 0;
    memcpy(r->buf + r->bufN, in, sizeof(float) * inN);
    r->bufN += inN;

    int produced = 0;
    while (r->t + r->half < (double)r->bufN) {
        int base = (int)floor(r->t);
        double acc = 0.0;
        for (int j_r1 = -r->half + 1; j_r1 <= r->half; j_r1++) {
            int idx = base + j_r1;
            if (idx < 0 || idx >= r->bufN) continue;
            double x = r->t - (double)idx;
            double xc = x * r->cut;
            double sinc = fabs(xc) < 1e-9 ? 1.0 : sin(M_PI * xc) / (M_PI * xc);
            double wn = x / (r->half + 1.0);
            double w = 0.5 + 0.5 * cos(M_PI * wn);   /* Hann */
            acc += r->buf[idx] * sinc * w * r->cut;
        }
        out[produced++] = (float)acc;
        r->t += r->step;
    }

    int drop = (int)floor(r->t) - r->half;
    if (drop > 0) {
        if (drop > r->bufN) drop = r->bufN;
        memmove(r->buf, r->buf + drop, sizeof(float) * (r->bufN - drop));
        r->bufN -= drop;
        r->t -= drop;
    }
    return produced;
}
