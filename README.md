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

Requirements: Linux, `g++` with C++17 support, `git`, and a terminal. Nothing else.

    git clone --recursive https://github.com/briefnotion/vtshow.git
    cd vtshow
    ./build.sh

The libraries come as submodules, which `--recursive` fetches. If you cloned without them, or
`vtscreen/` or `vtpen/` is empty, run `git submodule update --init`; after a `git pull` that changes
a library, run `git submodule update`.

    ./bonfire/bonfire --loop               one scene on its own (also moonphases, starburst, singularity, thunderstorm, meadow, matrix)
    ./show/show                            the show (q quits, n or space: next scene,
                                           p or b: previous, 1-9: that scene,
                                           h: hold the scene that is up, h again: let go)

Options, for both programs: `--fps N` (default 30), `--no-color`, `--cols N --rows N`.
The scene program also has `--loop` (keep going after its 24 seconds, no restart) and
`--still T` (print the frame at T seconds as plain text).

- Colour is 24-bit. Most desktop terminals and SSH clients support it; the plain
  Linux text console does not, so use `--no-color` there.
- If you resize the window while a program is running, it follows: the display is made
  again at the new size, every scene is set up again for it, the clock and any fade carry
  on, and the screen is cleared and redrawn. A terminal smaller than the minimum is not
  followed. With `--cols` or `--rows` that dimension stays as you gave it. On Linux this
  uses the terminal's resize signal (`SIGWINCH`), on Windows the console's window-size event.
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
frame with `--still`. The live animation has been run in Windows Terminal (PowerShell) at
high resolution: it runs smoothly, the scene keys work, and resizing the window while the
program runs is followed, even during a fade, through the console's window-size event (the
same as the Linux signal). Quitting with q puts the console back as it was. Other Windows
consoles have not been tried; report any problem.

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

## Line art over the letters (ReGIS or Sixel)

On a terminal that can show pictures the moon is also drawn as thin vector line art over its letters: the outline, a grid of
latitude and longitude that turns with the moon, the line between light and dark, the moon's axis, and the sun going round a
dashed orbit. The scene describes the lines as a [vtpen](https://github.com/briefnotion/vtpen) `Drawing` of shapes and hands it
to vtscreen, whose Display shows it the way the terminal does best: as **ReGIS** lines on a terminal that lists ReGIS (xterm
built with `--enable-regis-graphics`, run as a VT340: `xterm -ti 340`; also VT125, VT240, VT340 hardware, untried), else as
**Sixel** pixels (Windows Terminal 1.22 and later, xterm with Sixel on, foot, WezTerm, iTerm2 and others). The programs ask the
terminal at start-up (about a moment) what it can do; if it can do neither, or vtpen is missing, or you set `VTSHOW_NO_IMAGES=1`,
everything is the letters it always was. `--no-color` also leaves the lines out. `VTSHOW_NO_REGIS=1` keeps them as Sixel pixels
on a terminal that does both (to compare).

A pixel picture costs bytes in proportion to the cells the lines cross: about 1 MB/s at 100 x 40 and 1.4 MB/s at 209 x 51, on top
of the letters' 0.3 MB/s. ReGIS lines cost about a quarter of that (measured on the moon at 30 frames a second: 100 columns,
929 KB/s as Sixel against 385 KB/s as ReGIS, 177 KB/s of that the letters). Settings for trying other values: `VTSHOW_PEN_FPS` (how
often the lines are drawn again, default 15), `VTSHOW_PEN_AA` (edge softness of pixel lines, 1 to 8, default 2) and
`VTSHOW_PEN_LETTERS` (how often a letter under a pixel line may change: -1 the default, any number from 0 a limit per second; low
values show the moon a little late; not used for ReGIS).

What to know about ReGIS in xterm: it is drawn from the top left of the window, never stretched, and no bigger than xterm's
`maxGraphicSize` (1000 x 1000 by default; the moon is in the middle of the window, so a window wider than that needs it raised, for
example `xterm -xrm 'XTerm*maxGraphicSize: 1920x1080' -xrm 'XTerm*regisScreenSize: 1920x1080'`); a letter written into a cell takes
that cell's ReGIS pixels with it, so the lines are drawn again whenever letters under them change; and scrolling with a ReGIS picture
on screen is slow. The build looks for vtpen's headers in `vtpen/include` (see `build.sh`); on Windows (`build.bat`) the scenes
are letters only.

## The library

The drawing library, `vtscreen`, lives in its own repository,
**https://github.com/briefnotion/vtscreen**, and is included here as a git submodule in
the `vtscreen/` folder. Its README describes how it works: the picture of cells
(brightness, colour, shimmer, the alt colour, text), how letters are picked, the status
bar, mixing two pictures for a fade, UTF-8 and resizing. This repo is the scenes and the
show that use it.

To update the library to its newest version, from this repo:

    git submodule update --remote vtscreen
    git add vtscreen
    git commit -m "vtscreen: update"

## Writing a scene

Derive from `vtscreen::Scene` (`info()`, `setup(cols, rows)`, `frame(t, Picture&)`),
put its internals in an anonymous namespace, and end the file with a factory like
`make_thunderstorm()` (see `thunderstorm/thunderstorm.cpp`, `bonfire/bonfire.cpp` and
`scenes.h`). For a single-scene program, `main` is `vtscreen::run(scene, argc, argv)`.
`setup(cols, rows)` is called again, with the new size, whenever the terminal is resized,
so it must start the scene's state over from scratch each time rather than rely on being
called once; `frame()` stays a pure function of `t`.
The comment at the top of `vtscreen/vtscreen.h` has a short example. Compile a scene with
`-DVTSHOW_NO_MAIN` to leave out its `main` so it can be linked into `show`.

## Licence

MIT, see `LICENSE`. The letter table in `vtscreen/vtscreen.h` is the channel's, see
Credit above.
