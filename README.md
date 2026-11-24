# vtshow

Animations made of letters, drawn in a terminal. A small header-only C++ library
(`vtscreen`) turns a picture of brightness and colour into letters and sends only
what changed, and `show` plays scenes one after another with cross-fades.

Included scenes:

- `bonfire`: a fire on the ground, with flames that lick and sway in a breeze, a
  warm glow on the ground, rising sparks and specks of ash.
- `moonphases`: a moon on a starry sky that goes through a whole cycle of phases in
  24 seconds, with earthshine on the dark side and the real dark seas and craters
  at their true positions and sizes (Tranquillitatis, Imbrium, Tycho and the rest).
- `starburst`: fireworks over a night sky, with real physics: shells fired from the
  middle that climb for 3 to 4 seconds on a ballistic arc, bursts in several styles
  (peony, chrysanthemum, ring, willow, double burst) in a theme colour with accent
  and glitter sparks, sparks that are braked by air drag, hang and sink while cooling
  from white-hot to ember red, faint clouds lit by each burst, and a finale.
- `singularity`: a black hole with a glowing, turning accretion disk seen at a tilt.
  The paths of light round the hole are worked out (for a hole that does not spin), so
  the disk's light is bent right round it: an arc over the top, a fainter second image
  below and a thin ring at the shadow's edge. The disk's brightness is the real flux of
  a thin disk, its colour is the colour of a black body at its temperature, and the
  Doppler and gravitational shifts make the side coming towards us hotter and brighter.
  The stars are bent and stretched round it.
- `thunderstorm`: storm clouds in perspective, drizzle, distant rain curtains,
  lightning at different distances, thunder arriving after the flash.
- `meadow`: a green meadow in the wind: rolling hills fading into haze, a lone tree on a
  ridge, flowers in the grass and clouds drifting across a blue sky. Gusts run across
  the grass in waves, seen in perspective: where one passes the blades shimmer faster
  and turn silver, the tree's leaves move with it, and cloud shadows slide over the
  hills. The letter is the blade and the second colour is the dark ground between them.
- `matrix`: digital rain: columns of green half-width katakana and digits falling in
  three layers of depth, each with a bright head, a soft glow behind it and a long fading
  trail. Quiet touches from the colour support: columns that lean a little teal or
  yellow-green and drift slowly, a faint vignette, a slow wave of light sweeping down and
  now and then a pale cyan stream. The font needs half-width katakana (most do, through
  a fallback font); set `USE_KATAKANA` to false in `matrix/matrix.cpp` for plain ASCII.

You can write more; see "Writing a scene".

## Credit

The look of this project, letters grouped by how much ink they carry and a scene
described as `setup(cols, rows)` once and then `frame(t)` as a pure function of
time, comes from the **python.animate** YouTube channel:

**https://www.youtube.com/@python.animate**

The table of letter groups (`LEVELS` in `vtscreen/vtscreen.h`) is the channel's.
Everything else in this repo, including the bonfire, moonphases, starburst,
singularity, thunderstorm, meadow and matrix scenes, was written for this project, with Claude.
Those scenes are original, not copies or translations of anything by the channel.
This is an unofficial personal project, not affiliated with or endorsed by the
channel. I have also made C++ ports of some of the channel's animations for my own
use. They are not included here because the author has not published a licence for
their code.

## Build and run

Requirements: Linux, `g++` with C++17 support, and a terminal. Nothing else.

    ./build.sh

    ./bonfire/bonfire --loop               one scene on its own (also moonphases, starburst, singularity, thunderstorm, meadow, matrix)
    ./show/show                            the show (q quits, n or space: next scene,
                                           p or b: previous, 1-9: that scene,
                                           h: hold the scene that is up, h again: let go)

Options, for both programs: `--fps N` (default 30), `--no-color`, `--cols N --rows N`.
The scene program also has `--loop` (keep going after its 24 seconds, no restart) and
`--still T` (print the frame at T seconds as plain text).

- Colour is 24-bit. Most desktop terminals and SSH clients support it; the plain
  Linux text console does not, so use `--no-color` there.
