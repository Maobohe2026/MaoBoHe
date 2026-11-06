// ============================================================
//  zaoyo_shell.cpp —— 单一 DLL 版壳：直接调用内嵌引擎（不再释放 DLL）
//  引擎源码（finder/features/sound/audiocap/soundhost）编译进同一个 DLL，
//  通过 finder.cpp 导出的 ZaoyoInvokeDirect(dispid, nArgs, args, result) 调用。
//  NOTE: keep this file ASCII-only; VC6 reads it as ANSI.
// ============================================================
#include "stdafx.h"
#include "CQMPlugin.h"
#include <oleauto.h>

/* 引擎入口（finder.cpp 导出） */
extern "C" HRESULT ZaoyoInvokeDirect(int dispid, int nArgs, VARIANT *argsIn, VARIANT *result);
extern "C" void ZaoyoSetInstance(void *hInstance);

#define ZH_PIN_SELF() do { static bool _zh_pinned = false; if (!_zh_pinned) { _zh_pinned = true; InternalAddRef(); } } while (0)

static BSTR ToBstr(LPCTSTR s)
{
    if (!s) s = "";
    int n = lstrlen(s);
    int wn = MultiByteToWideChar(CP_ACP, 0, s, n, NULL, 0);
    BSTR b = SysAllocStringLen(NULL, wn);
    if (b) MultiByteToWideChar(CP_ACP, 0, s, n, b, wn);
    return b;
}

/* 首次使用时把本模块句柄告诉引擎（音效要用） */
static void EnsureEngine(void)
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    ZaoyoSetInstance((void *)AfxGetInstanceHandle());
}
/* 把声明顺序的参数直接交给引擎；参数不足时补 VT_EMPTY */
static BSTR CallEngine(int dispid, VARIANT *args, int nArgs)
{
    VARIANT in[16];
    VARIANT out;
    HRESULT hr;
    EnsureEngine();
    int i;
    if (nArgs > 16) nArgs = 16;
    for (i = 0; i < nArgs; i++) in[i] = args[i];
    VariantInit(&out);
    hr = ZaoyoInvokeDirect(dispid, nArgs, in, &out);
    if (SUCCEEDED(hr) && out.vt == VT_BSTR && out.bstrVal) {
        BSTR b = SysAllocString(out.bstrVal);
        VariantClear(&out);
        return b;
    }
    VariantClear(&out);
    return SysAllocStringLen(NULL, 0);
}

static VARIANT VLong(long v)   { VARIANT x; VariantInit(&x); x.vt = VT_I4; x.lVal = v; return x; }
static VARIANT VBstr(LPCTSTR s) { VARIANT x; VariantInit(&x); x.vt = VT_BSTR; x.bstrVal = ToBstr(s); return x; }
static VARIANT VDouble(double v) { VARIANT x; VariantInit(&x); x.vt = VT_R8; x.dblVal = v; return x; }

static void FreeArgs(VARIANT *a, int n)
{
    int i;
    for (i = 0; i < n; i++) VariantClear(&a[i]);
}

// ---- 10 Version ----
// Forward to the engine so Version() always reports the engine we actually run.
// (It used to be a hard-coded "2.8.0" in CQMPlugin.cpp, which made every new
//  build look uninstalled during troubleshooting.)
BSTR CQMPlugin::Version()
{
    ZH_PIN_SELF();
    return CallEngine(10, NULL, 0);
}

// ---- 1 Find ----
BSTR CQMPlugin::Find(long lLeft, long lTop, long lRight, long lBottom, LPCTSTR sImg,
                     double dSim, long lDraw, LPCTSTR sColor, long lDirection)
{
	ZH_PIN_SELF();
    VARIANT a[9];
    a[0] = VLong(lLeft); a[1] = VLong(lTop); a[2] = VLong(lRight); a[3] = VLong(lBottom);
    a[4] = VBstr(sImg); a[5] = VDouble(dSim); a[6] = VLong(lDraw); a[7] = VBstr(sColor); a[8] = VLong(lDirection);
    BSTR r = CallEngine(1, a, 9);
    FreeArgs(a, 9);
    return r;
}

// ---- 2 FindAll ----
BSTR CQMPlugin::FindAll(long lLeft, long lTop, long lRight, long lBottom, LPCTSTR sList,
                        double dSim, long lDraw, LPCTSTR sColor, long lDirection)
{
	ZH_PIN_SELF();
    VARIANT a[9];
    a[0] = VLong(lLeft); a[1] = VLong(lTop); a[2] = VLong(lRight); a[3] = VLong(lBottom);
    a[4] = VBstr(sList); a[5] = VDouble(dSim); a[6] = VLong(lDraw); a[7] = VBstr(sColor); a[8] = VLong(lDirection);
    BSTR r = CallEngine(2, a, 9);
    FreeArgs(a, 9);
    return r;
}

// ---- 3 FindAllPos ----
BSTR CQMPlugin::FindAllPos(long lLeft, long lTop, long lRight, long lBottom, LPCTSTR sImg,
                           double dSim, long lDraw, LPCTSTR sColor, long lDirection)
{
	ZH_PIN_SELF();
    VARIANT a[9];
    a[0] = VLong(lLeft); a[1] = VLong(lTop); a[2] = VLong(lRight); a[3] = VLong(lBottom);
    a[4] = VBstr(sImg); a[5] = VDouble(dSim); a[6] = VLong(lDraw); a[7] = VBstr(sColor); a[8] = VLong(lDirection);
    BSTR r = CallEngine(3, a, 9);
    FreeArgs(a, 9);
    return r;
}

