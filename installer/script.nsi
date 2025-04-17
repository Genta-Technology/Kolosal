; ==================================
; NSIS script for Kolosal AI
; ==================================

;-----------------------------------
; Include Modern UI
;-----------------------------------
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "nsProcess.nsh"

;-----------------------------------
; Variables
;-----------------------------------
Var StartMenuFolder
Var ChatHistoryDir
Var DefaultChatDir
Var OldVersion
Var NewVersion
Var IsUpgrade

;-----------------------------------
; Embed version info (metadata)
;-----------------------------------
!define VERSION "0.1.9.0"
VIProductVersion "${VERSION}"
VIAddVersionKey "ProductName" "Kolosal AI Installer"
VIAddVersionKey "CompanyName" "Genta Technology"
VIAddVersionKey "FileDescription" "Kolosal AI Installer"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2025"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "OriginalFilename" "KolosalAI_Installer.exe"
VIAddVersionKey "Comments" "Installer for Kolosal AI"
VIAddVersionKey "Publisher" "Genta Technology"

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
; Pages Configuration
;-----------------------------------
!define MUI_ABORTWARNING

; Define the chat history directory page variables
!define CHATHISTORY_TITLE "Choose Chat History Location"
!define CHATHISTORY_SUBTITLE "Choose the folder where chat histories will be stored"
!define MUI_PAGE_HEADER_TEXT "${CHATHISTORY_TITLE}"
!define MUI_PAGE_HEADER_SUBTEXT "${CHATHISTORY_SUBTITLE}"
!define MUI_DIRECTORYPAGE_VARIABLE $ChatHistoryDir

; Start Menu configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\KolosalAI"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "Kolosal AI"

Function .onInit
    ; Initialize default chat directory
    StrCpy $DefaultChatDir "$LOCALAPPDATA\KolosalAI\ChatHistory"
    StrCpy $ChatHistoryDir $DefaultChatDir
    
    ; Check for previous installation
    StrCpy $IsUpgrade "false"
    ReadRegStr $R0 HKLM "Software\KolosalAI" "Install_Dir"
    ReadRegStr $OldVersion HKLM "Software\KolosalAI" "Version"
    StrCpy $NewVersion "${VERSION}"
    
    ${If} $R0 != ""
        StrCpy $IsUpgrade "true"
        
        ; Detect if the application is running
        ${nsProcess::FindProcess} "KolosalDesktop.exe" $R1
        ${If} $R1 == 0
            MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
                "Kolosal AI is currently running. Please close it before continuing.$\n$\nPress OK to automatically close the application and continue with the update, or Cancel to abort installation." \
                IDCANCEL abort
                
            ; Kill the process if user chose to continue
            ${nsProcess::KillProcess} "KolosalDesktop.exe" $R1
            Sleep 2000 ; Give it time to fully terminate
        ${EndIf}
    ${EndIf}
    
    Return
    
abort:
    Abort "Installation aborted. Please close Kolosal AI and run the installer again."
FunctionEnd

Function ChatHistoryDirectoryPre
    StrCpy $ChatHistoryDir $DefaultChatDir
    !undef MUI_DIRECTORYPAGE_VARIABLE
    !define MUI_DIRECTORYPAGE_VARIABLE $ChatHistoryDir
    !undef MUI_PAGE_HEADER_TEXT
    !define MUI_PAGE_HEADER_TEXT "${CHATHISTORY_TITLE}"
    !undef MUI_PAGE_HEADER_SUBTEXT
    !define MUI_PAGE_HEADER_SUBTEXT "${CHATHISTORY_SUBTITLE}"
FunctionEnd

; Page order
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY

