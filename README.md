# open-dvr

An open recorder for the BAYCKRC / COOAI "MINI DVR", a small analog FPV DVR board (Goke GK7202V300 SoC + Techpoint TP2850 video decoder). It's built on open-source work from the [OpenIPC](https://openipc.org) project.

- Real 50 fps PAL and 59.94 fps NTSC: every video field is recorded, where the stock firmware repeats each picture twice.
- H.264 in MPEG-TS clips, safe against power cuts, with the board's microphone as an in-sync MP3 track.
- Runs from the SD card through the board's own boot hook. Nothing is flashed, and removing the files brings back the stock firmware.

| Folder | What |
|---|---|
| [`dvr-recorder/`](dvr-recorder/) | The recorder (`dvrrec`): specs, wiring, settings, build steps, credits |
| [`dvr-probe/`](dvr-probe/) | The SD card probe script used to work out the hardware |

Quick start: copy everything in `dvr-recorder/sdcard/` to the root of the board's SD card. The details are in [`dvr-recorder/README.md`](dvr-recorder/README.md).

## Licence

GPL-3.0, see [`LICENSE`](LICENSE). Third-party parts keep their own licences; see [Credits and licences](dvr-recorder/README.md#credits-and-licences).

Not affiliated with OpenIPC or with the board's maker.
