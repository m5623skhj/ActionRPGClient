@echo off
setlocal

set "CLIENT_ROOT=%~dp0"
set "SOURCE_DIR=%CLIENT_ROOT%Assets"
set "CONFIGURATION=%~1"

if not defined CONFIGURATION set "CONFIGURATION=Debug"
if /I "%CONFIGURATION%"=="Debug" goto configuration_ok
if /I "%CONFIGURATION%"=="Release" goto configuration_ok

echo [ERROR] Configuration must be Debug or Release.
echo Usage: %~nx0 [Debug^|Release]
exit /b 1

:configuration_ok
set "OUTPUT_DIR=%CLIENT_ROOT%artifacts\bin\x64\%CONFIGURATION%"
set "CLIENT_EXE=%OUTPUT_DIR%\ActionRPGClient.exe"
set "TARGET_DIR=%OUTPUT_DIR%\Assets"

if not exist "%SOURCE_DIR%\" (
    echo [ERROR] Source Assets directory was not found.
    echo         %SOURCE_DIR%
    exit /b 1
)

if not exist "%CLIENT_EXE%" (
    echo [ERROR] ActionRPGClient.exe was not found.
    echo         %CLIENT_EXE%
    echo Build ActionRPGClient %CONFIGURATION% x64 once before synchronizing assets.
    exit /b 1
)

echo Synchronizing client assets...
echo   Source: %SOURCE_DIR%
echo   Target: %TARGET_DIR%

robocopy "%SOURCE_DIR%" "%TARGET_DIR%" /MIR /R:2 /W:1 /NFL /NDL /NP
set "COPY_RESULT=%ERRORLEVEL%"

if %COPY_RESULT% GEQ 8 (
    echo [ERROR] Asset synchronization failed. Robocopy exit code: %COPY_RESULT%
    exit /b %COPY_RESULT%
)

echo [OK] Assets were synchronized for %CONFIGURATION% x64.
echo Restart a running client to reload changed assets.
exit /b 0
