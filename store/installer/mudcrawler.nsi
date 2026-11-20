; The Mud Crawler - traditional Windows installer (NSIS).
; Installs to Program Files (UAC prompt), optional Start Menu and Desktop shortcuts, uninstaller + Add/Remove Programs entry.
; Build on Linux:  bash store/installer/build.sh      (needs the `nsis` package and a built dist/MudCrawler.exe)
; Silent install:  MudCrawler-Setup-1.0.0.exe /S          (Start Menu shortcut only, default folder)
;                  MudCrawler-Setup-1.0.0.exe /S /DESKTOP=1 /D=C:\Apps\MudCrawler   (also a desktop icon, custom folder; /D must be last)
; Silent uninstall: "C:\Program Files\Sterile\TheMudCrawler\Uninstall.exe" /S

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define APPNAME   "The Mud Crawler"
!define COMPANY   "Sterile Development"
!define VERSION   "1.0.0"
!define VERSION4  "1.0.0.0"
!define EXENAME   "MudCrawler.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MudCrawler"
!define APPKEY    "Software\Sterile Development\The Mud Crawler"
!define WEBSITE   "https://steriledevelopment.github.io/MudCrawler/"

Name "${APPNAME}"
OutFile "..\..\dist\MudCrawler-Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\Sterile\TheMudCrawler"
RequestExecutionLevel admin                  ; asks for UAC approval
BrandingText "${COMPANY}"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION4}"
VIAddVersionKey "ProductName"      "${APPNAME}"
VIAddVersionKey "CompanyName"      "${COMPANY}"
VIAddVersionKey "FileDescription"  "${APPNAME} Setup"
VIAddVersionKey "FileVersion"      "${VERSION4}"
VIAddVersionKey "ProductVersion"   "${VERSION4}"
VIAddVersionKey "LegalCopyright"   "Copyright (C) 2026 ${COMPANY}"

!define MUI_ICON   "..\..\build\icon.ico"
!define MUI_UNICON "..\..\build\icon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP   "sidebar.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "sidebar.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP   "header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "header.bmp"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME}"
!define MUI_COMPONENTSPAGE_SMALLDESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ---- helpers -----------------------------------------------------------------
!macro CheckNotRunning ID
  retry_${ID}:
  FindWindow $0 "McMain" ""
  ${If} $0 != 0
    ${If} ${Silent}
      Abort "${APPNAME} is running. Close it and try again."
    ${EndIf}
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APPNAME} is running.$\r$\n$\r$\nClose it, then click Retry." IDRETRY retry_${ID}
    Abort
  ${EndIf}
!macroend

Function un.onInit
  SetRegView 64
  !insertmacro CheckNotRunning uninit
FunctionEnd

; ---- install -------------------------------------------------------------------
Section "${APPNAME} (required)" SecMain
  SectionIn RO
  SetRegView 64
  SetOutPath "$INSTDIR"
  File "/oname=${EXENAME}" "..\..\dist\MudCrawler.exe"
  File "..\..\LICENSE.txt"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "${APPKEY}" "InstallDir" "$INSTDIR"
  ; Add/Remove Programs entry
  WriteRegStr   HKLM "${UNINSTKEY}" "DisplayName"          "${APPNAME}"
  WriteRegStr   HKLM "${UNINSTKEY}" "DisplayVersion"       "${VERSION}"
  WriteRegStr   HKLM "${UNINSTKEY}" "Publisher"            "${COMPANY}"
  WriteRegStr   HKLM "${UNINSTKEY}" "DisplayIcon"          "$INSTDIR\${EXENAME}"
  WriteRegStr   HKLM "${UNINSTKEY}" "InstallLocation"      "$INSTDIR"
  WriteRegStr   HKLM "${UNINSTKEY}" "UninstallString"      '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr   HKLM "${UNINSTKEY}" "URLInfoAbout"         "${WEBSITE}"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" $0
SectionEnd

Section "Start Menu shortcut" SecStart
  SetShellVarContext all
  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\${EXENAME}" 0
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  SetShellVarContext all
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\${EXENAME}" 0
SectionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${APPNAME} needs 64-bit Windows 10 or newer."
    Abort
  ${EndIf}
  SetRegView 64
  ; upgrade in place: reuse the folder of an earlier install (unless /D= was given)
  ${If} $INSTDIR == "$PROGRAMFILES64\Sterile\TheMudCrawler"
    ReadRegStr $0 HKLM "${APPKEY}" "InstallDir"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${EndIf}
  !insertmacro CheckNotRunning init
  ; silent switch: /DESKTOP=1 also creates the desktop shortcut
  ${GetParameters} $1
  ${GetOptions} $1 "/DESKTOP=" $2
  ${If} $2 == "1"
    SectionSetFlags ${SecDesktop} ${SF_SELECTED}
  ${EndIf}
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}    "The program itself (required)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart}   "Add ${APPNAME} to the Start menu."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Put a ${APPNAME} icon on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---- uninstall -----------------------------------------------------------------
Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$INSTDIR\${EXENAME}"
  Delete "$INSTDIR\LICENSE.txt"
  Delete /REBOOTOK "$INSTDIR\Uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"
  RMDir "$PROGRAMFILES64\Sterile"      ; only removed when empty
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "${APPKEY}"
  DeleteRegKey /ifempty HKLM "Software\Sterile Development"

  ; the user's own data (profiles, saved logins, rules, maps, logs) is only removed if they say so
  SetShellVarContext current
  ${IfNot} ${Silent}
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your ${APPNAME} profiles, saved logins, triggers, maps and logs?$\r$\n$\r$\n$APPDATA\TheMudCrawler$\r$\n$\r$\nChoose No to keep them (for example, if you plan to reinstall)." IDNO keepdata
    RMDir /r "$APPDATA\TheMudCrawler"
    keepdata:
  ${EndIf}
SectionEnd
