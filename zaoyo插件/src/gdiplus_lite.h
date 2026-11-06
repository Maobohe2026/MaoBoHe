/* ============================================================
   gdiplus_lite.h —— 用 gdiplus.dll 的扁平 C 接口加载图片，
   不 include 任何 GDI+ 头文件（VC6 没有 gdiplus.h）。
   只做一件事：把 bmp/jpg/png/gif 文件读成 32bppARGB 像素。
   ============================================================ */
#ifndef GDIPLUS_LITE_H
#define GDIPLUS_LITE_H

#include <windows.h>
#ifndef _ULONG_PTR_DEFINED_
typedef unsigned long ULONG_PTR_vc6;
#define ULONG_PTR ULONG_PTR_vc6
#define _ULONG_PTR_DEFINED_
#endif

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GdipStartupInputLite;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    INT32  Stride;
    INT32  PixelFormat;
    void  *Scan0;
    UINT_PTR Reserved;
} GdipBitmapDataLite;

typedef int (WINAPI *PFN_GdiplusStartup)(ULONG_PTR *token, const void *input, void *output);
typedef int (WINAPI *PFN_GdipCreateBitmapFromFile)(const WCHAR *filename, void **bitmap);
typedef int (WINAPI *PFN_GdipDisposeImage)(void *image);
typedef int (WINAPI *PFN_GdipGetImageWidth)(void *image, UINT *width);
typedef int (WINAPI *PFN_GdipGetImageHeight)(void *image, UINT *height);
typedef int (WINAPI *PFN_GdipBitmapLockBits)(void *bitmap, const void *rect, UINT flags,
                                             INT32 format, void *lockedBitmapData);
typedef int (WINAPI *PFN_GdipBitmapUnlockBits)(void *bitmap, void *lockedBitmapData);

typedef struct {
    HMODULE hMod;
    PFN_GdiplusStartup          Startup;
    PFN_GdipCreateBitmapFromFile CreateFromFile;
    PFN_GdipDisposeImage        DisposeImage;
    PFN_GdipGetImageWidth       GetWidth;
    PFN_GdipGetImageHeight      GetHeight;
    PFN_GdipBitmapLockBits      LockBits;
    PFN_GdipBitmapUnlockBits    UnlockBits;
    ULONG_PTR token;
    int inited;
} GdipLite;

#define GDIP_LITE_PIXFMT_32BPPARGB 0x00026200
#define GDIP_LITE_LOCK_READ        1

static GdipLite g_gdipLite = {0};

static int GdipLiteInit(void)
{
    if (g_gdipLite.inited) return 1;
    g_gdipLite.hMod = LoadLibraryA("gdiplus.dll");
    if (!g_gdipLite.hMod) return 0;
    g_gdipLite.Startup = (PFN_GdiplusStartup)GetProcAddress(g_gdipLite.hMod, "GdiplusStartup");
    g_gdipLite.CreateFromFile = (PFN_GdipCreateBitmapFromFile)GetProcAddress(g_gdipLite.hMod, "GdipCreateBitmapFromFile");
    g_gdipLite.DisposeImage = (PFN_GdipDisposeImage)GetProcAddress(g_gdipLite.hMod, "GdipDisposeImage");
    g_gdipLite.GetWidth = (PFN_GdipGetImageWidth)GetProcAddress(g_gdipLite.hMod, "GdipGetImageWidth");
    g_gdipLite.GetHeight = (PFN_GdipGetImageHeight)GetProcAddress(g_gdipLite.hMod, "GdipGetImageHeight");
    g_gdipLite.LockBits = (PFN_GdipBitmapLockBits)GetProcAddress(g_gdipLite.hMod, "GdipBitmapLockBits");
    g_gdipLite.UnlockBits = (PFN_GdipBitmapUnlockBits)GetProcAddress(g_gdipLite.hMod, "GdipBitmapUnlockBits");
    if (!g_gdipLite.Startup || !g_gdipLite.CreateFromFile || !g_gdipLite.DisposeImage ||
        !g_gdipLite.GetWidth || !g_gdipLite.GetHeight || !g_gdipLite.LockBits || !g_gdipLite.UnlockBits) {
        FreeLibrary(g_gdipLite.hMod);
        ZeroMemory(&g_gdipLite, sizeof(g_gdipLite));
        return 0;
    }
    {
        GdipStartupInputLite si;
        ZeroMemory(&si, sizeof(si));
        si.GdiplusVersion = 1;
        if (g_gdipLite.Startup(&g_gdipLite.token, &si, NULL) != 0) {
            FreeLibrary(g_gdipLite.hMod);
            ZeroMemory(&g_gdipLite, sizeof(g_gdipLite));
            return 0;
        }
    }
    g_gdipLite.inited = 1;
    return 1;
}

/* 把图片文件读成 32bppARGB，回调给像素（BGRA 顺序），失败返回 0 */
static int GdipLiteLoad32(const WCHAR *path, UINT *w, UINT *h, void **ppvPixels, int *pStride)
{
    void *bmp = NULL;
    INT32 rc[4];
    GdipBitmapDataLite bd;
    UINT ww = 0, hh = 0;
    if (!GdipLiteInit()) return 0;
    if (g_gdipLite.CreateFromFile(path, &bmp) != 0 || !bmp) return 0;
    if (g_gdipLite.GetWidth(bmp, &ww) != 0 || g_gdipLite.GetHeight(bmp, &hh) != 0 || !ww || !hh) {
        g_gdipLite.DisposeImage(bmp);
        return 0;
    }
    rc[0] = 0; rc[1] = 0; rc[2] = (INT32)ww; rc[3] = (INT32)hh;
    ZeroMemory(&bd, sizeof(bd));
    if (g_gdipLite.LockBits(bmp, rc, GDIP_LITE_LOCK_READ, GDIP_LITE_PIXFMT_32BPPARGB, &bd) != 0) {
        g_gdipLite.DisposeImage(bmp);
        return 0;
    }
    *w = ww; *h = hh;
    *ppvPixels = bd.Scan0;      /* 直接用，调用方读完必须 Unlock */
    *pStride = bd.Stride;
    /* 注意：这里故意不 Unlock / 不 Dispose —— 由调用方通过 GdipLiteRelease 释放 */
    return 1;
}

/* 释放 GdipLiteLoad32 拿到的资源 */
static void GdipLiteRelease(void *bmp, void *scan0)
{
    (void)scan0;
    if (bmp && g_gdipLite.inited) g_gdipLite.DisposeImage(bmp);
}

#endif /* GDIPLUS_LITE_H */
