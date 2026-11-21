# Re-route only some nets, keeping every other track and via (hand edits in KiCad included).
#
#   powershell -ExecutionPolicy Bypass -File decoder-board\kicad\tools\route\reroute.ps1 -Nets "/5V,/3V3"
#
# First brings the board in line with the schematic (update_board.py: nets, new footprints,
# PLACE moves, J3 labels) and removes the listed nets' copper, then routes them with route.py.
param(
    [Parameter(Mandatory = $true)][string]$Nets,
    [string]$KiCad = "C:\Program Files\KiCad\10.0",
    [string]$Python = "python"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$kdir = Resolve-Path (Join-Path $here "..\..")
$kp = Join-Path $KiCad "bin\python.exe"
$kcli = Join-Path $KiCad "bin\kicad-cli.exe"
$pcb = Join-Path $kdir "open-dvr-decoder.kicad_pcb"
$geo = Join-Path $env:TEMP "open-dvr-geometry.json"
$routes = Join-Path $env:TEMP "open-dvr-routes.json"
$drc = Join-Path $env:TEMP "open-dvr-drc.txt"

& $kp (Join-Path $here "update_board.py") $pcb $Nets
& $kp (Join-Path $here "extract.py") $pcb $geo
& $Python (Join-Path $here "route.py") $geo $routes --reroute $Nets
& $kp (Join-Path $here "apply.py") $pcb $routes
& $kcli pcb drc --refill-zones --save-board --schematic-parity --severity-all -o $drc $pcb
Write-Host "DRC report: $drc"
