; ==================================
; NSIS script for Kolosal AI
; ==================================

;-----------------------------------
; Include Modern UI
;-----------------------------------
!include "MUI2.nsh"  ; Updated to MUI2 for better compatibility

;-----------------------------------
; Embed version info (metadata)
;-----------------------------------
VIProductVersion "0.1.1.0"
VIAddVersionKey "ProductName" "Kolosal AI Installer"
VIAddVersionKey "CompanyName" "Genta Technology"
VIAddVersionKey "FileDescription" "Kolosal AI Installer"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2025"
VIAddVersionKey "FileVersion" "0.1.1.0"
VIAddVersionKey "ProductVersion" "0.1.1.0"
VIAddVersionKey "OriginalFilename" "KolosalAI_Installer.exe"
VIAddVersionKey "Comments" "Installer for Kolosal AI"
VIAddVersionKey "Publisher" "Genta Technology"

;-----------------------------------
; Variables
;-----------------------------------
Var StartMenuFolder

;-----------------------------------
; Basic Installer Info
;-----------------------------------
Name "Kolosal AI"
OutFile "KolosalAI_Installer.exe"
BrandingText "Genta Technology"

; Use the same icon for installer and uninstaller
!define MUI_ICON "assets\icon.ico"
!define MUI_UNICON "assets\icon.ico"

; The default install directory
InstallDir "$PROGRAMFILES\KolosalAI"

; Store installation folder
InstallDirRegKey HKLM "Software\KolosalAI" "Install_Dir"

; Require admin rights for installation
RequestExecutionLevel admin

;-----------------------------------
; Pages
;-----------------------------------
!define MUI_ABORTWARNING

; Start Menu configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\KolosalAI"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "Kolosal AI"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;-----------------------------------
; Installation Section
;-----------------------------------
Section "Kolosal AI" SecKolosalAI
  SetOutPath "$INSTDIR"
  
  ; Set write permissions
  AccessControl::GrantOnFile "$INSTDIR" "(S-1-5-32-545)" "FullAccess"
  
  ; Copy main files
  File "InferenceEngineLib.dll"
  File "InferenceEngineLibVulkan.dll"
  File "KolosalDesktop.exe"
  File "libcrypto-3-x64.dll"
  File "libssl-3-x64.dll"
  File "libcurl.dll"
  File "LICENSE"

  ; Create and populate subdirectories
  CreateDirectory "$INSTDIR\assets"
  SetOutPath "$INSTDIR\assets"
  File /r "assets\*.*"

  CreateDirectory "$INSTDIR\fonts"
  SetOutPath "$INSTDIR\fonts"
  File /r "fonts\*.*"

  CreateDirectory "$INSTDIR\models"
  SetOutPath "$INSTDIR\models"
  File /r "models\*.*"

  SetOutPath "$INSTDIR"  ; Reset working directory to main install dir

  ; Create Start Menu shortcuts
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"

    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Kolosal AI.lnk" \
      "$INSTDIR\KolosalDesktop.exe" \
      "" \
      "$INSTDIR\assets\icon.ico" \
      0 \
      SW_SHOWNORMAL \
      "" \
      "Kolosal AI Desktop Application"
    
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" \
      "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END

  ; Create desktop shortcut
  CreateShortCut "$DESKTOP\Kolosal AI.lnk" \
    "$INSTDIR\KolosalDesktop.exe" \
    "" \
    "$INSTDIR\assets\icon.ico" \
    0 \
    SW_SHOWNORMAL \
    "" \
    "Kolosal AI Desktop Application"

  ; Write registry information
  WriteRegStr HKLM "Software\KolosalAI" "Install_Dir" "$INSTDIR"
  
  ; Write uninstaller registry information
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" \
    "DisplayName" "Kolosal AI"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" \
    "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" \
    "Publisher" "Genta Technology"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" \
    "DisplayIcon" "$INSTDIR\assets\icon.ico"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

;-----------------------------------
; Uninstall Section
;-----------------------------------
Section "Uninstall"
  ; Retrieve Start Menu folder from registry
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder

  MessageBox MB_ICONQUESTION|MB_YESNO \
    "Are you sure you want to uninstall Kolosal AI?" \
    IDNO noRemove

  ; Remove shortcuts
  Delete "$SMPROGRAMS\$StartMenuFolder\Kolosal AI.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  Delete "$DESKTOP\Kolosal AI.lnk"

  ; Remove directories and files
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\models"
  Delete "$INSTDIR\*.*"
  RMDir "$INSTDIR"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI"
  DeleteRegKey HKLM "Software\KolosalAI"

noRemove:
SectionEnd