- The size is read when the program starts. If you resize the window while it is
  running, restart the program.
- Minimum size is 40 x 20 characters. Output is about 0.7 MiB/s at 150 x 60, 30 fps,
  in colour for the thunderstorm, and grows with `--fps`.
- The status bar at the bottom shows the scene, the time and `actual/target fps`,
  for example `29.9/30 fps`. If the first number is lower, the program or the
  terminal is not keeping up.

`show` plays the bonfire, the moon, the fireworks, the black hole, the thunderstorm, the meadow and the matrix
in turn: 30 seconds alone on screen, then a 10 second cross-fade (`STAY_SECONDS` and
`FADE_SECONDS` in `show/show.cpp`). Press `h` to hold the scene that is up: the
status bar shows `HOLD` and the show stays on it (it keeps animating) until you press
`h` again. The status bar also shows the scene's number, like `3/7`.

By hand, `n` (or space) goes to the next scene, `p` or `b` to the previous one and
`1` to `9` to that scene. The keys work at any time, in the middle of a fade too: the
new scene starts fading in over whatever is on screen, so the picture never jumps. Up
to three scenes can be on screen at once; another press while three are up drops the
oldest.

## Windows

The code also has a Windows branch (`#ifdef _WIN32` in `vtscreen/vtscreen.h`, which
uses the Windows console API instead of termios and poll). It needs **Windows 10 or
later**; Windows Terminal is the best place to run it. It builds cleanly, with no
warnings, with Visual Studio 2026 (MSVC 14.51, x64), and every scene prints a correct
frame with `--still`. The live animation in the Windows console has not been tested yet; report
any problem.

- **Visual C++ (easiest):** run `build.bat` from a normal Command Prompt or
  PowerShell (or double-click it). It finds Visual Studio itself (2019 or later,
  with "Desktop development with C++") and builds the seven scenes and the show as
  `.exe` files next to their sources, the same layout as `build.sh`:
  `show\show.exe`, `bonfire\bonfire.exe --loop` and so on.
- **MinGW-w64 (for example MSYS2, "MINGW64" shell):** the same `./build.sh` works,
  and it adds `-static` there so the programs need no DLLs. By hand:
  `g++ -O2 -std=c++17 -static -o show/show show/show.cpp ...` as in `build.sh`.
- **Visual C++ by hand (x64 Native Tools prompt)**, one program, for example:
  `cl /nologo /std:c++17 /O2 /EHsc /utf-8 /Fe:thunderstorm\thunderstorm.exe thunderstorm\thunderstorm.cpp`
  and for the show, add `/DVTSHOW_NO_MAIN show\show.cpp` and the seven scene files.
- With Visual C++ the thunderstorm's random details (its hills and drops) differ a
  little from Linux and MinGW, because the standard library's random distributions
  are not the same everywhere.
- On Cygwin or WSL it is treated as Linux and needs no changes.

## How it fits together

    scene (your code)                  vtscreen/vtscreen.h
    ------------------                 ----------------------------------------
    setup(cols, rows)                  Display: the terminal session (size,
    frame(t, Picture&):                  alternate screen, keys, Ctrl-C), the clock,
      for every cell a brightness        the fps counter, the status bar
      0..1, a colour and a shimmer     letters: 16 steps, the top eight in inverse
      speed, or a letter of its own      video, plus a shimmer that follows --fps
      (text, such as the readout)      colours rounded to 16 levels per channel
                                       compares with what the terminal shows and
                                         sends only the differences
                                       fade(a, b, pct): mix two pictures

A scene never sees a letter or an escape code. Because a `Picture` is just numbers,
two of them can be mixed cell by cell and the letters picked once for the result,
which is how the cross-fade works. Only the scene on screen (and the one fading
out) is computed.

A `Picture` is a grid of `PicCell`s, the same shape and size as the `VtScreen` of
finished letters that the Display keeps: the whole terminal, with the status bar as its
last row. `F.init(R, C)` takes the rows of the picture proper (what `setup()` was given)
and adds the bar row underneath; `F.at(i, j)` reaches a cell, `F.bar_row()` the bar. A
scene fills in, for each cell:

