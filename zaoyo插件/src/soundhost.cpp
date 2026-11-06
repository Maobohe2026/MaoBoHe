#include "vc6compat.h"
/* soundhost.cpp - 音效识别宿主：WASAPI 环回 -> 高通 -> 双模板匹配 -> 事件
 *
 * 阈值是按 165s 实战录音标定的（见 build/snd_calibration.md）：
 *   闪避 0.35（真实峰值 0.68~0.74，非事件 <0.05）
 *   反击 0.25（真实峰值 0.63~0.65，非事件最高 0.131）
 * 采样块 80ms，与标定时一致。
 */
#include <windows.h>
#include "sound.h"
#include "audiocap.h"
#include "soundhost.h"

#define SND_TH_DODGE    0.20f
#define SND_TH_COUNTER  0.18f
#define SND_CD_DODGE    0.5f
#define SND_CD_COUNTER  1.2f
#define SND_CHUNK       2560          /* 80ms @32000 */
#define IDR_SND_DODGE   301
#define IDR_SND_COUNTER 302

struct SndHost {
    volatile LONG  state;            /* 0=停止 1=运行 2=启动中 */
    HANDLE         evt;
    CRITICAL_SECTION cs;
    HAUDIOCAP      cap;
    HSNDTPL        tpl[2];
    HSNDDET        det[2];
    SndHp*         hp;
    volatile LONG  pending;          /* bit0=闪避 bit1=反击 */
    volatile LONG  tickDodge;        /* 命中时刻（GetTickCount） */
    volatile LONG  tickCounter;
    float          score[2];
    int            rate, ch;
};

static SndHost g_snd;

static void hostReset()
{
    for (int i_r2 = 0; i_r2 < 2; i_r2++) {
        if (g_snd.det[i_r2]) { SndDetFree(g_snd.det[i_r2]); g_snd.det[i_r2] = NULL; }
        if (g_snd.tpl[i_r2]) { SndTplFree(g_snd.tpl[i_r2]); g_snd.tpl[i_r2] = NULL; }
    }
    if (g_snd.hp) { SndHpFree(g_snd.hp); g_snd.hp = NULL; }
    g_snd.score[0] = g_snd.score[1] = 0.0f;
    g_snd.pending = 0;
}

/* 采集线程回调：mono 为 32kHz 单声道、未被高通处理 */
static void hostOnFrame(void* ctx, const float* mono, int n)
{
    SndHost* h = &g_snd;
    (void)ctx;
    if (!h->hp || !h->det[0] || !h->det[1]) return;

    SndHpRun(h->hp, (float*)mono, n);

    int fired = 0;
    float sd = SndDetFeed(h->det[0], mono, n, &fired);
    h->score[0] = sd;
    LONG pend = 0;
    if (fired) pend |= 1;

    fired = 0;
    float sc = SndDetFeed(h->det[1], mono, n, &fired);
    h->score[1] = sc;
    if (fired) pend |= 2;

    if (pend) {
        DWORD now = GetTickCount();
        if (pend & 1) InterlockedExchange(&h->tickDodge, (LONG)now);
        if (pend & 2) InterlockedExchange(&h->tickCounter, (LONG)now);
        InterlockedOr(&h->pending, pend);
        SetEvent(h->evt);
    }
}

