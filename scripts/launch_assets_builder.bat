@echo off
REM Assets Builder Launcher - Windows Batch Script
REM This script launches the Assets Builder GUI tool

setlocal enabledelayedexpansion

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0

REM Check if Python is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo ================================================================================
    echo ERROR: Python is not installed or not in PATH!
    echo ================================================================================
    echo.
    echo Please install Python 3.7 or higher from: https://www.python.org/downloads/
    echo.
    echo During installation, make sure to:
    echo   1. Check "Add Python to PATH"
    echo   2. Install Python for all users
    echo.
    pause
    exit /b 1
)

REM Check if Pillow is installed
python -c "import PIL" >nul 2>&1
if errorlevel 1 (
    echo.
    echo ================================================================================
    echo Installing required package: pillow
    echo ================================================================================
    echo.
    python -m pip install pillow --quiet
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to install pillow!
        echo Please run: pip install pillow
        echo.
        pause
        exit /b 1
    )
    echo Done!
    echo.
)

REM Launch the GUI application
echo.
echo ================================================================================
echo Launching Assets Builder GUI
echo ================================================================================
echo.

python "%SCRIPT_DIR%assets_builder_simple.py"

REM Check if the application closed normally
if errorlevel 1 (
    echo.
    echo ================================================================================
    echo ERROR: Application failed to launch!
    echo ================================================================================
    echo.
    pause
    exit /b 1
)

endlocal
