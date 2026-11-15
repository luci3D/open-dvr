# Downloads symbols, footprints and 3D models for the decoder board's non-standard parts
# from LCSC/EasyEDA with easyeda2kicad, into decoder-board/kicad/lib/ (lcsc.kicad_sym,
# lcsc.pretty, lcsc.3dshapes). Plain 0402/0603/0805 resistors and capacitors use KiCad's
# own footprints and are not fetched.
#
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File decoder-board\kicad\fetch_parts.ps1
# Add -Overwrite to refresh parts that are already in the library.

param([switch]$Overwrite)

$parts = @(
    'C2852602',  # U1  TP9950 video decoder (QFN-40 5x5)
    'C9008',     # Y1  27 MHz crystal 3225
    'C82942',    # U2  ME6211C33M5G-N 3.3 V LDO
    'C141836',   # U3  TLV62569DBVR buck
    'C354533',   # L1  2.2 uH inductor 2016
    'C236671',   # U4  ME6211C18M5G-N 1.8 V LDO
    'C53099',    # U5  ME6211C28M5G-N 2.8 V LDO
    'C14709',    # FB  BLM18PG121SN1D ferrite 0603
    'C316041',   # D1  ESD9L5.0ST5G-N video ESD DFN1006-2
    'C193402',   # D2  SMF5.0A TVS SOD-123FL
    'C15127',    # Q1  AO3401A P-MOSFET
    'C883108',   # F1  BSMD0805-050-6V polyfuse
    'C3171831',  # MK1 IM73A135V01XTSA1 MEMS mic
    'C5362157'   # J1  FPC 0.5 mm 20 P (pin count to confirm)
)
# J2 (SH 1.0 mm 3 P, C3029342) uses KiCad's built-in footprint
# Connector_JST:JST_SH_SM03B-SRSS-TB_1x03-1MP_P1.00mm_Horizontal; EasyEDA returned 403 for it.
#
# Parts that share a package (the SOT-23-5 LDOs and buck) print "already exists" for the
# footprint or 3D model after the first one: that's expected, the symbol is still added.
# If every request starts failing with HTTP 403, EasyEDA is rate-limiting: wait and rerun.

$lib = Join-Path $PSScriptRoot 'lib\lcsc'
New-Item -ItemType Directory -Force (Split-Path $lib) | Out-Null

foreach ($id in $parts) {
    $args = @('--full', '--lcsc_id', $id, '--output', $lib, '--project-relative')
    if ($Overwrite) { $args += '--overwrite' }
    Write-Host "== $id"
    & easyeda2kicad @args
    if ($LASTEXITCODE -ne 0) { Write-Warning "$id returned exit $LASTEXITCODE (fine if it only said 'already exists')" }
}
