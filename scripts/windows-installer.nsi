; QuickRecord's Windows installer: a per-user install (no admin, no UAC prompt) of the deployed
; QuickRecord/ folder, the same one that goes into the zip. Built by `just package` on Windows,
; or from any deployed folder with `just windows-installer <dir>` (makensis runs on Linux too).
;
; makensis runs from this directory, so relative paths start here.
; Defines (all required): VERSION (e.g. 0.0.13 or dev), VI_VERSION (four numbers, e.g. 0.0.13.0),
; SRC_DIR (absolute path of the deployed folder), OUT_FILE (absolute path of the setup .exe).

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!include MUI2.nsh
!include FileFunc.nsh

!define APP "QuickRecord"
!define EXE "QuickRecord.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
; The value the app itself writes for "Start QuickRecord when I log in" (src/app/Settings.cpp),
; so its Settings checkbox and this installer agree.
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"

Name "${APP}"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VI_VERSION}"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "LegalCopyright" "${APP}"

!define MUI_ICON "../icons/quickrecord.ico"
!define MUI_UNICON "../icons/quickrecord.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${APP}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE OwnFolder
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Installing (and uninstalling) removes the install folder, so it must be one of our own: a
; folder picked as, say, Documents becomes Documents\QuickRecord.
Function OwnFolder
    StrCpy $0 $INSTDIR "" -11
    StrCmp $0 "${APP}" +2
    StrCpy $INSTDIR "$INSTDIR\${APP}"
FunctionEnd

; Asks a running QuickRecord to quit, then waits until its exe can be deleted (up to 10 s).
!macro QUIT_RUNNING un
Function ${un}QuitRunning
    IfFileExists "$INSTDIR\${EXE}" 0 done
    ExecWait '"$INSTDIR\${EXE}" --quit'
    StrCpy $1 0
    retry:
        ClearErrors
        Delete "$INSTDIR\${EXE}"
        IfErrors 0 done
        IntOp $1 $1 + 1
        IntCmp $1 40 done
        Sleep 250
        Goto retry
    done:
FunctionEnd
!macroend
!insertmacro QUIT_RUNNING ""
!insertmacro QUIT_RUNNING "un."

; The app's own updater runs `setup.exe /S /RELAUNCH`: start the new version once it's in.
; (An interactive install has the Launch checkbox on its last page instead.)
Function .onInstSuccess
    IfSilent 0 done
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/RELAUNCH" $1
    IfErrors done
    Exec '"$INSTDIR\${EXE}"'
    done:
FunctionEnd

Section "-${APP}"
    Call OwnFolder
    Call QuitRunning
    ; An upgrade starts from an empty folder, so nothing of the previous version lingers.
    IfFileExists "$INSTDIR\Uninstall.exe" 0 +2
    RMDir /r "$INSTDIR"

    SetOutPath "$INSTDIR"
    File /r "${SRC_DIR}/*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}"

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APP}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" '"$INSTDIR\${EXE}"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section /o "Desktop shortcut"
    CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}"
SectionEnd

Section /o "Start when I log in"
    WriteRegStr HKCU "${RUN_KEY}" "${APP}" '"$INSTDIR\${EXE}"'
SectionEnd

Section "Uninstall"
    Call un.QuitRunning
    Delete "$SMPROGRAMS\${APP}.lnk"
    Delete "$DESKTOP\${APP}.lnk"
    ; Only if it starts this copy, not a portable one from the zip.
    ReadRegStr $0 HKCU "${RUN_KEY}" "${APP}"
    StrCmp $0 '"$INSTDIR\${EXE}"' 0 +2
    DeleteRegValue HKCU "${RUN_KEY}" "${APP}"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    ; Only our own folder (see OwnFolder). Settings, recordings and screenshots live elsewhere
    ; and stay.
    StrCpy $0 $INSTDIR "" -11
    StrCmp $0 "${APP}" 0 +2
    RMDir /r "$INSTDIR"
SectionEnd
