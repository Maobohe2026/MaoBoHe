@echo off
rem ============================================================
rem  build.bat - 构建单文件插件 zaoyo.dll（MFC 壳 + 引擎代码，零运行时释放）
rem  用法：在任意目录执行  qmfindpic-single\build.bat
rem  产物：build\zaoyo.dll
rem ============================================================
setlocal
cd /d "%~dp0"

set "VC98=C:\Program Files (x86)\Microsoft Visual Studio\VC98"
set "MSDEV=C:\Program Files (x86)\Microsoft Visual Studio\Common\MSDev98\Bin\MSDEV.EXE"
if not exist "%VC98%\Include" ( echo [ERROR] VC98 not found & exit /b 1 )
if not exist "%MSDEV%"        ( echo [ERROR] msdev.exe not found & exit /b 1 )

if not exist "build" mkdir "build"
if not exist "probe" mkdir "probe"
set "TMP=%~dp0probe"
set "TEMP=%~dp0probe"
set "PATH=%VC98%\Bin;C:\Program Files (x86)\Microsoft Visual Studio\Common\MSDev98\Bin;%PATH%"
set "INCLUDE=%VC98%\Include;%VC98%\MFC\Include;%VC98%\ATL\Include"
set "LIB=%VC98%\Lib;%VC98%\MFC\Lib"

echo [1/3] compiling engine (ZAOYO_NO_COM: no DllMain/DllGetClassObject)
pushd src
for %%f in (finder.cpp features.cpp sound.cpp audiocap.cpp soundhost.cpp) do (
    cl.exe /nologo /c /MD /W3 /GX /O2 /D WIN32 /D NDEBUG /D _WINDOWS /D _WINDLL /D _AFXDLL /D _MBCS /D _USRDLL /D ZAOYO_NO_COM /Fo"..\build\\" %%f || goto :fail
)
popd

echo [2/3] building MFC shell (single DLL)
pushd mfc
"%MSDEV%" File.dsw /MAKE "File - Win32 Release" /REBUILD > build_shell.log 2>&1
if errorlevel 1 ( type build_shell.log & popd & goto :fail )
popd
if not exist "mfc\Release\File.dll" ( echo [ERROR] File.dll not produced & goto :fail )

echo [3/3] copy result
copy /y "mfc\Release\File.dll" "build\zaoyo.dll" >nul || goto :fail
echo.
echo [OK] build\zaoyo.dll  (single DLL, no runtime file writes)
echo      部署：把 zaoyo.dll + zaoyo.ini（+ zaoyo.html）放进 按键精灵\plugin\
exit /b 0

:fail
echo.
echo [ERROR] build failed
exit /b 1
