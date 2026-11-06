# zaoyo 按键精灵插件 —— AI 交接文档（单文件版 v2.9.1）

> 写给"以后接手这个项目的 AI"。读完应该能直接改代码、构建、部署、验证，不用再问人类背景问题。
> 本文档取代旧版的《AI接手文档》（旧版描述的是 `qmfindpic-native` 里"MFC 壳 + 内嵌引擎 + 运行时释放"的 2.8.x 架构）。

---

## 0. 一句话概述

`zaoyo.dll` 是 **32 位 Windows ActiveX DLL**，放在按键精灵（Quick Macro 2014）的 `plugin\` 目录，脚本里只用一种写法：

```vb
Plugin.zaoyo.命令(参数...)      ' 不要 Set p = CreateObject(...)，也不要 p.命令(...)
```

**v2.9.1 起是"单文件插件"**：MFC 壳和全部算法代码编在**同一个 DLL** 里，用 VC6 构建，运行时**零文件写入**
（不落 `%TEMP%`、不落 `%APPDATA%`、不 `LoadLibrary` 任何外部 DLL）。

| 项 | 值 |
|---|---|
| 插件名 / 显示名 | `zaoyo` |
| ProgID | `QMPlugin.zaoyo`（按键精灵里写 `Plugin.zaoyo`） |
| 壳 CLSID | `{9A3B7C21-4D5E-4F60-8A71-B2C3D4E5F607}` |
| LIBID | `{C7CC9058-59A4-4CE1-ACB3-4E2C049E4C31}` |
| 版本 | `Plugin.zaoyo.Version()` → `2.9.1` |
| 交付 | `dist\zaoyo.dll` + `zaoyo.ini` + `zaoyo.html` |
| 体积 | 217088 字节（2.8.9 是壳 294912 + 运行时再落 250368 引擎缓存） |
| 工程 | `E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\` |

命令：找图（ZNCC）、多点找色、扇形测角、箭头朝向、相对鼠标移动、音效识别（闪避/反击）、单独画方框。

---

## 1. 目录结构

```
qmfindpic-single\
├─ build.bat            一键构建 → build\zaoyo.dll（引擎 obj → msdev 编壳 → 复制）
├─ src\                 引擎源码（VC6 可编译；GBK+CRLF）
│  ├─ finder.cpp        COM 对象 / 命令分发 / 找图 / 画框 / 注册 + ZaoyoInvokeDirect 入口
│  ├─ features.cpp/.h   算法库：附件解析、抓屏、多点找色、扇形、箭头
│  ├─ sound.cpp/.h      音效匹配：模板载入、高通、FFT、PHAT 打分、双帧确认
│  ├─ audiocap.cpp/.h   WASAPI 环回采集（后台线程 → 32k 单声道分块回调）
│  ├─ soundhost.cpp/.h  音效宿主：阈值、双检测器、事件通知
│  ├─ vc6compat.h       ★ VC6 兼容层（WINVER / 缺的宏 / 新 API 的动态包装 / Interlocked）
│  ├─ wasapi_vc6.h      ★ 手写 WASAPI 接口声明（VC6 没有 mmdeviceapi.h）
│  ├─ gdiplus_lite.h    ★ 动态加载 gdiplus.dll 的扁平 C 接口（VC6 没有 gdiplus.h）
│  ├─ qmids.h / plugin.def / tlres.rc / sndres.rc
│  └─ snd\dodge.bin / counter.bin   音效模板原始数据（float32 小端 32000Hz）
├─ mfc\                 MFC 壳工程（File.dsw）：CQMPlugin.*、zaoyo_shell.cpp、QMPlugin.odl/.rc/.def
├─ build\               构建产物：zaoyo.dll（+ 2.8.9 回滚备份）
├─ dist\                交付：zaoyo.dll + zaoyo.ini + zaoyo.html + 使用说明.md
├─ tools\               构建/验证工具（vc6fix3.py、sndtest3.cpp、sndtest_offline.cpp、eval_*.py、mix_snd.py）
├─ SOUND_TUNING.md      音效抗噪调优记录（实测数据 + 改造说明）
└─ AI接手文档.md        本文件
```

**与 2.8.x 的关键差别**：没有"引擎 DLL"这个概念了。`src\` 里的 5 个 .cpp 编成 .obj 后直接链进壳，
通过 `finder.cpp` 导出的 `ZaoyoInvokeDirect(dispid, nArgs, args, result)` 在同进程内调用。

---

## 2. 构建

```
qmfindpic-single\build.bat        → build\zaoyo.dll
```

三步：

1. `cl.exe /c /MD ... /D ZAOYO_NO_COM` 编 `src\` 的 5 个引擎 cpp → `build\*.obj`
2. `msdev File.dsw /MAKE "File - Win32 Release" /REBUILD`（在 `mfc\`）编 MFC 壳，链接时把 5 个 obj 一起链进去
3. `mfc\Release\File.dll` 复制成 `build\zaoyo.dll`

工具链：

- 引擎+壳都用 **VC6**（`C:\Program Files (x86)\Microsoft Visual Studio\VC98`，CL 12.00.8168）
- `msdev.exe` 在 `...\Common\MSDev98\Bin`
- 需要设 `INCLUDE=VC98\Include;VC98\MFC\Include;VC98\ATL\Include`、`LIB=VC98\Lib;VC98\MFC\Lib`（build.bat 已设）

### 三个必须记住的构建条件

1. **`.dsp` 的 LINK32 里 `MFCS42.LIB` 必须排在所有 obj 前面**
   ```
   /FORCE:MULTIPLE "<VC98>\MFC\Lib\MFCS42.LIB" ..\build\finder.obj ...
   ```
   否则静态 MFC 的 `DllMain` 会被 CRT 的 `dllmain.obj` 顶掉 → MFC 模块状态不初始化 →
   宿主里 `Version()` 返回空、注册路径被写成宿主 exe（踩过一次，很隐蔽）。
2. **引擎 obj 必须带 `/D ZAOYO_NO_COM`**：`finder.cpp` 里引擎自己的 `DllMain` / `DllGetClassObject` /
   `DllRegisterServer` 用 `#ifndef ZAOYO_NO_COM` 包起来了，链进壳时不能编，否则和 MFC 的同名符号冲突。
