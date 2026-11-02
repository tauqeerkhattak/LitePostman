; LitePostman NSIS Installer Script
Unicode True

!include "MUI2.nsh"
!include "FileFunc.nsh"

Name "LitePostman"
OutFile "LitePostman-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\LitePostman"
InstallDirRegKey HKCU "Software\LitePostman" "InstallDir"
RequestExecutionLevel user

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Installer Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\LitePostman.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch LitePostman"
!insertmacro MUI_PAGE_FINISH

; Uninstaller Pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

Section "LitePostman Application" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "build\LitePostman.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Start Menu Shortcuts
  CreateDirectory "$SMPROGRAMS\LitePostman"
  CreateShortcut "$SMPROGRAMS\LitePostman\LitePostman.lnk" "$INSTDIR\LitePostman.exe"
  CreateShortcut "$SMPROGRAMS\LitePostman\Uninstall LitePostman.lnk" "$INSTDIR\Uninstall.exe"
  
  ; Desktop Shortcut
  CreateShortcut "$DESKTOP\LitePostman.lnk" "$INSTDIR\LitePostman.exe"

  ; Add/Remove Programs Registry Entries
  WriteRegStr HKCU "Software\LitePostman" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman" "DisplayName" "LitePostman"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman" "DisplayIcon" '"$INSTDIR\LitePostman.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman" "Publisher" "LitePostman"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman" "DisplayVersion" "1.0.0"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\LitePostman.lnk"
  Delete "$SMPROGRAMS\LitePostman\LitePostman.lnk"
  Delete "$SMPROGRAMS\LitePostman\Uninstall LitePostman.lnk"
  RMDir "$SMPROGRAMS\LitePostman"

  Delete "$INSTDIR\LitePostman.exe"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LitePostman"
  DeleteRegKey HKCU "Software\LitePostman"
SectionEnd
