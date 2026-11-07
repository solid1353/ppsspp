# Launch options

Run-only options for external launchers. They start a game in a set state and none of them is saved
to `ppsspp.ini`, so a per-game config can't override them either. They live in `Core/CmdLine.cpp` and
reach the emulator through the `launch*` fields of `Config`.

| Option | Effect |
| --- | --- |
| `--cheats=FILE`, `--cheat-line=LINE` | Apply CwCheat text after the game's own cheat file, in command-line order, even while cheats are off for the game. |
| `--fast-forward-for-frames=N` | Fast-forward N vblanks after boot and after every reset. |
| `--turbo` | Start every boot and reset at alternate speed 1. After `--fast-forward-for-frames`, this is the speed it falls back to. |
| `--unlimited` | Start every boot and reset fast-forwarding, and stay there. Can't be combined with `--turbo` or `--fast-forward-for-frames`. |
| `--mute` | Silence game and UI audio by clearing the final mix, so audio timing doesn't change. |
| `--window=X,Y,W,H` | Place the window. |
| `--centered-window` | Fit the game inside a 1024x768 client area, whatever the display scaling, center the window on the primary monitor's work area, and don't start fullscreen. Windows only. Can't be combined with `--window`. |
| `--read-only-settings` | Write no ini file, global or per-game. |
| `--savedata=DIR` | Serve `ms0:/PSP/SAVEDATA` from this existing directory. A savestate doesn't depend on it. On a case-sensitive host, a save's folder has to exist under its exact name. |
| `--discard-savedata` | Report savedata utility writes and deletes as done without doing them. A game writing to `ms0:` through `sceIo` isn't affected. Can't be combined with an input recording. |
| `--volatile-savedata` | Run on a copy of the saves (`--savedata`'s, or the memory stick's) in `PSP/SYSTEM/CACHE`, deleted when the game stops or resets. Can't be combined with `--discard-savedata`. |
| `--debugger-local` | With `--debugger`, bind 127.0.0.1 only and report nothing to `report.ppsspp.org`. |

Input recording options are in [input-recording.md](input-recording.md).

## Hotkeys

Besides the options above, the Windows build drops the fixed Ctrl+W, Ctrl+M and F4 shortcuts, so
those keys can be mapped. Stop, the memory view and quick load stay in the menus. New mappable keys:

| Key | Effect |
| --- | --- |
| Turbo toggle, Slow motion toggle | Switch between normal speed and alternate speed 1 or 2. `SpeedToggle` still cycles through both. |
| Unlimited toggle | Turn fast-forward on or off. |
| Stop game | Stop the game and go back to the menu. |
| Frame Advance (double) | Run two frames, then stop. |
| Recenter window | Size and center the window as `--centered-window` does, on its current monitor. Windows only. |
| Save State and Next Slot | Save to the current slot and select the next one. |
| Load Backup State | Load the state the slot's last save replaced, leaving the slot as it is. |
| Delete State | Delete the current slot's state. |
| Input recording marker | See [input-recording.md](input-recording.md). |

Both frame advance keys keep advancing once held for 0.3 seconds.

## Savestate GE dumps

`SaveStateGEDump` in `[General]`, on by default with no UI, records the frame after each save to a slot
as a GE dump beside the state (`<prefix>_<slot>.ppdmp`). It's part of the state's files: it moves to
`.undo.ppdmp` with the undo state and goes when the slot is deleted. A recording already under way wins,
and the dump is only written once that frame runs. Loading a state before then, or undoing or deleting
the save, drops it.
