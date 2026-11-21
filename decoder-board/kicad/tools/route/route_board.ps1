# Re-route the decoder board from scratch: clears all tracks and vias, routes again, runs DRC.
#
#   powershell -ExecutionPolicy Bypass -File decoder-board\kicad\tools\route\route_board.ps1
#
# This REPLACES every track and via on the board, including any routing edited by hand in
# KiCad. Hand routes that should survive belong in HAND_TRACKS / HAND_VIAS in route.py.
# Needs KiCad 10 and a Python with numpy for route.py (pip install numpy).

param(
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

& $kp (Join-Path $here "apply.py") $pcb --moves-only        # placement tweaks, clear tracks
& $kp (Join-Path $here "extract.py") $pcb $geo              # pad geometry
& $Python (Join-Path $here "route.py") $geo $routes         # hand routes + grid router
& $kp (Join-Path $here "apply.py") $pcb $routes             # write tracks and vias
& $kcli pcb drc --refill-zones --save-board --schematic-parity --severity-all -o $drc $pcb
Write-Host "DRC report: $drc"
