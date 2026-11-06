#include "vc6compat.h"
/* ============================================================
   QmFindPic —— 按键精灵原生找图插件（ActiveX DLL）
   - 匹配算法：灰度金字塔 + 零均值归一化互相关(ZNCC)，
     对游戏渲染引起的轻微色差/亮度变化鲁棒
   - 支持 bmp/png/jpg（GDI+ 解码）
   - 多图查找：全部找到才成功，返回各图左上角 + 外接矩形中心
   - 命中后可画方框（置顶透明窗口，截屏前自动隐藏，不干扰识别）
   ============================================================ */
#include <windows.h>
#define WM_QM_DESTROY (WM_APP + 0x51)
#include <oleauto.h>
#include "gdiplus_lite.h"
#include <stdio.h>
#include <stdlib.h>
#include "qmids.h"
#include "features.h"
#include "soundhost.h"


static HINSTANCE g_hInst = NULL;

/* 自定义 new/delete：不链 libstdc++，体积小 */
void *operator new(size_t n)          { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
void *operator new[](size_t n)        { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
void  operator delete(void *p)        { if (p) HeapFree(GetProcessHeap(), 0, p); }
void  operator delete[](void *p)      { if (p) HeapFree(GetProcessHeap(), 0, p); }
void  operator delete(void *p, size_t){ if (p) HeapFree(GetProcessHeap(), 0, p); }

/* ---------------- 调试日志（仅 debug 版） ---------------- */
#ifdef QM_DEBUG
static void qmlog(const char *fmt, ...)
{
    char path[MAX_PATH], body[2048];
    GetModuleFileNameA(g_hInst, path, MAX_PATH);
    { char *p2 = strrchr(path, '\\'); if (p2) *(p2 + 1) = 0; }
    lstrcatA(path, "QmFindPic.log");
    SYSTEMTIME st; GetLocalTime(&st);
    int n = _snprintf(body, sizeof(body) - 1, "[%02d:%02d:%02d.%03d] ",
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    n += _vsnprintf(body + n, sizeof(body) - 1 - n, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    body[n++] = '\r'; body[n++] = '\n';
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; WriteFile(h, body, (DWORD)n, &w, NULL); CloseHandle(h);
}
#define LOG(...) qmlog(__VA_ARGS__)
#else
static void LOG(const char *fmt, ...) { (void)fmt; }
#endif

/* ================= 基础数据结构 ================= */
struct Gray {
    int w, h;
    unsigned char *px;
};

static void GrayFree(Gray *g) { if (g->px) { delete[] g->px; g->px = NULL; } g->w = g->h = 0; }

static bool GrayAlloc(Gray *g, int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    g->px = new unsigned char[w * h];
    if (!g->px) return false;
    g->w = w; g->h = h;
    return true;
}

/* ================= GDI+ 初始化 ================= */
static ULONG_PTR g_gdiToken = 0;
static bool g_gdiInited = false;

static void EnsureGdiPlus()
{
    if (g_gdiInited) return;
    if (GdipLiteInit()) g_gdiInited = true;
    else LOG("gdiplus.dll load failed");
}

/* ================= 图片加载（bmp/png/jpg）→ 灰度 ================= */
static bool LoadGrayImage(const wchar_t *path, Gray *out)
{
    EnsureGdiPlus();
    out->px = NULL; out->w = out->h = 0;

    UINT w = 0, h = 0;
    void *pixels = NULL;
    int stride = 0;
    if (!GdipLiteLoad32(path, &w, &h, &pixels, &stride) || !pixels) {
        LOG("LoadImage failed: %ls", path);
        return false;
    }
    if (!GrayAlloc(out, (int)w, (int)h)) return false;
    {
        const BYTE *base = (const BYTE *)pixels;
        int y;
        for (y = 0; y < (int)h; y++) {
            const BYTE *row = base + y * stride;
            unsigned char *o = out->px + y * (int)w;
            int x;
            for (x = 0; x < (int)w; x++) {
                const BYTE *p = row + x * 4;             /* BGRA */
                o[x] = (unsigned char)((p[2] * 77 + p[1] * 151 + p[0] * 28) >> 8);
            }
        }
    }
    return true;
}

/* ANSI 路径包装 */
static bool LoadGrayImageA(const char *path, Gray *out)
{
    wchar_t w[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, w, MAX_PATH)) return false;
    return LoadGrayImage(w, out);
}

/* ================= 屏幕捕获 ================= */
static bool CaptureRegion(int x, int y, int w, int h, Gray *out)
{
    out->px = NULL; out->w = out->h = 0;
    if (w <= 0 || h <= 0) return false;

    HDC hScreen = GetDC(NULL);
    if (!hScreen) return false;
    HDC hMem = CreateCompatibleDC(hScreen);
    if (!hMem) { ReleaseDC(NULL, hScreen); return false; }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;          /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(hMem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        if (hbm) DeleteObject(hbm);
        DeleteDC(hMem); ReleaseDC(NULL, hScreen);
        return false;
    }
    HGDIOBJ old = SelectObject(hMem, hbm);
    BitBlt(hMem, 0, 0, w, h, hScreen, x, y, SRCCOPY);
    GdiFlush();

    if (!GrayAlloc(out, w, h)) {
        SelectObject(hMem, old); DeleteObject(hbm); DeleteDC(hMem); ReleaseDC(NULL, hScreen);
        return false;
    }
    const BYTE *base = (const BYTE *)bits;
    for (int yy_r66 = 0; yy_r66 < h; yy_r66++) {
        const BYTE *row = base + yy_r66 * w * 4;
        unsigned char *o = out->px + yy_r66 * w;
        for (int xx_r65 = 0; xx_r65 < w; xx_r65++) {
            const BYTE *p = row + xx_r65 * 4;
            o[xx_r65] = (unsigned char)((p[2] * 77 + p[1] * 151 + p[0] * 28) >> 8);
        }
    }

    SelectObject(hMem, old);
    DeleteObject(hbm);
    DeleteDC(hMem);
    ReleaseDC(NULL, hScreen);
    return true;
}

/* ================= 降采样 ================= */
static bool Downscale(const Gray &src, int factor, Gray *dst)
{
    dst->px = NULL; dst->w = dst->h = 0;
    if (factor <= 1) {
        if (!GrayAlloc(dst, src.w, src.h)) return false;
        for (int i_r64 = 0; i_r64 < src.w * src.h; i_r64++) dst->px[i_r64] = src.px[i_r64];
        return true;
    }
    int w = src.w / factor, h = src.h / factor;
    if (w < 1 || h < 1) return false;
    if (!GrayAlloc(dst, w, h)) return false;
    for (int y_r63 = 0; y_r63 < h; y_r63++) {
        for (int x_r62 = 0; x_r62 < w; x_r62++) {
            int sum = 0;
            for (int dy_r61 = 0; dy_r61 < factor; dy_r61++) {
                const unsigned char *row = src.px + (y_r63 * factor + dy_r61) * src.w + x_r62 * factor;
                for (int dx_r60 = 0; dx_r60 < factor; dx_r60++) sum += row[dx_r60];
            }
            dst->px[y_r63 * w + x_r62] = (unsigned char)(sum / (factor * factor));
        }
    }
    return true;
}

/* ================= ZNCC 匹配 ================= */
struct Cand { int x, y; double score; };

static void SortCands(Cand *c, int n)
{
    for (int i_r59 = 1; i_r59 < n; i_r59++) {
        Cand key = c[i_r59];
        int j = i_r59 - 1;
        while (j >= 0 && c[j].score < key.score) { c[j + 1] = c[j]; j--; }
        c[j + 1] = key;
    }
}

/* 全分辨率、在 (ox,oy) 处的 ZNCC */
static double ZnccAt(const Gray &src, int ox, int oy, const Gray &tpl,
                     double sumT, double sumT2, double n)
{
    double sumS = 0, sumS2 = 0, sumTS = 0;
    for (int y_r58 = 0; y_r58 < tpl.h; y_r58++) {
        const unsigned char *sr = src.px + (oy + y_r58) * src.w + ox;
        const unsigned char *tr = tpl.px + y_r58 * tpl.w;
        for (int x_r57 = 0; x_r57 < tpl.w; x_r57++) {
            double u = (double)sr[x_r57], v = (double)tr[x_r57];
            sumS += u; sumS2 += u * u; sumTS += u * v;
        }
    }
    double varS = sumS2 - sumS * sumS / n;
    double varT = sumT2 - sumT * sumT / n;
    if (varS <= 1e-6 || varT <= 1e-6) return 0.0;
    double num = sumTS - sumS * sumT / n;
    double score = num / (sqrt(varS) * sqrt(varT));
    if (score < 0) score = 0;
    if (score > 1) score = 1;
    return score;
}

/* 在 src 中搜索 tpl：粗搜(降采样+积分图+整数累积) → 粗层NMS → 少量候选精定位 */
static int SearchImage(const Gray &src, const Gray &tpl, double sim, bool wantAll,
                       int maxOut, Cand *out, int dir)
{
    int cnt = 0;
    if (tpl.w < 2 || tpl.h < 2 || tpl.w > src.w || tpl.h > src.h) return 0;

    /* 粗搜缩放系数：模板越大缩得越狠 */
    int factor;
    if (tpl.w < 16 || tpl.h < 16)      factor = 1;
    else if (tpl.w < 40 || tpl.h < 40) factor = 2;
    else if (tpl.w < 80 || tpl.h < 80) factor = 4;
    else                               factor = 6;

    Gray s4, t4;
    ZeroMemory(&s4, sizeof(s4)); ZeroMemory(&t4, sizeof(t4));
    if (!Downscale(src, factor, &s4) || !Downscale(tpl, factor, &t4)) {
        GrayFree(&s4); GrayFree(&t4);
        return 0;
    }
    if (t4.w < 2 || t4.h < 2) { GrayFree(&s4); GrayFree(&t4); return 0; }

    int W = s4.w, H = s4.h, tw = t4.w, th = t4.h;
    if (W - tw + 1 <= 0 || H - th + 1 <= 0) { GrayFree(&s4); GrayFree(&t4); return 0; }

    /* 积分图（double，避免 int64 溢出问题） */
    int iw = W + 1, ih = H + 1;
    double *I1 = new double[iw * ih];
    double *I2 = new double[iw * ih];
    if (!I1 || !I2) { delete[] I1; delete[] I2; GrayFree(&s4); GrayFree(&t4); return 0; }
    for (int i_r56 = 0; i_r56 < iw; i_r56++) { I1[i_r56] = 0; I2[i_r56] = 0; }
    for (int y_r55 = 1; y_r55 < ih; y_r55++) {
        double r1 = 0, r2 = 0;
        I1[y_r55 * iw] = 0; I2[y_r55 * iw] = 0;
        const unsigned char *row = s4.px + (y_r55 - 1) * W;
        for (int x_r54 = 1; x_r54 < iw; x_r54++) {
            double v = (double)row[x_r54 - 1];
            r1 += v; r2 += v * v;
            I1[y_r55 * iw + x_r54] = I1[(y_r55 - 1) * iw + x_r54] + r1;
            I2[y_r55 * iw + x_r54] = I2[(y_r55 - 1) * iw + x_r54] + r2;
        }
    }

    double n = (double)tw * th, sumT = 0, sumT2 = 0;
    for (int i_r53 = 0; i_r53 < tw * th; i_r53++) {
        double v = (double)t4.px[i_r53];
        sumT += v; sumT2 += v * v;
    }
    double varT = sumT2 - sumT * sumT / n;
    if (varT <= 1e-6) { delete[] I1; delete[] I2; GrayFree(&s4); GrayFree(&t4); return 0; }

    const double coarseThr = 0.50;
    const int KEEP = 96;                  /* 始终保留分数最高的 KEEP 个候选 */
    Cand keep[KEEP];
    int nc = 0;

    int maxX = W - tw, maxY = H - th;
    /* 扫描方向：1 左上→右下 / -1 右下→左上 / 2 右上→左下 / -2 左下→右上 / 3 不按方向（按分数） */
    int scan = (dir == 3) ? 1 : dir;
    int xAsc = (scan == 1 || scan == -2);
    int yAsc = (scan == 1 || scan == 2);
    for (int yi_r52 = 0; yi_r52 <= maxY; yi_r52++) {
        int y = yAsc ? yi_r52 : (maxY - yi_r52);
        for (int xi_r51 = 0; xi_r51 <= maxX; xi_r51++) {
            int x = xAsc ? xi_r51 : (maxX - xi_r51);
            int sumTS = 0;
            for (int ty_r50 = 0; ty_r50 < th; ty_r50++) {
                const unsigned char *sr = s4.px + (y + ty_r50) * W + x;
                const unsigned char *tr = t4.px + ty_r50 * tw;
                for (int tx_r49 = 0; tx_r49 < tw; tx_r49++) sumTS += (int)sr[tx_r49] * (int)tr[tx_r49];
            }
            int x2 = x + tw, y2 = y + th;
            double sumS = I1[y2 * iw + x2] - I1[y * iw + x2] - I1[y2 * iw + x] + I1[y * iw + x];
            double sumS2 = I2[y2 * iw + x2] - I2[y * iw + x2] - I2[y2 * iw + x] + I2[y * iw + x];
            double varS = sumS2 - sumS * sumS / n;
            if (varS <= 1e-6) continue;
            double num = (double)sumTS - sumS * sumT / n;
            double score = num / (sqrt(varS) * sqrt(varT));
            if (score < coarseThr) continue;

            /* 维护降序 top-KEEP（KEEP 很小，插入代价可忽略） */
            if (nc < KEEP || score > keep[KEEP - 1].score) {
                int pos;
                if (nc < KEEP) pos = nc++;
                else           pos = KEEP - 1;      /* 覆盖最差的那个 */
                keep[pos].x = x; keep[pos].y = y; keep[pos].score = score;
                while (pos > 0 && keep[pos].score > keep[pos - 1].score) {
                    Cand t = keep[pos]; keep[pos] = keep[pos - 1]; keep[pos - 1] = t;
                    pos--;
                }
            }
        }
    }
    Cand *cand = keep;
    SortCands(cand, nc);
    LOG("Search: after sort best=%.3f @(%d,%d)", cand[0].score, cand[0].x, cand[0].y);

    /* 粗层先去重（同一个目标会有一堆相邻候选），只保留峰值 */
    int sepX = tw / 2; if (sepX < 3) sepX = 3;
    int sepY = th / 2; if (sepY < 3) sepY = 3;
    Cand *peaks = new Cand[nc > 0 ? nc : 1];
    int np = 0;
    if (!peaks) { return 0; }
    for (int i_r48 = 0; i_r48 < nc; i_r48++) {
        bool dup = false;
        for (int j_r47 = 0; j_r47 < np; j_r47++) {
            int dx = cand[i_r48].x - peaks[j_r47].x; if (dx < 0) dx = -dx;
            int dy = cand[i_r48].y - peaks[j_r47].y; if (dy < 0) dy = -dy;
            if (dx < sepX && dy < sepY) { dup = true; break; }
        }
        if (!dup) peaks[np++] = cand[i_r48];
    }


    /* 精定位：只算前 N 个峰，半径取一个粗粒度单位 */
    int refineMax = wantAll ? 24 : 6;
    if (refineMax > np) refineMax = np;
    int radius = factor + 1;

    double sumTF = 0, sumTF2 = 0;
    for (int k_r46 = 0; k_r46 < tpl.w * tpl.h; k_r46++) { double v = (double)tpl.px[k_r46]; sumTF += v; sumTF2 += v * v; }
    double nFull = (double)tpl.w * tpl.h;

    Cand *best = new Cand[refineMax > 0 ? refineMax : 1];
    int nb = 0;
    if (!best) { delete[] peaks; return 0; }
    for (int i_r45 = 0; i_r45 < refineMax; i_r45++) {
        int cx = peaks[i_r45].x * factor, cy = peaks[i_r45].y * factor;
        int x0 = cx - radius, y0 = cy - radius;
        int x1 = cx + radius, y1 = cy + radius;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > src.w - tpl.w) x1 = src.w - tpl.w;
        if (y1 > src.h - tpl.h) y1 = src.h - tpl.h;
        double bscore = -1; int bx = -1, by = -1;
        for (int y_r44 = y0; y_r44 <= y1; y_r44++) {
            for (int x_r43 = x0; x_r43 <= x1; x_r43++) {
                double s = ZnccAt(src, x_r43, y_r44, tpl, sumTF, sumTF2, nFull);
                if (s > bscore) { bscore = s; bx = x_r43; by = y_r44; }
            }
        }
        if (bx >= 0 && bscore >= sim) { best[nb].x = bx; best[nb].y = by; best[nb].score = bscore; nb++; }
    }
    delete[] peaks;
    LOG("Search: peaks=%d refineMax=%d nb=%d", np, refineMax, nb);
    if (nb == 0) { delete[] best; return 0; }
    /* 按分数从高到低排序：返回分数最高的那个（同分时先扫到的优先） */
    {
        for (int i_r42 = 1; i_r42 < nb; i_r42++) {
            Cand key = best[i_r42];
            int j = i_r42 - 1;
            while (j >= 0 && best[j].score < key.score) {
                best[j + 1] = best[j];
                j--;
            }
            best[j + 1] = key;
        }
    }

    /* 全分辨率：并查集聚类（同一目标归一组），每组取最高分，再按方向排序 */
    int mdX = tpl.w / 2; if (mdX < 4) mdX = 4;
    int mdY = tpl.h / 2; if (mdY < 4) mdY = 4;

    int *parent = new int[nb];
    for (int i_r41 = 0; i_r41 < nb; i_r41++) parent[i_r41] = i_r41;
    for (int i_r40 = 0; i_r40 < nb; i_r40++) {
        for (int j_r39 = i_r40 + 1; j_r39 < nb; j_r39++) {
            int dx = best[i_r40].x - best[j_r39].x; if (dx < 0) dx = -dx;
            int dy = best[i_r40].y - best[j_r39].y; if (dy < 0) dy = -dy;
            if (dx < mdX && dy < mdY) {
                int a = i_r40; while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
                int b = j_r39; while (parent[b] != b) { parent[b] = parent[parent[b]]; b = parent[b]; }
                if (a != b) parent[b] = a;
            }
        }
    }
    /* best 已按分数降序，每组第一次出现即为该组最高分 */
    int reps[256];
    int nRep = 0;
    for (int i_r38 = 0; i_r38 < nb; i_r38++) {
        int r = i_r38; while (parent[r] != r) { parent[r] = parent[parent[r]]; r = parent[r]; }
        bool seen = false;
        for (int k_r37 = 0; k_r37 < nRep; k_r37++) if (reps[k_r37] == r) { seen = true; break; }
        if (!seen && nRep < 256) reps[nRep++] = r;
    }
    delete[] parent;

    /* 代表点排序：
       dir=1/-1/2/-2 → 按扫描方向（谁先扫到谁在前）；
       dir=3        → 不排序，reps 本身就是"分数从高到低"（聚类时按分数降序生成），最高分排第一 */
    if (dir != 3) {
        int xAsc2 = (dir == 1 || dir == -2);
        int yAsc2 = (dir == 1 || dir == 2);
        for (int i_r36 = 1; i_r36 < nRep; i_r36++) {
            int key = reps[i_r36];
            long kk = (long)(yAsc2 ? best[key].y : 100000 - best[key].y) * 100000L +
                      (long)(xAsc2 ? best[key].x : 100000 - best[key].x);
            int j = i_r36 - 1;
            while (j >= 0) {
                long kj = (long)(yAsc2 ? best[reps[j]].y : 100000 - best[reps[j]].y) * 100000L +
                          (long)(xAsc2 ? best[reps[j]].x : 100000 - best[reps[j]].x);
                if (kj <= kk) break;
                reps[j + 1] = reps[j];
                j--;
            }
            reps[j + 1] = key;
        }
    }

    for (int i_r35 = 0; i_r35 < nRep && cnt < maxOut; i_r35++) {
        out[cnt++] = best[reps[i_r35]];
        if (!wantAll) break;
    }
    delete[] best;
    return cnt;
}
/* 结果格式化：x|y|0.931（相似度保留三位） */
static void AppendCoordW(wchar_t *out, int cap, int x, int y, double score)
{
    int s = (int)(score * 1000.0 + 0.5);
    if (s < 0) s = 0;
    if (s > 1000) s = 1000;
    if (cap > 0) wsprintfW(out, L"%d|%d|%d.%03d", x, y, s / 1000, s % 1000);
}

/* ================= 置顶透明画框（专用常驻线程 + 消息循环） =================
   关键点：窗口必须由"一个常驻线程"拥有并在该线程里绘制/显示/隐藏。
   按键精灵执行插件命令的线程可能是临时的，窗口若建在那个线程上，命令返回后
   线程一结束窗口就没了 —— 这正是"有时显示、有时完全不显示"的根因。 */
#define WM_QM_DRAW  (WM_APP + 0x50)
#define WM_QM_HIDE  (WM_APP + 0x52)

static RECT g_boxes[512];
static int  g_boxN = 0;
static int  g_labMilli[512];
static int  g_labCount = 0;
static RECT g_overlayRect = {0,0,0,0};
static int  g_drawOn = 0, g_lineW = 2;
static COLORREF g_lineColor = RGB(0,255,0);

/* 请求缓冲：脚本线程写入，画框线程在 WM_QM_DRAW 里取走 */
static RECT g_rqBoxes[512];
static int  g_rqLab[512];
static COLORREF g_rqColor[512];
static int  g_rqN = 0, g_rqLabN = 0;

/* 每个框独立计时：画上后 1.5 秒自己消失，新框不影响旧框 */
#define ZH_BOX_LIFE 1500
static COLORREF g_color[512];
static int      g_colorCount = 0;
static DWORD    g_boxTime[512];   /* 每框的到期时刻（GetTickCount 语义） */

static HWND   g_ovHwnd = NULL;
static HANDLE g_ovThread = NULL;
static CRITICAL_SECTION g_ovCs;
static bool   g_ovCsInit = false;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
/* 1 = 画框窗口已成功"排除出抓屏"（Win10 2004+）。成功时自己的 BitBlt 拍不到画框，
   于是搜索期间可以一直显示上一个框，等新框画出来的那一刻才替换 —— 消除闪烁。
   0 = 老系统不支持，退回"抓屏前先隐藏"的旧策略。 */
static int g_capExcl = 0;

static void PaintBoxes(HDC hdc, int ox, int oy)
{
    if (g_boxN <= 0) return;
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    SetBkMode(hdc, TRANSPARENT);

    HFONT fnt = NULL;
    if (g_labCount > 0) {
        fnt = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, "Tahoma");
        if (fnt) SelectObject(hdc, fnt);
    }
    for (int i_r34 = 0; i_r34 < g_boxN; i_r34++) {
        COLORREF c = (i_r34 < g_colorCount) ? g_color[i_r34] : g_lineColor;
        HPEN pen = CreatePen(PS_SOLID, g_lineW < 1 ? 1 : g_lineW, c);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        SetTextColor(hdc, c);
        int x = g_boxes[i_r34].left - ox;
        int y = g_boxes[i_r34].top - oy;
        int w = g_boxes[i_r34].right - g_boxes[i_r34].left;
        int h = g_boxes[i_r34].bottom - g_boxes[i_r34].top;
        Rectangle(hdc, x, y, x + w + 1, y + h + 1);
        /* 只有带分数标签的框（找图/找色）才写数字；DrawBox 这类纯画框不写 */
        if (fnt && i_r34 < g_labCount && w >= 30 && g_labMilli[i_r34] > 0) {
            int milli = g_labMilli[i_r34];
            char txt[32];
            wsprintfA(txt, "%d.%03d", milli / 1000, milli % 1000);
            TextOutA(hdc, x, y + h + 3, txt, lstrlenA(txt));
        }
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    if (fnt) DeleteObject(fnt);
    SelectObject(hdc, oldBr);
}

/* 把请求里的框贴到当前显示用的数组（调用前需持有锁） */
/* 把过期的框清掉（在线程里调用） */
static void PruneExpired(void)
{
    DWORD now = GetTickCount();
    int w = 0;
    for (int i_r33 = 0; i_r33 < g_boxN; i_r33++) {
        if ((long)(g_boxTime[i_r33] - now) > 0) {
            g_boxes[w] = g_boxes[i_r33];
            g_color[w] = g_color[i_r33];
            g_labMilli[w] = g_labMilli[i_r33];
            g_boxTime[w] = g_boxTime[i_r33];
            w++;
        }
    }
    g_boxN = w; g_labCount = w; g_colorCount = w;
}

/* 追加本批框：同一位置（±4px）只更新计时，不做替换 */
static void TakeRequest(void)
{
    int n = g_rqN;
    if (n > 512) n = 512;
    if (n < 0) n = 0;
    DWORD now = GetTickCount();
    for (int i_r32 = 0; i_r32 < n && g_boxN < 512; i_r32++) {
        int hit = -1;
        for (int j_r31 = 0; j_r31 < g_boxN; j_r31++) {
            int dx = (int)(g_boxes[j_r31].left - g_rqBoxes[i_r32].left); if (dx < 0) dx = -dx;
            int dy = (int)(g_boxes[j_r31].top - g_rqBoxes[i_r32].top); if (dy < 0) dy = -dy;
            if (dx <= 4 && dy <= 4) { hit = j_r31; break; }
        }
        if (hit >= 0) {
            g_boxes[hit] = g_rqBoxes[i_r32];
            g_color[hit] = g_rqColor[i_r32];
            g_labMilli[hit] = (i_r32 < g_rqLabN) ? g_rqLab[i_r32] : 0;
            g_boxTime[hit] = now + ZH_BOX_LIFE;
        } else {
            g_boxes[g_boxN] = g_rqBoxes[i_r32];
            g_color[g_boxN] = g_rqColor[i_r32];
            g_labMilli[g_boxN] = (i_r32 < g_rqLabN) ? g_rqLab[i_r32] : 0;
            g_boxTime[g_boxN] = now + ZH_BOX_LIFE;
            g_boxN++;
        }
    }
    g_labCount = g_boxN;
    g_colorCount = g_boxN;
}

static void DoDraw(HWND h)
{
    if (g_boxN <= 0) return;

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;

    int minX = 0x7FFFFFFF, minY = 0x7FFFFFFF, maxX = -0x7FFFFFFF, maxY = -0x7FFFFFFF;
    for (int i_r30 = 0; i_r30 < g_boxN; i_r30++) {
        RECT b = g_boxes[i_r30];
        if (b.left < vx) b.left = vx;
        if (b.top < vy) b.top = vy;
        if (b.right > vx + vw) b.right = vx + vw;
        if (b.bottom > vy + vh) b.bottom = vy + vh;
        if (b.right <= b.left) b.right = b.left + 1;
        if (b.bottom <= b.top) b.bottom = b.top + 1;
        g_boxes[i_r30] = b;
        if (b.left < minX) minX = b.left;
        if (b.top < minY) minY = b.top;
        if (b.right > maxX) maxX = b.right;
        if (b.bottom > maxY) maxY = b.bottom;
    }
    if (minX > maxX || minY > maxY) return;

    bool anyLab = false;
    for (int i_r29 = 0; i_r29 < g_boxN && i_r29 < g_labCount; i_r29++)
        if (g_boxes[i_r29].right - g_boxes[i_r29].left >= 30 && g_labMilli[i_r29] > 0) { anyLab = true; break; }
    minX -= 2; minY -= 2; maxX += 3;
    maxY += (anyLab ? 18 : 3);
    if (minX < vx) minX = vx;
    if (minY < vy) minY = vy;
    if (maxX > vx + vw) maxX = vx + vw;
    if (maxY > vy + vh) maxY = vy + vh;
    int dw = maxX - minX, dh = maxY - minY;
    if (dw <= 0 || dh <= 0) return;

    g_overlayRect.left = minX; g_overlayRect.top = minY;
    g_overlayRect.right = maxX; g_overlayRect.bottom = maxY;

    /* 双缓冲：先在内存位图上画好，再一次性贴上去，避免"先闪一下黑底再出框" */
    HDC hdcW = GetDC(h);
    HDC mem = hdcW ? CreateCompatibleDC(hdcW) : NULL;
    HBITMAP bmp = mem ? CreateCompatibleBitmap(hdcW, dw, dh) : NULL;
    HGDIOBJ oldBmp = NULL;
    if (mem && bmp) {
        oldBmp = SelectObject(mem, bmp);
        RECT rc = { 0, 0, dw, dh };
        FillRect(mem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        PaintBoxes(mem, minX, minY);
        GdiFlush();
    }

    SetLayeredWindowAttributes(h, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetWindowPos(h, HWND_TOPMOST, minX, minY, dw, dh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(h, SW_SHOWNOACTIVATE);

    if (hdcW) {
        if (mem && bmp) {
            BitBlt(hdcW, 0, 0, dw, dh, mem, 0, 0, SRCCOPY);
        } else {
            RECT rc = { 0, 0, dw, dh };
            FillRect(hdcW, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            PaintBoxes(hdcW, minX, minY);
        }
        GdiFlush();
        ReleaseDC(h, hdcW);
    }
    if (mem) {
        if (oldBmp) SelectObject(mem, oldBmp);
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
    }
    SetTimer(h, 1, 250, NULL);      /* 250ms 清一次过期框，全部过期才隐藏 */
    LOG("Draw: (%d,%d) %dx%d n=%d labels=%d visible=%d",
        minX, minY, dw, dh, g_boxN, g_labCount, (int)IsWindowVisible(h));
}

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_QM_DRAW:
        if (g_ovCsInit) EnterCriticalSection(&g_ovCs);
        PruneExpired();
        TakeRequest();
        if (g_ovCsInit) LeaveCriticalSection(&g_ovCs);
        DoDraw(h);
        return 0;
    case WM_QM_HIDE:
        KillTimer(h, 1);
        SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_TIMER:
        if (w == 1) {
            if (g_ovCsInit) EnterCriticalSection(&g_ovCs);
            PruneExpired();
            if (g_ovCsInit) LeaveCriticalSection(&g_ovCs);
            if (g_boxN <= 0) {
                KillTimer(h, 1);
                SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
                ShowWindow(h, SW_HIDE);
            } else {
                DoDraw(h);          /* 还有未过期的框，重绘（DoDraw 会重置定时器） */
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (dc) { PaintBoxes(dc, g_overlayRect.left, g_overlayRect.top); EndPaint(h, &ps); }
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI OverlayThreadProc(LPVOID)
{
    char cls[80];
    wsprintfA(cls, "QmZaoyoOverlay_%08X", (unsigned)GetModuleHandleA(NULL));
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    wc.hCursor = NULL;
    RegisterClassA(&wc);

    HWND h = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        cls, "", WS_POPUP, 0, 0, 16, 16, NULL, NULL, wc.hInstance, NULL);
    if (!h) { LOG("overlay: CreateWindow 失败 err=%lu", GetLastError()); return 0; }

    SetLayeredWindowAttributes(h, RGB(0,0,0), 0, LWA_COLORKEY);
    /* 让本窗口对 BitBlt 抓屏不可见（Win10 2004+）：这样搜索期间画框不用隐藏，
       旧框一直显示，新框画出来的同一瞬间被替换，不再一闪一闪 */
#ifdef ZAOYO_FORCE_OLD_HIDE
    int excl = 0;   /* 测试用：模拟"排除抓屏不可用"的老系统行为 */
    SetWindowDisplayAffinity(h, WDA_NONE);
    LOG("overlay capture-exclude=0 (forced old behaviour)");
#else
    SetLastError(0);
    int excl = SetWindowDisplayAffinity(h, WDA_EXCLUDEFROMCAPTURE) ? 1 : 0;
    LOG("overlay capture-exclude=%d err=%lu", excl, (unsigned long)GetLastError());
#endif
    g_capExcl = excl;
    g_ovHwnd = h;
    LOG("overlay thread ready hwnd=%p", h);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    g_ovHwnd = NULL;
    return 0;
}

static void EnsureOverlay(void)
{
    if (g_ovHwnd && IsWindow(g_ovHwnd)) return;
    if (!g_ovCsInit) { InitializeCriticalSection(&g_ovCs); g_ovCsInit = true; }
    if (!g_ovThread) {
        g_ovThread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
        LOG("overlay thread created=%p", g_ovThread);
    }
    for (int i_r28 = 0; i_r28 < 300 && !g_ovHwnd; i_r28++) Sleep(2);   /* 等窗口就绪，最多 0.6 秒 */
}

static void HideOverlay(void)
{
    if (g_ovHwnd && IsWindow(g_ovHwnd))
        SendMessageTimeoutA(g_ovHwnd, WM_QM_HIDE, 0, 0, SMTO_ABORTIFHUNG, 300, NULL);
}

static void DestroyOverlay(void)
{
    /* 常驻线程不销毁，关闭画框只隐藏 */
    HideOverlay();
}

static void ScheduleHide(void) { /* 由画框线程的 SetTimer 负责，无需外部定时器 */ }

/* 只有在"排除抓屏"不可用的系统上，画框才会被自己的抓屏拍进去，才需要抓屏前隐藏 */
static bool OverlayPollutes(void)
{
    return !g_capExcl && g_drawOn && g_ovHwnd && IsWindowVisible(g_ovHwnd);
}

static void DrawBoxesOnScreen(const RECT *boxes, int n, const COLORREF *colors = NULL, const int *labs = NULL)
{
    if (!g_drawOn || n <= 0) return;
    EnsureOverlay();
    if (!g_ovHwnd) { LOG("draw: 没有画框窗口"); return; }
    if (n > 512) n = 512;

    if (g_ovCsInit) EnterCriticalSection(&g_ovCs);
    for (int i_r27 = 0; i_r27 < n; i_r27++) g_rqBoxes[i_r27] = boxes[i_r27];
    g_rqN = n;
    for (int i_r26 = 0; i_r26 < n; i_r26++) g_rqColor[i_r26] = colors ? colors[i_r26] : g_lineColor;
    if (labs) {
        for (int i_r25 = 0; i_r25 < n; i_r25++) g_rqLab[i_r25] = labs[i_r25];
        g_rqLabN = n;
    } else {
        int ln = (g_labCount < n) ? g_labCount : n;
        if (ln < 0) ln = 0;
        for (int i_r24 = 0; i_r24 < ln; i_r24++) g_rqLab[i_r24] = g_labMilli[i_r24];
        g_rqLabN = ln;
    }
    if (g_ovCsInit) LeaveCriticalSection(&g_ovCs);

    DWORD_PTR r = 0;
    SendMessageTimeoutA(g_ovHwnd, WM_QM_DRAW, 0, 0, SMTO_ABORTIFHUNG, 800, &r);
}

/* ================= 字符串工具 ================= */
static void AppendStr(char *buf, int cap, const char *s)
{
    int n = lstrlenA(buf);
    lstrcpynA(buf + n, s, cap - n);
}

static void AppendInt(char *buf, int cap, int v)
{
    char tmp[32];
    wsprintfA(tmp, "%d", v);
    AppendStr(buf, cap, tmp);
}

static int SplitList(const char *s, char items[][MAX_PATH], int maxItems)
{
    int n = 0;
    const char *p = s;
    while (*p && n < maxItems) {
        const char *q = p;
        while (*q && *q != '|' && *q != ';') q++;
        int len = (int)(q - p);
        if (len > 0) {
            int copy = len < MAX_PATH - 1 ? len : MAX_PATH - 1;
            lstrcpynA(items[n], p, copy + 1);
            items[n][copy] = 0;
            n++;
        }
        p = (*q) ? q + 1 : q;
    }
    return n;
}

/* 把相对文件名拼成完整路径 */
static char g_imageDir[MAX_PATH] = {0};

static void ResolvePath(const char *name, char *out, int cap)
{


    bool absolute = (strchr(name, ':') != NULL) || (name[0] == '\\' && name[1] == '\\');
    if (absolute || g_imageDir[0] == 0) lstrcpynA(out, name, cap);
    else {
        lstrcpynA(out, g_imageDir, cap);
        int n = lstrlenA(out);
        if (n > 0 && out[n - 1] != '\\') lstrcpynA(out + n, "\\", cap - n);
        n = lstrlenA(out);
        lstrcpynA(out + n, name, cap - n);
    }
}

/* ================= 插件对象 ================= */
struct DescEntry { const wchar_t *name; const wchar_t *desc; const wchar_t *tmpl; };
static const DescEntry g_desc[] = {
    { L"", L"早柚插件（找图/找色/测角/鼠标，抗渲染色差）", L"" },
    { L"Find", L"找图(左,上,右,下,图片,相似度[,绘制开关,颜色,方向])[x|y|实际相似度]",
                 L"在$1,$2,$3,$4范围内找图$5，相似度$6" },
    { L"FindAll", L"多图同时查找，全部找到才成功(左,上,右,下,图片列表,相似度[,绘制开关,颜色,方向])[各图x|y|相似度;中心点]",
                    L"在$1,$2,$3,$4范围内找多图$5，相似度$6" },
    { L"FindAllPos", L"找一张图的所有匹配位置(左,上,右,下,图片,相似度[,绘制开关,颜色,方向])[数量;x|y|相似度...]",
                       L"找$5在$1,$2,$3,$4内的所有位置，相似度$6" },
    { L"FindColorMulti", L"多点找色(左,上,右,下,主色,偏色点,相似度[,绘制开关,颜色,方向])[x|y|实际相似度]",
                            L"在$1,$2,$3,$4内按主色$5和多点$6找色，相似度$7" },
    { L"FindColorMultiPos", L"多点找色的所有位置(左,上,右,下,主色,偏色点,相似度[,绘制开关,颜色,方向])[数量;x|y|相似度...]",
                               L"找主色$5和多点$6在$1,$2,$3,$4内的所有位置，相似度$7" },
    { L"GetSectorAngle", L"扇形角度(圆心X,圆心Y,内半径,外半径[,灵敏度,校准,绘制开关,颜色,方法])[角度|集中度|覆盖]",
                           L"测以$1,$2为圆心、$3~$4环带的扇形角度" },
    { L"GetArrowAngle", L"箭头朝向(圆心X,圆心Y,半径[,基准角,绘制开关,颜色])[角度|像素数|长度]",
                          L"测$1,$2附近$3像素内的箭头朝向，减基准角$4" },
    { L"MoveMouseR", L"相对移动鼠标-按计数(dx,dy[,步数,间隔,方法])[ok|步数|dx|dy]",
                       L"相对移动鼠标 $1,$2 计数；步数$3 间隔$4 方法$5" },
    { L"MoveMouseP", L"相对移动鼠标-按像素(dx,dy[,步数,间隔,方法])[ok|步数|dx|dy|计数]",
                       L"相对移动鼠标 $1,$2 像素；步数$3 间隔$4 方法$5" },
    { L"SoundStart", L"开始监听游戏音效(无参数，阈值已内置)[ok|设备Hz|声道]／fail|错误码",
                      L"开始监听游戏音效（默认播放设备的环回采集），阈值已内置：闪避0.35、反击0.25" },
    { L"SoundWait", L"等待音效命中(超时毫秒,有效期毫秒)[2=闪避|3=反击|0=超时|-1=未启动]",
                     L"等待音效命中，最多$1毫秒；结果保留$2毫秒（0=不限），过时丢弃" },
    { L"SoundScore", L"当前音效分数()[闪避分|反击分]", L"读取当前两个音效的匹配分数（调试用）" },
    { L"SoundStop", L"停止音效监听()[ok]", L"停止音效监听并释放采集设备" },
    { L"DrawBox", L"画一个正方形框，1.5 秒后自动消失(中心X,中心Y,边长,颜色)[ok|左|上|右|下]",
                  L"在$1,$2处画一个边长$3的正方形框，颜色$4（RRGGBB），1.5秒后自动消失" },
    { L"Version", L"插件版本号", L"" },
};static const int g_descCount = sizeof(g_desc) / sizeof(g_desc[0]);

enum { DISPID_DESC = 1001, DISPID_TMPL = 1002 };
static double g_defaultSim = 0.88;

class CPlugin : public IDispatch
{
public:
    CPlugin() : m_ref(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        LOG("QI %08lX-%04X", (unsigned long)riid.Data1, (unsigned)riid.Data2);
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch) ||
            IsEqualIID(riid, IID_IQmFindPic) || IsEqualIID(riid, IID_IQMPluginStandard)) {
            *ppv = static_cast<IDispatch *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }

    STDMETHODIMP GetTypeInfoCount(UINT *p) { if (!p) return E_POINTER; *p = 1; return S_OK; }

    STDMETHODIMP GetTypeInfo(UINT i, LCID, ITypeInfo **pp)
    {
        if (!pp) return E_POINTER;
        *pp = NULL;
        if (i != 0) return DISP_E_BADINDEX;
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(g_hInst, path, MAX_PATH);
        ITypeLib *ptl = NULL;
        HRESULT hr = LoadTypeLib(path, &ptl);
        if (FAILED(hr)) return hr;
        hr = ptl->GetTypeInfoOfGuid(IID_IQmFindPic, pp);
        ptl->Release();
        return hr;
    }

    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR *names, UINT cNames, LCID, DISPID *ids)
    {
        {
            char nm[128] = {0};
            if (names && names[0]) WideCharToMultiByte(CP_ACP, 0, names[0], -1, nm, sizeof(nm), NULL, NULL);
            LOG("GetIDsOfNames cNames=%u name=[%s]", (unsigned)cNames, nm);
        }
        if (!names || !ids || cNames != 1) return E_INVALIDARG;
        static const struct { const wchar_t *n; DISPID id; } map[] = {
            /* 带 zy 前缀的名字 */
            { L"Find", 1 }, { L"FindAll", 2 }, { L"FindAllPos", 3 },
            { L"FindColorMulti", 4 }, { L"FindColorMultiPos", 5 },
            { L"GetSectorAngle", 6 }, { L"GetArrowAngle", 7 },
            { L"MoveMouseR", 8 }, { L"MoveMouseP", 9 }, { L"Version", 10 },
            { L"SoundStart", 11 }, { L"SoundStop", 12 }, { L"SoundWait", 13 }, { L"SoundScore", 14 },
            { L"DrawBox", 15 },
            /* 旧脚本兼容：zy 前缀的写法仍然可以调用（仅分发表，不写进类型库） */
            { L"zyFind", 1 }, { L"zyFindAll", 2 }, { L"zyFindAllPos", 3 },
            { L"zyFindColorMulti", 4 }, { L"zyFindColorMultiPos", 5 },
            { L"zyGetSectorAngle", 6 }, { L"zyGetArrowAngle", 7 },
            { L"zyMoveMouseR", 8 }, { L"zyMoveMouseP", 9 }, { L"zyVersion", 10 },
            { L"zySoundStart", 11 }, { L"zySoundStop", 12 }, { L"zySoundWait", 13 }, { L"zySoundScore", 14 },
            { L"zyDrawBox", 15 },
            { L"Get_Plugin_Description", DISPID_DESC },
            { L"Get_Plugin_Interpret_Template", DISPID_TMPL },
        };
        for (int i_r23 = 0; i_r23 < (int)(sizeof(map) / sizeof(map[0])); i_r23++)
            if (lstrcmpiW(names[0], map[i_r23].n) == 0) { *ids = map[i_r23].id; return S_OK; }
        return DISP_E_UNKNOWNNAME;
    }

    /* ---------- 多点找色 ---------- */
    static bool ParseHexColor(const char *s, unsigned char *r, unsigned char *g, unsigned char *b)
    {
        if (!s) return false;
        while (*s == ' ' || *s == '#') s++;
        if (lstrlenA(s) < 6) return false;
        int v = 0;
        for (int i_r22 = 0; i_r22 < 6; i_r22++) {
            char c = s[i_r22]; int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            v = (v << 4) | d;
        }
        *r = (unsigned char)((v >> 16) & 0xFF);
        *g = (unsigned char)((v >> 8) & 0xFF);
        *b = (unsigned char)(v & 0xFF);
        return true;
    }

    /* 按键精灵的颜色是 BBGGRR（红色 = 0000FF），这里做一次转换 */
    static bool ParseColorKb(const char *s, unsigned char *r, unsigned char *g, unsigned char *b)
    {
        unsigned char x, y, z;               /* x=B  y=G  z=R */
        if (!ParseHexColor(s, &x, &y, &z)) return false;
        *r = z; *g = y; *b = x;
        return true;
    }

    static int ParseColorPoints(VARIANT *vMain, VARIANT *vList, QmSample *pts, int maxPts)
    {
        char main[32] = {0};
        FromBstr(vMain, main, 32);
        unsigned char r, g, b;
        if (!ParseColorKb(main, &r, &g, &b)) return 0;
        pts[0].dx = 0; pts[0].dy = 0; pts[0].r = r; pts[0].g = g; pts[0].b = b; pts[0].neg = 0;
        int n = 1;

        char list[4096] = {0};
        FromBstr(vList, list, 4096);
        const char *p = list;
        while (*p && n < maxPts) {
            while (*p == ' ' || *p == ',' || *p == ';' || *p == '\r' || *p == '\n') p++;
            if (!*p) break;
            char *end = NULL;
            int dx = (int)strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            if (*p != '|') { while (*p && *p != ',') p++; continue; }
            p++;
            int dy = (int)strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            if (*p != '|') { while (*p && *p != ',') p++; continue; }
            p++;
            char col[16] = {0};
            int ci = 0;
            while (*p && *p != ',' && *p != ';' && ci < 15) {
                if (*p != ' ' && *p != '\r' && *p != '\n') col[ci++] = *p;
                p++;
            }
            col[ci] = 0;
            /* 支持负色："-RRGGBB" 表示这个点不能是该颜色 */
            int neg = 0;
            const char *colp = col;
            if (*colp == '-') { neg = 1; colp++; }
            unsigned char rr, gg, bb;
            if (ParseColorKb(colp, &rr, &gg, &bb)) {
                pts[n].dx = dx; pts[n].dy = dy;
                pts[n].r = rr; pts[n].g = gg; pts[n].b = bb;
                pts[n].neg = neg;
                n++;
            }
        }
        return n;
    }

    HRESULT CmdFindColor(VARIANT *pL, VARIANT *pT, VARIANT *pR, VARIANT *pB,
                         VARIANT *pMain, VARIANT *pList, VARIANT *pSim,
                         VARIANT *pDraw, VARIANT *pColor, VARIANT *pDir, VARIANT *res, bool wantAll)
    {
        Rect rc; rc.l = (int)ToLong(pL); rc.t = (int)ToLong(pT);
        rc.r = (int)ToLong(pR); rc.b = (int)ToLong(pB);
        Normalize(&rc);
        double sim = ToDouble(pSim); if (sim <= 0 || sim > 1) sim = g_defaultSim;
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(255, 0, 0));
        int dir = (int)ToLong(pDir);
        if (dir != 1 && dir != -1 && dir != 2 && dir != -2 && dir != 3) dir = 1;

        QmSample pts[64];
        int nPts = ParseColorPoints(pMain, pList, pts, 64);
        if (nPts <= 0 || rc.r - rc.l <= 0 || rc.b - rc.t <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, wantAll ? L"0" : L"-1|-1");
        }

        if (OverlayPollutes()) {
            bool overlap = !(g_overlayRect.right <= rc.l || g_overlayRect.left >= rc.r ||
                             g_overlayRect.bottom <= rc.t || g_overlayRect.top >= rc.b);
            if (overlap) { HideOverlay(); Sleep(12); }
        }

        unsigned char *px = NULL; int cw = 0, ch = 0;
        if (!QmCaptureRGB(rc.l, rc.t, rc.r - rc.l, rc.b - rc.t, &px, &cw, &ch) || !px) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, wantAll ? L"0" : L"-1|-1");
        }

        int ox[512], oy[512];
        double os[512];
        int got = QmFindColorMulti(px, cw, ch, pts, nPts, sim, wantAll, 512, dir, ox, oy, os);
        free(px);

        int minDx = 0, minDy = 0, maxDx = 0, maxDy = 0;
        for (int i_r21 = 1; i_r21 < nPts; i_r21++) {
            if (pts[i_r21].dx < minDx) minDx = pts[i_r21].dx;
            if (pts[i_r21].dy < minDy) minDy = pts[i_r21].dy;
            if (pts[i_r21].dx > maxDx) maxDx = pts[i_r21].dx;
            if (pts[i_r21].dy > maxDy) maxDy = pts[i_r21].dy;
        }

        if (got <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, wantAll ? L"0" : L"-1|-1");
        }

        RECT boxes[512];
        for (int i_r20 = 0; i_r20 < got; i_r20++) {
            boxes[i_r20].left = rc.l + ox[i_r20] + minDx - 2;
            boxes[i_r20].top = rc.t + oy[i_r20] + minDy - 2;
            boxes[i_r20].right = rc.l + ox[i_r20] + maxDx + 2;
            boxes[i_r20].bottom = rc.t + oy[i_r20] + maxDy + 2;
        }
        {
            int lab[512];
            for (int i_r19 = 0; i_r19 < got && i_r19 < 512; i_r19++) {
                int s = (int)(os[i_r19] * 1000.0 + 0.5);
                lab[i_r19] = (s < 0) ? 0 : (s > 1000 ? 1000 : s);
            }
            ApplyDraw(draw, color, boxes, got, lab, got);
        }

        if (!wantAll) {
            wchar_t buf[64];
            int s = (int)(os[0] * 1000.0 + 0.5);
            if (s < 0) s = 0; if (s > 1000) s = 1000;
            wsprintfW(buf, L"%d|%d|%d.%03d", rc.l + ox[0], rc.t + oy[0], s / 1000, s % 1000);
            LOG("FindColorMulti ok %d,%d sim=%.3f", rc.l + ox[0], rc.t + oy[0], os[0]);
            return RetStr(res, buf);
        }

        wchar_t buf[8192];
        wsprintfW(buf, L"%d", got);
        for (int i_r18 = 0; i_r18 < got; i_r18++) {
            wchar_t tmp[96];
            int s = (int)(os[i_r18] * 1000.0 + 0.5);
            if (s < 0) s = 0; if (s > 1000) s = 1000;
            wsprintfW(tmp, L";%d|%d|%d.%03d", rc.l + ox[i_r18], rc.t + oy[i_r18], s / 1000, s % 1000);
            lstrcatW(buf, tmp);
        }
        LOG("FindColorMultiPos total=%d", got);
        return RetStr(res, buf);
    }
    /* ---------- 箭头朝向 ---------- */
    
    /* ---------- 箭头朝向 ---------- */
    HRESULT CmdArrowAngle(VARIANT *pCx, VARIANT *pCy, VARIANT *pRad, VARIANT *pBase,
                          VARIANT *pDraw, VARIANT *pColor, VARIANT *res)
    {
        int cx = (int)ToLong(pCx), cy = (int)ToLong(pCy);
        int radius = (int)ToLong(pRad);
        double base = ToDouble(pBase);
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(0, 255, 0));

        if (radius < 8 || radius > 60) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }

        int L = cx - radius, T = cy - radius, R = cx + radius, B = cy + radius;
        int vL = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vT = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vR = vL + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vB = vT + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (L < vL) L = vL;
        if (T < vT) T = vT;
        if (R > vR) R = vR;
        if (B > vB) B = vB;
        if (R - L <= 0 || B - T <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }

        if (OverlayPollutes()) {
            bool overlap = !(g_overlayRect.right <= L || g_overlayRect.left >= R ||
                             g_overlayRect.bottom <= T || g_overlayRect.top >= B);
            if (overlap) { HideOverlay(); Sleep(12); }
        }

        unsigned char *px = NULL; int cw = 0, ch = 0;
        if (!QmCaptureRGB(L, T, R - L, B - T, &px, &cw, &ch) || !px) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }

        double ang = 0, len = 0;
        int npx = 0;
        int ok = QmArrowAngle(px, cw, ch, cx - L, cy - T, radius, base, &ang, &npx, &len);
        free(px);

        RECT box;
        box.left = L; box.top = T; box.right = R; box.bottom = B;
        ApplyDraw(draw, color, &box, ok ? 1 : 0);

        if (!ok) { LOG("Arrow: not found"); return RetStr(res, L"-1"); }

        wchar_t buf[128];
        int a10 = (int)(ang * 10.0 + 0.5);
        int l10 = (int)(len * 10.0 + 0.5);
        wsprintfW(buf, L"%d.%d|%d|%d.%d", a10 / 10, a10 % 10, npx, l10 / 10, l10 % 10);
        LOG("Arrow: angle=%d.%d px=%d len=%d.%d", a10 / 10, a10 % 10, npx, l10 / 10, l10 % 10);
        return RetStr(res, buf);
    }

    /* ---------- DrawBox：以 (cx,cy) 为中心画一个正方形框，1.5 秒后自动消失 ----------
       走的是和找图/找色同一套画框通道（常驻画框线程 + 每框独立到期 + 排除抓屏），
       所以多个框可以同时存在、各自消失，也不会打断正在进行的找图找色。 */
    HRESULT CmdDrawBox(VARIANT *pX, VARIANT *pY, VARIANT *pSize, VARIANT *pColor, VARIANT *res)
    {
        int cx = (int)ToLong(pX);
        int cy = (int)ToLong(pY);
        int size = (int)ToLong(pSize);
        COLORREF color = ParseColor(pColor, RGB(255, 0, 0));

        if (size < 0) size = -size;
        if (size < 2) {
            LOG("DrawBox: bad size=%d", size);
            return RetStr(res, L"-1");
        }
        /* 黑色是画框窗口的色键透明色，画不出来 */
        color &= 0x00FFFFFF;
        if (color == 0) color = RGB(255, 0, 0);

        int half = size / 2;
        RECT box;
        box.left   = cx - half;
        box.top    = cy - half;
        box.right  = box.left + size;
        box.bottom = box.top + size;

        g_lineW = 1;
        g_lineColor = color;
        g_drawOn = 1;
        g_labCount = 0;                 /* 纯画框，不带分数标签 */
        DrawBoxesOnScreen(&box, 1, &color, NULL);

        wchar_t buf[128];
        wsprintfW(buf, L"ok|%d|%d|%d|%d",
                  (int)box.left, (int)box.top, (int)box.right, (int)box.bottom);
        LOG("DrawBox: center=(%d,%d) size=%d color=%06X", cx, cy, size, (unsigned long)color);
        return RetStr(res, buf);
    }

    STDMETHODIMP Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS *pd,
                        VARIANT *result, EXCEPINFO *, UINT *)
    {
        if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) return DISP_E_MEMBERNOTFOUND;
        int n = pd ? (int)pd->cArgs : 0;
        VARIANT *a = pd ? pd->rgvarg : NULL;
        if (n > 0 && a && (a[n - 1].vt == VT_BSTR || a[n - 1].vt == (VT_VARIANT | VT_BYREF))) {
            char ab[256] = {0};
            BSTR bs = (a[n - 1].vt == VT_BSTR) ? a[n - 1].bstrVal : NULL;
            if (bs) WideCharToMultiByte(CP_ACP, 0, bs, -1, ab, sizeof(ab), NULL, NULL);
            LOG("Invoke id=%d arg0=[%s]", (int)id, ab);
        }
        /* 第 i 个声明参数 = a[n-1-i] */
        #define ARG(i) (n > (i) ? &a[n - 1 - (i)] : NULL)

        LARGE_INTEGER t0, t1, freq;
        QueryPerformanceCounter(&t0);
        QueryPerformanceFrequency(&freq);

        HRESULT hr;
        switch (id) {
        case 1: hr = CmdFind(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), result); break;
        case 2: hr = CmdFindAll(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), result); break;
        case 3: hr = CmdFindAllPos(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), result); break;
        case 4: hr = CmdFindColor(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), result, false); break;
        case 5: hr = CmdFindColor(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), ARG(9), result, true); break;
        case 6: hr = CmdSectorAngle(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), result); break;
        case 7: hr = CmdArrowAngle(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), result); break;
        case 8: hr = CmdMoveMouseR(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), result); break;
        case 9: hr = CmdMoveMouseP(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), result); break;
        case 10: hr = RetStr(result, L"2.9.1"); break;
        case 11: {   /* SoundStart：阈值内置，无参数 */
            int r = SndHostStart(g_hInst);
            wchar_t buf[64];
            if (r == 0) wsprintfW(buf, L"ok|%d|%d", SndHostRate(), SndHostChannels());
            else wsprintfW(buf, L"fail|%d", r);
            hr = RetStr(result, buf);
            break;
        }
        case 12: {   /* SoundStop */
            SndHostStop();
            hr = RetStr(result, L"ok");
            break;
        }
        case 13: {   /* SoundWait(超时毫秒, 有效期毫秒) */
            int ms = (int)ToLong(ARG(0));
            VARIANT *va = ARG(1);
            int age = va ? (int)ToLong(va) : 800;      /* 缺省 800ms */
            if (ms < 0) ms = 0;
            int r = SndHostWait(ms, age);
            wchar_t buf[16];
            wsprintfW(buf, L"%d", r);
            hr = RetStr(result, buf);
            break;
        }
        case 14: {   /* SoundScore */
            float sd = 0.0f, sc = 0.0f;
            SndHostScore(&sd, &sc);
            int a = (int)(sd * 1000.0f + 0.5f), b = (int)(sc * 1000.0f + 0.5f);
            wchar_t buf[48];
            wsprintfW(buf, L"%d.%03d|%d.%03d", a / 1000, a % 1000, b / 1000, b % 1000);
            hr = RetStr(result, buf);
            break;
        }
        case 15: hr = CmdDrawBox(ARG(0), ARG(1), ARG(2), ARG(3), result); break;
        case DISPID_DESC: hr = Lookup(ARG(0), result, true); break;
        case DISPID_TMPL: hr = Lookup(ARG(0), result, false); break;
        default: hr = DISP_E_MEMBERNOTFOUND; break;
        }
        #undef ARG

        QueryPerformanceCounter(&t1);
        long ms = (long)((t1.QuadPart - t0.QuadPart) * 1000 / (freq.QuadPart ? freq.QuadPart : 1));
        LOG("Invoke id=%d args=%d hr=0x%08lX %ldms", (int)id, n, (unsigned long)hr, ms);
        return hr;
    }

