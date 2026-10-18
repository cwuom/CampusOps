# Downloads the CC0 photoscanned textures and models used by the campus map from Poly Haven (https://polyhaven.com, CC0).
# Re-running is safe: files that already exist are skipped. Output: SourceArt/PolyHaven/{Textures,Models}/<id>/
param(
    [string]$Root = (Join-Path $PSScriptRoot "..\SourceArt\PolyHaven"),
    [int]$ModelSizeLimitMB = 200
)

$ErrorActionPreference = "Stop"
$Textures = @(
    "beige_wall_001", "white_plaster_02", "concrete_floor_worn_001", "concrete_wall_008", "terrazzo_tiles",
    "rectangular_facade_tiles", "square_tiled_wall", "red_brick_pavers", "square_concrete_pavers", "asphalt_02",
    "rubberized_track", "concrete_floor_painted", "leafy_grass", "park_dirt", "wood_table_worn", "green_metal_rust",
    "metal_plate", "corrugated_iron_02", "pebble_embedded_concrete", "clay_roof_tiles_02", "rubber_tiles",
    "denim_fabric", "fabric_leather_01", "rusty_metal_02"
)
$Models = @(
    # vegetation
    "island_tree_01", "island_tree_02", "island_tree_03", "shrub_01", "shrub_02", "shrub_04", "pachira_aquatica_01",
    "potted_plant_01", "potted_plant_02", "grass_medium_01", "periwinkle_plant", "fern_02", "grass_bermuda_01",
    "shrub_03", "calathea_orbifolia_01",
    # school interior
    "SchoolDesk_01", "SchoolChair_01", "ceiling_fan", "mounted_fluorescent_lights", "wall_clock", "fire_alarm",
    "korean_fire_extinguisher_01", "metal_office_desk", "steel_frame_shelves_01", "plastic_monobloc_chair_01",
    "WetFloorSign_01", "metal_trash_can", "trashbag",
    # exterior / street
    "exterior_aircon_unit", "rollershutter_door", "rollershutter_window_01", "street_lamp_01", "utility_box_01",
    "utility_box_02", "security_camera_01", "fire_hydrant", "water_manhole_cover", "concrete_road_barrier",
    "concrete_road_barrier_02", "modular_electricity_poles", "korean_public_payphone_01", "painted_wooden_bench",
    # clutter / cover
    "cardboard_box_01", "plastic_crate_01", "plastic_crate_03", "wooden_crate_01", "Barrel_01", "old_tyre",
    "cement_bag", "portable_generator", "hand_truck", "propane_tank", "wooden_military_crate",
    # weapons
    "service_pistol"
)

function Get-File([string]$Url, [string]$Path) {
    if (Test-Path $Path) { return 0 }
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null
    & curl.exe -s -L --retry 3 -o "$Path.part" $Url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $Url" }
    Move-Item -Force "$Path.part" $Path
    return (Get-Item $Path).Length
}

$Downloaded = 0
foreach ($id in $Textures) {
    $f = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -TimeoutSec 60
    $dir = Join-Path $Root "Textures\$id"
    $maps = [ordered]@{ "diff" = $f.Diffuse; "nor_dx" = $f.nor_dx; "arm" = $f.arm }
    if (-not $f.arm) { $maps.Remove("arm"); $maps["rough"] = $f.Rough; $maps["ao"] = $f.AO }
    foreach ($k in $maps.Keys) {
        $entry = $maps[$k]
        if (-not $entry) { Write-Warning "$id has no $k map"; continue }
        $Downloaded += Get-File $entry.'2k'.jpg.url (Join-Path $dir "${id}_$k.jpg")
    }
    Write-Host "texture $id"
}

foreach ($id in $Models) {
    try { $f = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/$id" -TimeoutSec 60 } catch { Write-Warning "$id lookup failed: $($_.Exception.Message)"; continue }
    $pick = $null
    # Textures must be at least 2K; 1K is only a fallback for models that do not ship a 2K variant.
    foreach ($res in @("2k", "1k")) {
        $g = $f.gltf.$res.gltf
        if (-not $g) { continue }
        $size = $g.size + (($g.include.PSObject.Properties | ForEach-Object { $_.Value.size } | Measure-Object -Sum).Sum)
        $pick = @{ Res = $res; Gltf = $g; Size = $size }
        break
    }
    if (-not $pick) { Write-Warning "$id has no gltf"; continue }
    if ($pick.Size -gt $ModelSizeLimitMB * 1MB) { Write-Warning ("skip {0}: {1:N0} MB" -f $id, ($pick.Size / 1MB)); continue }
    $dir = Join-Path $Root "Models\$id"
    $existing = Join-Path $dir "$id.gltf"
    if ((Test-Path $existing) -and -not (Select-String -Path $existing -SimpleMatch "_$($pick.Res)." -Quiet)) {
        Remove-Item -Recurse -Force $dir
    }
    $Downloaded += Get-File $pick.Gltf.url (Join-Path $dir "$id.gltf")
    foreach ($inc in $pick.Gltf.include.PSObject.Properties) {
        $Downloaded += Get-File $inc.Value.url (Join-Path $dir ($inc.Name -replace '/', '\'))
    }
    Write-Host ("model {0} ({1}, {2:N1} MB)" -f $id, $pick.Res, ($pick.Size / 1MB))
}

Set-Content -Path (Join-Path $Root "LICENSE.txt") -Value "All assets in this folder are from Poly Haven (https://polyhaven.com) and are licensed CC0 (public domain)."
Write-Host ("done, downloaded {0:N0} MB" -f ($Downloaded / 1MB))
