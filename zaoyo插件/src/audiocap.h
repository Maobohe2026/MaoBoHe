/* audiocap.h - WASAPI 环回采集（系统输出混音），后台线程回调 32kHz 单声道分块 */
#ifndef ZAOYO_AUDIOCAP_H
#define ZAOYO_AUDIOCAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HAUDIOCAP;
/* mono: 32kHz 单声道 float（未经高通）；从采集线程回调，禁止在回调里做重活 */
typedef void (*SndFrameCb)(void* ctx, const float* mono, int n);

HAUDIOCAP AudioCapStart(SndFrameCb cb, void* ctx, int chunkSamples);
void     AudioCapStop(HAUDIOCAP c);
int      AudioCapDeviceRate(HAUDIOCAP c);   /* 设备混音采样率，失败 0 */
int      AudioCapDeviceChannels(HAUDIOCAP c);
int      AudioCapRunning(HAUDIOCAP c);

#ifdef __cplusplus
}
#endif
#endif
