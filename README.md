# CampusOps

A first-person shooter built with Unreal Engine 5.8, set across a school campus and a hotel.
The project is developed primarily in C++, including combat, AI, weapon customization,
first-person animation and UI. Map layouts, materials and some meshes are built by the
project's asset-generation tools.

> **Large binary files have not been uploaded yet. They are planned for distribution through
> Google Drive, and download links will be added to this README when available.**
>
> This repository contains source code, configuration and development tools. It does not include
> `Content/`, `SourceArt/`, NVIDIA plugins or a ready-to-play game package. Cloning the source
> alone does not provide the complete playable project.

## Features

- Two main combat maps: a school campus and a hotel
- Team Deathmatch (TDM) and round-based Gunfight modes, with AI team filling
- Weapon and attachment customization, reloading, melee combat, throwables and breakable glass
- Sprinting, crouching, sliding and mantling with corresponding first-person animations
- Configurable weather, time of day, graphics settings and key bindings
- C++ asset-generation commands, automated regression tests and screenshot review scripts

The project is under active development. Features and assets may change.

## Source Code and Assets

```powershell
git clone https://github.com/cwuom/CampusOps.git
cd CampusOps
```

The following asset packs are planned. **Neither pack has a published download link yet.**

| Asset pack | Contents | Purpose |
| --- | --- | --- |
| `CampusOps-Content.zip` | Unreal maps, materials, models, animations, audio and other assets | Required to open and run the complete project |
| `CampusOps-SourceArt.zip` | Source textures, models, fonts and sound recordings | Used when reimporting or generating assets |

Once the packs are published, download them into the same folder and run the commands below.
The script extracts them into the project root. Add `-All` to install the source art as well.

```powershell
# Run these commands after the asset packs are published
powershell -ExecutionPolicy Bypass -File Tools\DownloadAssets.ps1 -From "D:\Downloads"
powershell -ExecutionPolicy Bypass -File Tools\DownloadAssets.ps1 -From "D:\Downloads" -All
```

`Tools/AssetPacks.json` is currently a placeholder. Download URLs, the matching source revision
and SHA-256 hashes will be filled in when the packs are released. The download script verifies
a pack's hash only when its manifest entry includes a SHA-256 value.

Use `-Clean` when replacing an older pack if you need removed files to be deleted as well.
This option deletes the corresponding local asset folders before extraction, so back up any
local changes first.

Hosting an asset on Google Drive does not change its license. The contents and third-party
notices of future asset packs will be reviewed before release.

## Development Requirements

- Windows 10 or 11
- Unreal Engine **5.8**, obtained separately from Epic under its license agreement
- **Visual Studio 2026**, with the "Game development with C++" workload, an MSVC toolchain
  supported by UE 5.8 and the Windows SDK
- PowerShell; some asset and screenshot tools also require Python and their script-specific dependencies

Run the following commands from the project root and adjust `$EngineRoot` to match your
installation. **Running the complete game and packaging it require the asset packs.**

### Build the Editor Target

```powershell
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" CampusOpsEditor Win64 Development "-Project=$PWD\CampusOps.uproject" -WaitMutex -NoHotReloadFromIDE
```

After building, open `CampusOps.uproject` in Unreal Editor.

### Start a Match

```powershell
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" "$PWD\CampusOps.uproject" "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=1" -game -windowed -ResX=1280 -ResY=720
```

The main maps are `/Game/Campus/Maps/Map_Campus` and `/Game/Campus/Maps/Map_Hotel`.

| URL option | Meaning |
| --- | --- |
| `game=TDM` / `game=Gunfight` | Team Deathmatch / Gunfight |
| `Bots=0` / `Bots=1` | Disable / enable AI team filling |
| `Weather=0..5` | Weather preset; 3 is rain, 4 is storm and 5 is fog |
| `Time=0..4` | Time-of-day preset |

### Run Automated Tests

```powershell
powershell -ExecutionPolicy Bypass -File Tools\TestExperience.ps1 -EngineRoot "C:\Program Files\Epic Games\UE_5.8"
```

Test reports and logs are written to `Saved/ExperienceTests/` and `Saved/Logs/`.
Screenshot review scripts are in `Tools/Shots_*.txt`. Pass a script to the game process with
`-CampusScriptFile=<absolute-script-path>`; screenshots are saved to `Saved/AutoShots/`.

### Package for Windows

```powershell
$EngineRoot = "C:\Program Files\Epic Games\UE_5.8"
& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$PWD\CampusOps.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$PWD\Build" -utf8output -unattended
```

### Optional NVIDIA Features

The source export marks NVIDIA plugins as optional and uses the `CAMPUS_WITH_DLSS` compile-time
flag to handle their absence. To enable DLSS, frame generation and Reflex, obtain plugins
compatible with UE 5.8 from [NVIDIA](https://developer.nvidia.com/rtx/dlss/get-started).
Follow their installation instructions and place `DLSS`, `StreamlineCore`, `StreamlineDLSSG`,
`StreamlineReflex` and `StreamlineNGXCommon` in `Plugins/`, then rebuild. These plugins remain
subject to NVIDIA's own license terms.

## Code Layout

| Path | Responsibility |
| --- | --- |
| `Source/CampusOps/Game/` | Match rules, game state, environment and graphics settings |
| `Source/CampusOps/Player/` | Input, movement, camera, HUD and animation |
| `Source/CampusOps/Weapons/` | Weapons, throwables and combat effects |
| `Source/CampusOps/AI/` | AI controller logic |
| `Source/CampusOps/Map/` | Map layouts, interactions and environment objects |
| `Source/CampusOps/UI/`, `Source/CampusOps/Menu/` | Menus, loadouts and settings interfaces |
| `Source/CampusOps/Tests/` | Unreal automation regression tests |
| `Source/CampusOpsEditor/` | Project-specific asset import, generation and Bootstrap commands |
| `Config/` | Unreal project configuration |
| `Tools/` | Asset fetching, source export, testing and screenshot utilities |

## License and Credits

Original code, configuration, scripts and documentation that the project authors are entitled
to license are provided under the [MIT License](LICENSE). This does not cover Unreal Engine,
Epic examples and template content, third-party plugins, fonts, sound recordings, models,
textures or other third-party materials. They retain their respective licenses and notices.
See [Third-Party Notices](THIRD_PARTY_NOTICES.md) for details.

- Textures and some models: [Poly Haven](https://polyhaven.com/license), CC0
- Sound recordings: [Freesound](https://freesound.org/); the project download script selects
  CC0 recordings and stores their provenance alongside the audio source files
- Barlow, Rajdhani, Black Ops One and Noto CJK fonts: SIL OFL 1.1, with license files
  included alongside the font source files
- Unreal Engine and Epic-provided content: the applicable
  [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal) or content license

Epic lists MIT as a compatible license in its [official FAQ](https://www.unrealengine.com/ue-on-github).
MIT does not replace Epic's or any third party's licensing requirements. Using or distributing
a game that includes Unreal Engine remains subject to the applicable engine and asset terms.
