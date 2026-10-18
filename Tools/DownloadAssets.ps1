# Verifies the asset packs listed in Tools/AssetPacks.json and unpacks them into the project. By default only the packs
# needed to open and play the project (Content); -All adds SourceArt, which is only needed to re-run the CampusBootstrap
# import. Asset packs have not been uploaded yet; future Google Drive links will be published in README.md
# Download the zips into one folder and pass it with -From, or use the manifest's future "gdrive" id or "url"
#
#   powershell -ExecutionPolicy Bypass -File Tools\DownloadAssets.ps1 -From D:\Downloads
#   powershell -ExecutionPolicy Bypass -File Tools\DownloadAssets.ps1 -From D:\Downloads -All
#
# -Clean deletes the pack's folders before unpacking (use it when updating to a newer pack), -Force unpacks again even
# when the pack is already installed.
param(
    [switch]$All,
    [string]$From,
    [switch]$Clean,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Manifest = Get-Content -Raw (Join-Path $PSScriptRoot "AssetPacks.json") | ConvertFrom-Json
$Cache = Join-Path $Repo "Saved\AssetPacks"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

function Test-Zip([string]$Path) {
    $Stream = [IO.File]::OpenRead($Path)
    try { return $Stream.ReadByte() -eq 0x50 -and $Stream.ReadByte() -eq 0x4B } finally { $Stream.Close() }
}

function Get-Pack($Pack, [string]$Zip) {
    $Urls = @()
    if ($Pack.gdrive) { $Urls += "https://drive.usercontent.google.com/download?id=$($Pack.gdrive)&export=download&confirm=t" }
    if ($Pack.url) { $Urls += $Pack.url }
    if ($Urls.Count -eq 0) { throw "no download link is configured for $($Pack.file); asset packs have not been uploaded yet. Check README.md for future Google Drive links, then download the pack and use -From <folder>" }
    foreach ($Url in $Urls) {
        Write-Host ("downloading {0} ({1:N0} MB) from {2}" -f $Pack.file, ($Pack.size / 1MB), $Url)
        & curl.exe -L --fail --retry 3 -# -o "$Zip.part" $Url
        if ($LASTEXITCODE -eq 0 -and (Test-Zip "$Zip.part")) {
            Move-Item -Force "$Zip.part" $Zip
            return
        }
        # Google Drive answers with an HTML page when the file is not shared publicly or its download quota is used up.
        Write-Warning "download from $Url failed or returned a web page instead of the zip"
        Remove-Item -Force -ErrorAction SilentlyContinue "$Zip.part"
    }
    throw "could not download $($Pack.file); check the download link in README.md, download it in a browser and use -From <folder>"
}

foreach ($Pack in $Manifest.packs) {
    if (-not $Pack.required -and -not $All) { continue }
    $Stamp = Join-Path $Cache "$($Pack.name).sha256"
    $Present = @($Pack.paths | Where-Object { -not (Test-Path (Join-Path $Repo $_)) }).Count -eq 0
    if (-not $Force -and $Present -and $Pack.sha256 -and (Test-Path $Stamp) -and (Get-Content $Stamp) -eq $Pack.sha256) {
        Write-Host "$($Pack.name) is up to date"
        continue
    }

    $Zip = if ($From) { Join-Path $From $Pack.file } else { Join-Path $Cache $Pack.file }
    if (-not (Test-Path $Zip)) {
        if ($From) { throw "$Zip not found" }
        Get-Pack $Pack $Zip
    }
    if ($Pack.sha256) {
        Write-Host "checking $($Pack.file)"
        $Hash = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLowerInvariant()
        if ($Hash -ne $Pack.sha256) {
            if (-not $From) { Remove-Item -Force $Zip }
            throw "$($Pack.file) does not match this version of the code (sha256 $Hash, expected $($Pack.sha256))"
        }
    }

    if ($Clean) {
        foreach ($P in $Pack.paths) { Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Repo $P) }
    }
    Write-Host "unpacking $($Pack.file)"
    & tar.exe -x -f $Zip -C $Repo
    if ($LASTEXITCODE -ne 0) { throw "unpacking $($Pack.file) failed" }
    Set-Content -Path $Stamp -Value $Pack.sha256
    Write-Host "$($Pack.name) installed"
}
