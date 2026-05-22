@echo off
echo ========================================
echo   Defect_Data_Display Deployment Script
echo ========================================
echo.

set DEPLOY_DIR=%~dp0deployment
set BUILD_DIR=%~dp0x64\Debug
set QT_BIN=D:\Qt\6.2.0\msvc2019_64\bin

echo [1/5] Cleaning old deployment...
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"
echo Done
echo.

echo [2/5] Copying executable...
if exist "%BUILD_DIR%\Defect_Data_Display.exe" (
    copy /y "%BUILD_DIR%\Defect_Data_Display.exe" "%DEPLOY_DIR%\" >nul
    echo Defect_Data_Display.exe copied
) else (
    echo [ERROR] Defect_Data_Display.exe not found
    echo Please build the project first
    pause
    exit /b 1
)
echo.

echo [3/5] Deploying Qt dependencies...
if exist "%QT_BIN%\windeployqt.exe" (
    cd /d "%DEPLOY_DIR%"
    "%QT_BIN%\windeployqt.exe" Defect_Data_Display.exe --no-translations
    cd /d "%~dp0"
    echo Qt libraries deployed
) else (
    echo [WARNING] windeployqt.exe not found at:
    echo %QT_BIN%
)
echo.

echo [4/5] Copying VC++ runtime...
set VCREDIST_TARGET=%DEPLOY_DIR%\vcredist_x64
mkdir "%VCREDIST_TARGET%" 2>nul

set FOUND_VC=0
for /d %%i in ("%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
    for /d %%j in ("%%i\VC\Tools\MSVC\*") do (
        if exist "%%j\redist\x64\Microsoft.VC*.CRT\msvcp140.dll" (
            xcopy /y /e "%%j\redist\x64\Microsoft.VC*.CRT\*" "%VCREDIST_TARGET%\" >nul 2>&1
            set FOUND_VC=1
        )
    )
)

if "%FOUND_VC%"=="1" (
    echo VC++ runtime copied
) else (
    echo [INFO] Please download VC++ runtime:
    echo https://aka.ms/vs/17/release/vc_redist.x64.exe
)
echo.

echo [5/5] Creating zip archive...
set ZIP_NAME=Defect_Data_Display_v1.0_x64
powershell -command "Compress-Archive -Path '%DEPLOY_DIR%' -DestinationPath '%~dp0%ZIP_NAME%.zip' -Force"
if exist "%~dp0%ZIP_NAME%.zip" (
    echo %ZIP_NAME%.zip created
)
echo.

echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo Deployment folder: %DEPLOY_DIR%
echo Zip archive: %~dp0%ZIP_NAME%.zip
echo.
echo Target machine requirements:
echo   1. MySQL ODBC 5.3 Driver
echo      https://dev.mysql.com/downloads/connector/odbc/
echo   2. Windows 10/11 x64
echo.
pause