; Custom chat history directory page
!define MUI_PAGE_CUSTOMFUNCTION_PRE ChatHistoryDirectoryPre
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;-----------------------------------
; Installation Section
;-----------------------------------
Section "Kolosal AI" SecKolosalAI
  ; Force overwrite of existing files
  SetOverwrite on
  
  ; If this is an upgrade, remove old files first (except chat history and models folder)
  ${If} $IsUpgrade == "true"
    ; Display upgrade message
    DetailPrint "Upgrading from version $OldVersion to $NewVersion"
    
    ; Remove previous program files but keep the models folder intact to preserve user-downloaded models.
    RMDir /r "$INSTDIR\assets"
    RMDir /r "$INSTDIR\fonts"
    ; NOTE: Do NOT remove "$INSTDIR\models" so that any user-downloaded models are not deleted.
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\*.exe"
    Delete "$INSTDIR\LICENSE"
    
    ; Small delay to ensure all files are released
    Sleep 1000
  ${EndIf}
  
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
  File "kolosal_server.dll"
  File "vcomp140.dll"
  File "LICENSE"

  ; Create and populate subdirectories
  CreateDirectory "$INSTDIR\assets"
  SetOutPath "$INSTDIR\assets"
  File /r "assets\*.*"

  CreateDirectory "$INSTDIR\fonts"
  SetOutPath "$INSTDIR\fonts"
  File /r "fonts\*.*"

  ; Update files within models folder without deleting the folder itself
  CreateDirectory "$INSTDIR\models"
  SetOutPath "$INSTDIR\models"
  File /r "models\*.*"

  ; Create chat history directory if it doesn't exist (for a new install)
  ${If} $IsUpgrade == "false"
    CreateDirectory "$ChatHistoryDir"
    AccessControl::GrantOnFile "$ChatHistoryDir" "(S-1-5-32-545)" "FullAccess"
  ${EndIf}

  SetOutPath "$INSTDIR"

  ; Create Start Menu shortcuts
  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Kolosal AI.lnk" "$INSTDIR\KolosalDesktop.exe" "" "$INSTDIR\assets\icon.ico" 0 SW_SHOWNORMAL "" "Kolosal AI Desktop Application"
    CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  !insertmacro MUI_STARTMENU_WRITE_END

  ; Create desktop shortcut
  CreateShortCut "$DESKTOP\Kolosal AI.lnk" "$INSTDIR\KolosalDesktop.exe" "" "$INSTDIR\assets\icon.ico" 0 SW_SHOWNORMAL "" "Kolosal AI Desktop Application"

  ; Write registry information
  WriteRegStr HKLM "SOFTWARE\KolosalAI" "Install_Dir" "$INSTDIR"
  WriteRegStr HKLM "SOFTWARE\KolosalAI" "ChatHistory_Dir" "$ChatHistoryDir"
  WriteRegStr HKLM "SOFTWARE\KolosalAI" "Version" "${VERSION}"
  
  WriteRegStr HKCU "SOFTWARE\KolosalAI" "Install_Dir" "$INSTDIR"
  WriteRegStr HKCU "SOFTWARE\KolosalAI" "ChatHistory_Dir" "$ChatHistoryDir"
  WriteRegStr HKCU "SOFTWARE\KolosalAI" "Version" "${VERSION}"
  
  ; Write uninstaller registry information
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayName" "Kolosal AI"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "Publisher" "Genta Technology"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayIcon" "$INSTDIR\assets\icon.ico"
  WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayVersion" "${VERSION}"
  
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayName" "Kolosal AI"
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "Publisher" "Genta Technology"
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayIcon" "$INSTDIR\assets\icon.ico"
  WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI" "DisplayVersion" "${VERSION}"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  
  ; Clean temporary files that might be left from previous versions
  RMDir /r "$TEMP\KolosalAI"
  Delete "$TEMP\KolosalAI_*.log"
  Delete "$TEMP\KolosalAI_*.tmp"
SectionEnd

;-----------------------------------
; Uninstall Section
;-----------------------------------
Section "Uninstall"
  ; Check if the application is running before uninstalling
  ${nsProcess::FindProcess} "KolosalDesktop.exe" $R1
  ${If} $R1 == 0
      MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
          "Kolosal AI is currently running. Please close it before continuing.$\n$\nPress OK to automatically close the application and continue with uninstallation, or Cancel to abort." \
          IDCANCEL abortUninstall
          
      ; Kill the process if user chose to continue
      ${nsProcess::KillProcess} "KolosalDesktop.exe" $R1
      Sleep 2000 ; Give it time to fully terminate
  ${EndIf}

  ; Retrieve Start Menu folder from registry
  !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
  
  ; Read chat history directory from registry
  ReadRegStr $ChatHistoryDir HKLM "Software\KolosalAI" "ChatHistory_Dir"

  MessageBox MB_ICONQUESTION|MB_YESNO "Are you sure you want to uninstall Kolosal AI?" IDNO noRemove
    
  MessageBox MB_ICONQUESTION|MB_YESNO "Would you like to keep your chat history? Click Yes to keep, No to delete." IDYES keepChatHistory
    
  ; Remove chat history if user chose to delete it
  RMDir /r "$ChatHistoryDir"
  
keepChatHistory:
  ; Remove shortcuts
  Delete "$SMPROGRAMS\$StartMenuFolder\Kolosal AI.lnk"
  Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
  RMDir "$SMPROGRAMS\$StartMenuFolder"
  Delete "$DESKTOP\Kolosal AI.lnk"

  ; Remove directories and files
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\models"  ; For uninstallation, the entire models folder is removed.
  Delete "$INSTDIR\*.*"
  RMDir "$INSTDIR"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI"
  DeleteRegKey HKLM "Software\KolosalAI"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KolosalAI"
  DeleteRegKey HKCU "Software\KolosalAI"
  
  ${nsProcess::Unload}
  Goto done

abortUninstall:
  Abort "Uninstallation aborted. Please close Kolosal AI and try again."

noRemove:
done:
SectionEnd