private:
    ~CPlugin() {}
    LONG m_ref;

    static void FromBstr(VARIANT *v, char *out, int cap)
    {
        out[0] = 0;
        if (!v) return;
        BSTR b = NULL;
        if (v->vt == VT_BSTR) b = v->bstrVal;
        else if (v->vt == (VT_VARIANT | VT_BYREF) && v->pvarVal && v->pvarVal->vt == VT_BSTR) b = v->pvarVal->bstrVal;
        if (b) WideCharToMultiByte(CP_ACP, 0, b, -1, out, cap, NULL, NULL);
    }
    static long ToLong(VARIANT *v)
    {
        if (!v) return 0;
        if (v->vt == VT_I4) return v->lVal;
        if (v->vt == VT_I2) return v->iVal;
        if (v->vt == VT_R8) return (long)v->dblVal;
        if (v->vt == VT_BSTR && v->bstrVal) return wcstol(v->bstrVal, NULL, 10);
        if (v->vt == (VT_VARIANT | VT_BYREF) && v->pvarVal) return ToLong(v->pvarVal);
        return 0;
    }
    static double ToDouble(VARIANT *v)
    {
        if (!v) return 0;
        if (v->vt == VT_R8) return v->dblVal;
        if (v->vt == VT_I4) return (double)v->lVal;
        if (v->vt == VT_BSTR && v->bstrVal) return wcstod(v->bstrVal, NULL);
        if (v->vt == (VT_VARIANT | VT_BYREF) && v->pvarVal) return ToDouble(v->pvarVal);
        return 0;
    }
    static HRESULT RetStr(VARIANT *r, const wchar_t *s)
    {
        if (!r) return S_OK;
        VariantInit(r);
        r->vt = VT_BSTR;
        r->bstrVal = SysAllocString(s);
        return r->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    static HRESULT RetLong(VARIANT *r, long v)
    {
        if (!r) return S_OK;
        VariantInit(r);
        r->vt = VT_I4;
        r->lVal = v;
        return S_OK;
    }
    static HRESULT Lookup(VARIANT *a, VARIANT *r, bool wantDesc)
    {
        wchar_t item[128] = {0};
        if (a && a->vt == VT_BSTR && a->bstrVal) lstrcpynW(item, a->bstrVal, 128);
        for (int i_r17 = 0; i_r17 < g_descCount; i_r17++)
            if (lstrcmpiW(item, g_desc[i_r17].name) == 0)
                return RetStr(r, wantDesc ? g_desc[i_r17].desc : g_desc[i_r17].tmpl);
        return RetStr(r, L"");
    }

    /* "RRGGBB" → COLORREF（非法则返回默认色） */
    static COLORREF ParseColor(VARIANT *v, COLORREF def)
    {
        char s[32] = {0};
        FromBstr(v, s, 32);
        if (lstrlenA(s) < 6) return def;
        int val = 0;
        for (int i_r16 = 0; i_r16 < 6; i_r16++) {
            char ch = s[i_r16]; int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else return def;
            val = (val << 4) | d;
        }
        return RGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }

    /* 把 "a.bmp|b.bmp|C:\pics\*.bmp" 展开成实际文件列表 */
    static int ExpandItems(VARIANT *vSpec, char items[][MAX_PATH], int maxItems)
    {
        char spec[4096] = {0};
        FromBstr(vSpec, spec, 4096);
        int n = 0;
        const char *p = spec;
        while (*p && n < maxItems) {
            const char *q = p;
            while (*q && *q != '|' && *q != ';') q++;
            char token[MAX_PATH] = {0};
            int len = (int)(q - p);
            if (len > MAX_PATH - 1) len = MAX_PATH - 1;
            if (len > 0) {
                for (int k_r15 = 0; k_r15 < len; k_r15++) token[k_r15] = p[k_r15];
                token[len] = 0;


                if (strchr(token, '*') || strchr(token, '?')) {
                    wchar_t wpat[MAX_PATH];
                    MultiByteToWideChar(CP_ACP, 0, token, -1, wpat, MAX_PATH);
                    WIN32_FIND_DATAW fd;
                    HANDLE h = FindFirstFileW(wpat, &fd);
                    if (h != INVALID_HANDLE_VALUE) {
                        char dir[MAX_PATH];
                        lstrcpyA(dir, token);
                        char *bs = strrchr(dir, '\\');
                        if (bs) *(bs + 1) = 0; else dir[0] = 0;
                        do {
                            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            if (n >= maxItems) break;
                            char name[MAX_PATH];
                            WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, name, MAX_PATH, NULL, NULL);
                            lstrcpynA(items[n], dir, MAX_PATH);
                            int l2 = lstrlenA(items[n]);
                            lstrcpynA(items[n] + l2, name, MAX_PATH - l2);
                            n++;
                        } while (FindNextFileW(h, &fd));
                        FindClose(h);
                    }
                } else {
                    lstrcpynA(items[n], token, MAX_PATH);
                    n++;
                }
            }
            p = (*q) ? q + 1 : q;
        }
        return n;
    }

    struct Rect { int l, t, r, b; };

    static void Normalize(Rect *rc)
    {
        if (rc->l > rc->r) { int t = rc->l; rc->l = rc->r; rc->r = t; }
        if (rc->t > rc->b) { int t = rc->t; rc->t = rc->b; rc->b = t; }
    }

    /* 抓屏。画框关闭时零开销；开启且压在搜索区域上时才隐藏+等一帧 */
    static bool Grab(const Rect &rc, Gray *screen)
    {
        if (OverlayPollutes()) {
            bool overlap = !(g_overlayRect.right <= rc.l || g_overlayRect.left >= rc.r ||
                             g_overlayRect.bottom <= rc.t || g_overlayRect.top >= rc.b);
            if (overlap) { HideOverlay(); Sleep(12); }
        }
        return CaptureRegion(rc.l, rc.t, rc.r - rc.l, rc.b - rc.t, screen);
    }

    /* 绘制开关：关掉或没命中就销毁叠加层（零开销） */
    
