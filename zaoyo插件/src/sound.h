/* sound.h - 音效模板匹配引擎（32 位，纯 Win32，本插件内部使用）
 *
 * 移植自 Wattls/NTESoundTrigger（GPL-3.0）的识别管线：
 *   32kHz 单声道 -> 4 阶 1kHz 高通 -> 0.5s 滑动窗 RMS 归一化 -> FFT 互相关取最大值
 * 模板直接读该项目的 *_32000.npy（已含零相位高通处理，float64 一维数组）。
 */
#ifndef ZAOYO_SOUND_H
#define ZAOYO_SOUND_H

#define SND_SR 32000

#ifdef __cplusplus
extern "C" {
#endif

typedef void* HSNDTPL;
typedef void* HSNDDET;
typedef void* HSNTRS;
typedef struct SndHp SndHp;

/* ---- 模板（.npy，float64/float32 一维，32kHz）---- */
HSNDTPL SndTplLoad(const wchar_t* path);
/* 从内存里的 float32 裸数组建模板（bytes = 采样数*4） */
HSNDTPL SndTplLoadF32(const void* data, int bytes);
void    SndTplFree(HSNDTPL t);
int     SndTplSamples(HSNDTPL t);      /* 采样点数（32kHz） */
float   SndTplPeak(HSNDTPL t);

/* ---- 检测器 ---- */
HSNDDET SndDetCreate(HSNDTPL t, float thresh, float cooldownSec);
void    SndDetFree(HSNDDET d);
void    SndDetReset(HSNDDET d);
/* 喂一段 32kHz 单声道样本，返回本帧分数；命中时 *fired=1 */
float   SndDetFeed(HSNDDET d, const float* mono, int n, int* fired);
float   SndDetScore(HSNDDET d);        /* 最近一次分数 */
int     SndDetHits(HSNDDET d);         /* 累计命中次数 */
void    SndDetSetThresh(HSNDDET d, float thresh);

/* ---- 4 阶 Butterworth 高通（默认 1000Hz @32kHz）---- */
SndHp*  SndHpCreate(float fc, int sr);
void    SndHpFree(SndHp* f);
void    SndHpRun(SndHp* f, float* x, int n);   /* 原地，保持帧间状态 */

/* ---- 重采样到 32kHz（任意比，窗函数 sinc）---- */
HSNTRS  SndRsCreate(int srcRate);
void    SndRsFree(HSNTRS r);
int     SndRsRun(HSNTRS r, const float* in, int inN, float* out); /* out 至少 ceil(inN*32000/srcRate)+8 */

#ifdef __cplusplus
}
#endif
#endif
