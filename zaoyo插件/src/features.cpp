#include "vc6compat.h"
/* ============================================================
   features.cpp
   1) 附件解析：按键精灵把附件以 base64(zip/deflate) 内嵌在 .Q 的 [Attachment] 段
   2) 多点颜色匹配：对采样点做"绝对色差 + 归一化结构相关"的混合评分
   ============================================================ */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "features.h"

/* ---------------- 调试日志（%TEMP%\QmFindPic\trace.log） ---------------- */
#ifdef QM_DEBUG
static void flog(const char *fmt, ...)
{
    char dir[MAX_PATH] = {0}, path[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    wsprintfA(path, "%sQmFindPic", dir);
    CreateDirectoryA(path, NULL);
    wsprintfA(path, "%sQmFindPic\\trace.log", dir);
    char body[1024];
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(body, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    body[n++] = '\r'; body[n++] = '\n';
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; WriteFile(h, body, (DWORD)n, &w, NULL); CloseHandle(h);
}
#else
static void flog(const char *fmt, ...) { (void)fmt; }
#endif

/* ---------------- 小工具 ---------------- */
static void WCopy(wchar_t *dst, const wchar_t *src, int cap) { if (cap > 0) { lstrcpynW(dst, src, cap); } }
static void WCat(wchar_t *dst, const wchar_t *src, int cap)
{
    int n = lstrlenW(dst);
    if (n < cap - 1) lstrcpynW(dst + n, src, cap - n);
}
static int B64Val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* 从 in 的 start 位置（必须是 4 的倍数）开始解码；返回解码长度 */
static int Base64DecodeFrom(const char *in, int len, int start,
                            unsigned char *out, int cap)
{
    int val = 0, valb = -8, o = 0;
    for (int i_r86 = start; i_r86 < len; i_r86++) {
        char c = in[i_r86];
        if (c == '=') break;
        int d = B64Val(c);
        if (d < 0) {
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
            break;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            if (o < cap) out[o++] = (unsigned char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return o;
}

/* ---------------- raw deflate（cabinet.dll 的 Decompress API） ---------------- */
typedef BOOL (WINAPI *PFN_CreateD)(ULONG, PVOID, PVOID *);
typedef BOOL (WINAPI *PFN_Decomp)(PVOID, PVOID, SIZE_T, PVOID, SIZE_T, PSIZE_T);
typedef BOOL (WINAPI *PFN_CloseD)(PVOID);

/* Windows 的压缩算法 ID：DEFLATE 在不同 SDK 里是 6（也有文档写 4），两个都试 */
static const ULONG kAlgorithms[] = { 6, 4 };

static bool InflateRaw(const unsigned char *src, int srcLen, unsigned char *dst, int dstLen)
{
    static HMODULE hCab = NULL;
    static PFN_CreateD pCreate = NULL;
    static PFN_Decomp pDecomp = NULL;
    static PFN_CloseD pClose = NULL;

    if (!hCab) {
        hCab = LoadLibraryA("cabinet.dll");
        if (hCab) {
            pCreate = (PFN_CreateD)GetProcAddress(hCab, "CreateDecompressor");
            pDecomp = (PFN_Decomp)GetProcAddress(hCab, "Decompress");
            pClose  = (PFN_CloseD)GetProcAddress(hCab, "CloseDecompressor");
        }
    }
    if (!pCreate || !pDecomp || !pClose) return false;

    for (int i_r85 = 0; i_r85 < 2; i_r85++) {
        HANDLE h = NULL;
        if (!pCreate(kAlgorithms[i_r85], NULL, &h)) continue;
        SIZE_T got = 0;
        BOOL ok = pDecomp(h, (PVOID)src, (SIZE_T)srcLen, dst, (SIZE_T)dstLen, &got);
        DWORD err = GetLastError();
        pClose(h);
        flog("inflate alg=%lu ok=%d got=%lu/%d err=%lu",
             (unsigned long)kAlgorithms[i_r85], (int)ok, (unsigned long)got, dstLen, (unsigned long)err);
        if (ok && (int)got == dstLen) return true;
    }
    return false;
}

/* ---------------- zip 解析（支持 store/deflate） ---------------- */
static unsigned short Rd16(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }
static unsigned int   Rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* 在 zip 里找条目并解出内容（返回值需要 free） */
static bool ZipExtract(const unsigned char *z, int n, const char *want,
                       unsigned char **outBuf, int *outLen)
{
    *outBuf = NULL; *outLen = 0;
    if (n < 22) return false;

    /* 从尾部找 EOCD: PK\x05\x06 */
    int eocd = -1;
    int scanFrom = n - 22;
    int limit = n - 65557; if (limit < 0) limit = 0;
    for (int i_r84 = scanFrom; i_r84 >= limit; i_r84--) {
        if (z[i_r84] == 0x50 && z[i_r84+1] == 0x4B && z[i_r84+2] == 0x05 && z[i_r84+3] == 0x06) { eocd = i_r84; break; }
    }
    if (eocd < 0) return false;

    unsigned int cdCount = Rd16(z + eocd + 10);
    unsigned int cdOff   = Rd32(z + eocd + 16);
    if ((int)cdOff >= n) return false;

    unsigned int p = cdOff;
    for (unsigned int k = 0; k < cdCount && (int)p + 46 <= n; k++) {
        if (!(z[p] == 0x50 && z[p+1] == 0x4B && z[p+2] == 0x01 && z[p+3] == 0x02)) break;
        unsigned short method  = Rd16(z + p + 10);
        unsigned int compSize  = Rd32(z + p + 20);
        unsigned int uncompSize= Rd32(z + p + 24);
        unsigned short nameLen = Rd16(z + p + 28);
        unsigned short extraLen= Rd16(z + p + 30);
        unsigned short cmtLen  = Rd16(z + p + 32);
        unsigned int localOff  = Rd32(z + p + 42);
        const char *name = (const char *)(z + p + 46);
        if ((int)(p + 46 + nameLen) > n) break;

        /* 注意：zip 里的文件名不带 NUL 结尾，必须用 nameLen 判长度 */
        bool match = ((int)nameLen == (int)strlen(want)) &&
                     (_strnicmp(name, want, nameLen) == 0);
        if (k < 3) {
            char nm[128] = {0};
            int cp2 = nameLen < 127 ? nameLen : 127;
            memcpy(nm, name, (size_t)cp2);
            flog("zip entry[%u]=[%s] method=%u comp=%u uncomp=%u match=%d",
                 k, nm, method, compSize, uncompSize, (int)match);
        }

        if (match && (int)localOff + 30 <= n && (unsigned int)uncompSize > 0) {
            const unsigned char *lf = z + localOff;
            unsigned short lnLen = Rd16(lf + 26);
            unsigned short leLen = Rd16(lf + 28);
            int dataOff = localOff + 30 + lnLen + leLen;
            if (dataOff + (int)compSize > n) return false;

            unsigned char *buf = (unsigned char *)malloc(uncompSize + 1);
            if (!buf) return false;
            bool ok = false;
            if (method == 0) {
                memcpy(buf, z + dataOff, uncompSize);
                ok = true;
            } else if (method == 8) {
                ok = InflateRaw(z + dataOff, (int)compSize, buf, (int)uncompSize);
            }
            if (!ok) { free(buf); return false; }
            buf[uncompSize] = 0;
            *outBuf = buf; *outLen = (int)uncompSize;
            return true;
        }
        p += 46 + nameLen + extraLen + cmtLen;
    }
    return false;
}

/* 只解码 base64 尾部若干字节，判断 zip 里有没有这个文件名（便宜） */
static bool ZipHasNameInTail(const char *b64, int len, const char *want)
{
    if (len < 8) return false;
    int take = len < 8000 ? len : 8000;
    int start = len - take;
    start -= (start % 4);
    if (start < 0) start = 0;
    int cap = (len - start) * 3 / 4 + 8;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return false;
    int got = Base64DecodeFrom(b64, len, start, buf, cap);
    bool found = false;
    if (got > 0) {
        /* zip 的中央目录里文件名是明文 */
        for (int i_r83 = 0; i_r83 + (int)strlen(want) <= got; i_r83++) {
            if (buf[i_r83] == (unsigned char)want[0] &&
                _strnicmp((const char *)(buf + i_r83), want, strlen(want)) == 0) { found = true; break; }
        }
    }
    free(buf);
    return found;
}

/* 从 .Q 文本里取出 [Attachment] 段的 base64 */
static bool ExtractAttachmentB64(const char *text, int len,
                                 const char **b64, int *b64Len)
{
    const char *key = "[Attachment]";
    int klen = (int)strlen(key);
    int pos = -1;
    for (int i_r82 = 0; i_r82 + klen <= len; i_r82++) {
        if (text[i_r82] == '[' && _strnicmp(text + i_r82, key, klen) == 0) { pos = i_r82; break; }
    }
    if (pos < 0) return false;
    int i = pos + klen;
    while (i < len && (text[i] == '\r' || text[i] == '\n' || text[i] == ' ')) i++;
    int start = i;
    /* 直到空行或下一个 [Section] */
    while (i < len) {
        if (text[i] == '\r' || text[i] == '\n') {
            int j = i + 1;
            if (j < len && text[j] == '\n') j++;
            while (j < len && (text[j] == '\r' || text[j] == '\n' || text[j] == ' ')) j++;
            if (j >= len || text[j] == '[') break;
            i = j;
        } else i++;
    }
    if (i <= start) return false;
    *b64 = text + start;
    *b64Len = i - start;
    return true;
}

/* ---------------- 缓存目录 ---------------- */
static void GetAttCacheDir(char *out, int cap)
{
    char tmp[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, tmp);
    wsprintfA(out, "%sQmFindPic\\att\\", tmp);
    CreateDirectoryA(tmp, NULL);
    char d1[MAX_PATH];
    wsprintfA(d1, "%sQmFindPic", tmp);
    CreateDirectoryA(d1, NULL);
    CreateDirectoryA(out, NULL);
}

static bool ReadWholeFile(const char *path, char **out, int *outLen)
{
    *out = NULL; *outLen = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 32u * 1024 * 1024) { CloseHandle(h); return false; }
    char *buf = (char *)malloc(size + 1);
    if (!buf) { CloseHandle(h); return false; }
    DWORD got = 0;
    if (!ReadFile(h, buf, size, &got, NULL) || got != size) { free(buf); CloseHandle(h); return false; }
    buf[size] = 0;
    CloseHandle(h);
    *out = buf; *outLen = (int)size;
    return true;
}

/* 递归扫 QMScript\*.Q，找到含该附件的脚本并解出来 */
struct QmFile { char path[MAX_PATH]; FILETIME ft; };

static int CollectScripts(const char *qmsPath, QmFile *files, int maxFiles)
{
    char dirs[64][MAX_PATH];
    int nd = 0;
    DWORD at = GetFileAttributesA(qmsPath);
    {
        char hx[512] = {0};
        int hp = wsprintfA(hx, "qms ansi hex:");
        for (int i_r81 = 0; i_r81 < 24 && qmsPath[i_r81]; i_r81++) hp += wsprintfA(hx + hp, " %02X", (unsigned char)qmsPath[i_r81]);
        flog("%s attr=%lu", hx, (unsigned long)at);
    }
    if (at == INVALID_FILE_ATTRIBUTES) return 0;
    lstrcpynA(dirs[nd], qmsPath, MAX_PATH);
    nd++;

    int nf = 0;
    for (int di_r80 = 0; di_r80 < nd && nd < 64; di_r80++) {
        WIN32_FIND_DATAA fd;
        char pat[MAX_PATH];
        lstrcpynA(pat, dirs[di_r80], MAX_PATH);
        lstrcatA(pat, "\\*");
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.cFileName[0] == '.') continue;
                if (nd < 64) {
                    char sub[MAX_PATH] = {0};
                    lstrcpynA(sub, dirs[di_r80], MAX_PATH);
                    lstrcatA(sub, "\\");
                    lstrcatA(sub, fd.cFileName);
                    lstrcpynA(dirs[nd], sub, MAX_PATH);
                    nd++;
                }
                continue;
            }
            int nlen = lstrlenA(fd.cFileName);
            if (nlen < 3 || _stricmp(fd.cFileName + nlen - 2, ".Q") != 0) continue;
            if (nf < maxFiles) {
                lstrcpynA(files[nf].path, dirs[di_r80], MAX_PATH);
                lstrcatA(files[nf].path, "\\");
                lstrcatA(files[nf].path, fd.cFileName);
                files[nf].ft = fd.ftLastWriteTime;
                nf++;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (int i_r79 = 1; i_r79 < nf; i_r79++) {
        QmFile key = files[i_r79];
        int j = i_r79 - 1;
        while (j >= 0 && CompareFileTime(&files[j].ft, &key.ft) < 0) { files[j + 1] = files[j]; j--; }
        files[j + 1] = key;
    }
    return nf;
}

static bool ScanScriptsByQmsPath(const char *qmsPath, const char *want,
                                   unsigned char **outBuf, int *outLen)
{
    static QmFile files[400];
    int nf = CollectScripts(qmsPath, files, 400);
    flog("att: scan %d scripts", nf);
    if (nf <= 0) return false;

    DWORD startTick = GetTickCount();
    for (int i_r78 = 0; i_r78 < nf; i_r78++) {
        if (GetTickCount() - startTick > 6000) { flog("att: scan timeout at %d", i_r78); return false; }
        char *text = NULL; int tlen = 0;
        if (!ReadWholeFile(files[i_r78].path, &text, &tlen)) continue;

        const char *b64 = NULL; int blen = 0;
        bool ok = false;
        bool hasSec = ExtractAttachmentB64(text, tlen, &b64, &blen);
        if (hasSec && ZipHasNameInTail(b64, blen, want)) {
            flog("att: hit %s (b64=%d)", files[i_r78].path, blen);
            int cap = blen * 3 / 4 + 16;
            unsigned char *zip = (unsigned char *)malloc(cap);
            if (zip) {
                int zl = Base64DecodeFrom(b64, blen, 0, zip, cap);
                if (zl > 0 && ZipExtract(zip, zl, want, outBuf, outLen)) {
                    ok = true;
                    flog("att: extracted %d bytes", *outLen);
                } else {
                    flog("att: extract failed zip=%d", zl);
                }
                free(zip);
            }
        }
        free(text);
        if (ok) return true;
    }
    flog("att: scanned all, not found");
    return false;
}
bool QmResolveAttachment(const char *name, char *outPath, int cap)
{
    flog("att: resolve [%s]", name);
    char cacheDir[MAX_PATH];
    GetAttCacheDir(cacheDir, MAX_PATH);

    char cached[MAX_PATH];
    wsprintfA(cached, "%s%s", cacheDir, name);
    if (GetFileAttributesA(cached) != INVALID_FILE_ATTRIBUTES) {
        lstrcpynA(outPath, cached, cap);
        return true;
    }

    /* 先按"本 DLL 所在的 plugin 目录"推断按键精灵根目录（最可靠）。
       全程用宽字符 API，避免中文路径在 ANSI 转换中丢失。 */
    /* 纯 ANSI 字节处理：宿主 exe（Runner.exe）一般在按键精灵目录下 */
    char host[MAX_PATH] = {0};
    {
        char self[MAX_PATH] = {0};
        /* 优先用本 DLL 的路径推断（小精灵场景也在同一目录体系） */
        HMODULE hSelf = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&QmResolveAttachment, &hSelf) && hSelf) {
            GetModuleFileNameA(hSelf, self, MAX_PATH);
        }
        if (self[0] == 0) GetModuleFileNameA(NULL, self, MAX_PATH);
        char *bs = strrchr(self, '\\');
        if (bs) *bs = 0;

        char qms[MAX_PATH];
        lstrcpynA(qms, self, MAX_PATH);
        lstrcatA(qms, "\\QMScript");
        if (GetFileAttributesA(qms) != INVALID_FILE_ATTRIBUTES) {
            lstrcpynA(host, self, MAX_PATH);
        } else {
            /* 本 DLL 在 ...\plugin 下：去掉最后一级再试 */
            char up[MAX_PATH];
            lstrcpynA(up, self, MAX_PATH);
            char *bs2 = strrchr(up, '\\');
            if (bs2) {
                *bs2 = 0;
                lstrcpynA(qms, up, MAX_PATH);
                lstrcatA(qms, "\\QMScript");
                if (GetFileAttributesA(qms) != INVALID_FILE_ATTRIBUTES) lstrcpynA(host, up, MAX_PATH);
            }
            if (host[0] == 0) {
                /* 再退回宿主 exe 目录 */
                char alt[MAX_PATH] = {0};
                GetModuleFileNameA(NULL, alt, MAX_PATH);
                char *bs3 = strrchr(alt, '\\');
                if (bs3) *bs3 = 0;
                lstrcpynA(host, alt, MAX_PATH);
            }
        }
    }    {
        char hx[512] = {0};
        int hp = wsprintfA(hx, "host hex(len=%d):", lstrlenA(host));
        for (int i_r77 = 0; i_r77 < 20 && host[i_r77]; i_r77++) hp += wsprintfA(hx + hp, " %02X", (unsigned char)host[i_r77]);
        flog("%s", hx);
    }
    flog("att: host=%s", host);
    char *bs = strrchr(host, '\\');
    if (bs) *bs = 0;

    /* 先看几个约定目录（用户自己放的话直接命中） */
    char probe[MAX_PATH];
    wsprintfA(probe, "%s\\attachment\\%s", host, name);
    if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) { lstrcpynA(outPath, probe, cap); return true; }

    unsigned char *buf = NULL; int len = 0;
    {
        /* 显式构造 QMScript 路径（多路尝试），避免依赖中间变量 */
        char qms[MAX_PATH] = {0};
        char cand[MAX_PATH];

        /* 1) 本 DLL 的上级目录（…\plugin\ 的父级） */
        char self[MAX_PATH] = {0};
        HMODULE hSelf = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&QmResolveAttachment, &hSelf) && hSelf) {
            GetModuleFileNameA(hSelf, self, MAX_PATH);
        }
        if (self[0]) {
            char up[MAX_PATH];
            lstrcpynA(up, self, MAX_PATH);
            char *bs = strrchr(up, '\\');           /* 去掉文件名 → …\plugin */
            if (bs) {
                *bs = 0;
                lstrcpynA(cand, up, MAX_PATH);
                lstrcatA(cand, "\\QMScript");       /* …\plugin\QMScript（不对） */
                char up2[MAX_PATH];
                lstrcpynA(up2, up, MAX_PATH);
                char *bs2 = strrchr(up2, '\\');     /* 再去掉 plugin → 按键精灵目录 */
                if (bs2) {
                    *bs2 = 0;
                    lstrcpynA(cand, up2, MAX_PATH);
                    lstrcatA(cand, "\\QMScript");
                    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) lstrcpynA(qms, cand, MAX_PATH);
                }
            }
        }
        /* 2) 宿主 exe 目录 */
        if (qms[0] == 0) {
            char base[MAX_PATH] = {0};
            GetModuleFileNameA(NULL, base, MAX_PATH);
            char *bs = strrchr(base, '\\');
            if (bs) *bs = 0;
            lstrcpynA(cand, base, MAX_PATH);
            lstrcatA(cand, "\\QMScript");
            if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) lstrcpynA(qms, cand, MAX_PATH);
        }
        /* 3) 传入的 host */
        if (qms[0] == 0) {
            lstrcpynA(cand, host, MAX_PATH);
            lstrcatA(cand, "\\QMScript");
            if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) lstrcpynA(qms, cand, MAX_PATH);
        }
        if (qms[0] == 0) { flog("att: no QMScript found"); return false; }
        if (!ScanScriptsByQmsPath(qms, name, &buf, &len)) { flog("att: scan miss"); return false; }
    }

    /* 写缓存 */
    char dst[MAX_PATH];
    wsprintfA(dst, "%s%s", cacheDir, name);
    char *sub = strrchr(dst, '\\');
    HANDLE h = CreateFileA(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(buf); return false; }
    DWORD wrote = 0;
    WriteFile(h, buf, (DWORD)len, &wrote, NULL);
    CloseHandle(h);
    free(buf);

    lstrcpynA(outPath, dst, cap);
    return true;
}