3. **音效模板 `301/302` 必须编进壳的 `mfc\QMPlugin.rc`**（`301 RCDATA "..\src\snd\dodge.bin"`）。
   漏了的话 `SoundStart()` 返回 `fail|-3`（模板加载失败）。

其他：源码必须是 **GBK + CRLF**（VC6 按 CP936 读，UTF-8 中文注释会被拆成非法字节，产生莫名其妙的语法错误）。

---

## 3. 插件规范（当前实现的真实行为）

### 3.1 in-proc COM 服务器

导出（`mfc\QMPlugin.def`，**只能这三个，全 PRIVATE**）：

```c
DllCanUnloadNow()        // 固定 S_FALSE（常驻）
DllGetClassObject(REFCLSID, REFIID, void**)
DllRegisterServer()      // ★ 用 MFC 自带实现，不要自己写
```

另外 `finder.cpp` 额外导出（给壳同进程调用，不属于 COM 接口）：
`ZaoyoInvokeDirect(int dispid, int nArgs, VARIANT *argsIn, VARIANT *result)`、`ZaoyoSetInstance(void *hInst)`。

### 3.2 类型库（TYPELIB 资源 id 必须是 1）

`mfc\QMPlugin.odl` 由 VC6 的 mktyplib 编成 `QMPlugin.tlb` 再进资源：

```
library QMPlugin
├─ dispinterface IQMPlugin           （每个命令一个 func + 参数）
├─ dispinterface IQMPluginStandard   （IID {EE1035BA-1025-4CDE-AD4C-A04B3878BF00}）
└─ coclass zaoyo                     （flags=0x2 FCANCREATE，不能带 FAPPOBJECT）
```

- `library` 名固定 `QMPlugin`，`coclass` 名就是插件名 → ProgID `Plugin.zaoyo`
- `SetHelpFileName("zaoyo.html")`：按键精灵据此在 `plugin\` 找帮助页
- 新增/改名命令要同步 **五处**：`tools\mktlb`（旧工程才有）/ 引擎 `g_desc`+`Invoke` 的 `switch` /
  壳 `zaoyo_shell.cpp` 的转发函数 / `CQMPlugin.h`、`CQMPlugin.cpp` 的 `DISPATCH_MAP` / `QMPlugin.odl`
  外加 `zaoyo.ini`、`zaoyo.html`

### 3.3 注册表：宿主自己写，插件不要管

实测：按键精灵（进程内是 `Runner.exe`）加载插件后**会自己调用 `DllRegisterServer`**。所以：

- 插件只要正确导出 MFC 的 `DllRegisterServer` 就行
- **绝不要**把 `.def` 改成别名导出（`DllRegisterServer = 自己的函数`）→ 宿主无法实例化插件
- `.def` 里加 `DllUnregisterServer` 会 `LNK2001`（MFC 库不导出它，旧工程实测）
- `DllMain` 里不能碰注册表/窗口/GDI（加载器锁内）

宿主写出来的有效最小集：

```
HKLM\Software\Classes\QMPlugin.zaoyo                    (Default) = QMPlugin.zaoyo
HKLM\Software\Classes\QMPlugin.zaoyo\CLSID              (Default) = {9A3B7C21-…}
HKLM\Software\Classes\Wow6432Node\CLSID\{9A3B7C21-…}\InProcServer32
                                                        (Default) = <dll 全路径>
                                                        ThreadingModel = Apartment
