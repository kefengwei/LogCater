@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
echo.
echo === Configuring CMake ===
cmake -G "NMake Makefiles" -B "G:\TempProjects\logcater\build" "G:\TempProjects\logcater"
if %ERRORLEVEL% NEQ 0 (
    echo CMake configure failed!
    exit /b %ERRORLEVEL%
)
echo.
echo === Building ===
cmake --build "G:\TempProjects\logcater\build" --config Release
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b %ERRORLEVEL%
)
echo.
echo === Build succeeded! ===
