@echo off
setlocal
set "QT_DIR=C:\Qt\6.9.0\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1310_64"
set "CMAKE_DIR=C:\Qt\Tools\CMake_64"
set "NINJA_DIR=C:\Qt\Tools\Ninja"

set "PATH=%CMAKE_DIR%\bin;%NINJA_DIR%;%MINGW_DIR%\bin;%QT_DIR%\bin;%PATH%"

set "SRC_DIR=%~dp0GUI"
set "BUILD_DIR_DEBUG=%~dp0GUI\build\Desktop_Qt_6_9_0_MinGW_64_bit-Debug"
set "BUILD_DIR_RELEASE=%~dp0GUI\build\Desktop_Qt_6_9_0_MinGW_64_bit-Release"
set "STANDALONE_DIR=%~dp0Release_Standalone"

if not exist "%BUILD_DIR_DEBUG%" mkdir "%BUILD_DIR_DEBUG%"
if not exist "%BUILD_DIR_RELEASE%" mkdir "%BUILD_DIR_RELEASE%"
if not exist "%STANDALONE_DIR%" mkdir "%STANDALONE_DIR%"

echo ==============================================================================
echo  1/2 Kompiliere SHS_Logistics C++ GUI [RELEASE - Standalone] (Qt 6.9.0 MinGW)
echo ==============================================================================
cmake -S "%SRC_DIR%" -B "%BUILD_DIR_RELEASE%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if %errorlevel% neq 0 (
    echo Fehler bei CMake Release-Konfiguration!
    exit /b %errorlevel%
)
cmake --build "%BUILD_DIR_RELEASE%"
if %errorlevel% neq 0 (
    echo Fehler beim Release-Kompilieren!
    exit /b %errorlevel%
)

echo Deploye Standalone-Release nach: %STANDALONE_DIR%
copy /Y "%BUILD_DIR_RELEASE%\SHS_Logistics.exe" "%STANDALONE_DIR%\SHS_Logistics.exe" >nul
windeployqt.exe --release --compiler-runtime "%STANDALONE_DIR%\SHS_Logistics.exe"
windeployqt.exe --release --compiler-runtime "%BUILD_DIR_RELEASE%\SHS_Logistics.exe"

echo.
echo ==============================================================================
echo  2/2 Kompiliere SHS_Logistics C++ GUI [DEBUG - Qt Creator] (Qt 6.9.0 MinGW)
echo ==============================================================================
cmake -S "%SRC_DIR%" -B "%BUILD_DIR_DEBUG%" -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="%QT_DIR%"
cmake --build "%BUILD_DIR_DEBUG%"
windeployqt.exe --release --compiler-runtime "%BUILD_DIR_DEBUG%\SHS_Logistics.exe"

if exist "%BUILD_DIR_DEBUG%\tile_cache" (
    xcopy /E /I /Y "%BUILD_DIR_DEBUG%\tile_cache" "%STANDALONE_DIR%\tile_cache" >nul
    xcopy /E /I /Y "%BUILD_DIR_DEBUG%\tile_cache" "%BUILD_DIR_RELEASE%\tile_cache" >nul
)

for %%F in ("%~dp0*.pdf" "%~dp0*.docx" "%~dp0SHS_Logo.png" "%~dp0SHSIcon.png" "%~dp0startseite_img_de.jpg") do (
    if exist "%%~fF" (
        copy /Y "%%~fF" "%STANDALONE_DIR%\" >nul
        copy /Y "%%~fF" "%BUILD_DIR_RELEASE%\" >nul
        copy /Y "%%~fF" "%BUILD_DIR_DEBUG%\" >nul
    )
)

echo.
echo ==============================================================================
echo  Build und Standalone-Deployment erfolgreich abgeschlossen!
echo  Standalone Release-Ordner: %STANDALONE_DIR%\SHS_Logistics.exe
echo  Release-Build-Ordner:      %BUILD_DIR_RELEASE%\SHS_Logistics.exe
echo  Debug-Build-Ordner:        %BUILD_DIR_DEBUG%\SHS_Logistics.exe
echo ==============================================================================

