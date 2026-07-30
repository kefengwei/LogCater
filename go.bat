@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
echo === Configuring CMake ===
cmake -G "NMake Makefiles" -B "G:\TempProjects\logcater\build_msvc" "G:\TempProjects\logcater"
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
echo === Building ===
cmake --build "G:\TempProjects\logcater\build_msvc" --config Release