/* ---------------- 离线自检（仅调试版）：验证负色逻辑 ---------------- */
extern "C" __declspec(dllexport) void QmSelfTestColor(void)
{
    /* 造一张 30x30 图：整体底色 A，位置(10,10)附近点 B 和 C */
    const int W = 30, H = 30;
    unsigned char *img = (unsigned char *)malloc((size_t)W * H * 4);
    for (int i_r76 = 0; i_r76 < W * H; i_r76++) {
        img[i_r76 * 4 + 0] = 0x20; img[i_r76 * 4 + 1] = 0x10; img[i_r76 * 4 + 2] = 0x08; img[i_r76 * 4 + 3] = 0xFF;
    }
    /* 在 (10,10) 放主色 A=(R=200,G=100,B=50)，(+8,0) 放 B=(R=10,G=200,B=10)，(0,+8) 放 C=(R=90,G=90,B=200) */
    int anchors[3][3] = { {10,10,0}, {18,10,0}, {10,18,0} };
    unsigned char cols[3][3] = { {200,100,50}, {10,200,10}, {90,90,200} };
    for (int k_r75 = 0; k_r75 < 3; k_r75++) {
        int x = anchors[k_r75][0], y = anchors[k_r75][1];
        unsigned char *q = img + ((size_t)y * W + x) * 4;
        q[0] = cols[k_r75][2]; q[1] = cols[k_r75][1]; q[2] = cols[k_r75][0];
    }
    QmSample pts[4];
    pts[0].dx = 0; pts[0].dy = 0; pts[0].r = 200; pts[0].g = 100; pts[0].b = 50; pts[0].neg = 0;
    pts[1].dx = 8; pts[1].dy = 0; pts[1].r = 10;  pts[1].g = 200; pts[1].b = 10; pts[1].neg = 0;
    pts[2].dx = 0; pts[2].dy = 8; pts[2].r = 90;  pts[2].g = 90;  pts[2].b = 200; pts[2].neg = 0;
    int ox[8], oy[8]; double os[8];

    /* 1) 三个正色点：应命中 (10,10) 且分数接近 1 */
    int n1 = QmFindColorMulti(img, W, H, pts, 3, 0.9, 0, 8, 1, ox, oy, os);

    /* 2) 加一个负色点 (4,4)，给它一个"那里不存在"的颜色 → 应命中，且分数不被负色点拉低 */
    pts[3].dx = 4; pts[3].dy = 4; pts[3].r = 0xFF; pts[3].g = 0xFF; pts[3].b = 0xFF; pts[3].neg = 1;
    int n2 = QmFindColorMulti(img, W, H, pts, 4, 0.9, 0, 8, 1, ox, oy, os);
    double s2 = (n2 > 0) ? os[0] : -1;

    /* 3) 负色点改成该位置的真实颜色（底板色）→ 容差 0、全等即否决 → 应判失败 */
    pts[3].r = 0x08; pts[3].g = 0x10; pts[3].b = 0x20;
    int n3 = QmFindColorMulti(img, W, H, pts, 4, 0.9, 0, 8, 1, ox, oy, os);

    char out[512];
    wsprintfA(out, "selftest: n1=%d(x=%d,y=%d) n2=%d score2=%d.%03d n3=%d",
              n1, n1 ? ox[0] : -1, n1 ? oy[0] : -1, n2,
              (int)s2, (int)((s2 - (int)s2) * 1000), n3);
    flog("%s", out);
    free(img);
}

