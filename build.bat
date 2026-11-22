@echo off
rem Windows build with Visual C++ (Visual Studio 2019 or later, "Desktop development
rem with C++"). The Windows counterpart of build.sh: builds every scene as its own
rem program, and the show that plays them all in turn. Run it from anywhere, from a
rem normal prompt or a Native Tools prompt. Object files go to obj\.
rem To add a scene, list it in SCENES below as well as in build.sh.
setlocal
cd /d "%~dp0"

set "SCENES=bonfire moonphases starburst singularity thunderstorm"
set "FLAGS=/nologo /std:c++17 /O2 /EHsc /utf-8 /W3"

rem Not in a Native Tools prompt: find Visual Studio and set up the x64 compiler.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
where cl >nul 2>nul
if errorlevel 1 (
    if not exist "%VSWHERE%" (
        echo Visual C++ not found. Install Visual Studio with "Desktop development with C++",
        echo or run this from an "x64 Native Tools Command Prompt".
        exit /b 1
    )
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 (
    echo Could not set up the Visual C++ compiler.
    exit /b 1
)

set FAIL=0
set FILES=
for %%d in (%SCENES%) do (
    echo building %%d ...
    if not exist obj\%%d mkdir obj\%%d
    cl %FLAGS% /Foobj\%%d\ /Fe:%%d\%%d.exe %%d\%%d.cpp || set FAIL=1
)
for %%d in (%SCENES%) do call set "FILES=%%FILES%% %%d\%%d.cpp"

echo building show ...
if not exist obj\show mkdir obj\show
cl %FLAGS% /DVTSHOW_NO_MAIN /Foobj\show\ /Fe:show\show.exe show\show.cpp %FILES% || set FAIL=1

if "%FAIL%"=="1" (
    echo build FAILED, see the errors above.
    exit /b 1
)
echo done. try:  show\show.exe   or   bonfire\bonfire.exe --loop   (q quits, --no-color for the plain console)
