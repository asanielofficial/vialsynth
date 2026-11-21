; Inno Setup script for the VialSynth installer (VST3), built by .github/workflows/build.yml.
; Build: ISCC /DSourceFile=C:\path\to\VialSynth.vst3 /DAppVersion=1.0 installer\windows\VialSynth.iss
#ifndef AppVersion
  #define AppVersion "1.0"
#endif
#ifndef SourceFile
  #define SourceFile "..\..\dist\VialSynth.vst3"
#endif

[Setup]
AppId={{B2D7F1E4-6C3A-4F58-8E9B-1D4A7C5E2F60}
AppName=VialSynth
AppVersion={#AppVersion}
AppPublisher=VialSynth
DefaultDirName={autopf}\VialSynth
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=VialSynth-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=VialSynth

[Files]
Source: "{#SourceFile}"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion

[Messages]
FinishedLabel=VialSynth was installed to C:\Program Files\Common Files\VST3. Rescan plugins in your DAW.