/* ---------------- 抓屏 BGRA ---------------- */
bool QmCaptureRGB(int x, int y, int w, int h, unsigned char **outPx, int *outW, int *outH)
{
    *outPx = NULL; *outW = *outH = 0;
    if (w <= 0 || h <= 0) return false;
    HDC hScreen = GetDC(NULL);
    if (!hScreen) return false;
    HDC hMem = CreateCompatibleDC(hScreen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
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
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (px) { memcpy(px, bits, (size_t)w * h * 4); *outPx = px; *outW = w; *outH = h; }
    SelectObject(hMem, old);
    DeleteObject(hbm);
    DeleteDC(hMem);
    ReleaseDC(NULL, hScreen);
    return px != NULL;
}

/* ---------------- 多点颜色匹配 ---------------- */
struct Cand2 { int x, y; double score; int seq; };

/* ===== 候选池：最小堆（堆顶 = 当前最差候选）=====
   目标是"始终保留分数最高的 maxCand 个"：
   用最小堆替换堆顶即可 O(log n)，不必每次扫两遍 512 找最差、再重算最小值。
   同分时 seq 大的算更差 → 与"同分保留先扫到的"语义一致。 */
static int CandWorse(const Cand2 *c, int a, int b)
{
    if (c[a].score != c[b].score) return c[a].score < c[b].score;
    return c[a].seq > c[b].seq;
}

static void HeapUp(int *h, int i, const Cand2 *c)
{
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (!CandWorse(c, h[i], h[p])) break;
        int t = h[i]; h[i] = h[p]; h[p] = t;
        i = p;
    }
}

static void HeapDown(int *h, int n, int i, const Cand2 *c)
{
    for (;;) {
        int l = (i << 1) + 1, r = l + 1, m = i;
        if (l < n && CandWorse(c, h[l], h[m])) m = l;
        if (r < n && CandWorse(c, h[r], h[m])) m = r;
        if (m == i) break;
        int t = h[i]; h[i] = h[m]; h[m] = t;
        i = m;
    }
}

/* 只给"正色点"打分（负色点不进来，见 QmFindColorMulti 的门限） */
static double ColorScore(const unsigned char *pix, int W,
                         const QmSample *pts, int n,
                         double tplMean, double tplStd)
{
    /* 绝对色差点：1 - 归一化色差，对正色点取平均 */
    double sumD = 0;
    for (int i_r74 = 0; i_r74 < n; i_r74++) {
        const unsigned char *p = pix + (size_t)pts[i_r74].dy * W * 4 + (size_t)pts[i_r74].dx * 4;
        double dr = (double)p[2] - pts[i_r74].r;
        double dg = (double)p[1] - pts[i_r74].g;
        double db = (double)p[0] - pts[i_r74].b;
        double d = sqrt(dr * dr + dg * dg + db * db) / 441.673;   /* 0~1 */
        sumD += 1.0 - d;
    }
    double absScore = sumD / n;
    if (absScore < 0) absScore = 0;
    if (absScore > 1) absScore = 1;

    /* 单点无法做结构相关 */
    if (n < 2 || tplStd < 1e-6) return absScore;

    /* 结构相关：把 RGB 当标量序列做 ZNCC */
    double sMean = 0;
    for (int i_r73 = 0; i_r73 < n; i_r73++) {
        const unsigned char *p = pix + (size_t)pts[i_r73].dy * W * 4 + (size_t)pts[i_r73].dx * 4;
        sMean += (double)p[2] + (double)p[1] + (double)p[0];
    }
    sMean /= (3.0 * n);
    double sVar = 0, cross = 0;
    for (int i_r72 = 0; i_r72 < n; i_r72++) {
        const unsigned char *p = pix + (size_t)pts[i_r72].dy * W * 4 + (size_t)pts[i_r72].dx * 4;
        double vals[3] = { (double)p[2], (double)p[1], (double)p[0] };
        double tv[3] = { (double)pts[i_r72].r, (double)pts[i_r72].g, (double)pts[i_r72].b };
        for (int k_r71 = 0; k_r71 < 3; k_r71++) {
            double dv = vals[k_r71] - sMean;
            sVar += dv * dv;
            cross += dv * (tv[k_r71] - tplMean);
        }
    }
    sVar /= (3.0 * n);
    double sStd = sqrt(sVar);
    if (sStd < 1e-6) return absScore;
    double shape = cross / (3.0 * n) / (sStd * tplStd);
    if (shape < 0) shape = 0;
    if (shape > 1) shape = 1;
    double score = 0.5 * absScore + 0.5 * shape;
    if (score > 1) score = 1;
    return score;
}

