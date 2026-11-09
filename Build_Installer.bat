@echo off
setlocal
rem Erzeugt PDF-Manuals, NSIS-Grafiken und kompiliert setup_SHS_Logistics.exe aus Installer\SHS_Logistics.nsi
echo ==============================================================================
echo  1/2 Aktualisiere PDF-Manuals und NSIS-Grafiken (Logo + startseite_img_de.jpg)
echo ==============================================================================
python "%~dp0Scripts\Generate-WordManual.py"

echo.
echo ==============================================================================
echo  2/2 Erstelle NSIS-Installer setup_SHS_Logistics.exe
echo ==============================================================================
set "MAKENSIS="
if exist "%LOCALAPPDATA%\Programs\NSIS\makensis.exe" set "MAKENSIS=%LOCALAPPDATA%\Programs\NSIS\makensis.exe"
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
if not defined MAKENSIS (
  where makensis >nul 2>nul && set "MAKENSIS=makensis"
)
if not defined MAKENSIS (
  echo makensis.exe nicht gefunden!
  exit /b 1
)
"%MAKENSIS%" /INPUTCHARSET UTF8 "%~dp0Installer\SHS_Logistics.nsi"
if errorlevel 1 exit /b 1
echo.
echo ==============================================================================
echo  NSIS-Installer erfolgreich erstellt: %~dp0setup_SHS_Logistics.exe
echo ==============================================================================

