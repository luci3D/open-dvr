# DVR probe hook

`sdcard/encrypt` goes in the root of the DVR's SD card (FAT32). The board runs it as root at every boot, just before the recorder starts. Nothing is written to the board's flash.

This is how the hardware was worked out before writing the recorder in `../dvr-recorder/`. To just record, use that instead.

## Round 3: raw frame grab (current script)

1. Camera on PAL. Copy the new `encrypt` over the old one on the card.
2. Power the board and **keep the camera moving the whole time** (pan the quad around, or wave a hand close to the lens). The grab needs motion at about the 60-second mark.
3. At about 60 s the probe stops the recorder (same as cutting power for the video file) and copies the raw video buffers (~19 MB) to `probe/vb.bin`.
4. When `probe/DONE.txt` exists, power off and read the results in `probe/` on the card.

Power-cycle the board afterwards to record normally. Delete `encrypt` (or add an empty `probe_off` file) to go back to stock behaviour.

## Optional switches (not needed for round 3)

These are not commands. Each one is an empty file you create in the card's root folder, next to `encrypt`. The script only checks whether a file with that exact name exists. Leave them out unless you need one.

- `uartshell`: opens a command line on the board's serial (UART) pins at 115200 baud, for a USB-serial adapter.
- `probe_off`: turns the probe off, so the board just records normally.
- `encrypt.orig`: only if a card ever arrives with its own file called `encrypt`. Rename that file to `encrypt.orig` before copying ours, and ours runs it at the end.

To make an empty file in Windows Explorer, first turn on View > Show > File name extensions. Then right-click > New > Text Document, and rename it to exactly `uartshell` (no `.txt`). Or in PowerShell:

```
New-Item D:\uartshell -ItemType File
```

## Results so far

- Round 1 (NTSC) and round 2 (PAL): the stock recorder writes 60/50 fps files where each pair of frames is the same picture, so only 30/25 distinct images per second.
- The TP2850 is set up with Techpoint's standard CVBS tables (identical to Rockchip's open TP9950 driver).
- Round 3 (raw frame grab): each raw frame holds both fields, stacked (field 1 in the top half, field 2 in the bottom half), and they really differ. The stock recorder converts the top half twice and drops field 2. That's what `../dvr-recorder/` fixes.