int QmFindColorMulti(const unsigned char *bgra, int W, int H,
                     const QmSample *pts, int nPts, double sim,
                     int wantAll, int maxOut, int dir,
                     int *outX, int *outY, double *outScore)
{
    if (!bgra || nPts <= 0 || W <= 0 || H <= 0) return 0;

    /* ===== 正 / 负色点拆分 =====
       负色点：完全不参与打分，只做"全等"否决（容差 0）——
               候选位置该点的 R/G/B 与给定颜色完全相同，就判为不匹配
       正色点：参与绝对色差项 + 结构相关项 */
    QmSample *pos = (QmSample *)malloc(sizeof(QmSample) * nPts);
    QmSample *negs = (QmSample *)malloc(sizeof(QmSample) * nPts);
    if (!pos || !negs) { free(pos); free(negs); return 0; }
    int nPos = 0, nNeg = 0;
    for (int i_r70 = 0; i_r70 < nPts; i_r70++) {
        if (pts[i_r70].neg) negs[nNeg++] = pts[i_r70];
        else            pos[nPos++] = pts[i_r70];
    }
    /* 只有负色点、没有锚点，无法定位 */
    if (nPos <= 0) { free(pos); free(negs); return 0; }

    /* 模板统计（结构相关用，只取正色点） */
    double tplMean = 0;
    for (int i_r69 = 0; i_r69 < nPos; i_r69++)
        tplMean += (double)pos[i_r69].r + pos[i_r69].g + pos[i_r69].b;
    tplMean /= (3.0 * nPos);
    double tplVar = 0;
    for (int i_r68 = 0; i_r68 < nPos; i_r68++) {
        double t[3] = { (double)pos[i_r68].r, (double)pos[i_r68].g, (double)pos[i_r68].b };
        for (int k_r67 = 0; k_r67 < 3; k_r67++) { double d = t[k_r67] - tplMean; tplVar += d * d; }
    }
    tplVar /= (3.0 * nPos);
    double tplStd = sqrt(tplVar);

    /* 采样点的包围盒（用于边界与画框） */
    int minDx = 0, minDy = 0, maxDx = 0, maxDy = 0;
    for (int i_r66 = 1; i_r66 < nPts; i_r66++) {
        if (pts[i_r66].dx < minDx) minDx = pts[i_r66].dx;
        if (pts[i_r66].dy < minDy) minDy = pts[i_r66].dy;
        if (pts[i_r66].dx > maxDx) maxDx = pts[i_r66].dx;
        if (pts[i_r66].dy > maxDy) maxDy = pts[i_r66].dy;
    }
    /* 扫描边界：只要求所有采样点落在图内，不再额外留 margin
       （margin 会白吃掉几行/几列，小范围找色时经常因此找不到） */
    int x0 = -minDx, x1 = W - 1 - maxDx;
    int y0 = -minDy, y1 = H - 1 - maxDy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;

    const int maxCand = 512;
    Cand2 *cands = (Cand2 *)malloc(sizeof(Cand2) * maxCand);
    int heap[512];
    if (!cands) { free(pos); free(negs); return 0; }
    int nc = 0;
    int seq = 0;

    /* ===== 粗筛一：锚点（主色）===== */
    const unsigned char mr = pos[0].r, mg = pos[0].g, mb = pos[0].b;
    const int preTol = 80;                /* 粗筛容差（欧氏距离 ≈ 138） */
    const int preTol2 = preTol * preTol * 3;

    /* ===== 粗筛二：挑颜色离主色最远的那个正色点 =====
       同分时取偏移更小的（跳步小、cache 友好）；差异太小说明没有区分力，就不用 */
    int pre2 = -1, pre2Step = 0, bestD2 = -1;
    for (int i_r65 = 1; i_r65 < nPos; i_r65++) {
        int qr = (int)pos[i_r65].r - (int)mr;
        int qg = (int)pos[i_r65].g - (int)mg;
        int qb = (int)pos[i_r65].b - (int)mb;
        int q2 = qr * qr + qg * qg + qb * qb;
        int step = (pos[i_r65].dy < 0 ? -pos[i_r65].dy : pos[i_r65].dy) * W
                 + (pos[i_r65].dx < 0 ? -pos[i_r65].dx : pos[i_r65].dx);
        if (q2 > bestD2 || (q2 == bestD2 && step < pre2Step)) {
            bestD2 = q2; pre2Step = step; pre2 = i_r65;
        }
    }
    if (bestD2 < 400) pre2 = -1;          /* 欧氏 < 20：和主色几乎一样，筛不出东西 */

    const __int64 off2 = (pre2 > 0)
        ? ((__int64)pos[pre2].dy * W + (__int64)pos[pre2].dx) * 4 : 0;
    const unsigned char r2 = (pre2 > 0) ? pos[pre2].r : 0;
    const unsigned char g2 = (pre2 > 0) ? pos[pre2].g : 0;
    const unsigned char b2 = (pre2 > 0) ? pos[pre2].b : 0;

    /* 扫描方向：1 左上→右下 / -1 右下→左上 / 2 右上→左下 / -2 左下→右上 / 3 不按方向（按分数） */
    int scan = (dir == 3) ? 1 : dir;
    int xAsc = (scan == 1 || scan == -2);
    int yAsc = (scan == 1 || scan == 2);
    int hn = y1 - y0, wn = x1 - x0;
    for (int yi_r64 = 0; yi_r64 <= hn; yi_r64++) {
        int y = yAsc ? (y0 + yi_r64) : (y1 - yi_r64);
        const unsigned char *row = bgra + (size_t)y * W * 4;
        for (int xi_r63 = 0; xi_r63 <= wn; xi_r63++) {
            int x = xAsc ? (x0 + xi_r63) : (x1 - xi_r63);
            const unsigned char *p = row + (size_t)x * 4;
            int dr = (int)p[2] - mr, dg = (int)p[1] - mg, db = (int)p[0] - mb;
            if (dr * dr + dg * dg + db * db > preTol2) continue;

            if (pre2 > 0) {                       /* 粗筛二 */
                const unsigned char *q = p + off2;
                int er = (int)q[2] - (int)r2, eg = (int)q[1] - (int)g2, eb = (int)q[0] - (int)b2;
                if (er * er + eg * eg + eb * eb > preTol2) continue;
            }

            /* 负色门限：容差 0，全等即否决（放在两道颜色粗筛后面，少做跳步访存） */
            if (nNeg > 0) {
                int hitNeg = 0;
                for (int k_r62 = 0; k_r62 < nNeg; k_r62++) {
                    const unsigned char *q =
                        p + ((__int64)negs[k_r62].dy * W + (__int64)negs[k_r62].dx) * 4;
                    if (q[2] == negs[k_r62].r && q[1] == negs[k_r62].g && q[0] == negs[k_r62].b) { hitNeg = 1; break; }
                }
                if (hitNeg) continue;
            }

            double score = ColorScore(p, W, pos, nPos, tplMean, tplStd);
            if (score < sim) continue;

            /* 入池：始终保留分数最高的 maxCand 个（否则背景噪声会占满名额）；
               堆顶就是当前最差候选，比它低的直接丢弃 */
            const int mySeq = seq++;
            if (nc < maxCand) {
                cands[nc].x = x; cands[nc].y = y;
                cands[nc].score = score; cands[nc].seq = mySeq;
                heap[nc] = nc;
                HeapUp(heap, nc, cands);
                nc++;
            } else if (score > cands[heap[0]].score) {
                int w = heap[0];
                cands[w].x = x; cands[w].y = y;
                cands[w].score = score; cands[w].seq = mySeq;
                HeapDown(heap, nc, 0, cands);
            }
        }
    }

    flog("color: nPos=%d nNeg=%d pre2=%d nc=%d sim=%d",
         nPos, nNeg, pre2, nc, (int)(sim * 1000));
    if (nc == 0) { free(cands); free(pos); free(negs); return 0; }

    /* 候选按分数降序的索引表（堆排序，O(n log n)）：
       idx[0] 最高分；同分时 seq 小的（先扫到的）在前 */
    int idx[512];
    {
        int m = nc;
        while (m > 0) {
            int t = heap[0];
            m--;
            heap[0] = heap[m];
            idx[m] = t;             /* 最差的先落到数组尾部 */
            HeapDown(heap, m, 0, cands);
        }
    }

    /* ===== 按"同一目标"聚类（并查集），每组取最高分作代表，再按方向排序 =====
       间距 = max(12, 图案宽高的一半)；两点两轴距离都小于间距视为同一目标 */
    int sepX = (maxDx - minDx) / 2; if (sepX < 12) sepX = 12;
    int sepY = (maxDy - minDy) / 2; if (sepY < 12) sepY = 12;

    int *parent = (int *)malloc(sizeof(int) * nc);
    if (!parent) { free(cands); free(pos); free(negs); return 0; }
    for (int i_r61 = 0; i_r61 < nc; i_r61++) parent[i_r61] = i_r61;
    for (int i_r60 = 0; i_r60 < nc; i_r60++) {
        for (int j_r59 = i_r60 + 1; j_r59 < nc; j_r59++) {
            int dx = cands[i_r60].x - cands[j_r59].x; if (dx < 0) dx = -dx;
            int dy = cands[i_r60].y - cands[j_r59].y; if (dy < 0) dy = -dy;
            if (dx < sepX && dy < sepY) {
                /* 合并 */
                int a = i_r60; while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
                int b = j_r59; while (parent[b] != b) { parent[b] = parent[parent[b]]; b = parent[b]; }
                if (a != b) parent[b] = a;
            }
        }
    }

    /* 每组取分数最高的代表（idx 已按分数降序，第一次遇到即最优） */
    int rep[512];
    int nRep = 0;
    for (int i_r58 = 0; i_r58 < nc; i_r58++) {
        int r = idx[i_r58]; while (parent[r] != r) { parent[r] = parent[parent[r]]; r = parent[r]; }
        bool seen = false;
        for (int k_r57 = 0; k_r57 < nRep; k_r57++) if (rep[k_r57] == r) { seen = true; break; }
        if (!seen && nRep < 512) rep[nRep++] = r;
    }

    /* 代表点排序：
       dir=1/-1/2/-2 → 按扫描方向（谁先扫到谁在前）；
       dir=3        → 不排序，rep 本身就是"分数从高到低"，最高分排第一 */
    int order[512];
    for (int i_r56 = 0; i_r56 < nRep; i_r56++) order[i_r56] = rep[i_r56];   /* 存的是"候选下标" */
    if (dir != 3) {
        for (int i_r55 = 1; i_r55 < nRep; i_r55++) {
            int key = order[i_r55];
            int kx = cands[key].x, ky = cands[key].y;
            long kk = (long)(yAsc ? ky : 100000 - ky) * 100000L + (long)(xAsc ? kx : 100000 - kx);
            int j = i_r55 - 1;
            while (j >= 0) {
                int ox2 = cands[order[j]].x, oy2 = cands[order[j]].y;
                long kj = (long)(yAsc ? oy2 : 100000 - oy2) * 100000L + (long)(xAsc ? ox2 : 100000 - ox2);
                if (kj <= kk) break;
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = key;
        }
    }

    int cnt = 0;
    for (int i_r54 = 0; i_r54 < nRep && cnt < maxOut; i_r54++) {
        int key = order[i_r54];
        outX[cnt] = cands[key].x;
        outY[cnt] = cands[key].y;
        outScore[cnt] = cands[key].score;
        cnt++;
        if (!wantAll) break;            /* 单个结果模式：只返回方向上第一个目标 */
    }
    free(parent);    free(cands);    free(pos);    free(negs);
    return cnt;
}

/* ---------------- 扇形角度检测（v1.7） ----------------
   思路：
   1) 环带按半径分档，每档用"亮度中位数"当基线（地形/渐变不影响）
   2) 每像素的亮度超出量 > 阈值 才算命中；高饱和(彩色UI)、过亮(白色图标)直接排除
   3) 按角度分桶，统计命中强度 + 径向覆盖率（扇形横跨整个环带，局部亮块做不到）
   4) 取最宽的连续角段，用圆均值求中线角度；可选用"角段最窄"来校正圆心
   ------------------------------------------------------------ */

#define SEC_BINS   360
#define SEC_BANDS  48
#define SEC_HIST   256

struct SecWork {
    unsigned short hist[SEC_BANDS][SEC_HIST];
    unsigned int   bandTotal[SEC_BANDS];
    double weight[SEC_BINS];
    unsigned __int64 mask[SEC_BINS];
    int nBands;
    int r1, r2;
};

/* 返回角段宽度（度），并输出角度/强度/覆盖；找不到返回 0 */
static int SectorMeasure(const unsigned char *bgra, int W, int H,
                         int cx, int cy, int r1, int r2, double sens,
                         SecWork *wk, double *outAngle, double *outConc, double *outCover);

static int SectorRun(const unsigned char *bgra, int W, int H,
                     int cx, int cy, int r1, int r2, double sens,
                     double *outAngle, double *outConc, double *outCover);

int QmSectorAngle(const unsigned char *bgra, int W, int H,
                  int cx, int cy, int r1, int r2, int sens, int calib,
                  double *outAngle, double *outConc, double *outCover,
                  int *outCx, int *outCy)
{
    *outAngle = -1; *outConc = 0; *outCover = 0;
    *outCx = cx; *outCy = cy;
    if (!bgra || W <= 0 || H <= 0) return 0;
    if (r2 <= r1 + 3 || r1 < 2) return 0;

    double sv = sens;
    if (sv < 0) sv = 0;
    if (sv > 100) sv = 100;

    /* calib==2：用中心那个亮黄色箭头自动定圆心（箭头就画在旋转中心） */
    if (calib == 2) {
        double ax = 0, ay = 0, an = 0;
        int sx0 = cx - r2, sy0 = cy - r2, sx1 = cx + r2, sy1 = cy + r2;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > W - 1) sx1 = W - 1;
        if (sy1 > H - 1) sy1 = H - 1;
        for (int y_r53 = sy0; y_r53 <= sy1; y_r53++) {
            const unsigned char *row = bgra + (size_t)y_r53 * W * 4;
            for (int x_r52 = sx0; x_r52 <= sx1; x_r52++) {
                const unsigned char *q = row + (size_t)x_r52 * 4;
                int B = q[0], G = q[1], R = q[2];
                if (R < 170 || G < 130) continue;
                if (R - B < 45) continue;                 /* 亮黄：R 明显大于 B */
                ax += x_r52; ay += y_r53; an += 1;
            }
        }
        if (an > 30) {
            int nx = (int)(ax / an + 0.5), ny = (int)(ay / an + 0.5);
            /* 箭头质心离给的圆心太远就当失败，避免抓到别处的黄色 */
            int ddx = nx - cx, ddy = ny - cy;
            if (ddx * ddx + ddy * ddy < (r2 / 2) * (r2 / 2)) { cx = nx; cy = ny; }
        }
        calib = 0;                                        /* 定了圆心就不用再扫 */
    }

    double ang = 0, conc = 0, cover = 0;
    int width = SectorRun(bgra, W, H, cx, cy, r1, r2, sv, &ang, &conc, &cover);
    if (width <= 0) return 0;

    /* 交叉校验：换一个内环带再算一次，两次不一致就降权（道路/地形干扰的典型特征） */
    {
        int span = r2 - r1;
        int r1b = r1 + (int)(span * 0.28);
        int r2b = r2 - (int)(span * 0.18);
        if (r2b > r1b + 8) {
            double a2 = 0, c2 = 0, v2 = 0;
            if (SectorRun(bgra, W, H, cx, cy, r1b, r2b, sv, &a2, &c2, &v2) > 0) {
                double d = a2 - ang;
                while (d > 180) d -= 360;
                while (d < -180) d += 360;
                if (d < 0) d = -d;
                if (c2 < conc) conc = c2;
                if (d > 20.0) conc *= 0.5;          /* 两次差得多 → 可信度砍半 */
                if (d > 45.0) {                      /* 差太多 → 直接判不可信 */
                    *outAngle = -1; *outConc = conc * 0.3; *outCover = cover;
                    *outCx = cx; *outCy = cy;
                    return 0;
                }
            }
        }
    }

    int bestCx = cx, bestCy = cy;
    double bestWidth = width;

    /* 圆心校正：扇形的两条边是从圆心发出的直线，
       圆心偏了会让测得的角段变宽 —— 所以"角段最窄"就是最准的圆心 */
    if (calib) {
        double bestW2 = width, a2 = ang, c2 = conc, v2 = cover;
        for (int dy_r51 = -4; dy_r51 <= 4; dy_r51++) {
            for (int dx_r50 = -4; dx_r50 <= 4; dx_r50++) {
                if (!dx_r50 && !dy_r51) continue;
                double a3 = 0, c3 = 0, v3 = 0;
                int w3 = SectorRun(bgra, W, H, cx + dx_r50, cy + dy_r51, r1, r2, sv, &a3, &c3, &v3);
                if (w3 <= 0) continue;
                if (w3 < bestW2 - 0.5 && c3 >= c2 * 0.6) {
                    bestW2 = w3; a2 = a3; c2 = c3; v2 = v3;
                    bestCx = cx + dx_r50; bestCy = cy + dy_r51;
                }
            }
        }
        if (bestW2 < bestWidth * 0.85) {         /* 只有明显改善才采纳 */
            ang = a2; conc = c2; cover = v2; bestWidth = bestW2;
        } else {
            bestCx = cx; bestCy = cy;
        }
    }

    *outAngle = ang;
    *outConc = conc;
    *outCover = cover;
    *outCx = bestCx; *outCy = bestCy;
    return 1;
}

