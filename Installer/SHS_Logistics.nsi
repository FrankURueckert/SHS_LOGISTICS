Unicode true
; ============================================================================
; SHS Logistics GmbH - Leitsystem Installer (NSIS 3.x)
; Build:  Build_Installer.bat   ->  setup_SHS_Logistics.exe (Repository Root)
;
; Wissenschaftliche Leitung & Entwicklung:
; Frank U. Rueckert (frank.rueckert@htwsaar.de)
; htw saar - Hochschule fuer Technik und Wirtschaft des Saarlandes
; Kooperation mit SHS Logistics GmbH (Stahl-Holding-Saar)
; ============================================================================
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

!define APPNAME   "SHS_Logistics"
!define APPTITLE  "SHS Logistics GmbH - Werksbahn & Logistik Leitsystem"
!define APPVER    "0.8"
!define REPO      ".."
!define RELDIR    "Release_Standalone"
!define AUTHOR    "Frank U. Rueckert"
!define EMAIL     "frank.rueckert@htwsaar.de"

Name "${APPTITLE} ${APPVER}"
OutFile "${REPO}\setup_SHS_Logistics.exe"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"
SetCompressor /SOLID lzma
BrandingText "SHS Logistics GmbH - Partner [schafft Zukunft] Stahl"

; ---- SHS Corporate Design Icons & Bilder (Logo + startseite_img_de.jpg) ----
!define MUI_ICON "${REPO}\Installer\SHS_Icon.ico"
!define MUI_UNICON "${REPO}\Installer\SHS_Icon.ico"

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${REPO}\Installer\nsis_header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "${REPO}\Installer\nsis_header.bmp"

!define MUI_WELCOMEFINISHPAGE_BITMAP "${REPO}\Installer\nsis_welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${REPO}\Installer\nsis_welcome.bmp"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Willkommen beim Setup von SHS Logistics GmbH (${APPVER})"
!define MUI_WELCOMEPAGE_TEXT "Dieser Assistent installiert das C++/Qt-6 Werksbahn-, Waggon-, Gueter-, Quellen/Senken- und Bahn-API-Leitsystem der SHS Logistics GmbH ('Partner [schafft Zukunft] Stahl') auf Ihrem Computer.$\r$\n$\r$\nWissenschaftliche Leitung & Entwicklung:$\r$\nFrank U. Rueckert (frank.rueckert@htwsaar.de)$\r$\nhtw saar - Hochschule fuer Technik und Wirtschaft des Saarlandes$\r$\n$\r$\nEnthalten im Release-Paket:$\r$\n- Alleine lauffaehiges Release (SHS_Logistics.exe inkl. Qt 6.9 & Offline-Kartenkacheln)$\r$\n- Alle PDF-Manuals (Technisches Handbuch & Kurzanleitung)$\r$\n- Standorte Voelklingen, Dillinger Huette (1420 Grad C Torpedowagen), Burbach & Neunkirchen"

!define MUI_FINISHPAGE_RUN "$INSTDIR\SHS_Logistics.exe"
!define MUI_FINISHPAGE_RUN_TEXT "SHS Logistics Leitsystem jetzt starten"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Technisches Handbuch & GUI-Manual (PDF) oeffnen"

Var PreviewDialog
Var PreviewImageCtrl
Var PreviewBmpHandle

; ---- Installationsseiten ----
!insertmacro MUI_PAGE_WELCOME
Page custom ShsPreviewPageCreate ShsPreviewPageLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ---- Deinstallationsseiten ----
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  InitPluginsDir
  File "/oname=$PLUGINSDIR\nsis_startseite_banner.bmp" "${REPO}\Installer\nsis_startseite_banner.bmp"
FunctionEnd

Function ShsPreviewPageCreate
  !insertmacro MUI_HEADER_TEXT "SHS Logistics GmbH - Partner [schafft Zukunft] Stahl" "Werksbahn-, Waggon-, Quellen/Senken-, Torpedowagen- & DB-API-Leitsystem"
  nsDialogs::Create 1018
  Pop $PreviewDialog
  ${If} $PreviewDialog == error
    Abort
  ${EndIf}

  ${NSD_CreateBitmap} 8u 2u 284u 120u ""
  Pop $PreviewImageCtrl
  ${NSD_SetImage} $PreviewImageCtrl "$PLUGINSDIR\nsis_startseite_banner.bmp" $PreviewBmpHandle

  ${NSD_CreateLabel} 8u 124u 284u 18u "Leitung: Frank U. Rueckert (frank.rueckert@htwsaar.de) | htw saar in Kooperation mit SHS Logistics GmbH"
  Pop $0

  nsDialogs::Show
  ${NSD_FreeImage} $PreviewBmpHandle
FunctionEnd

Function ShsPreviewPageLeave
FunctionEnd

Section "SHS Logistics Leitsystem (Release & PDF-Manuals)" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"
  ; ---- Kompletter Standalone-Release-Ordner inkl. Exe, DLLs, Plugins, Tile-Cache & PDF-Manuals ----
  File /r "${REPO}\${RELDIR}\*.*"

  ; ---- Marken-Assets & Startseiten-Bilder & PDF-Manuals im Hauptverzeichnis sicherstellen ----
  File "${REPO}\SHS_Logo.png"
  File "${REPO}\SHSIcon.png"
  File "${REPO}\startseite_img_de.jpg"
  File "${REPO}\startseite_imag_de.jpg"
  File "${REPO}\README.md"
  File "${REPO}\SHS_Logistics_Dokumentation_und_Handbuch.md"
  File "${REPO}\SHS_Logistics_Handbuch_und_Projektbeschreibung.docx"
  File "${REPO}\SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf"
  File "${REPO}\SHS_Logistics_Kurzanleitung_und_Schnellstart.pdf"

  SetOutPath "$INSTDIR\Installer"
  File "${REPO}\Installer\SHS_Icon.ico"

  ; ---- Startmenue- und Desktop-Verknuepfungen ----
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\SHS Logistics GmbH"
  CreateShortcut "$SMPROGRAMS\SHS Logistics GmbH\SHS Logistics Leitsystem.lnk" "$INSTDIR\SHS_Logistics.exe" "" "$INSTDIR\Installer\SHS_Icon.ico" 0
  CreateShortcut "$SMPROGRAMS\SHS Logistics GmbH\Technisches Handbuch & GUI-Manual (PDF).lnk" "$INSTDIR\SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf"
  CreateShortcut "$SMPROGRAMS\SHS Logistics GmbH\Kurzanleitung & Schnellstart (PDF).lnk" "$INSTDIR\SHS_Logistics_Kurzanleitung_und_Schnellstart.pdf"
  CreateShortcut "$SMPROGRAMS\SHS Logistics GmbH\SHS Logistics deinstallieren.lnk" "$INSTDIR\Uninstall.exe"

  CreateShortcut "$DESKTOP\SHS Logistics Leitsystem.lnk" "$INSTDIR\SHS_Logistics.exe" "" "$INSTDIR\Installer\SHS_Icon.ico" 0

  ; ---- Uninstaller & Windows-Systemsteuerungseintrag ----
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPTITLE}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Publisher" "SHS Logistics GmbH / Frank U. Rueckert (frank.rueckert@htwsaar.de)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "Contact" "${EMAIL}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "HelpLink" "mailto:${EMAIL}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayIcon" "$INSTDIR\Installer\SHS_Icon.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\SHS Logistics Leitsystem.lnk"
  RMDir /r "$SMPROGRAMS\SHS Logistics GmbH"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKCU "Software\${APPNAME}"
  RMDir /r "$INSTDIR"
SectionEnd
