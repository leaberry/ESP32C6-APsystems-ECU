#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Elf
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$Elf) {
    $artifactRoot = Join-Path $repo 'artifacts'
    $matches = if (Test-Path $artifactRoot) {
        @(Get-ChildItem $artifactRoot -Recurse -File -Filter 'ESP32C6_ECU-8MB-OTA.elf')
    } else { @() }
    if ($matches.Count -ne 1) {
        throw 'Download and extract the 8 MB CI artifact under artifacts, or pass -Elf with its ELF path.'
    }
    $Elf = $matches[0].FullName
}
$Elf = (Resolve-Path -LiteralPath $Elf).Path
$openocdRoot = Get-ChildItem 'C:\Espressif\tools\openocd-esp32' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$openocd = Get-ChildItem $openocdRoot.FullName -Recurse -Filter openocd.exe |
    Select-Object -First 1 -ExpandProperty FullName
$scripts = Get-ChildItem $openocdRoot.FullName -Recurse -Directory |
    Where-Object { $_.FullName -like '*share\openocd\scripts' } |
    Select-Object -First 1 -ExpandProperty FullName
$gdb = Get-ChildItem 'C:\Espressif\tools\riscv32-esp-elf' -Recurse -Filter riscv32-esp-elf-gdb.exe |
    Select-Object -First 1 -ExpandProperty FullName
if (!$openocd -or !$scripts -or !$gdb) { throw 'OpenOCD or the ESP32-C6 GDB executable was not found.' }

$log = Join-Path $env:TEMP 'esp32c6-openocd.log'
$openocdArgs = @('-s', $scripts, '-f', 'board/esp32c6-builtin.cfg')
Write-Host 'Starting the ESP32-C6 built-in USB-JTAG server...'
$server = Start-Process -FilePath $openocd -ArgumentList $openocdArgs `
    -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
try {
    Start-Sleep -Seconds 2
    if ($server.HasExited) {
        Get-Content $log -ErrorAction SilentlyContinue
        Get-Content "$log.err" -ErrorAction SilentlyContinue
        throw 'OpenOCD could not connect. Confirm the board uses its native USB/JTAG connector.'
    }
    & $gdb $Elf -ex 'target extended-remote localhost:3333' -ex 'monitor reset halt'
} finally {
    if (!$server.HasExited) { Stop-Process -Id $server.Id }
}
