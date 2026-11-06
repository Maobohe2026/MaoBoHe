/* ============================================================
   features.h —— 附件解析 + 多点颜色匹配（供 finder.cpp 调用）
   ============================================================ */
#ifndef QM_FEATURES_H
#define QM_FEATURES_H

#include <windows.h>

/* 一个颜色采样点：相对偏移 + 期望颜色 */
struct QmSample {
    int dx, dy;
    unsigned char r, g, b;
    int neg;                  /* 1 = 负色（该点不能是这个颜色） */
};

/* 把 "Attachment:\x.bmp" 解析成真实磁盘路径：
   先从 %TEMP%\QmFindPic\att 缓存找，找不到就扫描 按键精灵\QMScript 下所有 .Q
   的 [Attachment] 段落（base64 的 zip），解出该文件并缓存。 */
bool QmResolveAttachment(const char *name, char *outPath, int cap);

/* 抓屏为 32bpp BGRA */
bool QmCaptureRGB(int x, int y, int w, int h, unsigned char **outPx, int *outW, int *outH);

/* 多点颜色匹配：在 BGRA 图里找 pts[0]（位于 (0,0)）及其余偏移点
   返回命中数；outX/outY 是相对图像左上角的坐标，outScore 是 0~1 相似度 */
int QmFindColorMulti(const unsigned char *bgra, int W, int H,
                     const QmSample *pts, int nPts, double sim,
                     int wantAll, int maxOut, int dir,
                     int *outX, int *outY, double *outScore);

/* 扇形角度检测：
   cx,cy=圆心（屏幕坐标），r1/r2=环内外半径，sens=灵敏度 0~100，
   calib=1 时自动微调圆心（±4 像素，取角度集中度最大的位置）
   返回 1 成功；角度存 outAngle（0=正上，顺时针 0~360），
   outConc=集中度 0~1，outCover=环内覆盖率 0~1，outCx/outCy=实际使用的圆心 */
int QmSectorAngle(const unsigned char *bgra, int W, int H,
                  int cx, int cy, int r1, int r2, int sens, int calib,
                  double *outAngle, double *outConc, double *outCover,
                  int *outCx, int *outCy);

/* 径向梯度法（v1.8，推荐）：扇形靠圆心最亮、向外衰减；
   返回 1 成功，outConc=峰强度，outCover=角段宽度占比 */
int QmSectorAngleGrad(const unsigned char *bgra, int W, int H,
                      int cx, int cy, int r1, int r2, int sens,
                      double *outAngle, double *outConc, double *outCover);

/* 箭头朝向（v1.9）：亮黄连通域 + 最远点对 + 左少右多定尖头
   返回 1 成功；outAngle 已减去 baseAngle，outPixels=像素数，outLength=长度 */
int QmArrowAngle(const unsigned char *bgra, int W, int H,
                 int cx, int cy, int radius, double baseAngle,
                 double *outAngle, int *outPixels, double *outLength);

#endif
