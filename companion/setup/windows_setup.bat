@echo off
setlocal

echo ============================================================
echo  ESP32 Marauder Companion - Windows Setup
echo ============================================================
echo.

:: Install Python dependencies
echo Installing Python dependencies...
pip install -r "%~dp0..\requirements.txt"
if %errorlevel% neq 0 (
    echo ERROR: pip install failed. Make sure Python 3.8+ is installed.
    pause
    exit /b 1
)
echo.

echo ============================================================
echo  USB Driver Download Links
echo ============================================================
echo.
echo If your Marauder device is not recognized, install the driver
echo matching your USB-to-serial chip:
echo.
echo  CH340 / CH341 (most common):
echo    https://www.wch-ic.com/downloads/CH341SER_EXE.html
echo.
echo  CP2102 / CP2104 (Silicon Labs):
echo    https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
echo.
echo  FTDI FT232:
echo    https://ftdichip.com/drivers/vcp-drivers/
echo.
echo After installing the driver, unplug and re-plug your device.
echo.
echo ============================================================
echo  Setup complete. Run the companion with:
echo    python companion\marauder_ui.py
echo ============================================================
echo.
pause