int SndHostStart(void* hInst)
{
    if (InterlockedCompareExchange(&g_snd.state, 2, 0) != 0)
        return g_snd.state == 1 ? 0 : -1;          /* 已在运行/正在启动 */

    if (!g_snd.evt) g_snd.evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_snd.evt) { g_snd.state = 0; return -2; }

    HINSTANCE hi = (HINSTANCE)hInst;
    HRSRC rd = FindResourceW(hi, MAKEINTRESOURCEW(IDR_SND_DODGE), MAKEINTRESOURCEW(10));
    HRSRC rc = FindResourceW(hi, MAKEINTRESOURCEW(IDR_SND_COUNTER), MAKEINTRESOURCEW(10));
    if (!rd || !rc) { g_snd.state = 0; return -3; }
    HGLOBAL hd = LoadResource(hi, rd);
    HGLOBAL hc = LoadResource(hi, rc);
    void* pd = hd ? LockResource(hd) : NULL;
    void* pc = hc ? LockResource(hc) : NULL;
    if (!pd || !pc) { g_snd.state = 0; return -3; }

    g_snd.tpl[0] = SndTplLoadF32(pd, (int)SizeofResource(hi, rd));
    g_snd.tpl[1] = SndTplLoadF32(pc, (int)SizeofResource(hi, rc));
    if (!g_snd.tpl[0] || !g_snd.tpl[1]) { hostReset(); g_snd.state = 0; return -4; }

    g_snd.hp = SndHpCreate(1000.0f, SND_SR);
    g_snd.det[0] = SndDetCreate(g_snd.tpl[0], SND_TH_DODGE, SND_CD_DODGE);
    g_snd.det[1] = SndDetCreate(g_snd.tpl[1], SND_TH_COUNTER, SND_CD_COUNTER);
    if (!g_snd.hp || !g_snd.det[0] || !g_snd.det[1]) { hostReset(); g_snd.state = 0; return -5; }

    g_snd.pending = 0;
    g_snd.tickDodge = 0;
    g_snd.tickCounter = 0;
    g_snd.score[0] = g_snd.score[1] = 0.0f;
    g_snd.cap = AudioCapStart(hostOnFrame, &g_snd, SND_CHUNK);
    if (!g_snd.cap) { hostReset(); g_snd.state = 0; return -6; }

    /* 等设备信息（最多 1.5 秒） */
    for (int i_r1 = 0; i_r1 < 30; i_r1++) {
        if (AudioCapDeviceRate(g_snd.cap) > 0) break;
        Sleep(50);
    }
    g_snd.rate = AudioCapDeviceRate(g_snd.cap);
    g_snd.ch = AudioCapDeviceChannels(g_snd.cap);

    g_snd.state = 1;
    return 0;
}

int SndHostStop(void)
{
    if (InterlockedExchange(&g_snd.state, 0) == 0) return 0;
    if (g_snd.cap) {
        AudioCapStop(g_snd.cap);
        g_snd.cap = NULL;
    }
    hostReset();
    return 0;
}

int SndHostRunning(void) { return g_snd.state == 1; }
int SndHostRate(void) { return g_snd.rate; }
int SndHostChannels(void) { return g_snd.ch; }

int SndHostWait(int ms, int maxAgeMs)
{
    if (g_snd.state != 1 || !g_snd.evt) return -1;
    if (ms < 0) ms = 0;

    DWORD start = GetTickCount();
    for (;;) {
        DWORD elapsed = GetTickCount() - start;
        int remain = ms > 0 ? ms - (int)elapsed : 0;
        if (remain < 0) remain = 0;

        DWORD r = WaitForSingleObject(g_snd.evt, (DWORD)remain);
        if (r != WAIT_OBJECT_0) return 0;             /* 超时 */

        LONG pend = InterlockedExchange(&g_snd.pending, 0);
        if (!pend) {
            if (ms == 0) return 0;                    /* 非阻塞：没东西就回 */
            continue;
        }

        DWORD now = GetTickCount();
        LONG which = 0;
        if (pend & 1) {
            DWORD age = now - (DWORD)g_snd.tickDodge;
            if (maxAgeMs <= 0 || age <= (DWORD)maxAgeMs) which = 1;   /* 否则丢弃 */
        }
        if (!which && (pend & 2)) {
            DWORD age = now - (DWORD)g_snd.tickCounter;
            if (maxAgeMs <= 0 || age <= (DWORD)maxAgeMs) which = 2;
        }
        if (which == 1) return 2;                     /* 闪避优先 */
        if (which == 2) return 3;

        if (ms == 0) return 0;                        /* 全过期 && 非阻塞 */
    }
}

void SndHostScore(float* dodge, float* counter)
{
    if (dodge) *dodge = g_snd.score[0];
    if (counter) *counter = g_snd.score[1];
}

void SndHostShutdown(void)
{
    /* DLL_PROCESS_DETACH：加载器锁内，只置标志，不做任何等待以外的操作 */
    if (g_snd.state == 0) return;
    g_snd.state = 0;
    if (g_snd.cap) {
        AudioCapStop(g_snd.cap);
        g_snd.cap = NULL;
    }
    hostReset();
}
