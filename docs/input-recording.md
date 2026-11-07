# Input recording

A recording holds a whole boot's input, saved through `Core/Replay.h` from power-on, so it replays the
same game from the same state. `Core/InputRecording.cpp` drives it from the command line.

| Option | Build | Effect |
| --- | --- | --- |
| `--input-recording=FILE` | both | Replay FILE from boot. |
| `--input-recording-create=FILE` | both | Record to FILE from boot. |
| `--input-recording-capture-dir=DIR` | headless | Capture at the replay's markers into DIR and exit when it ends. |
| `--input-recording-capture-mode=MODE` | headless | `full` (default), `screenshots` or `savestates`. |
| `--input-recording-capture-markers=SPEC` | headless | Capture only these 1-based markers, such as `1,3-5`. Overlapping and adjacent ranges merge. |

A recording is flushed every 300 vblanks and finished when the game stops. A reset ends it, and the
next boot doesn't start it over. A game's `sceKernelLoadExec` isn't a new boot, so a recording spans
it. Loading a state ends a recording or a replay, since it moves the emulated clock they run on.

A replay runs from each boot, including after a reset. When it reaches its end the CPU stops, and
resuming hands the game back to real input. With a capture directory, headless exits there instead.

## Replay file format

`Core/Replay.cpp` describes the format: a header, then events in time order. Two actions exist for
this: `MARKER` (0x02) and `END` (0x03, when the recording stopped). Events carry emulated time in
microseconds, so editing a marker means keeping that order: replay stops reading at the first event
still in the future.

## Markers

The `Input recording marker` hotkey adds a marker at the next input sample, so holding it adds one.
Markers are events of their own, not buttons, so the game never sees them. Markers are numbered in
replay order, including ones not selected for capture, and captures are named after them:

| Mode | Output for marker `NNN` |
| --- | --- |
| `full` | `NNN.png`, `sstates/NNN.ppst` and `sstates/NNN.ppdmp` |
| `screenshots` | `NNN.png` |
| `savestates` | `sstates/NNN.ppst` and `sstates/NNN.ppdmp` |

A capture happens at the first frame boundary after its marker. The GE dump is the frame after that,
so a marker on a recording's last frame has no dump. Two markers a frame apart get one dump, since
a recording under way wins.

## Determinism

A replay takes its RTC base time, input and memory stick reads from the file. Everything else has to
match the recording: run it on the same build and with the same settings. Headless reads no config of
its own, so pass the app's `ppsspp.ini` with `--appendconfig` and its memory stick with `--memstick`.

A replay still writes through the game's saves, so give it `--volatile-savedata` to keep them.
`--discard-savedata` is refused here: the file holds each save's results, and skipping one puts the
rest out of step. While recording or replaying, the save dialog waits for its IO, so host speed
doesn't decide how many frames it lasts.

## Tests

`TestInputRecording` in `unittest/UnitTest.cpp` covers marker specs, capture modes, and a marker and
the end surviving a round trip. `TestCmdLine` covers the options and what can't be combined.
