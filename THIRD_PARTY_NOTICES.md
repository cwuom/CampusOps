# Third-party licensing scope

The root [MIT License](LICENSE) applies only to the original CampusOps code,
configuration, scripts and documentation that the project authors are entitled
to license. It grants no rights to Unreal Engine or other third-party material.
Existing third-party notices and license terms must be retained.

The source export excludes `Content/`, `SourceArt/`, `Plugins/`, engine files,
compiled binaries and build caches. Future asset downloads are separate from
the MIT-licensed source and retain their respective licenses.

| Component | License and source | Notes |
| --- | --- | --- |
| Unreal Engine | [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal) | Obtain the engine separately from Epic. The project MIT license does not relicense Engine Code, Examples, Starter Content or their modifications. |
| Epic templates and other Epic content | Unreal Engine EULA or the applicable [Epic Content License Agreement](https://www.unrealengine.com/eula/content) / acquisition terms | The applicable terms depend on the asset's origin. Do not assume all Epic or Fab assets may be redistributed as editable source assets. |
| NVIDIA DLSS / Streamline plugins | NVIDIA's bundled license terms; [official downloads](https://developer.nvidia.com/rtx/dlss/get-started) | Excluded from the source export and asset packs produced by `Tools/ExportPublic.ps1`. Obtain compatible plugins separately. |
| Poly Haven textures and models | [CC0](https://polyhaven.com/license) | Fetch helper: `Tools/FetchAssets.ps1`. Asset notice: `SourceArt/PolyHaven/LICENSE.txt` when the source pack is installed. |
| Freesound recordings | CC0 1.0 for the recordings selected by `Tools/fetch_audio.py` | Item URLs and provenance are recorded in `SourceArt/Audio/Sources/LICENSES.txt` and `sources.json` when installed. This is not a license claim about all Freesound content. |
| Barlow / Barlow Condensed | SIL Open Font License 1.1 | `SourceArt/Fonts/OFL-Barlow.txt` when installed |
| Rajdhani | SIL Open Font License 1.1 | `SourceArt/Fonts/OFL-Rajdhani.txt` when installed |
| Black Ops One | SIL Open Font License 1.1 | `SourceArt/Fonts/OFL-BlackOpsOne.txt` when installed |
| Noto Sans / Serif CJK | SIL Open Font License 1.1 | `SourceArt/Fonts/OFL-NotoCJK.txt` when installed |

## Distribution boundaries

MIT is a permissive license for the project's original work. Epic lists MIT as
an acceptable non-copyleft license in its [official FAQ](https://www.unrealengine.com/ue-on-github).
MIT does not require Unreal Engine to be relicensed and does not waive Epic's
distribution, attribution or other obligations. See Unreal Engine EULA sections
5, 6(c), 6(d) and 7. In particular, section 5(b) permits distribution of Examples
(content from the engine's Samples and Templates folders), including modifications,
in source or object code to third parties; that permission does not relicense them
under MIT.

`Source/CampusOpsEditor/` contains this game's asset-generation code and depends
on Epic's editor modules. The source export does not bundle Unreal Engine editor
modules or precompiled editor tools. Any distribution that includes Engine Code or
qualifies as Engine Tools must follow Epic's distribution rules; check that scope
before a public release or separate tooling distribution.

Before releasing a Google Drive pack, verify the provenance and redistribution
rights of every included asset, retain all required notices and font licenses,
and publish a matching version and SHA-256 in `Tools/AssetPacks.json`. Moving an
asset to Google Drive does not change its license or remove distribution limits.
