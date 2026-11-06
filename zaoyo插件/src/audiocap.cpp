#include "vc6compat.h"
/* audiocap.cpp - WASAPI 环回采集实现 */
#include <windows.h>
#include "wasapi_vc6.h"   /* VC6: 手写 WASAPI 声明（无 SDK 头）*/
#include <mmreg.h>
#include "audiocap.h"
#include "sound.h"

#ifndef KSDATAFORMAT_SUBTYPE_PCM_G
static const GUID SUB_PCM = { 0x00000001, 0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID SUB_FLOAT = { 0x00000003, 0x0000, 0x0010,
    { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
#endif

static const GUID G_CLSID_MMDE = { 0xBCDE0395, 0xE52F, 0x467C,
    { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID G_IID_MMDE = { 0xA95664D2, 0x9614, 0x4F35,
    { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID G_IID_AC = { 0x1CB9AD4C, 0xDBFA, 0x4C32,
    { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const GUID G_IID_ACC = { 0xC8ADBD64, 0xE71E, 0x48A0,
    { 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17 } };

struct AudioCap {
    volatile LONG stop;
    HANDLE  th;
    SndFrameCb cb;
    void*   ctx;
    int     chunk;
    int     rate;
    int     channels;
    volatile LONG failed;
};

/* ---------- 音频格式识别 ---------- */
static int fmtIsFloat(const WAVEFORMATEX* wf)
{
    if (!wf) return 0;
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return 1;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* we = (const WAVEFORMATEXTENSIBLE*)wf;
        return IsEqualGUID(we->SubFormat, SUB_FLOAT) ? 1 : 0;
    }
    return 0;
}

static void convertToMono(const BYTE* data, UINT32 frames, const WAVEFORMATEX* wf,
                          int isFloat, float* out)
{
    int ch = wf->nChannels;
    int bits = wf->wBitsPerSample;
    if (ch < 1) ch = 1;
    for (UINT32 i = 0; i < frames; i++) {
        double acc = 0.0;
        for (int c = 0; c < ch; c++) {
            const BYTE* p = data + ((size_t)i * ch + c) * (bits / 8);
            double v = 0.0;
            if (isFloat) {
                if (bits == 32) v = *(const float*)p;
                else if (bits == 64) v = *(const double*)p;
            } else {
                if (bits == 16) v = (*(const short*)p) / 32768.0;
                else if (bits == 8) v = (p[0] - 128) / 128.0;
                else if (bits == 24) {
                    int iv = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16);
                    if (iv & 0x800000) iv |= ~0xFFFFFF;
                    v = iv / 8388608.0;
                } else if (bits == 32) {
                    v = (*(const int*)p) / 2147483648.0;
                }
            }
            acc += v;
        }
        out[i] = (float)(acc / ch);
    }
}

/* ---------- 采集主循环（单次打开设备）---------- */
static int capOpenAndRun(AudioCap* c)
{
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioClient* ac = NULL;
    IAudioCaptureClient* cc = NULL;
    WAVEFORMATEX* wf = NULL;
    HSNTRS rs = NULL;
    float* mono = NULL;
    float* res = NULL;
    float* acc = NULL;
    int accN = 0;
    int result = 0;

    HRESULT hr = CoCreateInstance(G_CLSID_MMDE, NULL, CLSCTX_ALL, G_IID_MMDE, (void**)&en);
    if (FAILED(hr) || !en) goto done;
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (FAILED(hr) || !dev) goto done;
    hr = dev->Activate(G_IID_AC, CLSCTX_ALL, NULL, (void**)&ac);
    if (FAILED(hr) || !ac) goto done;
    hr = ac->GetMixFormat(&wf);
    if (FAILED(hr) || !wf) goto done;

    c->rate = (int)wf->nSamplesPerSec;
    c->channels = wf->nChannels;

    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                        2000000, 0, wf, NULL);
    if (FAILED(hr)) goto done;
    hr = ac->GetService(G_IID_ACC, (void**)&cc);
    if (FAILED(hr) || !cc) goto done;

    {
        int isFloat = fmtIsFloat(wf);
        int maxFrames = (int)wf->nSamplesPerSec;          /* 1s 上限 */
        mono = (float*)malloc(sizeof(float) * maxFrames);
        res  = (float*)malloc(sizeof(float) * (int)((double)maxFrames * SND_SR / c->rate + 64));
        acc  = (float*)malloc(sizeof(float) * (c->chunk * 2 + 4096));
        rs = SndRsCreate(c->rate);
        if (!mono || !res || !acc || !rs) goto done;

        hr = ac->Start();
        if (FAILED(hr)) goto done;
        result = 1;                                        /* 设备已就绪 */

        while (!c->stop) {
            UINT32 packet = 0;
            if (FAILED(cc->GetNextPacketSize(&packet))) goto done;
            if (packet == 0) { Sleep(4); continue; }
            while (packet) {
                BYTE* data = NULL;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = cc->GetBuffer(&data, &frames, &flags, NULL, NULL);
                if (FAILED(hr)) goto done;
                if (frames > (UINT32)maxFrames) frames = (UINT32)maxFrames;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    memset(mono, 0, sizeof(float) * frames);
                } else {
                    convertToMono(data, frames, wf, isFloat, mono);
                }
                cc->ReleaseBuffer(frames);

                int got = SndRsRun(rs, mono, (int)frames, res);
                if (got > 0) {
                    if (accN + got > c->chunk * 2 + 4096) got = 0;   /* 防御 */
                    if (got > 0) {
                        memcpy(acc + accN, res, sizeof(float) * got);
                        accN += got;
                        while (accN >= c->chunk) {
                            if (c->cb) c->cb(c->ctx, acc, c->chunk);
                            memmove(acc, acc + c->chunk, sizeof(float) * (accN - c->chunk));
                            accN -= c->chunk;
                        }
                    }
                }
                if (FAILED(cc->GetNextPacketSize(&packet))) goto done;
            }
        }
        ac->Stop();
    }

done:
    if (rs) SndRsFree(rs);
    free(mono); free(res); free(acc);
    if (wf) CoTaskMemFree(wf);
    if (cc) cc->Release();
    if (ac) ac->Release();
    if (dev) dev->Release();
    if (en) en->Release();
    return result;
}

static DWORD WINAPI capThread(LPVOID p)
{
    AudioCap* c = (AudioCap*)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (!c->stop) {
        int ok = capOpenAndRun(c);
        if (!ok && !c->stop) {
            InterlockedExchange(&c->failed, 1);
            Sleep(1000);
        } else if (ok && !c->stop) {
            Sleep(200);              /* 设备切换后重启 */
        }
    }
    CoUninitialize();
    return 0;
}

HAUDIOCAP AudioCapStart(SndFrameCb cb, void* ctx, int chunkSamples)
{
    AudioCap* c = (AudioCap*)calloc(1, sizeof(AudioCap));
    if (!c) return NULL;
    c->cb = cb;
    c->ctx = ctx;
    c->chunk = chunkSamples > 0 ? chunkSamples : (SND_SR / 12);
    c->stop = 0;
    c->th = CreateThread(NULL, 0, capThread, c, 0, NULL);
    if (!c->th) { free(c); return NULL; }
    return (HAUDIOCAP)c;
}

void AudioCapStop(HAUDIOCAP h)
{
    AudioCap* c = (AudioCap*)h;
    if (!c) return;
    InterlockedExchange(&c->stop, 1);
    if (c->th) {
        WaitForSingleObject(c->th, 3000);
        CloseHandle(c->th);
        c->th = NULL;
    }
    free(c);
}

int AudioCapDeviceRate(HAUDIOCAP h) { AudioCap* c = (AudioCap*)h; return c ? c->rate : 0; }
int AudioCapDeviceChannels(HAUDIOCAP h) { AudioCap* c = (AudioCap*)h; return c ? c->channels : 0; }
int AudioCapRunning(HAUDIOCAP h) { AudioCap* c = (AudioCap*)h; return c ? (c->failed ? 0 : 1) : 0; }