```

- `HKCU\Software\Classes\...` 优先于 HKLM：残留会盖住 HKLM，排查先查 HKCU
- `regsvr32` 对这类 MFC 插件**无效**（`LoadLibrary` 报 `err=1114`，官方 `File.dll` 也一样）

### 3.4 调用方式

- 唯一写法 `Plugin.zaoyo.命令(...)`；小精灵打包器认这个字面量，会自动把插件打进 exe
- **参数必须写全**（MFC 的 `DISPATCH_MAP` 不支持可选参数，虽然 `mktlb` 里声明了）
- 老写法 `CreateObject("Plugin.zaoyo")` 作废

### 3.5 命令面板与帮助

- `plugin\zaoyo.ini`：`[language]` 段，每条命令两行（签名 + 带 `$1..$n` 的说明）。宿主会重写这个文件，正常
- `plugin\zaoyo.html`：帮助页，表格形式，GB2312

---

## 4. 命令清单（DISPID 顺序不可随意改动）

| DISPID | 命令 | 参数 | 返回 |
|---|---|---|---|
| 1 | `Find` | 左,上,右,下,图片,相似度,绘制,颜色,方向 | `x\|y\|相似度` / `-1\|-1` |
| 2 | `FindAll` | 左,上,右,下,图片列表,相似度,绘制,颜色,方向 | `各图x\|y\|s;...;中心x\|中心y` |
| 3 | `FindAllPos` | 同上 | `数量;x\|y\|s;...` |
| 4 | `FindColorMulti` | 左,上,右,下,主色,偏色点,相似度,绘制,颜色,方向 | `x\|y\|s` / `-1\|-1` |
| 5 | `FindColorMultiPos` | 同上 | `数量;x\|y\|s;...` |
| 6 | `GetSectorAngle` | 圆心X,圆心Y,内半径,外半径,灵敏度,校准,绘制,颜色,方法 | `角度\|集中度\|覆盖` / `-1` |
| 7 | `GetArrowAngle` | 圆心X,圆心Y,半径,基准角,绘制,颜色 | `角度\|像素数\|长度` / `-1` |
| 8 | `MoveMouseR` | dx,dy,步数,间隔,方法 | `ok\|步数\|dx\|dy` |
| 9 | `MoveMouseP` | dx,dy,步数,间隔,方法 | `ok\|步数\|dx\|dy\|计数` |
| 10 | `Version` | — | `2.9.1` |
| 11 | `SoundStart` | — | `ok\|采样率\|声道` / `fail\|错误码` |
| 12 | `SoundStop` | — | `ok` |
| 13 | `SoundWait` | 超时毫秒,有效期毫秒 | `2`=闪避 / `3`=反击 / `0`=超时 / `-1`=未启动 |
| 14 | `SoundScore` | — | `闪避分\|反击分` |
| 15 | `DrawBox` | 中心X,中心Y,边长,颜色 | `ok\|左\|上\|右\|下` / `-1` |

### 参数约定

- **相似度**：0~1，常用 0.88
- **绘制开关**：1=画框，0=不画；框 1.5 秒无新框自动消失（窗口同时销毁）
- **方框颜色**：`RRGGBB`；**不能 000000**（黑是色键透明色，会回落红色）
- **方向**：1=左上→右下（默认）、-1=右下→左上、2=右上→左下、-2=左下→右上、**3=按分数降序**
- **图片**：完整路径 / `a.bmp|b.bmp` / 通配符 `C:\pic\*.bmp`
- **找色颜色**：**BBGGRR**（红=0000FF、绿=00FF00、蓝=FF0000）
- **偏色点**：`dx|dy|颜色`，逗号分隔；颜色前加 `-` 表示"该点不能是这个颜色"
- **角度**：0=正上，顺时针 0~360

---

## 5. 算法说明

### 5.1 找图（`Find` / `FindAllPos`）

```
抓屏(BitBlt，不用 CAPTUREBLT) → 灰度 0.299R+0.587G+0.114B
→ 金字塔粗搜（按模板尺寸自动选 1/2/4/6 倍），积分图算窗口均值/方差
→ 保留分数最高的 96 个候选 → 粗层 NMS → 回原分辨率 ZNCC 精定位（单图前 6、多图 24）
→ 全分辨率 NMS（间距=模板尺寸一半）
```

ZNCC 对整体亮度/对比度/轻微色偏免疫 —— 这是"抗游戏渲染色差"的来源。无多尺度、无旋转。
旧工程实测 2293×960 全屏找 120×120 ≈ 43ms（VC6 `/O2` 版本未重新标定，理论上更快）。

### 5.2 多点找色（`FindColorMulti`）

```
负色点门限（不参与打分）：该点 R/G/B 与给定颜色全等（容差 0）→ 直接否决
绝对色差项：只取正色点，1-归一化色差，对正色点求平均
结构相关项：只取正色点，把 RGB 当标量序列做 ZNCC（对整体色偏免疫）
总分 = 0.5×绝对项 + 0.5×结构项
```

三道粗筛：锚点（主色）容差 `preTol=80` → 第二点自动挑"颜色离主色最远"的正色点 → 负色点全等门限。
扫描边界 = 采样点包围盒本身，**调用方给的范围必须完整覆盖所有采样点**，否则会静默裁掉可行锚点、表现为"怎么调都找不到"。

### 5.5 鼠标相对移动（`MoveMouseR` / `MoveMouseP`）

- `MoveMouseR`：`SendInput` + `MOUSEEVENTF_MOVE | 0x2000(MOVE_NOCOALESCE)`，单位是鼠标计数
- `MoveMouseP`：像素 → 计数，读 `SPI_GETMOUSESPEED` / `SPI_GETMOUSE` 做分段反解
- 分步发送避免游戏单帧限幅；余数分散到前面几步保证总量精确；`方法=1` 改用 `mouse_event`
- 锁鼠标的游戏只读 Raw Input 相对增量，`SetCursorPos`（按键精灵的 `MoveTo`）无效

### 5.6 音效识别（`SoundStart/Stop/Wait/Score`）—— 2.9.1 大改

```
WASAPI 环回（默认播放设备混音，48k/2ch float32）
→ 下混单声道 → sinc 重采样到 32000Hz
→ 4 阶 Butterworth 高通 1kHz（两级 RBJ biquad，因果，帧间保持状态）
→ 80ms 一块喂检测器
→ 窗长 = 模板长度（不是 max(模板,0.5s)）
→ PHAT 打分：R = X·conj(T)/(|X·conj(T)|+ε)，score = max|ifft(R)|（完全匹配≈1）
→ score ≥ 阈值 且连续 2 帧 且过冷却 → 命中
```

- 阈值（`soundhost.cpp`）：`SND_TH_DODGE 0.20`、`SND_TH_COUNTER 0.18`；冷却 0.5s / 1.2s
- **分数尺度变了**：安静时 `SoundScore()` 返回 ≈0.000（旧版 RMS 归一化时是 0.02~0.07），事件时 0.3~0.9
- 模板从 DLL 资源读（301/302，float32 裸数组），不需要外部文件
- 命中通过 `SetEvent` 通知，`SoundWait` 用 `WaitForSingleObject` + `InterlockedExchange` 取走
- 采集线程在 `AudioCapStart` 里 `CoInitializeEx(MTA)`；`SndHostShutdown()` 由 `DllMain(DETACH)` 调用，只置标志+关线程

**为什么改**：混战时旧算法漏检严重（40 秒实战 9 个事件只命中 6 个）。原因三条：
窗长固定 0.5s 把无关声音算进归一化、整窗 RMS 归一化把模板段压小、单帧固定阈值。
详细数据、改造前后对比、离线验证方法见 **`SOUND_TUNING.md`**。

**换音效模板**：录一段 → 剪出音效 → `python tools\gen_tmpl.py`（在旧工程里）→
`np.load(...).astype('<f4').tofile('src\snd\dodge.bin')` → `build.bat` → 用离线台复核分数分布。

### 5.7 画方框（`DrawBox`）

```
矩形 = (中心X-边长/2, 中心Y-边长/2)，宽高各 = 边长
g_lineW=1、g_lineColor=颜色、g_drawOn=1 → DrawBoxesOnScreen(&box, 1, &color, NULL)
```

天然获得：画框线程常驻、每框独立 1.5s 到期、`WDA_EXCLUDEFROMCAPTURE` 排除出抓屏。
同位置 ±4px 合并成刷新计时；`g_boxN` 上限 512；颜色 `RRGGBB`，`000000` 回落红色；不画分数标签。

---

## 6. 实现层面的陷阱（改前必读）

1. **`DllMain` 里绝不能碰窗口/GDI/注册表**（`DLL_PROCESS_DETACH` 在加载器锁内，会让宿主崩溃）
2. **画框窗口**：顶层、透明、穿透点击，色键透明（黑）+ GDI 画线；**窗口属于创建它的线程**，线程变了必须重建；
   隐藏用 `ShowWindowAsync`，销毁用 `PostMessage`
3. **画框与抓屏**：`SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` 成功后搜索期间不用隐藏旧框（不闪烁）；
   失败时退回"抓屏前先隐藏 + `Sleep(12)`"。改这块先跑旧工程的 `tools\ovtest.cpp`
4. **DISPID 五处同步**（见 3.2）
5. **`ARG(i)` 宏在参数不足时返回 `NULL`**，所有 `ToLong/ToDouble/FromBstr` 都做了空判断 —— 新增命令沿用
6. **颜色格式别混**：找色是 BBGGRR，方框是 RRGGBB
7. **`Attachment:\` 不支持**（按键精灵把附件以 base64 zip 内嵌在 .Q 里，deflate 这一环没打通），要用 `PutAttachment` 释放到磁盘
8. **VC6 特性**：没有 `for (int i…)` 作用域（同一函数第二个同名循环报 C2374）、没有 `long long`（用 `__int64`）、
   没有变参宏、没有 `DWORD_PTR`/`ULONG_PTR`、`Interlocked*` 不收 `volatile long*` 且没有 `InterlockedOr`
   —— 这些都已在 `vc6compat.h` 里处理；源码必须是 GBK + CRLF

---

## 7. 验证方法

### 7.1 端到端冒烟（AutoRun 法，必须跑）

往 `D:\按键精灵\QMScript\` 写一个 `.Q`（**GBK+CRLF**）设 `AutoRun=1`，启动按键精灵它会自动执行，
脚本里用 `Call Plugin.File.WriteFileEx(路径, 内容)` 把每个命令的返回值写文件，20~30 秒后读文件即可判断。
**不用点界面、不用截图、不用开调试窗口**。

```ini
[General]
MacroID={随机GUID}
SyntaxVersion=2
Enable=1
AutoRun=1
Description=dsh_zaoyotest
[Repeat]
Type=0
Number=1
[SetupUI]
Type=2
[Script]
Call Plugin.File.WriteFileEx("D:\qmtest\t1.txt", "ver=[" & Plugin.zaoyo.Version() & "]")
Call Plugin.File.WriteFileEx("D:\qmtest\t2.txt", "snd=[" & Plugin.zaoyo.SoundStart() & "]")
Delay 1500
Call Plugin.File.WriteFileEx("D:\qmtest\t3.txt", "score=[" & Plugin.zaoyo.SoundScore() & "]")
Call Plugin.zaoyo.SoundStop()
```

- **先提权结束宿主**（宿主是 `requireAdministrator`，单实例：已在跑时再启动会立刻退出，脚本根本不执行）
- 换 DLL 前也必须先结束宿主（会锁住 `plugin\zaoyo.dll`）
- 跑完删掉 `.Q`，别让它在下次启动又执行一遍

2.9.1 的实测输出：

```
ver=[2.9.1]  snd=[ok|48000|2]  score=[0.000|0.000]  find=[-1|-1]
%TEMP% 里 zao*  0 个   %APPDATA%\MyMacro\zaoyo  不存在（单文件版不写任何文件）
```

### 7.2 离线音效台（改音效必跑）

```powershell
# 从实战录像抽音轨（人工标注的时间点必须和这份音轨对齐）
ffmpeg -i 录像.mkv -vn -ac 1 -ar 32000 -c:a pcm_s16le video_audio.wav

