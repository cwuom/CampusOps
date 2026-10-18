# Downloads the open-licensed (SIL OFL 1.1) fonts used by the UI and HUD into SourceArt/Fonts.
# Re-running is safe: files that already exist are skipped.
param([string]$Root = (Join-Path $PSScriptRoot "..\SourceArt\Fonts"))

$ErrorActionPreference = "Stop"
$GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"
$Noto = "https://raw.githubusercontent.com/notofonts/noto-cjk/main"
$Files = [ordered]@{
    "BarlowCondensed-Medium.ttf"    = "$GF/barlowcondensed/BarlowCondensed-Medium.ttf"
    "BarlowCondensed-SemiBold.ttf"  = "$GF/barlowcondensed/BarlowCondensed-SemiBold.ttf"
    "BarlowCondensed-Bold.ttf"      = "$GF/barlowcondensed/BarlowCondensed-Bold.ttf"
    "BarlowCondensed-ExtraBold.ttf" = "$GF/barlowcondensed/BarlowCondensed-ExtraBold.ttf"
    "Barlow-Regular.ttf"            = "$GF/barlow/Barlow-Regular.ttf"
    "Barlow-Medium.ttf"             = "$GF/barlow/Barlow-Medium.ttf"
    "Barlow-SemiBold.ttf"           = "$GF/barlow/Barlow-SemiBold.ttf"
    "Barlow-Bold.ttf"               = "$GF/barlow/Barlow-Bold.ttf"
    "Rajdhani-Medium.ttf"           = "$GF/rajdhani/Rajdhani-Medium.ttf"
    "Rajdhani-SemiBold.ttf"         = "$GF/rajdhani/Rajdhani-SemiBold.ttf"
    "Rajdhani-Bold.ttf"             = "$GF/rajdhani/Rajdhani-Bold.ttf"
    "BlackOpsOne-Regular.ttf"       = "$GF/blackopsone/BlackOpsOne-Regular.ttf"
    "NotoSansSC-Regular.otf"        = "$Noto/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf"
    "NotoSansSC-Medium.otf"         = "$Noto/Sans/SubsetOTF/SC/NotoSansSC-Medium.otf"
    "NotoSansSC-Bold.otf"           = "$Noto/Sans/SubsetOTF/SC/NotoSansSC-Bold.otf"
    "NotoSansSC-Black.otf"          = "$Noto/Sans/SubsetOTF/SC/NotoSansSC-Black.otf"
    "NotoSerifTC-Black.otf"         = "$Noto/Serif/SubsetOTF/TC/NotoSerifTC-Black.otf"
    "OFL-Barlow.txt"                = "$GF/barlow/OFL.txt"
    "OFL-Rajdhani.txt"              = "$GF/rajdhani/OFL.txt"
    "OFL-BlackOpsOne.txt"           = "$GF/blackopsone/OFL.txt"
    "OFL-NotoCJK.txt"               = "$Noto/Sans/LICENSE"
}

New-Item -ItemType Directory -Force -Path $Root | Out-Null
foreach ($name in $Files.Keys) {
    $path = Join-Path $Root $name
    if (Test-Path $path) { continue }
    & curl.exe -s -L --retry 3 -o "$path.part" $Files[$name]
    if ($LASTEXITCODE -ne 0) { throw "download failed: $($Files[$name])" }
    Move-Item -Force "$path.part" $path
    Write-Host ("{0} ({1:N1} MB)" -f $name, ((Get-Item $path).Length / 1MB))
}
Write-Host "fonts ready in $Root"