static int SectorRun(const unsigned char *bgra, int W, int H,
                     int cx, int cy, int r1, int r2, double sens,
                     double *outAngle, double *outConc, double *outCover)
{
    static SecWork wk;                          /* 大结构，放静态区 */
    ZeroMemory(&wk, sizeof(wk));

    int nBands = (r2 - r1) / 2;
    if (nBands < 3) nBands = 3;
    if (nBands > SEC_BANDS) nBands = SEC_BANDS;
    wk.nBands = nBands; wk.r1 = r1; wk.r2 = r2;

    int x0 = cx - r2, x1 = cx + r2, y0 = cy - r2, y1 = cy + r2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x1 <= x0 || y1 <= y0) return 0;

    int r1s = r1 * r1, r2s = r2 * r2;

    /* ---- 第 1 遍：每半径档的亮度直方图（求中位数基线） ---- */
    for (int y_r49 = y0; y_r49 <= y1; y_r49++) {
        int dy = y_r49 - cy;
        const unsigned char *row = bgra + (size_t)y_r49 * W * 4;
        for (int x_r48 = x0; x_r48 <= x1; x_r48++) {
            int dx = x_r48 - cx;
            int d2 = dx * dx + dy * dy;
            if (d2 < r1s || d2 > r2s) continue;
            const unsigned char *q = row + (size_t)x_r48 * 4;
            int B = q[0], G = q[1], R = q[2];
            int mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
            int mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
            if (mx - mn > 35) continue;                      /* 彩色 UI 排除 */
            int L = (int)(0.299 * R + 0.587 * G + 0.114 * B);
            if (L > 200) continue;                           /* 过亮的白色图标排除 */
            int band = (int)(sqrt((double)d2) - r1) / 2;
            if (band < 0) band = 0;
            if (band >= nBands) band = nBands - 1;
            wk.hist[band][L]++;
            wk.bandTotal[band]++;
        }
    }

    /* 每档中位数 */
    double med[SEC_BANDS];
    int validBands = 0;
    for (int b_r47 = 0; b_r47 < nBands; b_r47++) {
        med[b_r47] = 0;
        unsigned int total = wk.bandTotal[b_r47];
        if (total < 20) { med[b_r47] = 0; continue; }
        /* 这一圈整体太亮（被道路/建筑占据）→ 扇形在这里看不见，直接跳过 */
        {
            unsigned int acc2 = 0;
            int m2 = 0;
            for (int v_r46 = 0; v_r46 < SEC_HIST; v_r46++) {
                acc2 += wk.hist[b_r47][v_r46];
                if (acc2 * 2 >= total) { m2 = v_r46; break; }
            }
            if (m2 > 42) { med[b_r47] = -1; continue; }     /* -1 = 该档禁用 */
        }
        unsigned int acc = 0;
        for (int v_r45 = 0; v_r45 < SEC_HIST; v_r45++) {
            acc += wk.hist[b_r47][v_r45];
            if (acc * 2 >= total) { med[b_r47] = v_r45; break; }
        }
        validBands++;
    }
    if (validBands < 2) return 0;

    /* ---- 第 2 遍：按角度累计"超出基线的强度"和径向覆盖 ---- */
    double thr = 6.0 - (sens / 100.0) * 3.0;       /* 灵敏度越高阈值越低：6 → 3 */
    if (thr < 2.0) thr = 2.0;

    for (int y_r44 = y0; y_r44 <= y1; y_r44++) {
        int dy = y_r44 - cy;
        const unsigned char *row = bgra + (size_t)y_r44 * W * 4;
        for (int x_r43 = x0; x_r43 <= x1; x_r43++) {
            int dx = x_r43 - cx;
            int d2 = dx * dx + dy * dy;
            if (d2 < r1s || d2 > r2s) continue;
            const unsigned char *q = row + (size_t)x_r43 * 4;
            int B = q[0], G = q[1], R = q[2];
            int mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
            int mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
            if (mx - mn > 35) continue;
            int L = (int)(0.299 * R + 0.587 * G + 0.114 * B);
            if (L > 200) continue;

            int band = (int)(sqrt((double)d2) - r1) / 2;
            if (band < 0) band = 0;
            if (band >= nBands) band = nBands - 1;
            if (med[band] < 0) continue;                 /* 亮环带禁用 */
            double e = (double)L - med[band];
            if (e <= thr) continue;

            double a = atan2((double)dy, (double)dx);       /* -pi..pi，0=右 */
            int bin = (int)((a + 3.14159265358979) / 6.28318530717959 * SEC_BINS);
            if (bin < 0) bin = 0;
            if (bin >= SEC_BINS) bin = SEC_BINS - 1;
            wk.weight[bin] += e;
            if (band < 64) wk.mask[bin] |= (1ui64 << band);
        }
    }

    /* 平滑（环形 ±6 桶）：抑制"窄而亮"的干扰（道路、图标边缘） */
    double sm[SEC_BINS];
    for (int i_r42 = 0; i_r42 < SEC_BINS; i_r42++) {
        double s = 0;
        for (int k_r41 = -6; k_r41 <= 6; k_r41++) s += wk.weight[(i_r42 + k_r41 + SEC_BINS) % SEC_BINS];
        sm[i_r42] = s / 13.0;
    }

    /* 选角段：按"宽度 × 强度"评分，宽的才像扇形，窄的像道路 */
    double bestScore = 0, peakVal = 0;
    int lo = 0, hi = 0, found = 0;
    for (int i_r40 = 0; i_r40 < SEC_BINS; i_r40++) {
        if (sm[i_r40] < 0.5) continue;
        double cut = sm[i_r40] * 0.35;
        int l2 = i_r40, h2 = i_r40;
        for (int k_r39 = 1; k_r39 < SEC_BINS / 2; k_r39++) {
            int idx = (i_r40 - k_r39 + SEC_BINS) % SEC_BINS;
            if (sm[idx] < cut) break;
            l2 = idx;
        }
        for (int k_r38 = 1; k_r38 < SEC_BINS / 2; k_r38++) {
            int idx = (i_r40 + k_r38) % SEC_BINS;
            if (sm[idx] < cut) break;
            h2 = idx;
        }
        int sp = (h2 - l2 + SEC_BINS) % SEC_BINS + 1;
        int deg = sp * 360 / SEC_BINS;
        if (deg < 12 || deg > 200) continue;               /* 太窄不像扇形，太宽不合理 */
        double score = (double)deg * sm[i_r40];
        if (score > bestScore) { bestScore = score; lo = l2; hi = h2; peakVal = sm[i_r40]; found = 1; }
    }
    if (!found) return 0;
    int span = (hi - lo + SEC_BINS) % SEC_BINS + 1;
    if (span * 360 / SEC_BINS < 10) return 0;        /* 太窄 → 不是扇形 */

    /* 角段内的加权圆均值 */
    double sx = 0, sy = 0, sw = 0, total = 0, coverSum = 0;
    int cnt = 0;
    for (int k_r37 = 0; k_r37 < span; k_r37++) {
        int idx = (lo + k_r37) % SEC_BINS;
        double w = wk.weight[idx];
        total += wk.weight[idx];
        if (w > 0) {
            double a = ((double)idx + 0.5) / SEC_BINS * 6.28318530717959 - 3.14159265358979;
            sx += w * cos(a);
            sy += w * sin(a);
            sw += w;
            cnt++;
        }
        int bits = 0;
        unsigned __int64 m = wk.mask[idx];
        while (m) { bits += (int)(m & 1); m >>= 1; }
        coverSum += (double)bits / nBands;
    }
    if (sw < 1e-6) return 0;

    double angScreen = atan2(sy, sx);
    double compass = 90.0 + angScreen * 180.0 / 3.14159265358979;
    while (compass < 0) compass += 360.0;
    while (compass >= 360.0) compass -= 360.0;

    /* 归一化：段内总权重占比（0~1） */
    double all = 0;
    for (int i_r36 = 0; i_r36 < SEC_BINS; i_r36++) all += wk.weight[i_r36];
    *outAngle = compass;
    *outConc = (all > 1e-6) ? total / all : 0;
    *outCover = (cnt > 0) ? coverSum / cnt : 0;
    return span * 360 / SEC_BINS;
}

