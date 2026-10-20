# rapture.ini

The Rapture engine (moses.dll) reads an optional `rapture.ini` from the **current directory** (the game folder; the original used
`GetPrivateProfileInt`, which would also look in `%WINDIR%` for a relative name - the port does not). The file is plain INI:
`[section]` headers, `key=value` lines, `;` comment lines; section and key names are case-insensitive. Values are read as decimal
integers (`0x1` reads as 0; text after the digits is ignored; an empty or missing value gives the default). The file is optional.

The original reads exactly four options (the whole of moses.dll and Aaron.dll was searched for `GetPrivateProfile*` /
`WritePrivateProfile*`: only `IniGetInt` is ever called, 4 times; `IniWriteInt`/`IniWriteString` exist but nothing calls them).

| Section | Key | Default | Original | Port |
|---|---|---|---|---|
| `[drawlib]` | `NoDirectDraw` | 0 | `CDrawSys::ChangeDisplayMode` returns 0 at once: no DirectDraw display-mode change | Value is read (`CDrawSys::NoDirectDraw()`), but the port has no display-mode change at all (fixed 640x480 SDL window), so there is nothing to disable; the call always reports success. |
| `[drawlib]` | `ComposeRects` | 0 | "show recomposed rectangles" - **dead flag**: stored, never read | Read and stored, still dead. See `DebugRects` below. |
| `[drawlib]` | `DisplayRects` | 0 | "show display rectangles" - **dead flag** | Read and stored, still dead. |
| `[audio]` | `RemapDrums` | 0 | The MIDI thread rewrites channel 10 (drums) to channel 16 | Implemented: `MidiPrepareCompact` (platform/Midi.cpp) applies it to the compact MIDI stream before playback. |

`RemapDrums=1`: every short MIDI message whose low nibble is 9 (channel 10) gets `| 0xf`, i.e. goes to channel 16. As in the original this
happens **before** the muted-channel test (`MidiSetChannel`), so the mute applies to channel 16.

## Port-only options

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[port]` | `DebugRects` | 0 | `1` enables the debug-rects overlay (below). |

Command line (any game built on the engine): `--debug-rects` does the same without touching the ini.

### Debug-rects overlay

An extra for people working on the engine; off by default, never active unless asked. When on, `CDrawView::PresentTo` repaints the whole
frame every time and draws 1-pixel outlines over it: **red** = the clipped rectangle recomposed for each object in `CDrawView::Display`,
**green** = the rectangle displayed by each `Display` call. The rectangles gathered since the previous frame are drawn and cleared.
The dead `ComposeRects`/`DisplayRects` flags do not enable it (they stay dead as in the original).

Example `rapture.ini`:

```
[port]
DebugRects=1

[audio]
RemapDrums=1
```

## API (moses/Ini.h)

`IniGetInt(section, key, short def)` (0x1000ff40), `IniWriteInt(section, key, short)` (0x1000ff60), `IniWriteString(section, key, text)`
(0x1000ffa0; `nullptr` deletes the key; creates file and section; written with CRLF), `IniGetString` (port), `IniSetFile(path)` (tests).