| field | meaning |
|---|---|
| `lum` | brightness, 0 to 1 |
| `r`, `g`, `b` | colour, 0 to 255 |
| `shimmer` | how fast the cell's letter rotates inside its brightness group: 1 (the default) is the normal rate, 0 holds the letter still, 2 is twice as fast. A star could use 0.2 for a slow twinkle and an ember 3. A cell cannot change letter more than once a frame, so at 30 fps nothing above about 2.5 looks different |
| `alt_lum`, `alt_r`, `alt_g`, `alt_b` | the alt colour, the other colour of the pair: brightness 0 to 1 and colour 0 to 255. See below. The default, `alt_lum = 0`, means none |
| `text`, `ch`, `inv` | a letter the scene chooses itself: when `text` is set the cell is drawn as the character `ch` (inverse video if `inv`) in the colour `r`, `g`, `b`, and `lum` and `shimmer` are ignored |

`shimmer` only chooses which letter of the group shows. For a cell that dims and
brightens, the scene also varies `lum`. `init()` resets every cell to the defaults
each frame, so a scene only sets what it wants different.

**The alt colour.** Every cell is drawn with two colours: the primary `r, g, b` and an
alt colour. In a dim cell (`lum` below 0.5) the primary is the letter and the alt is the
background behind it. In a bright cell (0.5 and above, drawn in inverse video) the
primary is the solid block and the alt is the letter on it. So a meadow can be a dark
green block with lighter green letters: set `lum` above 0.5, `r, g, b` to the dark green,
and `alt_lum = 1` with `alt_r, alt_g, alt_b` the light green. The colour shown is
`alt_r, alt_g, alt_b` scaled by `alt_lum`, and `alt_lum = 0` (the default) means no alt
colour: the terminal's own colour is left alone and nothing extra is sent, so a scene
that never sets it looks and costs what it always did. The primary decides the letter and
the flip at 0.5; the alt never does. `--no-color` ignores the alt, like all colour.
In a fade the alt's brightness and colour are mixed like the rest, so an alt that one
scene lacks fades toward none.

**Text.** `F.text(row, col, "flame 2.2 m")` writes a string as text cells, in the readout
grey unless you give a colour: `F.text(row, col, s, r, g, b, inv)`. The scenes use it
for the readout in the top-left corner. Text is UTF-8 and each cell holds one Unicode
character (`char32_t`), so `°`, `─` or `█` work. Every character is taken to be one
column wide: wide characters (Chinese, emoji) and combining marks are not handled. On
Windows the console is switched to UTF-8 while the program runs.

**The status bar.** `Display::bar(picture, info, t, loop, suffix)` writes the default
bar into the bottom row as text cells: the scene's title (and the suffix, which `show`
uses for `[n h]` or `[HOLD]`), the time and the actual and target fps, in inverse video
in the scene's accent colour. `play()` and `show` call it for you. A scene that writes
its own text in the bar row keeps it. Because the bar is cells like the rest, a
cross-fade mixes the two bars' colours; the characters switch at the halfway point.
In a fade, text cells follow that rule: where both pictures have text the colours are
mixed and the letter comes from the picture that is ahead; where only one has text, that
cell is taken as it is.

## Writing a scene

Derive from `vtscreen::Scene` (`info()`, `setup(cols, rows)`, `frame(t, Picture&)`),
put its internals in an anonymous namespace, and end the file with a factory like
`make_thunderstorm()` (see `thunderstorm/thunderstorm.cpp`, `bonfire/bonfire.cpp` and
`scenes.h`). For a single-scene program, `main` is `vtscreen::run(scene, argc, argv)`.
The comment at the top of `vtscreen/vtscreen.h` has a short example. Compile a scene with
`-DVTSHOW_NO_MAIN` to leave out its `main` so it can be linked into `show`.

## Licence

MIT, see `LICENSE`. The letter table in `vtscreen/vtscreen.h` is the channel's, see
Credit above.
