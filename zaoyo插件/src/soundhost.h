/* soundhost.h - 音效识别宿主（采集线程 + 两个检测器 + 事件通知） */
#ifndef ZAOYO_SOUNDHOST_H
#define ZAOYO_SOUNDHOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 0=启动成功/已在运行, 负值=失败 */
int  SndHostStart(void* hInst);
int  SndHostStop(void);
int  SndHostRunning(void);
/* 等待命中：2=闪避, 3=反击, 0=超时, -1=未启动
   maxAgeMs: 命中后可保留的最大毫秒数，超过则丢弃（<=0 表示不限制） */
int  SndHostWait(int ms, int maxAgeMs);
/* 最近一次分数（可为 NULL） */
void SndHostScore(float* dodge, float* counter);
int  SndHostRate(void);
int  SndHostChannels(void);
/* DllMain detach 用：只置停止标志，不碰窗口/GDI */
void SndHostShutdown(void);

#ifdef __cplusplus
}
#endif
#endif
