@echo off
setlocal
echo ==============================================================================
echo  SHS Logistics GmbH — C++ / Qt 6 Werksbahn- und Waggon-Leitsystem (Release)
echo ==============================================================================
if exist "%~dp0Release_Standalone\SHS_Logistics.exe" (
    cd /d "%~dp0Release_Standalone"
    start "" "%~dp0Release_Standalone\SHS_Logistics.exe"
) else (
    cd /d "%~dp0GUI\build\Desktop_Qt_6_9_0_MinGW_64_bit-Debug"
    start "" "%~dp0GUI\build\Desktop_Qt_6_9_0_MinGW_64_bit-Debug\SHS_Logistics.exe"
)


