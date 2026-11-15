@echo off
rem Runs gen_gz.py from the folder this .bat file lives in.
rem Put gen_gz.bat, gen_gz.py and EVDCTRL.h in the same sketch folder.
cd /d "%~dp0"

if not exist "gen_gz.py" (
  echo ERROR: gen_gz.py not found in %CD%
  pause
  exit /b 1
)

where py >nul 2>nul
if %errorlevel%==0 (
  py gen_gz.py
) else (
  where python >nul 2>nul
  if %errorlevel%==0 (
    python gen_gz.py
  ) else (
    echo ERROR: Python was not found. Install it from python.org and tick "Add to PATH".
    pause
    exit /b 1
  )
)

if errorlevel 1 (
  echo.
  echo gen_gz.py FAILED - see the message above.
  pause
  exit /b 1
)

echo.
echo Done. EVDCTRL_GZ.h is up to date - now compile and upload in Arduino.
timeout /t 3 >nul
