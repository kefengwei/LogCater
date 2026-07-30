# PowerShell build script for LogCater
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Enterprise"
$vcvarsPath = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"

# Call vcvars64.bat and capture the environment changes
$batchOutput = cmd /c "`"$vcvarsPath`" > nul 2>&1 && set"
$envVars = @{}
foreach ($line in $batchOutput -split "`n") {
    if ($line -match "^(.*?)=(.*)$") {
        $envVars[$matches[1]] = $matches[2]
    }
}

# Apply environment variables
foreach ($key in $envVars.Keys) {
    [Environment]::SetEnvironmentVariable($key, $envVars[$key], "Process")
}

Write-Host "=== Configuring CMake ==="
Push-Location "G:\TempProjects\logcater"
try {
    cmake -G "NMake Makefiles" -B "build" "."
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed!"
        exit 1
    }

    Write-Host "=== Building ==="
    cmake --build "build" --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
        exit 1
    }

    Write-Host "=== Build succeeded! ==="
} finally {
    Pop-Location
}
