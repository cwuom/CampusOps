# CampusOps (光華行動) — agent guide

UE 5.8 project, almost everything is generated from C++: maps (`Source/CampusOps/Map/*Layout.cpp`), weapons, materials
and meshes (`Source/CampusOpsEditor/*`, written by the bootstrap commandlet). Talk to the user in Chinese.

## Git: commit automatically

- **Commit after every completed task or fix, without being asked.** One commit per logical change; do not let work
  pile up uncommitted. Commit before starting a risky change too, so it can be rolled back.
- Message: short English summary line, then a few lines of what changed and why. Example:
  `Knife: light slash / heavy stab` + body lines.
- Stage source, config, tools and the regenerated assets that belong to the change (`Content/**` .uasset / .umap are
  in Git LFS, see `.gitattributes`). Never commit `Binaries/`, `Intermediate/`, `Saved/`, `Build/`, `DerivedDataCache/`
  (ignored). Check `git status` before committing; do not commit stray files.
- No remote is configured: commit locally, never push unless the user asks.
- Never rewrite history (`reset --hard`, `rebase`, force anything) unless the user asks.

## Build / regenerate / test

```powershell
# Editor target (C++)
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" CampusOpsEditor Win64 Development "-Project=E:\game-project\CampusOps.uproject" -WaitMutex -NoHotReloadFromIDE
# Bootstrap (materials, maps; add -skipmaterials / -skipmaps / -skipimport to narrow it)
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\game-project\CampusOps.uproject" -run=CampusBootstrap -skipaudio -skipimport -skipfonts -skipterrain -skipweapons -unattended -nopause -nosplash
# Knife, hedge and turf meshes + HUD/optic materials only
... -run=CampusBootstrap -knifeonly ...
# Package
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=E:\game-project\CampusOps.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=E:\game-project\Build" -utf8output -unattended
```

- Verify visually: run `UnrealEditor.exe CampusOps.uproject "<map URL>" -game -windowed -ResX=1280 -ResY=720
  -CampusScriptFile=E:/game-project/Tools/Shots_<name>.txt`; screenshots land in `Saved/AutoShots`, contact sheets via
  `Tools/sheet.py` (Python: `E:\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\python_embeded\python.exe`).
- Map URLs: `/Game/Campus/Maps/Map_Campus` or `Map_Hotel` `?game=TDM?Bots=0?Weather=<0..5>?Time=<0..4>`
  (weather 3 rain, 4 storm, 5 fog).
- GPU cost: `Tools/Shots_PerfLow*.txt` (Low preset with TSR, `profilegpu` in the log). Low targets 80 fps on a GTX 1650.
- After packaging, smoke-test `Build\Windows\CampusOps.exe` with a map URL and bots, check its log for crashes.
