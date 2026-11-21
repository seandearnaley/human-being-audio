Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define NAME "$%NAME%"
!define VERSION "$%VERSION%"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\HumanBeingAudio-$%PRODUCT%"

Name "${NAME}"
OutFile "$%GITHUB_WORKSPACE%\dist\$%WINDOWS_FILE%"
InstallDir "$PROGRAMFILES64\$%PUBLISHER%\${NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "CompanyName" "$%PUBLISHER%"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "$%PUBLISHER%"
!uninstfinalize 'pwsh -NoProfile -NonInteractive -File "$%GITHUB_WORKSPACE%\factory\release\sign-uninstaller.ps1" "%1"' = 0

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "$%GITHUB_WORKSPACE%\design\studio\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "$%GITHUB_WORKSPACE%\design\studio\welcome.bmp"
BrandingText "$%PUBLISHER%"
Caption "${NAME} · $%PUBLISHER%"
!define MUI_WELCOMEPAGE_TITLE "${NAME}"
!define MUI_WELCOMEPAGE_TEXT "$%PUBLISHER%  /  ${VERSION}$\r$\n$\r$\nInstall the VST3 plug-in and standalone app. Close your audio host before installing.$\r$\n$\r$\nThe plug-in goes into the system VST3 folder. The app is available in the $%PUBLISHER% Start menu folder."
!define MUI_FINISHPAGE_LINK "$%PUBLISHER%"
!define MUI_FINISHPAGE_LINK_LOCATION "$%WEBSITE%"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK "${NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  SetRegView 64
  SetShellVarContext all
  SetOutPath "$COMMONFILES64\VST3\${NAME}.vst3"
  File /r "$%ARTIFACTS%\VST3\${NAME}.vst3\*"
  SetOutPath "$INSTDIR"
  File "$%ARTIFACTS%\Standalone\${NAME}.exe"
  File "$%GITHUB_WORKSPACE%\products\$%PRODUCT%\LICENSE"
  File "$%GITHUB_WORKSPACE%\products\$%PRODUCT%\README.md"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\$%PUBLISHER%"
  CreateShortcut "$SMPROGRAMS\$%PUBLISHER%\${NAME}.lnk" "$INSTDIR\${NAME}.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "$%WEBSITE%"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${NAME}.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "$%PUBLISHER%"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  RMDir /r "$COMMONFILES64\VST3\${NAME}.vst3"
  Delete "$INSTDIR\${NAME}.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\$%PUBLISHER%\${NAME}.lnk"
  RMDir "$SMPROGRAMS\$%PUBLISHER%"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
