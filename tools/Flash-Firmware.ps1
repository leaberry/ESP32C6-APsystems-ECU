#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('8MB', '4MB')]
    [string]$Variant = '8MB',
    [string]$Port,
    [string]$Image,
    [switch]$SkipErase
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$python = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
if (!(Test-Path $python)) { throw 'ESP-IDF 5.5 Python was not found under C:\Espressif.' }

if (!$Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($ports.Count -eq 0) { throw 'No COM ports found. Connect the board and try again.' }
    if ($ports.Count -gt 1) { throw "More than one COM port was found ($($ports -join ', ')). Run again with -Port COMx." }
    $Port = $ports[0]
}

$imageName = if ($Variant -eq '8MB') {
    'ESP32C6_ECU-8MB-OTA.merged.bin'
} else {
    'ESP32C6_ECU-4MB-noOTA.merged.bin'
}
if (!$Image) {
    $artifactRoot = Join-Path $repo 'artifacts'
    $matches = if (Test-Path $artifactRoot) {
        @(Get-ChildItem $artifactRoot -Recurse -File -Filter $imageName)
    } else { @() }
    if ($matches.Count -ne 1) {
        throw "Download the $Variant CI artifact, extract it under artifacts, or pass -Image with the merged-image path."
    }
    $Image = $matches[0].FullName
}
$Image = (Resolve-Path -LiteralPath $Image).Path

Write-Host "Board: ESP32-C6 $Variant on $Port"
Write-Host "Image: $Image"
if (!$SkipErase) {
    Write-Host 'Erasing flash (configuration and history will be cleared)...'
    & $python -m esptool --chip esp32c6 --port $Port erase-flash
    if ($LASTEXITCODE) { throw "Flash erase failed with exit code $LASTEXITCODE." }
}

$flashSize = if ($Variant -eq '8MB') { '8MB' } else { '4MB' }
& $python -m esptool --chip esp32c6 --port $Port --baud 460800 `
    --before default-reset --after hard-reset write-flash --flash-size $flashSize 0x0 $Image
if ($LASTEXITCODE) { throw "Firmware flash failed with exit code $LASTEXITCODE." }
Write-Host 'Flash complete. The first boot may take several seconds.' -ForegroundColor Green
