# Glide capture and replay

Records the glide3x calls the game makes for N frames, so a rendering change can be
checked on any Windows machine without the game: `d2gl_replay.exe` feeds the same calls
through D2GL's `Wrapper`/`Context` and writes one PNG per frame.

Code: `glide3x/src/glide/capture.{h,cpp}` (writer and replayer), entry hooks in
`glide3x/src/glide/glide3x.cpp`, the exe in `replay/main.cpp`.

## Capturing

1. In `d2gl.ini`, `[Other]`: `glide_capture=true` (optional `glide_capture_frames=60`, 1-3600).
   With it off nothing is recorded or polled; the entry points only test one bool.
2. In game, create an empty file named `d2gl_capture` next to the game (the working
   directory). D2GL checks every 30 frames, deletes the file and records the next
   `glide_capture_frames` frames. A number in the file overrides the frame count
   (`echo 10 > d2gl_capture`).
3. The result is `d2gl_capture_001.gcap` (next free number). `d2gl.log` gets a line at
   start and end. Size: about 1-4 MB per in-game frame plus the texture snapshot.

## Replaying

```
mkdir work && cd work
(extract d2gl.mpq here so that work\data\shaders\... and work\data\assets\... exist)
(optional: copy d2gl_hires*.pak here to test hi-res packs)
d2gl_replay.exe ..\d2gl_capture_001.gcap -o out [--ini my.ini] [--frames N] [--size 1280x960]
```

`d2gl_replay.exe` must sit next to `glide3x.dll` (or pass `--dll`). It sets
`D2GL_REPLAY=1` before loading the DLL; `dllAttach` then skips the version check and all
game hooks. `d2gl_replay()` (DLL export) writes the capture's embedded ini (or `--ini`) to
`work\d2gl_replay.ini`, loads it, opens a visible window of the captured size, creates the
normal `Context` + `Wrapper`, replays the stream and writes `out\frame_00000.png`, ...
(the default framebuffer, read on the render thread right before `SwapBuffers`).
Do not run it in the game directory: `getProc` would load the game DLLs.

Needs an OpenGL 3.3+ driver (a GPU, or Mesa llvmpipe's `opengl32.dll` next to the exe).
Exit code 0 = ok, 1 = truncated capture (replayed up to the cut), others printed.

Forced off during replay (they read live game memory or draw outside glide): HDR output,
vsync, fps limits, motion prediction, HD text, HD cursor, minimap, loot sparkle, fps
counter, option menu, load_dlls. Everything else follows the ini.

## Known gaps

- D2GL draws that do not come through glide are not in the capture: HD text, HD cursor,
  minimap, loot sparkle, silhouettes, and anything else pushed by the D2 function hooks.
  When HD text was on, game text is missing in the replay. The header `flags` say which
  were on; the exe prints a note.
- Per-draw game state read by hooks (unit being drawn, orb/potion flags, hi-res remap of
  recoloured sprites via `hires::g_pending`, light list) is not recorded. Hi-res lookups of
  plain downloads do work (same hash path as the game).
- `grDrawVertexArray` strips with an odd count read one vertex past the end in the game;
  the replay repeats the last vertex there.
- Untested on real hardware at the time of writing (written without a Windows box; CI only
  builds it).

## File format (version 1, little endian)

```
Header (60 bytes, packed)
  char     magic[8]      "D2GLCAP\0"
  u32      version       1
  u32      header_size   60 (the ini starts here)
  u32      tex_mem       glide texture memory per TMU in bytes (3 TMUs)
  u32      game_w, game_h
  u32      window_w, window_h
  u32      screen_res    GrScreenResolution_t of the last grSstWinOpen
  u32      screen        GameScreen (0 menu, 1 in game, 2 movie, 3 loading)
  u32      frames        frames requested
  u32      d2_version    helpers::getVersion() (Version enum)
  u32      flags         1 hd_text, 2 hd_cursor, 4 mini_map, 8 motion_prediction,
                         16 loot_sparkle, 32 silhouettes, 64 hi-res packs loaded
  u32      ini_size
u8 ini[ini_size]         d2gl.ini text at capture time
records...               until OP_END or end of file
```

Record: `u16 op, u16 nargs, u32 blob_size, u32 args[nargs], u8 blob[blob_size]` (no
padding). Readers skip unknown ops by size; new args are only ever appended.

| op | name | args | blob |
|---:|---|---|---|
| 0 | END | | |
| 1 | BUFFER_CLEAR | color, alpha, depth | |
| 2 | BUFFER_SWAP | swap_interval | (one frame) |
| 3 | DRAW_POINT | | 1 GlideVertex |
| 4 | DRAW_LINE | | 2 GlideVertex |
| 5 | DRAW_ARRAY | mode, count | count GlideVertex (dereferenced pointers) |
| 6 | DRAW_ARRAY_CONTIG | mode, count, stride | count GlideVertex, packed (the wrapper ignores stride) |
| 7 | ALPHA_BLEND | rgb_sf, rgb_df, alpha_sf, alpha_df | |
| 8 | ALPHA_COMBINE | function, factor, local, other, invert | |
| 9 | CHROMAKEY_MODE | mode | |
| 10 | COLOR_COMBINE | function, factor, local, other, invert | |
| 11 | CONSTANT_COLOR | value | |
| 12 | GAMMA_TABLE | n | u32 red[n], green[n], blue[n] |
| 13 | GAMMA_RGB | red, green, blue (float bits) | |
| 14 | TEX_SOURCE | tmu, start, evenOdd, smallLod, largeLod, aspect, format | |
| 15 | TEX_DOWNLOAD | same as TEX_SOURCE | w*h 8-bit texels |
| 16 | TEX_TABLE | type | 256 u32 ARGB palette |
| 17 | LFB_FRAME | writeMode, origin, stride | 640*480*4 movie frame (one frame) |
| 18 | SST_WIN_OPEN | screen_res, is_in_game | |
| 32 | GAME | screen, screen_shift, automap_on, level_no, is_in_game | |
| 33 | STAGE | DrawStage (D2GL draw stage hook) | |
| 34 | SNAP_TEX | address (TMU offset included), w, h, hash | w*h texels |

GlideVertex is 28 bytes: `float x, y; u32 pargb; float scale, s, t; u32 unused`.

A capture starts at a buffer swap with a snapshot: the last gamma call, palette, constant
colour, colour/alpha combine, chromakey and blend calls (shadowed while `glide_capture` is
on), then one SNAP_TEX per glide texture address the game has downloaded to (current
texture memory, so overlapping downloads stay consistent), a GAME record and the last
TEX_SOURCE. GAME records are written lazily: before any record when one of its values
changed. STAGE records are written from `Context::onStageChange` (map, HUD, UI, cursor
item, cursor), exactly where the game's hooks switch stage.