static void ApplyDraw(int on, COLORREF color, RECT *boxes, int n, const int *labMilli = NULL, int labCount = 0)
    {
        g_lineW = 1;
        g_lineColor = color;
        g_drawOn = on ? 1 : 0;
        if (!on) { DestroyOverlay(); return; }
        if (n <= 0) return;              /* 本次没找到：保留已有框，让它自己过期 */
        g_labCount = 0;
        if (labMilli && labCount > 0) {
            int k = (labCount < n) ? labCount : n;
            if (k > 512) k = 512;
            for (int i_r14 = 0; i_r14 < k; i_r14++) g_labMilli[i_r14] = labMilli[i_r14];
            g_labCount = k;
        }
        DrawBoxesOnScreen(boxes, n);
    }

    /* ---------- Find：区域内找一张图（支持通配符，取最相似的） ---------- */
    HRESULT CmdFind(VARIANT *pL, VARIANT *pT, VARIANT *pR, VARIANT *pB, VARIANT *pImg,
                    VARIANT *pSim, VARIANT *pDraw, VARIANT *pColor, VARIANT *pDir, VARIANT *res)
    {
        Rect rc; rc.l = (int)ToLong(pL); rc.t = (int)ToLong(pT);
        rc.r = (int)ToLong(pR); rc.b = (int)ToLong(pB);
        Normalize(&rc);
        double sim = ToDouble(pSim); if (sim <= 0 || sim > 1) sim = g_defaultSim;
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(255, 0, 0));
        int dir = (int)ToLong(pDir);
        if (dir != 1 && dir != -1 && dir != 2 && dir != -2 && dir != 3) dir = 1;

        char items[32][MAX_PATH];
        int nItems = ExpandItems(pImg, items, 32);
        if (nItems <= 0 || rc.r - rc.l <= 0 || rc.b - rc.t <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1|-1");
        }

        Gray screen; ZeroMemory(&screen, sizeof(screen));
        if (!Grab(rc, &screen)) { ApplyDraw(draw, color, NULL, 0); return RetStr(res, L"-1|-1"); }

        Cand bestC; ZeroMemory(&bestC, sizeof(bestC));
        int found = 0, bw = 0, bh = 0;
        for (int i_r13 = 0; i_r13 < nItems; i_r13++) {
            Gray tpl; ZeroMemory(&tpl, sizeof(tpl));
            if (!LoadGrayImageA(items[i_r13], &tpl)) continue;
            Cand c;
            if (SearchImage(screen, tpl, sim, false, 1, &c, dir) > 0) {
                if (!found || c.score > bestC.score) { bestC = c; bw = tpl.w; bh = tpl.h; found = 1; }
            }
            GrayFree(&tpl);
        }
        GrayFree(&screen);

        if (!found) { ApplyDraw(draw, color, NULL, 0); return RetStr(res, L"-1|-1"); }

        int sx = rc.l + bestC.x, sy = rc.t + bestC.y;
        RECT box = { sx, sy, sx + bw, sy + bh };
        {
            int s = (int)(bestC.score * 1000.0 + 0.5);
            int lab = (s < 0) ? 0 : (s > 1000 ? 1000 : s);
            ApplyDraw(draw, color, &box, 1, &lab, 1);
        }
        wchar_t buf[64];
        AppendCoordW(buf, 64, sx, sy, bestC.score);
        LOG("Find ok %d,%d sim=%.3f", sx, sy, bestC.score);
        return RetStr(res, buf);
    }

    /* ---------- FindAll：多图，全部找到才成功 ---------- */
    HRESULT CmdFindAll(VARIANT *pL, VARIANT *pT, VARIANT *pR, VARIANT *pB, VARIANT *pList,
                       VARIANT *pSim, VARIANT *pDraw, VARIANT *pColor, VARIANT *pDir, VARIANT *res)
    {
        Rect rc; rc.l = (int)ToLong(pL); rc.t = (int)ToLong(pT);
        rc.r = (int)ToLong(pR); rc.b = (int)ToLong(pB);
        Normalize(&rc);
        double sim = ToDouble(pSim); if (sim <= 0 || sim > 1) sim = g_defaultSim;
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(255, 0, 0));
        int dir = (int)ToLong(pDir);
        if (dir != 1 && dir != -1 && dir != 2 && dir != -2 && dir != 3) dir = 1;

        char items[32][MAX_PATH];
        int nItems = ExpandItems(pList, items, 32);
        if (nItems <= 0 || rc.r - rc.l <= 0 || rc.b - rc.t <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1|-1");
        }

        Gray screen; ZeroMemory(&screen, sizeof(screen));
        if (!Grab(rc, &screen)) { ApplyDraw(draw, color, NULL, 0); return RetStr(res, L"-1|-1"); }

        int xs[32], ys[32], ws[32], hs[32];
        double scores[32];
        int allFound = 1;
        for (int i_r12 = 0; i_r12 < nItems; i_r12++) {
            Gray tpl; ZeroMemory(&tpl, sizeof(tpl));
            if (!LoadGrayImageA(items[i_r12], &tpl)) { allFound = 0; break; }
            Cand c;
            if (SearchImage(screen, tpl, sim, false, 1, &c, dir) > 0) {
                xs[i_r12] = rc.l + c.x; ys[i_r12] = rc.t + c.y;
                ws[i_r12] = tpl.w; hs[i_r12] = tpl.h;
                scores[i_r12] = c.score;
            } else { allFound = 0; GrayFree(&tpl); break; }
            GrayFree(&tpl);
        }
        GrayFree(&screen);

        wchar_t buf[2048];
        buf[0] = 0;
        if (!allFound) {
            for (int i_r11 = 0; i_r11 < nItems; i_r11++) {
                if (i_r11) lstrcatW(buf, L";");
                lstrcatW(buf, L"-1|-1");
            }
            lstrcatW(buf, L";-1|-1");
            ApplyDraw(draw, color, NULL, 0);
            LOG("FindAll fail (need all)");
            return RetStr(res, buf);
        }

        int minX = xs[0], minY = ys[0], maxX = xs[0] + ws[0], maxY = ys[0] + hs[0];
        RECT boxes[32];
        for (int i_r10 = 0; i_r10 < nItems; i_r10++) {
            if (xs[i_r10] < minX) minX = xs[i_r10];
            if (ys[i_r10] < minY) minY = ys[i_r10];
            if (xs[i_r10] + ws[i_r10] > maxX) maxX = xs[i_r10] + ws[i_r10];
            if (ys[i_r10] + hs[i_r10] > maxY) maxY = ys[i_r10] + hs[i_r10];
            boxes[i_r10].left = xs[i_r10]; boxes[i_r10].top = ys[i_r10];
            boxes[i_r10].right = xs[i_r10] + ws[i_r10]; boxes[i_r10].bottom = ys[i_r10] + hs[i_r10];
        }
        int cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;

        for (int i_r9 = 0; i_r9 < nItems; i_r9++) {
            wchar_t tmp[96];
            AppendCoordW(tmp, 96, xs[i_r9], ys[i_r9], scores[i_r9]);
            lstrcatW(buf, tmp);
            lstrcatW(buf, L";");
        }
        wchar_t tmp2[64];
        wsprintfW(tmp2, L"%d|%d", cx, cy);
        lstrcatW(buf, tmp2);

        ApplyDraw(draw, color, boxes, nItems);
        LOG("FindAll ok n=%d center=%d,%d", nItems, cx, cy);
        return RetStr(res, buf);
    }

    /* ---------- FindAllPos：一张图的所有位置 ---------- */
    HRESULT CmdFindAllPos(VARIANT *pL, VARIANT *pT, VARIANT *pR, VARIANT *pB, VARIANT *pImg,
                          VARIANT *pSim, VARIANT *pDraw, VARIANT *pColor, VARIANT *pDir, VARIANT *res)
    {
        Rect rc; rc.l = (int)ToLong(pL); rc.t = (int)ToLong(pT);
        rc.r = (int)ToLong(pR); rc.b = (int)ToLong(pB);
        Normalize(&rc);
        double sim = ToDouble(pSim); if (sim <= 0 || sim > 1) sim = g_defaultSim;
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(255, 0, 0));
        int dir = (int)ToLong(pDir);
        if (dir != 1 && dir != -1 && dir != 2 && dir != -2 && dir != 3) dir = 1;

        char items[32][MAX_PATH];
        int nItems = ExpandItems(pImg, items, 32);
        if (nItems <= 0 || rc.r - rc.l <= 0 || rc.b - rc.t <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"0");
        }

        Gray screen; ZeroMemory(&screen, sizeof(screen));
        if (!Grab(rc, &screen)) { ApplyDraw(draw, color, NULL, 0); return RetStr(res, L"0"); }

        int total = 0;
        RECT boxes[200];
        double boxScores[200];
        wchar_t coords[8192];
        coords[0] = 0;

        for (int i_r8 = 0; i_r8 < nItems; i_r8++) {
            Gray tpl; ZeroMemory(&tpl, sizeof(tpl));
            if (!LoadGrayImageA(items[i_r8], &tpl)) continue;
            Cand cands[200];
            int got = SearchImage(screen, tpl, sim, true, 200, cands, dir);
            for (int k_r7 = 0; k_r7 < got && total < 200; k_r7++) {
                int sx = rc.l + cands[k_r7].x, sy = rc.t + cands[k_r7].y;
                wchar_t tmp[96];
                AppendCoordW(tmp, 96, sx, sy, cands[k_r7].score);
                lstrcatW(coords, L";");
                lstrcatW(coords, tmp);
                boxes[total].left = sx; boxes[total].top = sy;
                boxes[total].right = sx + tpl.w; boxes[total].bottom = sy + tpl.h;
                total++;
            }
            GrayFree(&tpl);
        }
        GrayFree(&screen);

        wchar_t buf[8192];
        wsprintfW(buf, L"%d", total);
        lstrcatW(buf, coords);
        {
            int lab[200];
            int cnt2 = (total < 200) ? total : 200;
            for (int i_r6 = 0; i_r6 < cnt2; i_r6++) {
                /* boxes 与 scores 一一对应（每张模板的命中） */
                int s = (int)(boxScores[i_r6] * 1000.0 + 0.5);
                lab[i_r6] = (s < 0) ? 0 : (s > 1000 ? 1000 : s);
            }
            ApplyDraw(draw, color, boxes, total, lab, cnt2);
        }
        LOG("FindAllPos total=%d", total);
        return RetStr(res, buf);
    }
    /* ---------- 扇形角度 ---------- */

    HRESULT CmdSectorAngle(VARIANT *pCx, VARIANT *pCy, VARIANT *pR1, VARIANT *pR2,
                           VARIANT *pSens, VARIANT *pCalib, VARIANT *pDraw, VARIANT *pColor,
                           VARIANT *pMethod, VARIANT *res)
    {
        int cx = (int)ToLong(pCx), cy = (int)ToLong(pCy);
        int r1 = (int)ToLong(pR1), r2 = (int)ToLong(pR2);
        /* 截图范围由"圆心 ± 外半径"推导，不再需要单独给矩形 */
        int L = cx - r2 - 2, T = cy - r2 - 2;
        int R = cx + r2 + 2, B = cy + r2 + 2;
        int sens = (int)ToLong(pSens);
        if (sens <= 0 || sens > 100) sens = 40;
        int calib = (int)ToLong(pCalib);
        if (calib < 0) calib = 0;                  /* 默认关：自校准对宽扇形不可靠 */
        if (calib > 1) calib = 1;
        int draw = (int)ToLong(pDraw);
        COLORREF color = ParseColor(pColor, RGB(0, 255, 0));

        if (r1 < 1 || r2 <= r1) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }
        int vL = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vT = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vR = vL + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vB = vT + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (L < vL) L = vL;
        if (T < vT) T = vT;
        if (R > vR) R = vR;
        if (B > vB) B = vB;
        if (R - L <= 0 || B - T <= 0) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }

        if (OverlayPollutes()) {
            bool overlap = !(g_overlayRect.right <= L || g_overlayRect.left >= R ||
                             g_overlayRect.bottom <= T || g_overlayRect.top >= B);
            if (overlap) { HideOverlay(); Sleep(12); }
        }

        unsigned char *px = NULL; int cw = 0, ch = 0;
        if (!QmCaptureRGB(L, T, R - L, B - T, &px, &cw, &ch) || !px) {
            ApplyDraw(draw, color, NULL, 0);
            return RetStr(res, L"-1");
        }

        int method = (int)ToLong(pMethod);
        double ang = 0, conc = 0, cover = 0;
        int ucx = cx, ucy = cy;
        int ok = 0;

        if (method == 1) {
            ok = QmSectorAngle(px, cw, ch, cx - L, cy - T, r1, r2, sens, calib,
                               &ang, &conc, &cover, &ucx, &ucy);
        } else {
            /* 默认：径向梯度法（实测更稳） */
            int lx = cx - L, ly = cy - T;
            ok = QmSectorAngleGrad(px, cw, ch, lx, ly, r1, r2, sens, &ang, &conc, &cover);
            if (ok) {
                /* 交叉校验：换内环带再算一次 */
                int sp = r2 - r1;
                int r1b = r1 + (int)(sp * 0.28);
                int r2b = r2 - (int)(sp * 0.18);
                if (r2b > r1b + 8) {
                    double a2 = 0, c2v = 0, v2 = 0;
                    if (QmSectorAngleGrad(px, cw, ch, lx, ly, r1b, r2b, sens, &a2, &c2v, &v2) > 0) {
                        double d = a2 - ang;
                        while (d > 180) d -= 360;
                        while (d < -180) d += 360;
                        if (d < 0) d = -d;
                        if (c2v < conc) conc = c2v;
                        if (d > 20.0) conc *= 0.5;
                        if (d > 45.0) ok = 0;
                    }
                }
            }
        }
        free(px);

        RECT boxes[3];
        int nb = 0;
        boxes[nb].left = L; boxes[nb].top = T; boxes[nb].right = R; boxes[nb].bottom = B; nb++;
        if (ok) {
            double rad = (ang - 90.0) * 3.14159265 / 180.0;
            int mx = L + ucx + (int)(cos(rad) * r2 + 0.5);
            int my = T + ucy + (int)(sin(rad) * r2 + 0.5);
            boxes[nb].left = mx - 4; boxes[nb].top = my - 4;
            boxes[nb].right = mx + 4; boxes[nb].bottom = my + 4; nb++;
        }
        ApplyDraw(draw, color, boxes, nb);

        if (!ok) { LOG("Sector: not found"); return RetStr(res, L"-1"); }

        wchar_t buf[128];
        int a10 = (int)(ang * 10.0 + 0.5);
        int c1000 = (int)(conc * 1000.0 + 0.5);
        int v1000 = (int)(cover * 1000.0 + 0.5);
        wsprintfW(buf, L"%d.%d|%d.%03d|%d.%03d",
                  a10 / 10, a10 % 10, c1000 / 1000, c1000 % 1000, v1000 / 1000, v1000 % 1000);
        LOG("Sector: angle=%d.%d conc=%d cover=%d center=%d,%d",
            a10 / 10, a10 % 10, c1000, v1000, ucx, ucy);
        return RetStr(res, buf);
    }


    /* ---------- 相对鼠标移动（锁鼠标游戏转视角用） ---------- */
    HRESULT CmdMoveMouseR(VARIANT *pDx, VARIANT *pDy, VARIANT *pSteps,
                          VARIANT *pDelay, VARIANT *pMethod, VARIANT *res)
    {
        int dx = (int)ToLong(pDx);
        int dy = (int)ToLong(pDy);
        int steps = (int)ToLong(pSteps);
        int delay = (int)ToLong(pDelay);
        int method = (int)ToLong(pMethod);

        if (steps < 1) steps = 1;
        if (steps > 200) steps = 200;
        if (delay <= 0) delay = 8;
        if (delay > 500) delay = 500;

        /* 把总位移拆成 steps 份，余数分散到前面的步里，保证累加正好等于 dx/dy */
        int baseX = dx / steps, remX = dx % steps;
        int baseY = dy / steps, remY = dy % steps;
        int signX = (dx >= 0) ? 1 : -1, signY = (dy >= 0) ? 1 : -1;

        int sent = 0;
        for (int i_r5 = 0; i_r5 < steps; i_r5++) {
            int sx = baseX + ((i_r5 < (remX * signX)) ? signX : 0);
            int sy = baseY + ((i_r5 < (remY * signY)) ? signY : 0);
            if (sx == 0 && sy == 0) continue;

            if (method == 1) {
                /* 老接口：mouse_event */
                mouse_event(MOUSEEVENTF_MOVE, sx, sy, 0, 0);
            } else {
                INPUT in;
                ZeroMemory(&in, sizeof(in));
                in.type = INPUT_MOUSE;
                in.mi.dx = sx;
                in.mi.dy = sy;
                in.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000;   /* MOVE_NOCOALESCE */
                in.mi.time = 0;
                in.mi.dwExtraInfo = 0;
                if (SendInput(1, &in, sizeof(INPUT)) == 1) sent++;
            }
            if (steps > 1) Sleep(delay);
        }

        wchar_t buf[96];
        wsprintfW(buf, L"ok|%d|%d|%d", steps, dx, dy);
        LOG("MoveMouseR dx=%d dy=%d steps=%d sent=%d method=%d", dx, dy, steps, sent, method);
        (void)sent;
        return RetStr(res, buf);
    }
    /* 像素 → 鼠标计数（考虑系统鼠标速度与"提高指针精确度"） */
    static int QmPixelsToCounts(int px)
    {
        int speed = 10;
        SystemParametersInfoA(SPI_GETMOUSESPEED, 0, &speed, 0);
        if (speed < 1) speed = 1;
        if (speed > 20) speed = 20;

        int accel[3] = { 6, 10, 0 };
        SystemParametersInfoA(SPI_GETMOUSE, 0, accel, 0);

        double t = fabs((double)px) * 10.0 / (double)speed;   /* 反解到"加速前"的计数 */
        int sign = (px >= 0) ? 1 : -1;
        double c;
        if (!accel[2]) {
            c = t;
        } else {
            int th1 = accel[0], th2 = accel[1];
            if (th1 < 1) th1 = 1;
            if (th2 <= th1) th2 = th1 + 1;
            double seg1 = th1;
            double seg2 = th1 + 2.0 * (th2 - th1);
            if (t <= seg1) c = t;
            else if (t <= seg2) c = th1 + (t - seg1) / 2.0;
            else c = th2 + (t - seg2) / 4.0;
        }
        int out = (int)(sign * (c + 0.5));
        if (out == 0 && px != 0) out = sign;
        return out;
    }

    /* 按像素相对移动（等价按键精灵 MoveR 的观感） */
    HRESULT CmdMoveMouseP(VARIANT *pDx, VARIANT *pDy, VARIANT *pSteps,
                          VARIANT *pDelay, VARIANT *pMethod, VARIANT *res)
    {
        int px = (int)ToLong(pDx);
        int py = (int)ToLong(pDy);
        int cx = QmPixelsToCounts(px);
        int cy = QmPixelsToCounts(py);

        int steps = (int)ToLong(pSteps);
        int delay = (int)ToLong(pDelay);
        int method = (int)ToLong(pMethod);
        if (steps < 1) steps = 1;
        if (steps > 200) steps = 200;
        if (delay <= 0) delay = 8;
        if (delay > 500) delay = 500;

        int baseX = cx / steps, remX = cx % steps;
        int baseY = cy / steps, remY = cy % steps;
        int signX = (cx >= 0) ? 1 : -1, signY = (cy >= 0) ? 1 : -1;

        for (int i_r4 = 0; i_r4 < steps; i_r4++) {
            int sx = baseX + ((i_r4 < (remX * signX)) ? signX : 0);
            int sy = baseY + ((i_r4 < (remY * signY)) ? signY : 0);
            if (sx == 0 && sy == 0) continue;
            if (method == 1) {
                mouse_event(MOUSEEVENTF_MOVE, sx, sy, 0, 0);
            } else {
                INPUT in;
                ZeroMemory(&in, sizeof(in));
                in.type = INPUT_MOUSE;
                in.mi.dx = sx;
                in.mi.dy = sy;
                in.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000;
                SendInput(1, &in, sizeof(INPUT));
            }
            if (steps > 1) Sleep(delay);
        }

        wchar_t buf[128];
        wsprintfW(buf, L"ok|%d|%d|%d|%d", steps, px, py, cx);
        LOG("MoveMouseP px=%d,%d -> counts=%d,%d steps=%d", px, py, cx, cy, steps);
        return RetStr(res, buf);
    }
};