/* ---------------- 离线自检：扇形角度（合成图像，含干扰） ---------------- */
extern "C" __declspec(dllexport) void QmSelfTestSector(void)
{
    const int W = 260, H = 260, cx = 130, cy = 130;
    unsigned char *img = (unsigned char *)malloc((size_t)W * H * 4);
    if (!img) return;

    /* 三组真值，各自构造干扰：亮的路、白色图标、灰度地形 */
    const double truths[3] = { 123.4, 12.0, 268.5 };
    char report[900];
    int rp = 0;

    for (int t_r35 = 0; t_r35 < 3; t_r35++) {
        double truth = truths[t_r35];
        srand(1000 + t_r35);
        for (int y_r34 = 0; y_r34 < H; y_r34++) {
            for (int x_r33 = 0; x_r33 < W; x_r33++) {
                unsigned char *q = img + ((size_t)y_r34 * W + x_r33) * 4;
                int base = 22 + (rand() % 5);                  /* 很暗的地图底 */
                /* 灰度地形：横向渐变，模拟道路/地面 */
                base += (x_r33 / 40) % 2 ? 6 : 0;
                q[0] = (unsigned char)base; q[1] = (unsigned char)base; q[2] = (unsigned char)base; q[3] = 255;
                double dx = (double)(x_r33 - cx), dy = (double)(y_r34 - cy);
                double r = sqrt(dx * dx + dy * dy);
                double cmp = atan2(dy, dx) * 180.0 / 3.14159265 + 90.0;
                while (cmp < 0) cmp += 360.0;
                while (cmp >= 360.0) cmp -= 360.0;

                /* 干扰 1：一条很亮的径向"路"（在 40 度方向，宽 4 度） */
                double dRoad = cmp - 40.0;
                while (dRoad > 180) dRoad -= 360;
                while (dRoad < -180) dRoad += 360;
                if (r >= 20 && r <= 90 && fabs(dRoad) <= 2.0) {
                    q[0] = 120; q[1] = 120; q[2] = 120;
                }
                /* 干扰 2：一个白色方块图标（在 200 度，半径 55） */
                double icx = cx + sin(200.0 * 3.14159265 / 180.0) * 55.0;
                double icy = cy - cos(200.0 * 3.14159265 / 180.0) * 55.0;
                if (fabs(x_r33 - icx) < 9 && fabs(y_r34 - icy) < 9) {
                    q[0] = 235; q[1] = 235; q[2] = 235;
                }
                /* 干扰 3：中心亮黄箭头（模拟角色朝向图标，位于圆心） */
                if (r <= 8) {
                    q[0] = 120; q[1] = 215; q[2] = 245;
                }
                /* 真目标：很淡的白色扇形，±30 度，半径 25~80 */
                double diff = cmp - truth;
                while (diff > 180) diff -= 360;
                while (diff < -180) diff += 360;
                if (r >= 25 && r <= 80 && fabs(diff) <= 30.0) {
                    int v = 42 + (rand() % 6);                 /* 只比底色亮约 20 */
                    q[0] = (unsigned char)v; q[1] = (unsigned char)(v + 1); q[2] = (unsigned char)(v + 2);
                }
            }
        }

        double ang = 0, conc = 0, cover = 0;
        int ucx = cx, ucy = cy;
        int ok = QmSectorAngle(img, W, H, cx, cy, 25, 80, 50, 0, &ang, &conc, &cover, &ucx, &ucy);
        int err = ok ? (int)(fabs(ang - truth) * 10 + 0.5) : -1;

        /* 圆心偏 3 像素 + 开启校正 */
        double ang2 = 0, conc2 = 0, cover2 = 0;
        int ucx2 = cx, ucy2 = cy;
        int ok2 = QmSectorAngle(img, W, H, cx + 3, cy - 2, 25, 80, 50, 1, &ang2, &conc2, &cover2, &ucx2, &ucy2);
        int err2 = ok2 ? (int)(fabs(ang2 - truth) * 10 + 0.5) : -1;

        /* 圆心故意给偏，让 calib=2 用箭头自动找回来 */
        double ang3 = 0, conc3 = 0, cover3 = 0;
        int ucx3 = cx + 6, ucy3 = cy - 5;
        int ok3 = QmSectorAngle(img, W, H, cx + 6, cy - 5, 25, 80, 50, 2, &ang3, &conc3, &cover3, &ucx3, &ucy3);
        int err3 = ok3 ? (int)(fabs(ang3 - truth) * 10 + 0.5) : -1;
        rp += wsprintfA(report + rp, "t_r35%d: truth=%d.%d ok=%d angle=%d.%d err=%d.%d | offset ok=%d err=%d.%d | arrowCenter ok=%d err=%d.%d center=%d,%d\r\n",
                        t_r35, (int)truth, (int)((truth - (int)truth) * 10 + 0.5),
                        ok, (int)ang, (int)((ang - (int)ang) * 10 + 0.5), 0, 0,
                        ok2, err2 / 10, err2 % 10,
                        ok3, err3 / 10, err3 % 10, ucx3, ucy3);
        if (rp > 700) break;
        continue;
        rp += wsprintfA(report + rp, "t_r35%d: truth=%d.%d ok=%d angle=%d.%d err=%d.%d deg | offset+calib ok=%d angle=%d.%d err=%d.%d center=%d,%d\r\n",
                        t_r35, (int)truth, (int)((truth - (int)truth) * 10 + 0.5),
                        ok, (int)ang, (int)((ang - (int)ang) * 10 + 0.5), err / 10, err % 10,
                        ok2, (int)ang2, (int)((ang2 - (int)ang2) * 10 + 0.5), err2 / 10, err2 % 10,
                        ucx2, ucy2);
        if (rp > 700) break;
    }
    flog("%s", report);
    free(img);
}
/* ---------------- 扇形角度：径向梯度法（v1.8） ----------------
   判据：扇形是"靠圆心最亮、向外逐渐衰减"的半透明楔形，
   即沿半径方向亮度**递减**（径向斜率为负）；
   道路/建筑是平的（斜率≈0）或整体偏亮，不会呈现这种衰减。
   ------------------------------------------------------------ */