# 用**改过的** sound.cpp 编离线台（sndtest_offline.cpp 就是旧工程的 tools\sndtest.cpp）
i686-w64-mingw32-g++.exe -O2 -static -o build\sndtest_new.exe tools\sndtest_offline.cpp src\sound.cpp

build\sndtest_new.exe video_audio.wav 闪避_自录.npy > frames.txt
python tools\eval_modes.py        # 按人工标注算命中率 / 非事件分 p95、p99、max
```

`tools\` 里还有 `sndtest3.cpp`（三种打分方式 A/B：窗长 / 逐滞后 NCC / PHAT）、`mix_snd.py`（按 SNR 混音测失效点）。

### 7.3 离线算法台

找图/找色/扇形等改动可以编小 exe 直接链接 `src\features.cpp` 加载 BMP 验证（不需要宿主）。
**壳层改动没法离线验证**（MFC DLL 只能被宿主加载，`LoadLibrary` 报 `err=1114`），必须走 7.1。

---

## 8. 已知限制

| 项 | 说明 |
|---|---|
| 无多尺度 | 图片尺寸必须与屏幕一致 |
| 无旋转 | 找图不支持旋转模板 |
| DPI | 不做 DPI 缩放，坐标是物理像素 |
| 附件 | 不支持 `Attachment:\` 路径 |
| 鼠标 | 需要游戏窗口在前台；部分反作弊会拦，只能上驱动级 |
| 64 位宿主 | 插件是 32 位，64 位小精灵用不了 |
| 音效 | 只对已录模板的那一种音效有效；游戏换音效必须重做模板 + 重新标阈值 |

---

## 9. 部署与回滚

```
部署：把 dist\zaoyo.dll + zaoyo.ini + zaoyo.html 放进 按键精灵\plugin\  →  重启按键精灵
      （注册项宿主会自己写，不需要管理员、不需要 regsvr32、不需要任何安装脚本）
