/* sndtest.exe - 离线测试台：读 wav -> 32k 单声道 -> 高通 -> 逐帧打分
 * 用法: sndtest.exe <wav> <模板1.npy> [模板2.npy ...] [-q]
 * 输出: 每帧分数 + 末尾汇总（最大分 / 出现时间 / 超过 0.13 的次数）
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/sound.h"

#pragma pack(push, 1)
typedef struct {
    char  riff[4];
    DWORD size;
    char  wave[4];
} RiffHdr;
typedef struct {
    char  id[4];
    DWORD size;
} ChunkHdr;
#pragma pack(pop)

static int readWav(const wchar_t* path, float** outMono, int* outRate)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open fail\n"); return 0; }
    DWORD sz = GetFileSize(h, NULL);
    unsigned char* buf = (unsigned char*)malloc(sz);
    DWORD got = 0;
    ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    if (got != sz) { free(buf); return 0; }

    RiffHdr* rh = (RiffHdr*)buf;
    if (memcmp(rh->riff, "RIFF", 4) || memcmp(rh->wave, "WAVE", 4)) { free(buf); return 0; }

    int ch = 0, bits = 0, rate = 0, fmt = 0;
    unsigned char* data = NULL;
    DWORD dataLen = 0;
    DWORD off = sizeof(RiffHdr);
    while (off + 8 <= sz) {
        ChunkHdr* ck = (ChunkHdr*)(buf + off);
        DWORD body = off + 8;
        if (body + ck->size > sz) break;
        if (!memcmp(ck->id, "fmt ", 4)) {
            unsigned short* p = (unsigned short*)(buf + body);
            fmt = p[0];
            ch = p[1];
            rate = *(DWORD*)(buf + body + 4);
            bits = p[7];
            if (fmt == 0xFFFE && ck->size >= 40) {
                unsigned short sub = *(unsigned short*)(buf + body + 24);
                fmt = sub;
            }
        } else if (!memcmp(ck->id, "data", 4)) {
            data = buf + body;
            dataLen = ck->size;
        }
        off = body + ck->size + (ck->size & 1);
    }
    if (!data || !ch || !bits || !rate) { free(buf); return 0; }

    int bytes = bits / 8;
    if (bytes < 1) { free(buf); return 0; }
    int frames = dataLen / (bytes * ch);
    float* mono = (float*)malloc(sizeof(float) * frames);
    for (int i = 0; i < frames; i++) {
        double s = 0.0;
        for (int c = 0; c < ch; c++) {
            const unsigned char* p = data + (size_t)(i * ch + c) * bytes;
            double v = 0.0;
            if (fmt == 3) {
                v = bytes == 4 ? *(const float*)p : (bytes == 8 ? *(const double*)p : 0.0);
            } else if (fmt == 1) {
                if (bytes == 2) v = (*(const short*)p) / 32768.0;
                else if (bytes == 1) v = (p[0] - 128) / 128.0;
                else if (bytes == 3) {
                    int iv = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16);
                    if (iv & 0x800000) iv |= ~0xFFFFFF;
                    v = iv / 8388608.0;
                } else if (bytes == 4) {
                    v = (*(const int*)p) / 2147483648.0;
                }
            }
            s += v;
        }
        mono[i] = (float)(s / ch);
    }
    free(buf);
    *outMono = mono;
    *outRate = rate;
    return frames;
}

#define MAXTPL 4

int main()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 3) {
        printf("usage: sndtest.exe <wav> <tpl1.npy> [tpl2.npy ...] [-q]\n");
        return 1;
    }
    int quiet = 0;
    for (int i = 1; i < argc; i++) if (!wcscmp(argv[i], L"-q")) quiet = 1;

    float* raw = NULL;
    int rate = 0;
    int n = readWav(argv[1], &raw, &rate);
    if (n <= 0) { printf("wav read fail: %d\n", n); return 1; }

    /* 重采样到 32kHz（若本来就是则直通） */
    float* p32 = raw;
    int n32 = n;
    HSNTRS rs = NULL;
    if (rate != SND_SR) {
        rs = SndRsCreate(rate);
        if (!rs) { printf("resampler fail\n"); return 1; }
        int cap = (int)ceil((double)n * SND_SR / rate) + 16;
        p32 = (float*)malloc(sizeof(float) * cap);
        n32 = SndRsRun(rs, raw, n, p32);
        SndRsFree(rs);
    } else {
        p32 = (float*)malloc(sizeof(float) * n);
        memcpy(p32, raw, sizeof(float) * n);
    }

    SndHp* hp = SndHpCreate(1000.0f, SND_SR);
    SndHpRun(hp, p32, n32);

    HSNTRS dummy = NULL;
    (void)dummy;

    HSNDTPL tpl[MAXTPL];
    HSNDDET det[MAXTPL];
    int nt = 0;
    int tplNameArg[MAXTPL];
    for (int i = 2; i < argc && nt < MAXTPL; i++) {
        if (!wcscmp(argv[i], L"-q")) continue;
        HSNDTPL t = SndTplLoad(argv[i]);
        if (!t) { printf("[warn] 模板载入失败, 跳过\n"); continue; }
        tpl[nt] = t;
        det[nt] = SndDetCreate(t, 0.13f, 0.5f);
        tplNameArg[nt] = i;
        nt++;
    }
    if (!nt) { printf("no template\n"); return 1; }

    int step = (int)(SND_SR * 0.08);       /* 80ms 帧 */
    int maxScoreIdx[MAXTPL], maxCount[MAXTPL], overCnt[MAXTPL];
    float maxScore[MAXTPL];
    double maxTime[MAXTPL];
    for (int i = 0; i < nt; i++) {
        maxScore[i] = -1.0f; maxScoreIdx[i] = -1; maxCount[i] = 0; overCnt[i] = 0;
        maxTime[i] = 0.0;
    }

    for (int pos = 0; pos < n32; pos += step) {
        int m = n32 - pos < step ? n32 - pos : step;
        double t = (double)pos / SND_SR;
        if (!quiet) {
            printf("%7.3f", t);
            for (int i = 0; i < nt; i++) {
                int fired = 0;
                float sc = SndDetFeed(det[i], p32 + pos, m, &fired);
                printf("  %8.4f%s", sc, fired ? "*" : " ");
                if (sc > maxScore[i]) { maxScore[i] = sc; maxTime[i] = t; }
                if (sc >= 0.13f) overCnt[i]++;
            }
            printf("\n");
        } else {
            for (int i = 0; i < nt; i++) {
                int fired = 0;
                float sc = SndDetFeed(det[i], p32 + pos, m, &fired);
                if (sc > maxScore[i]) { maxScore[i] = sc; maxTime[i] = t; }
                if (sc >= 0.13f) overCnt[i]++;
            }
        }
    }

    printf("\n==== 汇总 (采样率 %d -> 32000, 时长 %.2fs) ====\n", rate, (double)n32 / SND_SR);
    for (int i = 0; i < nt; i++) {
        printf("模板%d %-40ls 最长=%d 点 | 最大分 %.4f @ %.2fs | >=0.13 帧数 %d\n",
               i + 1, argv[tplNameArg[i]] + 0, SndTplSamples(tpl[i]),
               maxScore[i], maxTime[i], overCnt[i]);
        (void)maxScoreIdx[i];
        SndDetFree(det[i]);
        SndTplFree(tpl[i]);
    }
    SndHpFree(hp);
    free(raw);
    free(p32);
    LocalFree(argv);
    return 0;
}
