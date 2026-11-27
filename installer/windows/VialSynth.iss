; Inno Setup script for the VialSynth installer (VST3 and CLAP as separate components), built by .github/workflows/build.yml.
; Build: ISCC /DSourceFile=C:\path\to\VialSynth.vst3 /DClapFile=C:\path\to\VialSynth.clap /DAppVersion=1.2 installer\windows\VialSynth.iss
#ifndef AppVersion
  #define AppVersion "1.2"
#endif
#ifndef ClapFile
  #define ClapFile "..\..\dist-clap\VialSynth.clap"
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

[Types]
Name: "full"; Description: "Full installation (VST3 and CLAP)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (C:\Program Files\Common Files\CLAP)"; Types: full custom

[Files]
Source: "{#SourceFile}"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion; Components: vst3
Source: "{#ClapFile}"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap

[Messages]
FinishedLabel=VialSynth was installed to the plugin folders you selected. Rescan plugins in your DAW.