小精灵：用这份 zaoyo.dll 重新打包 exe；它会自己解压到 %APPDATA%\MyMacro\plugin\ 并注册
回滚：build\zaoyo_2.8.9_shell_backup.dll（旧版 2.8.9，A14A0713…）
      复制回 plugin\zaoyo.dll 和 %APPDATA%\MyMacro\plugin\ZAOYO.DLL，重启按键精灵
```

> 2.8.9 会重新开始往 `%APPDATA%\MyMacro\zaoyo\*.dat` 释放引擎缓存；2.9.x 不会写任何文件，
> 所以"`%APPDATA%\MyMacro\zaoyo` 是否存在"可以直接当作判断当前跑的是哪个架构的依据。

---

## 10. 变更历史

| 版本 | 内容 |
|---|---|
| 1.x | 找图（ZNCC）、多点找色、BBGGRR、方向参数、负色点、去重、画框 1.5s 自动消失 |
| 1.6~2.4 | 扇形测角（径向梯度+圆心校准）、箭头朝向（PCA/双质心/双连通域） |
| 2.5 / 2.6 | 相对鼠标移动（SendInput）、按像素补偿系统鼠标速度 |
| 3.0 | MFC 壳架构（按键精灵不认 mingw 编的纯 IDispatch 对象）；引擎内嵌 RCDATA、运行时释放到 %TEMP% |
| 2.7 | 画框 `WDA_EXCLUDEFROMCAPTURE` 排除出抓屏；双缓冲上屏；方向 `3`=按分数优先 |
| 2.8 | 音效识别（WASAPI 环回 + 1kHz 高通 + FFT 互相关）；DISPID 11~14 |
| 2.8.1~2.8.6 | 找色负色点门限/两点粗筛/最小堆；`DrawBox`；引擎缓存命名改 CRC32；`Version()` 转发引擎 |
| 2.8.9 | 引擎缓存迁出 `%TEMP%` → `%APPDATA%\MyMacro\zaoyo\*.dat`（消掉"自解压执行"特征） |
| **2.9.0** | **单文件架构**：引擎 5 个 cpp 用 VC6 编进壳，`ZaoyoInvokeDirect` 同进程调用，运行时零文件写入；体积 294912+250368 → 217088 |
| **2.9.1** | 音效抗噪改造：窗长=模板长度、PHAT 打分、连续 2 帧确认；阈值 0.35/0.25 → 0.20/0.18。40 秒实战：闪避 6/9 → 11/11、误触发 2 → 0；反击保持 2/2 |
| ~~2.8.7~~ | 作废未发布：把 `.def` 的 `DllRegisterServer` 改成别名导出，导致宿主无法实例化插件 |
