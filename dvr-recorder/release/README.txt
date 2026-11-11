OPEN DVR 0.9
============

An open recorder for the COOAI / BAYCKRC "MINI DVR" analog FPV DVR board
(printed on the board: cooeye MINIDVR V1.2).

It runs from the SD card. Nothing is written to the board's own memory, and
deleting these files brings the original recorder back.

Source code and details: https://github.com/luci3D/open-dvr


WHAT YOU GET
------------
- Real 50 fps (PAL) or 59.94 fps (NTSC). The original firmware records each
  picture twice, so its files only hold 25 or 30 different pictures a second.
- PAL: 960x576. NTSC: 960x480. Both play back as 4:3. PAL or NTSC is detected
  automatically.
- The board's microphone, in sync with the video.
- 5-minute clips (adjustable) that survive a power cut: pulling the battery
  loses about one second at most.
- Button: tap to stop or start recording.


INSTALL
-------
1. Put the DVR's SD card in your computer.
2. Extract this zip into the top folder of the card. "encrypt", "config.txt"
   and the "dvr" folder must sit directly on the card, not inside another
   folder.
   If the card already has a config.txt, your computer asks whether to
   replace it. Replace it to record at 20 Mbit/s in 5-minute clips, or keep
   yours.
3. Put the card back in the DVR and power it on.

That's all. The red LED stays on. The blue LED blinks quickly about 10 times,
then flashes once every 5 seconds: it is recording.

The clips are in the DCIM folder: DVR_0001.ts, DVR_0002.ts, ...
VLC plays them. To get an MP4 for a phone or an editor without losing
quality, use ffmpeg:

    ffmpeg -i DVR_0001.ts -c copy DVR_0001.mp4


BUTTON AND LEDS
---------------
Recording starts by itself at power-on.

Button
  Short tap             Stop recording (the clip is closed properly), or
                        start a new clip.
  Hold 2 s or longer    Nothing. (The original firmware formats the card when
                        the button is held for 5 s; this recorder never does.)

Blue LED
  One flash every 5 s   Recording
  On all the time       Stopped with the button
  Two flashes every 5 s Card full or not writable. Tap the button to try again.
  Quick blinking        Starting up

Red LED
  On                    The recorder is running

If the camera is unplugged, recording carries on with a black picture, and
the picture returns when the camera is connected again.


SETTINGS: config.txt
--------------------
The settings are in config.txt in the top folder of the card. This is the
same file the original firmware uses. Open it with Notepad. It is read at
power-on, so a change takes effect the next time you power the DVR.

- One setting per line, written as:  Name = value
- Write the names exactly as shown: MaxBitRate, not maxbitrate.
- You only need the lines you want to change. A missing line uses the value
  in the "If missing" column.

Name               What it does                              If missing
----------------   ---------------------------------------   ----------
MaxBitRate         Video quality in Mbit/s (whole number).   8
                   Tested up to 20. Higher = better picture
                   and bigger files.
                   Example:  MaxBitRate = 20

FileIntervalTime   Clip length in minutes.                   5
                   Example:  FileIntervalTime = 5

VideoFormat        PAL or NTSC forces that standard.         AUTO
                   AUTO detects it.
                   Example:  VideoFormat = NTSC

Audio              1 records the microphone, 0 does not.     1
                   Example:  Audio = 0

MicBoost           1 makes the microphone about 9 dB         0
                   louder. Good for voices on the bench;
                   propeller noise distorts with it.
                   Example:  MicBoost = 1

MicGain            Microphone gain step, 0 to 31.            3
                   Example:  MicGain = 2

AudioGain          Extra volume in dB, 0 to 30.              0
                   Example:  AudioGain = 6

LoopRecording      1: when the card is nearly full, delete   0
                   the oldest DVR_####.ts clips to make
                   room. 0: stop recording when the card is
                   full.
                   Example:  LoopRecording = 1

The config.txt in this zip:

    MaxBitRate = 20
    FileIntervalTime = 5
    UserOsd =

UserOsd belongs to the original firmware; this recorder ignores it.

How much fits on a card:

    MaxBitRate    per minute    5-minute clip    64 GB card
    8             60 MB         300 MB           about 17 hours
    15            113 MB        560 MB           about 9 hours
    20            150 MB        750 MB           about 7 hours


GOING BACK TO THE ORIGINAL RECORDER
-----------------------------------
- For a while: make an empty file named "stock" (no ".txt") in the top folder
  of the card. Delete it to use this recorder again.
- For good: delete "encrypt" and the "dvr" folder from the card.

If this recorder fails three times in a row right after starting, the
original recorder starts by itself, so the board still records.


IF SOMETHING IS WRONG
---------------------
- Nothing changed, the files are still .avi: check that "encrypt" and the
  "dvr" folder are in the top folder of the card. This release was tested on
  original firmware V2.4_20251208; other versions may not run "encrypt".
- AHD cameras are not supported yet. Use the original recorder for those
  (see above).
- The recorder writes what it does to dvr/log.txt on the card. Include that
  file when you report a problem:
  https://github.com/luci3D/open-dvr/issues


GOOD TO KNOW ABOUT 0.9
----------------------
- Tested on one board, with an analog FPV camera in PAL and NTSC, at
  20 Mbit/s, on a FAT32 card.
- Stopping when the card is full, and LoopRecording, are new and have not
  been tested on a full card yet.


LICENCE AND CREDITS
-------------------
Open DVR is free software under the GNU General Public License version 3.
It comes with no warranty. The licence texts and the notices for the parts
from other projects are in dvr/licenses.

It is built on open-source work from the OpenIPC project
(https://openipc.org). Not affiliated with OpenIPC or with the board's maker.
