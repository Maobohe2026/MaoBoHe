/* ============================================================
   vc6compat.h —— 让 mingw 风格的引擎源码能在 VC6 (cl 12.00) 下编译
   用法：在每个 .cpp 的最前面 #include "vc6compat.h"
   （它自己会 include <windows.h>，后面再 include 一次是安全的）
   ============================================================ */
#ifndef VC6COMPAT_H
#define VC6COMPAT_H

#ifndef WINVER
#define WINVER 0x0501        /* 让 VC6 的 windows.h 打开 2000/XP 的声明 */
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32_DCOM
#define _WIN32_DCOM      /* 让 VC6 的 objbase.h 声明 CoInitializeEx */
#endif
#include <objbase.h>

/* --- VC6 没有 DWORD_PTR / ULONG_PTR --- */
#ifndef DWORD_PTR
typedef unsigned long DWORD_PTR_vc6;
#define DWORD_PTR DWORD_PTR_vc6
#endif

/* --- 64 位注册表视图标志 --- */
#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif
#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif

/* --- 分层窗口常量：VC6 的 windows.h 有时不给（取决于 WINVER 宏生效顺序） --- */
#ifndef LWA_COLORKEY
#define LWA_COLORKEY 0x00000001
#endif
#ifndef LWA_ALPHA
#define LWA_ALPHA 0x00000002
#endif
#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED 0x00080000
#endif
#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000
#endif
#ifndef ULW_COLORKEY
#define ULW_COLORKEY 0x00000001
#endif

/* --- RegDeleteTreeW（Vista+），VC6 头里没有，动态取 --- */
static LONG Vc6_RegDeleteTreeW(HKEY root, LPCWSTR sub)
{
    typedef LONG (WINAPI *PFN_RDT)(HKEY, LPCWSTR);
    static PFN_RDT s_pfn = NULL;
    static int s_tried = 0;
    if (!s_tried) {
        HMODULE a = GetModuleHandleA("advapi32.dll");
        if (a) s_pfn = (PFN_RDT)GetProcAddress(a, "RegDeleteTreeW");
        s_tried = 1;
    }
    return s_pfn ? s_pfn(root, sub) : ERROR_CALL_NOT_IMPLEMENTED;
}
#define RegDeleteTreeW Vc6_RegDeleteTreeW
/* --- VC6 头文件里缺的常量 --- */
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif

#if defined(_MSC_VER) && (_MSC_VER <= 1200)

/* --- 虚拟屏度量（VC6 的 windows.h 里没有） --- */
#ifndef SM_XVIRTUALSCREEN
#define SM_XVIRTUALSCREEN  76
#define SM_YVIRTUALSCREEN  77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#endif

/* --- SetLayeredWindowAttributes：VC6 头里可能没有声明 --- */
static BOOL Vc6_SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)
{
    typedef BOOL (WINAPI *PFN_SLWA)(HWND, COLORREF, BYTE, DWORD);
    static PFN_SLWA s_pfn = NULL;
    static int s_tried = 0;
    if (!s_tried) {
        HMODULE u = GetModuleHandleA("user32.dll");
        if (u) s_pfn = (PFN_SLWA)GetProcAddress(u, "SetLayeredWindowAttributes");
        s_tried = 1;
    }
    return s_pfn ? s_pfn(hwnd, key, alpha, flags) : FALSE;
}
#define SetLayeredWindowAttributes Vc6_SetLayeredWindowAttributes
/* --- SetWindowDisplayAffinity（Vista+），VC6 头里没有，动态取 --- */
static BOOL Vc6_SetWindowDisplayAffinity(HWND hwnd, DWORD aff)
{
    typedef BOOL (WINAPI *PFN_SWDA)(HWND, DWORD);
    static PFN_SWDA s_pfn = NULL;
    static int s_tried = 0;
    if (!s_tried) {
        HMODULE u = GetModuleHandleA("user32.dll");
        if (u) s_pfn = (PFN_SWDA)GetProcAddress(u, "SetWindowDisplayAffinity");
        s_tried = 1;
    }
    return s_pfn ? s_pfn(hwnd, aff) : FALSE;
}
#define SetWindowDisplayAffinity Vc6_SetWindowDisplayAffinity
/* --- GetModuleHandleExA：XP 之后才有，VC6 头里没有，动态取 --- */
static BOOL Vc6_GetModuleHandleExA(DWORD flags, LPCSTR addr, HMODULE *out)
{
    typedef BOOL (WINAPI *PFN_GMHE)(DWORD, LPCSTR, HMODULE *);
    static PFN_GMHE s_pfn = NULL;
    static int s_tried = 0;
    if (!s_tried) {
        HMODULE k = GetModuleHandleA("kernel32.dll");
        if (k) s_pfn = (PFN_GMHE)GetProcAddress(k, "GetModuleHandleExA");
        s_tried = 1;
    }
    if (s_pfn) return s_pfn(flags, addr, out);
    {   /* 退路：用 VirtualQuery 反查所属模块 */
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
            *out = (HMODULE)mbi.AllocationBase;
            return TRUE;
        }
    }
    return FALSE;
}
#define GetModuleHandleExA Vc6_GetModuleHandleExA

/* --- Interlocked 系列：VC6 的原型不接受 volatile long*，也没有 InterlockedOr --- */
static LONG Vc6_InterlockedExchangeL(volatile LONG *t, LONG v)
{
    return InterlockedExchange((long *)t, v);
}
static LONG Vc6_InterlockedCompareExchangeL(volatile LONG *d, LONG x, LONG c)
{
    return (LONG)InterlockedCompareExchange((PVOID *)d, (PVOID)x, (PVOID)c);
}
static LONG Vc6_InterlockedOrL(volatile LONG *v, LONG m)
{
    for (;;) {
        LONG o = *v;
        LONG n = o | m;
        if (Vc6_InterlockedCompareExchangeL(v, n, o) == o) return o;
    }
}
#define InterlockedExchange Vc6_InterlockedExchangeL
#define InterlockedCompareExchange Vc6_InterlockedCompareExchangeL
#define InterlockedOr Vc6_InterlockedOrL
/* --- VC6 没有 va_copy / snprintf 的现代行为，统一给个别名 --- */
#ifndef _vsnprintf_s
#define _vsnprintf_s _vsnprintf
#endif

/* --- 64 位整型的格式串：mingw 用 %llu / %lld，MSVC 用 %I64u / %I64d --- */
#define VC6_QD "%I64d"
#define VC6_QU "%I64u"

#endif /* _MSC_VER <= 1200 */

#endif /* VC6COMPAT_H */