int QmSectorAngleGrad(const unsigned char *bgra, int W, int H,
                      int cx, int cy, int r1, int r2, int sens,
                      double *outAngle, double *outConc, double *outCover)
{
    const int BINS = 360;
    static double prof[BINS];
    static double slopeTab[BINS];
    *outAngle = -1; *outConc = 0; *outCover = 0;
    if (!bgra || W <= 0 || H <= 0) return 0;
    if (r2 <= r1 + 6) return 0;

    double sv = sens;
    if (sv < 0) sv = 0;
    if (sv > 100) sv = 100;
    /* 负斜率阈值：灵敏度越高要求越低 */
    double minSlope = 0.030 - (sv / 100.0) * 0.020;    /* 0.030 → 0.010 (亮度/像素) */

    for (int i_r32 = 0; i_r32 < BINS; i_r32++) slopeTab[i_r32] = 0;

    int validAngles = 0;
    for (int a_r31 = 0; a_r31 < BINS; a_r31++) {
        double rad = (double)a_r31 / BINS * 6.28318530717959;
        double dirx = cos(rad), diry = sin(rad);
        double sumR = 0, sumL = 0, sumRR = 0, sumRL = 0;
        int n = 0;
        for (int r_r30 = r1; r_r30 <= r2; r_r30 += 2) {
            int x = cx + (int)(dirx * r_r30 + 0.5);
            int y = cy + (int)(diry * r_r30 + 0.5);
            if (x < 0 || x >= W || y < 0 || y >= H) break;
            const unsigned char *q = bgra + ((size_t)y * W + x) * 4;
            int B = q[0], G = q[1], R = q[2];
            int mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
            int mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
            if (mx - mn > 35) continue;               /* 彩色 UI 排除 */
            double L = 0.299 * R + 0.587 * G + 0.114 * B;
            if (L > 200) continue;                    /* 过亮排除 */
            sumR += r_r30; sumL += L; sumRR += (double)r_r30 * r_r30; sumRL += (double)r_r30 * L;
            n++;
        }
        if (n < 8) continue;
        double den = n * sumRR - sumR * sumR;
        if (den < 1e-6) continue;
        double slope = (n * sumRL - sumR * sumL) / den;   /* 亮度随半径的斜率 */
        if (-slope > minSlope) {                          /* 越往外越暗 */
            slopeTab[a_r31] = -slope;
            validAngles++;
        }
    }
    if (validAngles < 8) return 0;

    /* 环形平滑 ±5 桶 */
    for (int i_r29 = 0; i_r29 < BINS; i_r29++) {
        double s = 0;
        for (int k_r28 = -5; k_r28 <= 5; k_r28++) s += slopeTab[(i_r29 + k_r28 + BINS) % BINS];
        prof[i_r29] = s / 11.0;
    }

    /* 按"宽度 × 强度"选角段（扇形宽、道路窄） */
    double bestScore = 0;
    int lo = 0, hi = 0; double peakVal = 0;
    for (int i_r27 = 0; i_r27 < BINS; i_r27++) {
        if (prof[i_r27] < minSlope * 0.5) continue;
        double cut = prof[i_r27] * 0.35;
        int l2 = i_r27, h2 = i_r27;
        for (int k_r26 = 1; k_r26 < BINS / 2; k_r26++) {
            int idx = (i_r27 - k_r26 + BINS) % BINS;
            if (prof[idx] < cut) break;
            l2 = idx;
        }
        for (int k_r25 = 1; k_r25 < BINS / 2; k_r25++) {
            int idx = (i_r27 + k_r25) % BINS;
            if (prof[idx] < cut) break;
            h2 = idx;
        }
        int span = (h2 - l2 + BINS) % BINS + 1;
        int deg = span * 360 / BINS;
        if (deg < 20 || deg > 220) continue;
        double score = (double)deg * prof[i_r27];
        if (score > bestScore) { bestScore = score; lo = l2; hi = h2; peakVal = prof[i_r27]; }
    }
    if (bestScore <= 0) return 0;

    int span = (hi - lo + BINS) % BINS + 1;
    double sx = 0, sy = 0, sw = 0;
    for (int k_r24 = 0; k_r24 < span; k_r24++) {
        int idx = (lo + k_r24) % BINS;
        double w = slopeTab[idx];
        if (w <= 0) continue;
        double rad = ((double)idx + 0.5) / BINS * 6.28318530717959;
        sx += w * cos(rad);
        sy += w * sin(rad);
        sw += w;
    }
    if (sw < 1e-6) return 0;

    double angScreen = atan2(sy, sx);
    double compass = 90.0 + angScreen * 180.0 / 3.14159265358979;
    while (compass < 0) compass += 360.0;
    while (compass >= 360.0) compass -= 360.0;

    *outAngle = compass;
    *outConc = peakVal;                                   /* 峰强度，越大越像扇形 */
    *outCover = (double)span * 360.0 / BINS / 360.0;      /* 角段宽度占比 */
    return 1;
}

/* ---------------- 箭头朝向（v1.9）：矢量法，相对精度可达 0.1° ----------------
   1) 中心附近找亮黄像素 → 8 邻域最大连通域
   2) 域内找最远点对 A、B（箭头长轴两端）
   3) 按 AB 直线把像素分成两侧，"点少的一侧"是尖头
   4) 方向 = 尖头 - 尾端；再用"质心→尖头"做加权融合提高稳定性
   5) 减去用户给的基准角 = 校准后的绝对朝向
   ------------------------------------------------------------ */

int QmArrowAngle(const unsigned char *bgra, int W, int H,
                 int cx, int cy, int radius, double baseAngle,
                 double *outAngle, int *outPixels, double *outLength)
{
    *outAngle = -1; *outPixels = 0; *outLength = 0;
    if (!bgra || W <= 0 || H <= 0 || radius < 8) return 0;

    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
    if (rw < 10 || rh < 10) return 0;
    if ((size_t)rw * rh > 400000) return 0;

    /* 亮黄掩码 */
    unsigned char *mask = (unsigned char *)malloc((size_t)rw * rh);
    if (!mask) return 0;
    memset(mask, 0, (size_t)rw * rh);
    int total = 0;
    double wpx = 0, wpy = 0;
    int wn = 0;
    int coreR = 0, coreG = 0, coreB = 0;
    for (int y_r23 = y0; y_r23 <= y1; y_r23++) {
        const unsigned char *row = bgra + (size_t)y_r23 * W * 4;
        for (int x_r22 = x0; x_r22 <= x1; x_r22++) {
            const unsigned char *q = row + (size_t)x_r22 * 4;
            int B = q[0], G = q[1], R = q[2];
            /* 白色高光（箭头左半边），实测色 (244,244,244) */
            if (R > 229 && G > 229 && B > 225) {
                int mxw = R > G ? (R > B ? R : B) : (G > B ? G : B);
                int mnw = R < G ? (R < B ? R : B) : (G < B ? G : B);
                if (mxw - mnw <= 14) { wpx += x_r22; wpy += y_r23; wn++; }
                continue;
            }
            /* 主体：只认实测的 3 种核心色（边缘与背景的混合色会被排除） */
            {
                int d1 = (R-255)*(R-255) + (G-231)*(G-231) + (B-149)*(B-149);
                int d2 = (R-255)*(R-255) + (G-239)*(G-239) + (B-184)*(B-184);
                int d3 = (R-255)*(R-255) + (G-222)*(G-222) + (B-114)*(B-114);
                int dm = d1; if (d2 < dm) dm = d2; if (d3 < dm) dm = d3;
                if (dm > 576) continue;              /* 24^2 */
                mask[(size_t)(y_r23 - y0) * rw + (x_r22 - x0)] = 1;
                total++;
            }
        }
    }
    if (total < 30) { free(mask); return 0; }

    /* 最大连通域（BFS，队列用坐标数组） */
    int *queue = (int *)malloc(sizeof(int) * (size_t)rw * rh);
    int *label = (int *)calloc((size_t)rw * rh, sizeof(int));
    if (!queue || !label) { free(mask); free(queue); free(label); return 0; }

    int curLabel = 0, bestLabel = 0, bestSize = 0;
    int secondLabel = 0, secondSize = 0;
    for (int i_r21 = 0; i_r21 < rw * rh; i_r21++) {
        if (!mask[i_r21] || label[i_r21]) continue;
        curLabel++;
        int head = 0, tail = 0, size = 0;
        queue[tail++] = i_r21;
        label[i_r21] = curLabel;
        while (head < tail) {
            int cur = queue[head++];
            int yy = cur / rw, xx = cur % rw;
            size++;
            for (int dy_r20 = -1; dy_r20 <= 1; dy_r20++) {
                for (int dx_r19 = -1; dx_r19 <= 1; dx_r19++) {
                    int ny = yy + dy_r20, nx = xx + dx_r19;
                    if (ny < 0 || ny >= rh || nx < 0 || nx >= rw) continue;
                    int ni = ny * rw + nx;
                    if (!mask[ni] || label[ni]) continue;
                    label[ni] = curLabel;
                    queue[tail++] = ni;
                }
            }
        }
        if (size > bestSize) { secondSize = bestSize; secondLabel = bestLabel; bestSize = size; bestLabel = curLabel; }
        else if (size > secondSize) { secondSize = size; secondLabel = curLabel; }
    }
    free(queue);
    if (bestSize < 20) { free(mask); free(label); return 0; }

    /* 收集最大域的点 */
    int *px2 = (int *)malloc(sizeof(int) * bestSize);
    int *py2 = (int *)malloc(sizeof(int) * bestSize);
    if (!px2 || !py2) { free(mask); free(label); free(px2); free(py2); return 0; }
    int ns = 0;
    for (int i_r18 = 0; i_r18 < rw * rh; i_r18++) {
        if (label[i_r18] == bestLabel) {
            px2[ns] = x0 + (i_r18 % rw);
            py2[ns] = y0 + (i_r18 / rw);
            ns++;
        }
    }
    free(label); free(mask);
    if (ns < 20) { free(px2); free(py2); return 0; }

    /* ===== 主体?尾巴 双连通域法（在 8 方向实拍图上验证：间隔 42.9/47.1，平均 45.0） ===== */
    double ux = 0, uy = 0;
    int ok6 = 0;

    if (secondSize >= 10 && secondSize * 8 >= bestSize) {   /* 第二块必须够大（尾巴约 34px） */
        double t2x = 0, t2y = 0; int t2n = 0;
        for (int i_r17 = 0; i_r17 < rw * rh; i_r17++) {
            if (label[i_r17] != secondLabel) continue;
            t2x += x0 + (i_r17 % rw);
            t2y += y0 + (i_r17 / rw);
            t2n++;
        }
        if (t2n >= 8) {
            double bx = 0, by = 0;
            for (int i_r16 = 0; i_r16 < ns; i_r16++) { bx += px2[i_r16]; by += py2[i_r16]; }
            bx /= ns; by /= ns;
            double dx = bx - t2x / t2n;
            double dy = by - t2y / t2n;
            double ll = sqrt(dx * dx + dy * dy);
            if (ll > 1.5) { ux = dx / ll; uy = dy / ll; ok6 = 1; }
        }
    }

    if (!ok6) {
        /* 兜底：矢量平均（与圆心相关，靠基准角吸收偏置） */
        double sumVX = 0, sumVY = 0;
        for (int i_r15 = 0; i_r15 < ns; i_r15++) {
            double dx = (double)px2[i_r15] - cx;
            double dy = (double)py2[i_r15] - cy;
            double len = sqrt(dx * dx + dy * dy);
            if (len < 0.5) continue;
            sumVX += dx / len;
            sumVY += dy / len;
        }
        if (sumVX * sumVX + sumVY * sumVY < 1e-9) { free(px2); free(py2); return 0; }
        double l2 = sqrt(sumVX * sumVX + sumVY * sumVY);
        ux = sumVX / l2; uy = sumVY / l2;
    }

    double ang = atan2(-uy, ux) * 180.0 / 3.14159265358979;
    if (ang < 0) ang += 360.0;
    double compass = 90.0 - ang;
    while (compass < 0) compass += 360.0;
    while (compass >= 360.0) compass -= 360.0;
    /* 减基准角 */
    double rel = compass - baseAngle;
    while (rel < 0) rel += 360.0;
    while (rel >= 360.0) rel -= 360.0;

    *outAngle = rel;
    *outPixels = ns;
    {
        int bx0 = 999999, by0 = 999999, bx1 = -999999, by1 = -999999;
        for (int i_r14 = 0; i_r14 < ns; i_r14++) {
            if (px2[i_r14] < bx0) bx0 = px2[i_r14];
            if (px2[i_r14] > bx1) bx1 = px2[i_r14];
            if (py2[i_r14] < by0) by0 = py2[i_r14];
            if (py2[i_r14] > by1) by1 = py2[i_r14];
        }
        double bw = bx1 - bx0, bh = by1 - by0;
        *outLength = sqrt(bw * bw + bh * bh);
    }
    free(px2); free(py2);
    return 1;
}

