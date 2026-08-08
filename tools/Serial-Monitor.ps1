#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Port,
    [int]$Baud = 115200
)

$ErrorActionPreference = 'Stop'
$python = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
if (!(Test-Path $python)) { throw 'ESP-IDF 5.5 Python was not found under C:\Espressif.' }
if (!$Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    if ($ports.Count -eq 0) { throw 'No COM ports found. Connect the board and try again.' }
    if ($ports.Count -gt 1) { throw "More than one COM port was found ($($ports -join ', ')). Run again with -Port COMx." }
    $Port = $ports[0]
}

Write-Host "Opening $Port at $Baud baud. Press Ctrl+] to exit."
& $python -m serial.tools.miniterm $Port $Baud --raw
