# Builds the open-source copy of the project: a separate Git repository with the code, config and tools only, plus zip
# packs of the large assets (Content, SourceArt) to upload to Google Drive. The NVIDIA plugins are left out of both.
#
# Everything is taken from HEAD, so commit first. Packs are written to -PackDir and their size / sha256 are stored in
# Tools/AssetPacks.json (commit that afterwards). Check asset redistribution rights before uploading packs to Google Drive
# Publish future download links in README.md and the manifest's "gdrive" / "url" fields. Nothing is pushed
param(
    [string]$PublicDir = "E:\CampusOps-public",
    [string]$PackDir = "E:\CampusOps-packs",
    [switch]$SkipPacks,
    [switch]$AllowDirty,
    [string]$Message
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ManifestPath = Join-Path $Repo "Tools\AssetPacks.json"
$Rev = (git -C $Repo rev-parse --short HEAD).Trim()
$Manifest = Get-Content -Raw $ManifestPath | ConvertFrom-Json
$Excluded = @("Content", "SourceArt", "Plugins")

function Invoke-Git {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed" }
}

if (-not $SkipPacks) {
    New-Item -ItemType Directory -Force -Path $PackDir | Out-Null
    foreach ($Pack in $Manifest.packs) {
        $Dirty = git -C $Repo status --porcelain --untracked-files=no -- $Pack.paths
        if ($Dirty -and -not $AllowDirty) {
            throw "$($Pack.paths -join ', ') has uncommitted changes ($(@($Dirty).Count) files); commit them or pass -AllowDirty"
        }
        $Files = @(git -C $Repo -c core.quotepath=off ls-tree -r --name-only HEAD -- $Pack.paths)
        foreach ($F in $Files) {
            $Item = Get-Item -LiteralPath (Join-Path $Repo $F)
            if ($Item.Length -lt 200 -and (Get-Content -LiteralPath $Item.FullName -TotalCount 1) -like "version https://git-lfs*") {
                throw "$F is an LFS pointer; run 'git lfs pull' first"
            }
        }
        $List = Join-Path $env:TEMP "campusops-$($Pack.name).txt"
        [IO.File]::WriteAllLines($List, $Files, (New-Object Text.UTF8Encoding $false))
        $Zip = Join-Path $PackDir $Pack.file
        Remove-Item -Force -ErrorAction SilentlyContinue $Zip
        Write-Host "packing $($Pack.name): $($Files.Count) files"
        & tar.exe -a -c -f $Zip -C $Repo -T $List
        if ($LASTEXITCODE -ne 0) { throw "tar failed for $($Pack.name)" }
        Remove-Item -Force $List
        $Pack.size = (Get-Item $Zip).Length
        $Pack.sha256 = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
        Write-Host ("  {0} {1:N0} MB" -f $Zip, ($Pack.size / 1MB))
    }
    $Manifest.commit = $Rev
    [IO.File]::WriteAllText($ManifestPath, ($Manifest | ConvertTo-Json -Depth 5) + "`n", (New-Object Text.UTF8Encoding $false))
}

# The public directory is wiped (except .git) on every run, so refuse anything that is not a previous export.
if (Test-Path $PublicDir) {
    $Existing = @(Get-ChildItem -Force -LiteralPath $PublicDir)
    $IsExport = (Test-Path (Join-Path $PublicDir ".git")) -and (Test-Path (Join-Path $PublicDir "CampusOps.uproject"))
    if ($Existing.Count -gt 0 -and -not $IsExport) { throw "$PublicDir is not empty and is not a previous export" }
    $Existing | Where-Object { $_.Name -ne ".git" } | Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $PublicDir | Out-Null
}
if (-not (Test-Path (Join-Path $PublicDir ".git"))) { Invoke-Git -C $PublicDir init -q -b main }

$Tar = Join-Path $env:TEMP "campusops-public.tar"
$Spec = @(".", ":(exclude).gitattributes") + ($Excluded | ForEach-Object { ":(exclude)$_" })
Invoke-Git -C $Repo archive -o $Tar HEAD -- @Spec
& tar.exe -x -f $Tar -C $PublicDir
if ($LASTEXITCODE -ne 0) { throw "extracting the export failed" }
Remove-Item -Force $Tar

# The NVIDIA plugins are optional in the public copy only, so it opens and builds without them. Here they must not be:
# a packaged (monolithic) game drops an optional plugin whose modules the target did not compile, and UBT only compiles
# the ones the game module links, so DLSS, frame generation and Reflex would all read as unsupported.
$Uproject = Join-Path $PublicDir "CampusOps.uproject"
$Text = [IO.File]::ReadAllText($Uproject)
foreach ($Name in @("DLSS", "StreamlineCore", "StreamlineDLSSG", "StreamlineReflex", "StreamlineNGXCommon")) {
    $Text = [regex]::Replace($Text, "(""Name"": ""$Name"",\r?\n(\t+)""Enabled"": true,)", "`$1`n`$2""Optional"": true,")
}
[IO.File]::WriteAllText($Uproject, $Text, (New-Object Text.UTF8Encoding $false))

# The manifest may have just been rewritten (pack hashes, Drive ids), so use the working copy rather than HEAD's.
Copy-Item -Force $ManifestPath (Join-Path $PublicDir "Tools\AssetPacks.json")
$Ignore = "`n# Large assets are not in this repository: Tools/DownloadAssets.ps1 fetches them, NVIDIA plugins go in Plugins/.`n" +
    (($Excluded | ForEach-Object { "/$_/" }) -join "`n") + "`n"
Add-Content -NoNewline -Path (Join-Path $PublicDir ".gitignore") -Value $Ignore

Invoke-Git -C $PublicDir add -A
git -C $PublicDir diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "public repository already up to date"
} else {
    if (-not $Message) { $Message = "Sync to $Rev" }
    Invoke-Git -C $PublicDir commit -q -m $Message
    Write-Host "committed to $PublicDir`: $Message"
}
