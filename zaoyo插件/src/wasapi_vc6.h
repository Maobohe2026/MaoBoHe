/* ============================================================
   wasapi_vc6.h —— VC6 用的 WASAPI 最小声明（手写 vtable，不用 SDK 头）
   只覆盖 audiocap.cpp 实际用到的接口与方法，顺序与 Windows 官方一致。
   ============================================================ */
#ifndef WASAPI_VC6_H
#define WASAPI_VC6_H

#include <objbase.h>        /* CoCreateInstance / CoTaskMemFree / IsEqualGUID */
#include <mmreg.h>         /* WAVEFORMATEX */

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

/* WAVEFORMATEXTENSIBLE：VC6 的 mmreg.h 里没有 */
typedef struct {
    WAVEFORMATEX Format;
    union {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID  SubFormat;
} WAVEFORMATEXTENSIBLE_VC6, *PWAVEFORMATEXTENSIBLE_VC6;

/* --- 枚举与常量 --- */
typedef enum { eRender_v = 0, eCapture_v = 1, eAll_v = 2 } EDataFlow_v;
typedef enum { eConsole_v = 0, eMultimedia_v = 1, eCommunications_v = 2 } ERole_v;

#define AUDCLNT_SHAREMODE_SHARED_v    0
#define AUDCLNT_SHAREMODE_EXCLUSIVE_v 1

#define AUDCLNT_STREAMFLAGS_CROSSPROCESS_v 0x00010000
#define AUDCLNT_STREAMFLAGS_LOOPBACK_v     0x00020000
#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK_v 0x00040000

#define AUDCLNT_BUFFERFLAGS_SILENT_v 0x00000002

#ifndef REFERENCE_TIME
typedef LONGLONG REFERENCE_TIME_v;
#else
typedef REFERENCE_TIME REFERENCE_TIME_v;
#endif

/* --- 前置声明（不用的接口就用 IUnknown 顶上，ABI 一致） --- */
struct IMMDeviceCollection;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

/* IMMDeviceEnumerator */
struct IMMDeviceEnumeratorVc6 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow_v dataFlow, DWORD dwStateMask,
                                                         struct IMMDeviceCollection **ppDevices) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow_v dataFlow, ERole_v role,
                                                              struct IMMDeviceVc6 **ppEndpoint) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR pwstrId, struct IMMDeviceVc6 **ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IUnknown *pClient) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IUnknown *pClient) = 0;
};

/* IMMDevice */
struct IMMDeviceVc6 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Activate(REFIID iid, DWORD dwClsCtx, void *pActivationParams,
                                               void **ppInterface) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD stgmAccess, IUnknown **ppProperties) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(LPWSTR *ppstrId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetState(DWORD *pdwState) = 0;
};

/* IAudioClient */
struct IAudioClientVc6 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(int ShareMode, DWORD StreamFlags,
                                                 REFERENCE_TIME_v hnsBufferDuration,
                                                 REFERENCE_TIME_v hnsPeriodicity,
                                                 const WAVEFORMATEX *pFormat,
                                                 const GUID *AudioSessionGuid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32 *pNumBufferFrames) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME_v *phnsLatency) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32 *pNumPaddingFrames) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsFormatSupported(int ShareMode, const WAVEFORMATEX *pFormat,
                                                        WAVEFORMATEX **ppClosestMatch) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX **ppDeviceFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME_v *phnsDefaultDevicePeriod,
                                                      REFERENCE_TIME_v *phnsMinimumDevicePeriod) = 0;
    virtual HRESULT STDMETHODCALLTYPE Start(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE eventHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void **ppv) = 0;
};

/* IAudioCaptureClient */
struct IAudioCaptureClientVc6 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE **ppData, UINT32 *pNumFramesToRead,
                                                DWORD *pdwFlags, UINT64 *pu64DevicePosition,
                                                UINT64 *pu64QPCPosition) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseBuffer(UINT32 NumFramesRead) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNextPacketSize(UINT32 *pNumFramesInNextPacket) = 0;
};

/* 让 audiocap.cpp 里的原始类型名能用（宏映射到 VC6 版本） */
#define IMMDeviceEnumerator     IMMDeviceEnumeratorVc6
#define IMMDevice               IMMDeviceVc6
#define IAudioClient            IAudioClientVc6
#define IAudioCaptureClient     IAudioCaptureClientVc6
#define WAVEFORMATEXTENSIBLE    WAVEFORMATEXTENSIBLE_VC6
#define eRender                 eRender_v
#define eCapture                eCapture_v
#define eConsole                eConsole_v
#define eMultimedia             eMultimedia_v
#define eCommunications         eCommunications_v
#define AUDCLNT_SHAREMODE_SHARED    AUDCLNT_SHAREMODE_SHARED_v
#define AUDCLNT_SHAREMODE_EXCLUSIVE AUDCLNT_SHAREMODE_EXCLUSIVE_v
#define AUDCLNT_STREAMFLAGS_LOOPBACK AUDCLNT_STREAMFLAGS_LOOPBACK_v
#define AUDCLNT_BUFFERFLAGS_SILENT   AUDCLNT_BUFFERFLAGS_SILENT_v
#define REFERENCE_TIME          REFERENCE_TIME_v

#endif /* WASAPI_VC6_H */
