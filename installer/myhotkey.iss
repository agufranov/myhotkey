; Inno Setup script for MyHotkey.
;
; Installs per machine, and that is deliberate rather than lazy. Registering a
; logon-triggered task needs administrator rights, and so does stopping an
; engine that such a task started with highest privileges. A per-user install
; could do neither, so the uninstaller would leave a running process and locked
; files behind - which is exactly the bug that made this script necessary.
;
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\myhotkey.iss
; Output: build\MyHotkey-Setup-<version>.exe

#define AppName        "MyHotkey"
#define AppVersion     "1.0.0"
#define AppPublisher   "MyHotkey"
#define EngineExe      "myhotkey-engine.exe"
#define UiExe          "myhotkey-ui.exe"
#define TaskName       "MyHotkey"

[Setup]
AppId={{8F3A6C21-49D7-4A1E-9C2B-7E5D0B4F1A93}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir=..\build
OutputBaseFilename=MyHotkey-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#UiExe}
WizardStyle=modern
; The whole product is a couple of hundred kilobytes; say so, because nobody
; expects that from a tool in this category.
AppComments=Переназначение клавиш. Настраивается мышкой, стартует мгновенно.

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.StartAtLogon=Запускать при входе в систему (рекомендуется)
en.StartAtLogon=Start at logon (recommended)
ru.LaunchNow=Запустить MyHotkey
en.LaunchNow=Launch MyHotkey
ru.OpenSettings=Открыть настройки сочетаний
en.OpenSettings=Open shortcut settings

[Tasks]
Name: "startup"; Description: "{cm:StartAtLogon}"; GroupDescription: "{code:AutostartGroup}"

[Files]
Source: "..\build\{#EngineExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\{#UiExe}";     DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\README.md";          DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}";                Filename: "{app}\{#UiExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Run]
; The task is created by schtasks rather than by Inno's own startup handling,
; because Inno can only write the Run key - and the Run key fires at the very
; end of logon, which is the delay this product exists to avoid.
Filename: "{sys}\schtasks.exe"; \
    Parameters: "/create /tn ""{#TaskName}"" /tr ""\""{app}\{#EngineExe}\"""" /sc onlogon /rl highest /f"; \
    Flags: runhidden; Tasks: startup; StatusMsg: "Регистрация автозапуска..."
; Start it now, not at the next logon. A silent install - which is what winget
; and any deployment tool performs - would otherwise report success while
; nothing at all happens until the user next signs in. Starting through the
; task rather than the exe means it comes up exactly as it will every morning:
; same privileges, same parent, so a problem shows up here and not tomorrow.
Filename: "{sys}\schtasks.exe"; Parameters: "/run /tn ""{#TaskName}"""; \
    Flags: runhidden; Tasks: startup
Filename: "{app}\{#EngineExe}"; Flags: runhidden nowait; Tasks: not startup

; Offered, not forced: someone installing by hand usually wants to set up a
; shortcut straight away, but nobody deploying silently wants a window.
Filename: "{app}\{#UiExe}"; Description: "{cm:OpenSettings}"; \
    Flags: postinstall nowait skipifsilent skipifdoesntexist

[UninstallRun]
; Order matters: drop the task first so nothing restarts the engine, then stop
; what is running. Both need the elevation this uninstaller already has.
Filename: "{sys}\schtasks.exe"; Parameters: "/delete /tn ""{#TaskName}"" /f"; \
    Flags: runhidden; RunOnceId: "DropTask"
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im {#EngineExe}"; \
    Flags: runhidden; RunOnceId: "StopEngine"
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im {#UiExe}"; \
    Flags: runhidden; RunOnceId: "StopUi"

[Code]
function AutostartGroup(Param: String): String;
begin
  if ActiveLanguage = 'ru' then
    Result := 'Автозапуск'
  else
    Result := 'Autostart';
end;

{ An engine holding its own exe open would make the copy fail with a confusing
  "file in use". Stop it before any file is touched. }
procedure StopRunning();
var
  code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im {#EngineExe}', '',
       SW_HIDE, ewWaitUntilTerminated, code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im {#UiExe}', '',
       SW_HIDE, ewWaitUntilTerminated, code);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunning();
  Result := '';
end;

procedure CurUninstallStepChanged(CurStep: TUninstallStep);
var
  settings: String;
begin
  if CurStep <> usPostUninstall then
    Exit;

  { Rules are the user's work, not ours. Removing them silently would be rude,
    and keeping them silently would be a surprise on reinstall - so ask.

    Never in a silent uninstall: a suppressed MsgBox returns its default answer,
    which here would be yes, and the user would lose their rules to a question
    they were never shown. Silence means keep. }
  if UninstallSilent() then
    Exit;

  settings := ExpandConstant('{userappdata}\MyHotkey');
  if not DirExists(settings) then
    Exit;

  if MsgBox('Удалить также настроенные правила?' + #13#10 + #13#10 + settings,
            mbConfirmation, MB_YESNO) = IDYES then
    DelTree(settings, True, True, True);
end;