// ---- 4 FindColorMulti ----
BSTR CQMPlugin::FindColorMulti(long lLeft, long lTop, long lRight, long lBottom, LPCTSTR sMain,
                               LPCTSTR sPoints, double dSim, long lDraw, LPCTSTR sColor, long lDirection)
{
	ZH_PIN_SELF();
    VARIANT a[10];
    a[0] = VLong(lLeft); a[1] = VLong(lTop); a[2] = VLong(lRight); a[3] = VLong(lBottom);
    a[4] = VBstr(sMain); a[5] = VBstr(sPoints); a[6] = VDouble(dSim);
    a[7] = VLong(lDraw); a[8] = VBstr(sColor); a[9] = VLong(lDirection);
    BSTR r = CallEngine(4, a, 10);
    FreeArgs(a, 10);
    return r;
}

// ---- 5 FindColorMultiPos ----
BSTR CQMPlugin::FindColorMultiPos(long lLeft, long lTop, long lRight, long lBottom, LPCTSTR sMain,
                                  LPCTSTR sPoints, double dSim, long lDraw, LPCTSTR sColor, long lDirection)
{
	ZH_PIN_SELF();
    VARIANT a[10];
    a[0] = VLong(lLeft); a[1] = VLong(lTop); a[2] = VLong(lRight); a[3] = VLong(lBottom);
    a[4] = VBstr(sMain); a[5] = VBstr(sPoints); a[6] = VDouble(dSim);
    a[7] = VLong(lDraw); a[8] = VBstr(sColor); a[9] = VLong(lDirection);
    BSTR r = CallEngine(5, a, 10);
    FreeArgs(a, 10);
    return r;
}

// ---- 6 GetSectorAngle ----
BSTR CQMPlugin::GetSectorAngle(long lCenterX, long lCenterY, long lRadiusIn, long lRadiusOut,
                               long lSensitivity, long lCalibrate, long lDraw, LPCTSTR sColor, long lMethod)
{
	ZH_PIN_SELF();
    VARIANT a[9];
    a[0] = VLong(lCenterX); a[1] = VLong(lCenterY); a[2] = VLong(lRadiusIn); a[3] = VLong(lRadiusOut);
    a[4] = VLong(lSensitivity); a[5] = VLong(lCalibrate); a[6] = VLong(lDraw);
    a[7] = VBstr(sColor); a[8] = VLong(lMethod);
    BSTR r = CallEngine(6, a, 9);
    FreeArgs(a, 9);
    return r;
}

// ---- 7 GetArrowAngle ----
BSTR CQMPlugin::GetArrowAngle(long lCenterX, long lCenterY, long lRadius,
                              double dBaseAngle, long lDraw, LPCTSTR sColor)
{
	ZH_PIN_SELF();
    VARIANT a[6];
    a[0] = VLong(lCenterX); a[1] = VLong(lCenterY); a[2] = VLong(lRadius);
    a[3] = VDouble(dBaseAngle); a[4] = VLong(lDraw); a[5] = VBstr(sColor);
    BSTR r = CallEngine(7, a, 6);
    FreeArgs(a, 6);
    return r;
}

// ---- 8 MoveMouseR ----
BSTR CQMPlugin::MoveMouseR(long lDx, long lDy, long lSteps, long lDelay, long lMethod)
{
	ZH_PIN_SELF();
    VARIANT a[5];
    a[0] = VLong(lDx); a[1] = VLong(lDy); a[2] = VLong(lSteps); a[3] = VLong(lDelay); a[4] = VLong(lMethod);
    BSTR r = CallEngine(8, a, 5);
    FreeArgs(a, 5);
    return r;
}

// ---- 9 MoveMouseP ----
BSTR CQMPlugin::MoveMouseP(long lDx, long lDy, long lSteps, long lDelay, long lMethod)
{
	ZH_PIN_SELF();
    VARIANT a[5];
    a[0] = VLong(lDx); a[1] = VLong(lDy); a[2] = VLong(lSteps); a[3] = VLong(lDelay); a[4] = VLong(lMethod);
    BSTR r = CallEngine(9, a, 5);
    FreeArgs(a, 5);
    return r;
}

// ---- 11 SoundStart ----
BSTR CQMPlugin::SoundStart()
{
	ZH_PIN_SELF();
    return CallEngine(11, NULL, 0);
}

// ---- 12 SoundStop ----
BSTR CQMPlugin::SoundStop()
{
	ZH_PIN_SELF();
    return CallEngine(12, NULL, 0);
}

// ---- 13 SoundWait ----
BSTR CQMPlugin::SoundWait(long lTimeout, long lMaxAge)
{
	ZH_PIN_SELF();
    VARIANT a[2];
    a[0] = VLong(lTimeout);
    a[1] = VLong(lMaxAge);
    BSTR r = CallEngine(13, a, 2);
    FreeArgs(a, 2);
    return r;
}

// ---- 14 SoundScore ----
BSTR CQMPlugin::SoundScore()
{
	ZH_PIN_SELF();
    return CallEngine(14, NULL, 0);
}

// ---- 15 DrawBox ----
BSTR CQMPlugin::DrawBox(long lCenterX, long lCenterY, long lSize, LPCTSTR sColor)
{
	ZH_PIN_SELF();
    VARIANT a[4];
    a[0] = VLong(lCenterX); a[1] = VLong(lCenterY); a[2] = VLong(lSize); a[3] = VBstr(sColor);
    BSTR r = CallEngine(15, a, 4);
    FreeArgs(a, 4);
    return r;
}
