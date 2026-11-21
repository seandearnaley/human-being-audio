Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define NAME "$%NAME%"
!define VERSION "$%VERSION%"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\HumanBeingAudio-$%PRODUCT%"

Name "${NAME}"
OutFile "$%GITHUB_WORKSPACE%\dist\$%WINDOWS_FILE%"
InstallDir "$PROGRAMFILES64\Human Being Audio\${NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "CompanyName" "Human Being Audio"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Human Being Audio"
!uninstfinalize 'pwsh -NoProfile -NonInteractive -File "$%GITHUB_WORKSPACE%\factory\release\sign-uninstaller.ps1" "%1"' = 0

!insertmacro MUI_PAGE_INSTFILES
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
  CreateDirectory "$SMPROGRAMS\Human Being Audio"
  CreateShortcut "$SMPROGRAMS\Human Being Audio\${NAME}.lnk" "$INSTDIR\${NAME}.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Human Being Audio"
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
  Delete "$SMPROGRAMS\Human Being Audio\${NAME}.lnk"
  RMDir "$SMPROGRAMS\Human Being Audio"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