/* ---------- 类工厂 ---------- */
class CFactory : public IClassFactory
{
public:
    CFactory() : m_ref(1) {}
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv)
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        CPlugin *p = new CPlugin();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) { return S_OK; }
private:
    ~CFactory() {}
    LONG m_ref;
};

/* ---------- 注册 ---------- */
static const wchar_t *kProgId    = L"Plugin.zaoyo";      /* 主 ProgID */
/* 同时注册这些别名，旧脚本用 QMPlugin.zaoyo 也还能跑 */
static const wchar_t *kProgIds[] = { L"Plugin.zaoyo", L"QMPlugin.zaoyo", L"zaoyo" };

static LONG RegSetStr(HKEY root, const wchar_t *subkey, const wchar_t *name, const wchar_t *value)
{
    HKEY k = NULL;
    LONG r = RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return r;
    if (value)
        r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value,
                           (DWORD)((lstrlenW(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r;
}

/* 带 KEY_WOW64_32KEY / KEY_WOW64_64KEY 的版本。
   ProgID 不在 WOW64 的重定向清单里（只有 CLSID/Interface/TypeLib 会被重定向到 Wow6432Node），
   所以普通写法只能落到 64 位视图，而按键精灵（32 位）读的是 Wow6432Node，于是拿不到 ProgID。
   必须用 KEY_WOW64_32KEY 显式写 32 位视图。 */
static LONG RegSetStrEx(HKEY root, const wchar_t *subkey, const wchar_t *name,
                        const wchar_t *value, REGSAM extra)
{
    HKEY k = NULL;
    LONG r = RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_WRITE | extra, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return r;
    if (value)
        r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value,
                           (DWORD)((lstrlenW(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r;
}

/* VB/VC 生成的插件在 DllRegisterServer 里都会把类型库注册到 HKCR\TypeLib。
   按键精灵靠这条信息识别"这是个可用的插件"（实测：能用的 Plug365New 有 TypeLib 注册，
   我们的没有，于是就出现"能枚举命令但 Plugin.zaoyo.命令 静默返回空"）。 */
static void RegTypeLib(const wchar_t *dll)
{
    ITypeLib *ptl = NULL;
    if (FAILED(LoadTypeLibEx(dll, REGKIND_NONE, &ptl)) || !ptl) { LOG("LoadTypeLibEx 失败"); return; }
    HRESULT hr = RegisterTypeLib(ptl, (OLECHAR *)dll, NULL);
    LOG("RegisterTypeLib hr=0x%08lX", (unsigned long)hr);
    ptl->Release();
}

static void UnregTypeLib(void)
{
    UnRegisterTypeLib(LIBID_QmFindPic, 1, 0, 0, SYS_WIN32);
}

static HRESULT DoRegister(BOOL reg)
{
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(g_hInst, dll, MAX_PATH);
    wchar_t clsid[64];
    StringFromGUID2(CLSID_QmFindPic, clsid, 64);

    wchar_t clsSub[128], inprocSub[192], progIdSub[192], progSub[192];
    wsprintfW(clsSub, L"CLSID\\%s", clsid);
    wsprintfW(inprocSub, L"CLSID\\%s\\InProcServer32", clsid);
    wsprintfW(progIdSub, L"%s\\CLSID", kProgId);
    wsprintfW(progSub, L"%s", kProgId);

    struct { HKEY root; const wchar_t *prefix; } roots[2] = {
        { HKEY_CURRENT_USER,  L"Software\\Classes\\" },   /* 用户级：普通权限也能写，放第一位 */
        { HKEY_LOCAL_MACHINE, L"Software\\Classes\\" },   /* 机器级：需要管理员，能写就写 */
    };
    /* 关键（v2.7 修）：WOW64 只会把 Software\Classes\CLSID / Interface / TypeLib
       这几个子键重定向到 Wow6432Node，**ProgID 不重定向**。所以 32 位进程写
       "Software\Classes\QMPlugin.zaoyo" 会落在 64 位视图，而按键精灵（32 位）
       读的是 Wow6432Node → 看不到 ProgID → 内部 CreateObject("") 报
       "无效的过程调用或参数"。因此 ProgID 必须显式写进 Wow6432Node 一份。 */
    bool okAny = false;
    for (int i_r3 = 0; i_r3 < 2; i_r3++) {
        wchar_t pCls[320], pInproc[384];
        wsprintfW(pCls, L"%s%s", roots[i_r3].prefix, clsSub);
        wsprintfW(pInproc, L"%s%s", roots[i_r3].prefix, inprocSub);
        if (reg) {
            bool okCls = (RegSetStr(roots[i_r3].root, pCls, NULL, kProgIds[0]) == ERROR_SUCCESS);
            if (okCls) {
                RegSetStr(roots[i_r3].root, pInproc, NULL, dll);
                RegSetStr(roots[i_r3].root, pInproc, L"ThreadingModel", L"Apartment");
            }
            bool okProg = false;
            for (int k_r2 = 0; k_r2 < 3; k_r2++) {
                /* ① 32 位视图（按键精灵真正读的地方）：必须用 KEY_WOW64_32KEY 显式指定，
                       因为 ProgID 不在 WOW64 自动重定向的清单里 */
                wchar_t pP[384], pPID[448];
                wsprintfW(pP, L"%s%s", roots[i_r3].prefix, kProgIds[k_r2]);
                wsprintfW(pPID, L"%s%s\\CLSID", roots[i_r3].prefix, kProgIds[k_r2]);
                if (RegSetStrEx(roots[i_r3].root, pP, NULL, kProgIds[k_r2], KEY_WOW64_32KEY) == ERROR_SUCCESS &&
                    RegSetStrEx(roots[i_r3].root, pPID, NULL, clsid, KEY_WOW64_32KEY) == ERROR_SUCCESS) okProg = true;
                /* ② 共享/64 位视图（兼容，无害） */
                RegSetStr(roots[i_r3].root, pP, NULL, kProgIds[k_r2]);
                RegSetStr(roots[i_r3].root, pPID, NULL, clsid);
            }
            if (okCls || okProg) okAny = true;
        } else {
            for (int k_r1 = 0; k_r1 < 3; k_r1++) {
                wchar_t pP[384], pPID[448];
                /* 共享/64 位视图那份 */
                wsprintfW(pPID, L"%s%s\\CLSID", roots[i_r3].prefix, kProgIds[k_r1]);
                RegDeleteTreeW(roots[i_r3].root, pPID);
                wsprintfW(pP, L"%s%s", roots[i_r3].prefix, kProgIds[k_r1]);
                RegDeleteTreeW(roots[i_r3].root, pP);
                /* 32 位视图那份（显式路径，RegDeleteTree 走不到 Wow64 标志） */
                wsprintfW(pPID, L"%sWow6432Node\\%s\\CLSID", roots[i_r3].prefix, kProgIds[k_r1]);
                RegDeleteTreeW(roots[i_r3].root, pPID);
                wsprintfW(pP, L"%sWow6432Node\\%s", roots[i_r3].prefix, kProgIds[k_r1]);
                RegDeleteTreeW(roots[i_r3].root, pP);
            }
            RegDeleteTreeW(roots[i_r3].root, pInproc);
            RegDeleteTreeW(roots[i_r3].root, pCls);
            okAny = true;
        }
    }
    /* 类型库注册（与 VB/VC 生成的插件一致） */
    if (reg) RegTypeLib(dll); else UnregTypeLib();
    /* 两个根都写不进（例如被组策略/沙箱挡住）要如实报错，
       否则 regsvr32 会假报成功，排查时会被误导 */
    return okAny ? S_OK : (reg ? 0x80040201L /* SELFREG_E_CLASS */ : S_OK);
}

#ifndef ZAOYO_NO_COM
extern "C" {

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    /* 注意：DLL_PROCESS_DETACH 时处于加载器锁内，绝不能碰窗口/GDI，
       否则会崩掉宿主进程（表现为"脚本突然停止执行"）。 */
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
#ifdef QM_DEBUG
        {
            wchar_t cmd[2048] = {0};
            GetCommandLineW();
            lstrcpynW(cmd, GetCommandLineW(), 2048);
            char cmda[4096] = {0}, cwd[MAX_PATH] = {0};
            WideCharToMultiByte(CP_ACP, 0, cmd, -1, cmda, 4096, NULL, NULL);
            GetCurrentDirectoryA(MAX_PATH, cwd);
            LOG("CMD=[%s]", cmda);
            LOG("CWD=[%s]", cwd);
        }
#endif
    } else if (reason == DLL_PROCESS_DETACH) {
        /* 只置停止标志 + 关采集线程（不创建/销毁窗口），否则会卡住宿主退出 */
        SndHostShutdown();
    }
    return TRUE;
}

STDMETHODIMP DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    LOG("DllGetClassObject clsid=%08lX iid=%08lX", (unsigned long)rclsid.Data1, (unsigned long)riid.Data1);
    if (!IsEqualCLSID(rclsid, CLSID_QmFindPic)) return CLASS_E_CLASSNOTAVAILABLE;
    if (!IsEqualIID(riid, IID_IClassFactory) && !IsEqualIID(riid, IID_IUnknown)) {
        CPlugin *p = new CPlugin();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    CFactory *f = new CFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDMETHODIMP DllCanUnloadNow(void) { return S_FALSE; }
STDMETHODIMP DllRegisterServer(void) { LOG("DllRegisterServer"); return DoRegister(TRUE); }
STDMETHODIMP DllUnregisterServer(void) { return DoRegister(FALSE); }


} /* extern "C" */
#endif /* ZAOYO_NO_COM */

/* ============================================================
   供 MFC 壳在同进程内直接调用（单一 DLL 方案，不走 IDispatch 客户端）
   ============================================================ */
extern "C" __declspec(dllexport) void ZaoyoSetInstance(void *hInstance)
{
    g_hInst = (HINSTANCE)hInstance;
}

extern "C" __declspec(dllexport) HRESULT ZaoyoInvokeDirect(int dispid, int nArgs,
                                                           VARIANT *argsIn, VARIANT *result)
{
    static CPlugin *s_obj = NULL;
    VARIANT rev[16];
    DISPPARAMS dp;
    int i;
    if (!s_obj) {
        s_obj = new CPlugin();
        if (!s_obj) return E_OUTOFMEMORY;
    }
    if (nArgs < 0) nArgs = 0;
    if (nArgs > 16) nArgs = 16;
    for (i = 0; i < nArgs; i++) rev[i] = argsIn[nArgs - 1 - i];   /* 声明顺序 -> DISPPARAMS 顺序 */
    dp.rgvarg = rev;
    dp.rgdispidNamedArgs = NULL;
    dp.cArgs = (ULONG)nArgs;
    dp.cNamedArgs = 0;
    return s_obj->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, result, NULL, NULL);
}