/* ---------------- 箭头朝向 v3：旋转模板匹配 ----------------
   1) 第一次调用：用"核心色"掩码把当前箭头存成模板（25x25，只含箭头像素）
   2) 之后每次调用：找出箭头质心，把模板按 0~359° 旋转并与之做 ZNCC
   3) 取相似度最高的角度，抛物线插值到 0.1°
   优点：不依赖质心/主轴/高光；背景变化被掩码挡住，边缘混合色不参与
   ------------------------------------------------------------ */

#define ARR_TPL   25
static unsigned char g_arrTplMask[ARR_TPL * ARR_TPL];
static float         g_arrTplVal[ARR_TPL * ARR_TPL];
static int           g_arrTplN = 0;          /* 模板有效像素数 */
static int           g_arrTplReady = 0;

/* 箭头核心色判断（实测调色板） */
static int ArrowCorePixel(int R, int G, int B)
{
    int d1 = (R-255)*(R-255) + (G-231)*(G-231) + (B-149)*(B-149);
    int d2 = (R-255)*(R-255) + (G-239)*(G-239) + (B-184)*(B-184);
    int d3 = (R-255)*(R-255) + (G-222)*(G-222) + (B-114)*(B-114);
    int dm = d1; if (d2 < dm) dm = d2; if (d3 < dm) dm = d3;
    return (dm <= 900);                       /* 30^2 */
}

/* 在区域里找箭头质心（核心色像素的平均位置） */
static int ArrowCentroid(const unsigned char *bgra, int W, int H,
                         int cx, int cy, int radius, double *ox, double *oy, int *cnt)
{
    double sx = 0, sy = 0;
    int n = 0;
    int x0 = cx - radius, x1 = cx + radius, y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    for (int y_r13 = y0; y_r13 <= y1; y_r13++) {
        const unsigned char *row = bgra + (size_t)y_r13 * W * 4;
        for (int x_r12 = x0; x_r12 <= x1; x_r12++) {
            const unsigned char *q = row + (size_t)x_r12 * 4;
            if (!ArrowCorePixel(q[2], q[1], q[0])) continue;
            sx += x_r12; sy += y_r13; n++;
        }
    }
    if (n < 25) return 0;
    *ox = sx / n; *oy = sy / n; *cnt = n;
    return 1;
}

/* 双线性取灰度 */
static double SampleGray(const unsigned char *bgra, int W, int H, double x, double y)
{
    int x0 = (int)floor(x), y0 = (int)floor(y);
    double fx = x - x0, fy = y - y0;
    double acc = 0, wsum = 0;
    for (int dy_r11 = 0; dy_r11 <= 1; dy_r11++) {
        for (int dx_r10 = 0; dx_r10 <= 1; dx_r10++) {
            int xx = x0 + dx_r10, yy = y0 + dy_r11;
            if (xx < 0 || xx >= W || yy < 0 || yy >= H) continue;
            const unsigned char *q = bgra + ((size_t)yy * W + xx) * 4;
            double L = 0.299 * q[2] + 0.587 * q[1] + 0.114 * q[0];
            double w = (dx_r10 ? fx : 1.0 - fx) * (dy_r11 ? fy : 1.0 - fy);
            acc += L * w; wsum += w;
        }
    }
    return (wsum > 1e-6) ? acc / wsum : 0.0;
}

int QmArrowAngle2(const unsigned char *bgra, int W, int H,
                  int cx, int cy, int radius, double baseAngle,
                  double *outAngle, int *outPixels, double *outLength)
{
    *outAngle = -1; *outPixels = 0; *outLength = 0;
    if (!bgra || radius < 8) return 0;

    double mcx = 0, mcy = 0;
    int npx = 0;
    if (!ArrowCentroid(bgra, W, H, cx, cy, radius, &mcx, &mcy, &npx)) return 0;
    *outPixels = npx;

    const int half = ARR_TPL / 2;

    /* ---- 第一次：捕获模板 ---- */
    if (!g_arrTplReady) {
        int cnt = 0;
        for (int j_r9 = 0; j_r9 < ARR_TPL; j_r9++) {
            for (int i_r8 = 0; i_r8 < ARR_TPL; i_r8++) {
                g_arrTplMask[j_r9 * ARR_TPL + i_r8] = 0;
                g_arrTplVal[j_r9 * ARR_TPL + i_r8] = 0;
            }
        }
        for (int j_r7 = 0; j_r7 < ARR_TPL; j_r7++) {
            for (int i_r6 = 0; i_r6 < ARR_TPL; i_r6++) {
                int x = (int)(mcx + 0.5) - half + i_r6;
                int y = (int)(mcy + 0.5) - half + j_r7;
                if (x < 0 || x >= W || y < 0 || y >= H) continue;
                const unsigned char *q = bgra + ((size_t)y * W + x) * 4;
                if (!ArrowCorePixel(q[2], q[1], q[0])) continue;
                g_arrTplMask[j_r7 * ARR_TPL + i_r6] = 1;
                g_arrTplVal[j_r7 * ARR_TPL + i_r6] = (float)(0.299 * q[2] + 0.587 * q[1] + 0.114 * q[0]);
                cnt++;
            }
        }
        if (cnt < 40) return 0;                 /* 模板不够，等下次 */
        g_arrTplN = cnt;
        g_arrTplReady = 1;
        /* 模板就是"当前朝向"，返回 0，由基准角决定零点 */
        *outAngle = 0.0;
        *outLength = sqrt((double)ARR_TPL * ARR_TPL);
        return 1;
    }

    /* 模板均值/方差（固定） */
    double tmean = 0;
    int tn = 0;
    for (int k_r5 = 0; k_r5 < ARR_TPL * ARR_TPL; k_r5++) {
        if (!g_arrTplMask[k_r5]) continue;
        tmean += g_arrTplVal[k_r5]; tn++;
    }
    if (tn < 40) { g_arrTplReady = 0; return 0; }
    tmean /= tn;
    double tvar = 0;
    for (int k_r4 = 0; k_r4 < ARR_TPL * ARR_TPL; k_r4++) {
        if (!g_arrTplMask[k_r4]) continue;
        double d = g_arrTplVal[k_r4] - tmean;
        tvar += d * d;
    }
    tvar /= tn;
    double tstd = sqrt(tvar);
    if (tstd < 1e-3) return 0;

    /* ---- 旋转搜索 0~359（1° 步进） ---- */
    double bestScore = -2, bestAng = 0;
    double scores[360];
    for (int a_r3 = 0; a_r3 < 360; a_r3++) {
        double rad = a_r3 * 3.14159265358979 / 180.0;
        double ca = cos(rad), sa = sin(rad);
        double sumS = 0, sumSS = 0, cross = 0;
        int n = 0;
        for (int j_r2 = 0; j_r2 < ARR_TPL; j_r2++) {
            for (int i_r1 = 0; i_r1 < ARR_TPL; i_r1++) {
                if (!g_arrTplMask[j_r2 * ARR_TPL + i_r1]) continue;
                double dx = i_r1 - half, dy = j_r2 - half;
                double rx = dx * ca - dy * sa;
                double ry = dx * sa + dy * ca;
                double L = SampleGray(bgra, W, H, mcx + rx, mcy + ry);
                double d = g_arrTplVal[j_r2 * ARR_TPL + i_r1] - tmean;
                sumS += L; sumSS += L * L; cross += d * L;
                n++;
            }
        }
        if (n < 40) { scores[a_r3] = -2; continue; }
        double sMean = sumS / n;
        double sVar = sumSS / n - sMean * sMean;
        if (sVar < 1e-6) { scores[a_r3] = -2; continue; }
        double sStd = sqrt(sVar);
        double score = cross / n / (sStd * tstd);
        scores[a_r3] = score;
        if (score > bestScore) { bestScore = score; bestAng = a_r3; }
    }
    if (bestScore < -1.5) return 0;

    /* 抛物线插值（亚度） */
    int b = (int)bestAng;
    double s0 = scores[(b + 359) % 360], s1 = scores[b], s2 = scores[(b + 1) % 360];
    double ref = 0;
    double denom = (s0 - 2 * s1 + s2);
    if (fabs(denom) > 1e-9) ref = 0.5 * (s0 - s2) / denom;
    if (ref > 1) ref = 1;
    if (ref < -1) ref = -1;
    double ang = bestAng + ref;

    /* 角度定义：0=正上、顺时针。模板初始朝向记为 0，搜索角 a 表示"模板逆时针转 a 后匹配"
       → 目标相对模板顺时针转了 a 度，故朝向角 = a */
    double compass = ang;
    while (compass < 0) compass += 360.0;
    while (compass >= 360.0) compass -= 360.0;

    double rel = compass - baseAngle;
    while (rel < 0) rel += 360.0;
    while (rel >= 360.0) rel -= 360.0;

    *outAngle = rel;
    *outLength = bestScore;
    return 1;
}

extern "C" __declspec(dllexport) void QmArrowResetTemplate(void)
{
    g_arrTplReady = 0;
    g_arrTplN = 0;
}
