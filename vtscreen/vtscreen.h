// vtscreen.h - draw a picture made of brightness and colour as letters on a
// VT100 terminal. Header only: #include it and compile with g++ -std=c++17.
//
// The split
//
//   the scene (your code)              this header
//   ---------------------              -----------------------------------------
//   setup(cols, rows)                  owns the terminal: size, alternate screen,
//   frame(t) -> for every cell:          raw keys, Ctrl-C, restore on exit
//       brightness 0..1                owns the clock: fps, pacing, --loop, the
//       colour r,g,b                     measured fps, the status line, q to quit
//     + a few HUD text lines           turns brightness into a letter: 16 steps,
//                                        inverse video, shimmer
//                                      rounds colours
//                                      shows a scene's picture on the terminal,
//                                        sending only what changed
//
// A scene never sees a letter, an escape code or a frame rate. Minimal use:
//
//   struct MyScene : vtscreen::Scene {
//       vtscreen::Info info() const override { return {"my scene", 24.0, {255, 140, 40}}; }
//       void setup(int cols, int rows) override { ... }
//       void frame(double t, vtscreen::Picture& F) override { ... }
//   };
//   int main(int argc, char** argv) { MyScene s; return vtscreen::run(s, argc, argv); }
//
// run() understands: --loop  --no-color  --still T  --fps N  --cols N  --rows N
//
// The letters (LEVELS) are grouped by how much ink they carry, in the style of
// the python.animate channel's ASCII shorts: https://www.youtube.com/@python.animate

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace vtscreen {

// ---------------------------------------------------------------- tuning

// Colours are rounded to this many levels per channel before they are sent, so
// neighbouring cells share a colour code more often (fewer bytes per frame).
#ifndef VTSCREEN_COLOR_LEVELS
#define VTSCREEN_COLOR_LEVELS 16
#endif
inline constexpr int COLOR_LEVELS = VTSCREEN_COLOR_LEVELS;

// Redraw everything now and then, in case the terminal missed something.
inline constexpr double REFRESH_SECONDS = 10.0;

// The letters inside a group rotate once every this many frames, so the shimmer
// speeds up with --fps (2.5 frames = 12 steps a second at the default 30 fps).
inline constexpr double SHIMMER_FRAMES = 2.5;

// Letters grouped by how much ink they carry, dark -> bright.
inline const std::vector<std::string> LEVELS = {
    " ", ".`'", ":;,-~", "=+*^", "ijltrxz/\\|", "oecnuv()", "0O8B&%", "#@$QW"};

// ---------------------------------------------------------------- what a scene gives us

// What a scene draws on: a brightness and a colour for every cell, no letters.
// lum has one entry per cell (rows * cols, row by row), 0..1; rgb has three
// numbers per cell, 0..255. hud holds short text lines drawn over the top-left
// corner, one per row. The Display turns this into letters.
struct Picture {
    std::vector<double> lum;
    std::vector<double> rgb;
    std::vector<std::string> hud;
};

struct Info {
    const char* title;     // left end of the status line
    double duration;       // seconds; without --loop the show stops here
    int accent[3];         // colour of the status line
    // How the letter inside a group is chosen. false: a fixed diagonal shuffle
    // (what the campfire, moon, fireworks and black hole use). true: a hash of
    // position and time, which has no diagonal pattern in large flat areas
    // (what the thunderstorm uses).
    bool hash_letters = false;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual Info info() const = 0;
    // Called once the size is known. rows is the picture only: the status line
    // takes one more row underneath.
    virtual void setup(int cols, int rows) = 0;
    // Fill F for time t (seconds). A pure function of t is easiest to reason about.
    virtual void frame(double t, Picture& F) = 0;

    // The scene's own picture, kept between frames so a Display can be pointed at
    // it (and, later, two of them mixed). cycle(t) refreshes it for time t.
    Picture screen;
    void cycle(double t) { frame(t, screen); }
};

// ---------------------------------------------------------------- mixing two pictures

// Mix two pictures of the same size into out: pct 0 gives a, 1 gives b, in
// between is a cross-fade. Brightness and colour are mixed cell by cell, before
// any letters are picked, so the Display picks the letters once for the mixture.
// The HUD text lines come from a up to the halfway point and from b after it.
inline void fade(const Picture& a, const Picture& b, double pct, Picture& out) {
    pct = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
    const double wa = 1.0 - pct, wb = pct;
    out.lum.resize(a.lum.size());
    out.rgb.resize(a.rgb.size());
    for (size_t i = 0; i < a.lum.size(); i++) out.lum[i] = a.lum[i] * wa + b.lum[i] * wb;
    for (size_t i = 0; i < a.rgb.size(); i++) out.rgb[i] = a.rgb[i] * wa + b.rgb[i] * wb;
    out.hud = pct < 0.5 ? a.hud : b.hud;
}

// ---------------------------------------------------------------- letters and colours

namespace detail {
inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}

// round one colour channel (0..255) to COLOR_LEVELS evenly spaced values
inline int quant(double v) {
    const double step = 255.0 / (COLOR_LEVELS - 1);
    return (int)(std::floor(detail::clampd(v, 0, 255) / step + 0.5) * step + 0.5);
}

// 16 brightness steps. 0 .. 0.5 uses groups 0..7 as they are; 0.5 .. 1 uses
// groups 7..0 drawn in inverse video, so the hottest cells are solid colour.
// The group picks the letters; position and time pick one, so it shimmers.
inline char pick_char(double lum, int i, int j, double t, int fps, bool& inv,
                      bool hash = false) {
    int n = (int)LEVELS.size();
    int step = std::min(std::max((int)(lum * 2 * n), 0), 2 * n - 1);
    inv = step >= n;
    const std::string& g = LEVELS[inv ? 2 * n - 1 - step : step];
    if (hash) {
        uint32_t h = (uint32_t)i * 73856093u ^ (uint32_t)j * 19349663u ^
                     (uint32_t)(long)(t * fps / SHIMMER_FRAMES) * 83492791u;
        return g[h % g.size()];
    }
    long shuffle = (long)i * 7 + (long)j * 13 + (long)(t * fps / SHIMMER_FRAMES);
    return g[shuffle % (long)g.size()];
}

// The whole frame as one block of text, every cell written (used for --still).
// vt_inv false: no inverse video codes, so the output is plain text.
inline std::string render_plain(const Picture& F, int R, int C, double t, int fps,
                                bool color, bool erase = true, bool vt_inv = true,
                                bool hash_letters = false) {
    std::string out;
    out.reserve((size_t)R * C * (color ? 12 : 2));
    char esc[40];
    for (int i = 0; i < R; i++) {
        int last[3] = {-1, -1, -1};
        bool on = false;                       // inverse video currently on
        const std::string* hud = i < (int)F.hud.size() ? &F.hud[i] : nullptr;
        if (hud && color) out += "\x1b[38;2;170;170;170m";
        for (int j = 0; j < C; j++) {
            if (hud && j < (int)hud->size()) { out += (*hud)[j]; continue; }
            if (hud && j == (int)hud->size()) { last[0] = -1; }   // force colour resync
            bool inv;
            char ch = pick_char(F.lum[i * C + j], i, j, t, fps, inv, hash_letters);
            inv = inv && vt_inv;
            if (inv != on) { out += inv ? "\x1b[7m" : "\x1b[27m"; on = inv; }
            if (ch == ' ' && !inv) { out += ' '; continue; }
            if (color) {
                int px[3];
                for (int c = 0; c < 3; c++)
                    px[c] = quant(F.rgb[(i * C + j) * 3 + c]);
                if (px[0] != last[0] || px[1] != last[1] || px[2] != last[2]) {
                    std::snprintf(esc, sizeof esc, "\x1b[38;2;%d;%d;%dm", px[0], px[1], px[2]);
                    out += esc;
                    std::memcpy(last, px, sizeof last);
                }
            }
            out += ch;
        }
        if (color || on) out += "\x1b[0m";
        if (erase) out += "\x1b[K";
        out += "\n";
    }
    return out;
}

// ---------------------------------------------------------------- a screen: what a scene draws on
//
// A VtScreen is plain data: a grid of cells, each a letter, an inverse flag and
// a colour. It knows nothing about terminals. Every scene has its own, and a
// Display (below) shows whichever one it is given.
struct VtCell {
    char ch = ' ';
    bool inv = false;
    unsigned char r = 0, g = 0, b = 0;

    bool operator==(const VtCell& o) const {
        return ch == o.ch && inv == o.inv && r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const VtCell& o) const { return !(*this == o); }
};

inline VtCell make_cell(char ch, bool inv, int r, int g, int b) {
    if (ch == ' ' && !inv) r = g = b = 0;     // a plain space has no visible colour
    VtCell k;
    k.ch = ch; k.inv = inv;
    k.r = (unsigned char)r; k.g = (unsigned char)g; k.b = (unsigned char)b;
    return k;
}

struct VtScreen {
    int rows = 0, cols = 0;
    std::vector<VtCell> cell;                 // rows * cols, row by row

    void init(int R, int C) { rows = R; cols = C; cell.assign((size_t)R * C, VtCell()); }
    void set(int i, int j, char ch, bool inv, int r, int g, int b) {
        cell[(size_t)i * cols + j] = make_cell(ch, inv, r, g, b);
    }
    const VtCell& at(int i, int j) const { return cell[(size_t)i * cols + j]; }
};

// ---------------------------------------------------------------- options and terminal basics

struct Options {
    bool loop = false;        // keep going after duration: the clock just runs on
    bool color = true;
    bool still = false;       // print one frame as text and exit
    double still_t = 0;
    int fps = 30;
    int cols = 0, rows = 0;   // 0: use the terminal's size
};

inline volatile sig_atomic_t g_stop = 0;
inline void on_sigint(int) { g_stop = 1; }

inline void write_all(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n <= 0) break;
        p += n;
        left -= (size_t)n;
    }
}

// The status line text, exactly cols wide. actual = frames really drawn per
// second over the last second (< 0: not known yet).
inline std::string status_text(const Info& info, int cols, double t, bool loop,
                               double actual, int fps) {
    char fpstext[32];
    if (actual >= 0) std::snprintf(fpstext, sizeof fpstext, "%.1f/%d fps", actual, fps);
    else             std::snprintf(fpstext, sizeof fpstext, "--/%d fps", fps);
    char right[80];
    if (loop)
        std::snprintf(right, sizeof right, "running  %5.1fs  %s", t, fpstext);
    else
        std::snprintf(right, sizeof right, "running  %4.1fs / %.0fs  %s", t, info.duration, fpstext);
    std::string left = info.title;
    int pad = std::max(1, cols - (int)left.size() - (int)std::strlen(right));
    std::string line = (left + std::string(pad, ' ') + right).substr(0, cols);
    line.resize(cols, ' ');
    return line;
}

#if 0   // retired: the status line is now drawn by the Display
static std::string status(int cols, double t, bool color, bool loop) {
    char right[64];
    if (loop)
        std::snprintf(right, sizeof right, "running  %5.1fs  %d fps", t, FPS);
    else
        std::snprintf(right, sizeof right, "running  %4.1fs / %.0fs  %d fps", t, DURATION, FPS);
    std::string left = "campfire (C++)";
    int pad = std::max(1, cols - (int)left.size() - (int)std::strlen(right));
    std::string line = (left + std::string(pad, ' ') + right).substr(0, cols);
    if (!color) return line;
    char head[48];
    std::snprintf(head, sizeof head, "\x1b[7;38;2;%d;%d;%dm", ACCENT[0], ACCENT[1], ACCENT[2]);
    return std::string(head) + line + "\x1b[0m";
}
#endif

// ---------------------------------------------------------------- the display: pictures onto the terminal
//
// The Display is the renderer. Given a Picture (brightness and colour for every
// cell) it picks the letters (16 steps, inverse video, shimmer), compares them
// with what the terminal is showing (a VtScreen of its own), sends only the
// cells that differ, and updates its copy. It tracks the terminal's "pen" (the
// last colour and inverse flag sent) and cursor, so codes and cursor moves are
// only sent when they have to be. It does not matter which Picture is shown from
// one frame to the next: the first frame after a switch sends the differences.
class Display {
public:
    bool color = true;            // send colour codes
    bool hash_letters = false;    // how the letter inside a group is chosen
    int  fps = 30;                // the shimmer speed follows this

    // pic_rows is the height of the pictures it will be given; one more row is
    // kept underneath for the status line.
    void init(int pic_rows, int cols) {
        shown.init(pic_rows + 1, cols);
        target.init(pic_rows, cols);
    }

    // clear the terminal and forget everything: the next render sends it all
    std::string refresh() {
        for (VtCell& k : shown.cell) k = VtCell();
        pen_inv = 0; pr = pg = pb = -1; cur_r = cur_c = -1;
        return "\x1b[0m\x1b[H\x1b[2J";
    }

    // pic: init()'s pic_rows by cols. t: seconds (drives the shimmer). status:
    // one line, exactly cols wide, drawn in the accent colour in inverse video
    // underneath the picture. Returns the bytes to write to the terminal.
    std::string render(const Picture& pic, double t, const std::string& status,
                       const int accent[3]) {
        to_letters(pic, t);
        return send(status, accent);
    }

    // ---- the terminal session: the part a program's main loop talks to ----
    //
    //   Display d;  d.hash_letters = ...;  d.open(options, cols, rows);
    //   (set up the scenes)  d.start();
    //   while (!d.stopping()) { t = d.time(); ...fill a picture...;
    //                           d.show(picture, t, info, loop);  d.end_frame(); }
    //
    // The terminal is put back when the Display goes out of scope.

    // Switch to the alternate screen, hide the cursor, read keys one at a time,
    // catch Ctrl-C. cols x rows is the whole terminal (see resolve_size()); the
    // picture is one row shorter, the status line takes the last row. Takes
    // color and fps from o.
    void open(const Options& o, int cols, int rows) {
        color = o.color;
        fps = o.fps;
        cols_ = cols; rows_ = rows;
        tty_in = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_tio) == 0;
        struct sigaction sa{};
        sa.sa_handler = on_sigint;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        write_all("\x1b[?1049h\x1b[?25l\x1b[2J");
        if (tty_in) {
            termios raw = old_tio;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
        }
        init(rows - 1, cols);                  // the picture; the status line goes underneath
        write_all(refresh());
        is_open = true;
    }

    void close() {
        if (!is_open) return;
        write_all("\x1b[0m\x1b[?25h\x1b[?1049l");
        if (tty_in) tcsetattr(STDIN_FILENO, TCSADRAIN, &old_tio);
        is_open = false;
    }
    ~Display() { close(); }

    int cols() const { return cols_; }
    int pic_rows() const { return rows_ - 1; }

    // Start the clock (call it once the scenes are set up, so t starts at 0).
    void start() {
        start_ = clk::now();
        n_ = 0; fps_frames_ = 0; fps_start_ = 0; actual_ = -1; last_refresh_ = 0;
    }
    double time() const { return std::chrono::duration<double>(clk::now() - start_).count(); }
    bool stopping() const { return g_stop != 0; }
    void stop() { g_stop = 1; }

    // Draw a picture on the terminal now: refresh everything if it is due, then
    // render() and write. info supplies the title, accent colour and duration for
    // the status line; loop only changes how the time is shown there.
    void show(const Picture& pic, double t, const Info& info, bool loop) {
        std::string pre;
        if (t - last_refresh_ >= REFRESH_SECONDS) { pre = refresh(); last_refresh_ = t; }
        write_all(pre + render(pic, t, status_text(info, cols_, t, loop, actual_, fps), info.accent));
        fps_frames_++;
        if (t - fps_start_ >= 1.0) { actual_ = fps_frames_ / (t - fps_start_); fps_frames_ = 0; fps_start_ = t; }
    }

    // The next key pressed since the last call (other than q, which quits), or 0
    // if there is none. Keys are collected by end_frame().
    char next_key() {
        if (keys_.empty()) return 0;
        char c = keys_.front();
        keys_.erase(0, 1);
        return c;
    }

    // Wait until the next frame is due, reading the keyboard meanwhile (q quits).
    void end_frame() {
        const double dt = 1.0 / fps;
        n_++;
        double wait = (n_ * dt) - time();
        if (wait < -1.0)                       // fell far behind: resync
            n_ = (long)(time() / dt);
        if (tty_in) {
            pollfd pfd{STDIN_FILENO, POLLIN, 0};
            int ms = wait > 0 ? (int)(wait * 1000) : 0;
            if (poll(&pfd, 1, ms) > 0) {
                char c;
                while (read(STDIN_FILENO, &c, 1) == 1) {
                    if (c == 'q' || c == 'Q') g_stop = 1;
                    else keys_.push_back(c);
                }
            }
        } else if (wait > 0) {
            usleep((useconds_t)(wait * 1e6));
        }
    }

private:
    using clk = std::chrono::steady_clock;
    bool is_open = false, tty_in = false;
    termios old_tio{};
    int cols_ = 0, rows_ = 0;
    std::string keys_;                            // keys pressed, not yet read
    clk::time_point start_ = clk::now();
    long n_ = 0;                                  // frames due so far
    long fps_frames_ = 0;                         // frames drawn in the current second
    double fps_start_ = 0, actual_ = -1, last_refresh_ = 0;

    VtScreen shown;                               // what the terminal is showing
    VtScreen target;                              // the picture as letters, this frame
    int pen_inv = 0, pr = -1, pg = -1, pb = -1;   // what the terminal will use next
    int cur_r = -1, cur_c = -1;                   // where its cursor is (-1: unknown)

    // brightness and colour -> letter, inverse flag and rounded colour per cell
    void to_letters(const Picture& F, double t) {
        const int R = target.rows, C = target.cols;
        for (int i = 0; i < R; i++) {
            const std::string* hud = i < (int)F.hud.size() ? &F.hud[i] : nullptr;
            for (int j = 0; j < C; j++) {
                if (hud && j < (int)hud->size()) {
                    target.set(i, j, (*hud)[j], false, 170, 170, 170);
                    continue;
                }
                bool inv;
                char ch = pick_char(F.lum[i * C + j], i, j, t, fps, inv, hash_letters);
                int px[3] = {0, 0, 0};
                if (color)
                    for (int c = 0; c < 3; c++) px[c] = quant(F.rgb[(i * C + j) * 3 + c]);
                target.set(i, j, ch, inv, px[0], px[1], px[2]);
            }
        }
    }

    // compare target (plus the status line) with shown and write the differences
    std::string send(const std::string& status, const int accent[3]) {
        const int rows = shown.rows, cols = shown.cols;
        std::string out;
        char esc[40];
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                const VtCell k = i < target.rows
                    ? target.at(i, j)
                    : make_cell(status[j], color, accent[0], accent[1], accent[2]);
                VtCell& sh = shown.cell[(size_t)i * cols + j];
                if (k == sh) continue;
                // A short gap of unchanged cells is cheaper to write again than to
                // skip with a cursor move, when no colour or inverse code is needed.
                if (cur_r == i && cur_c >= 0 && j - cur_c <= 3) {
                    bool free = true;
                    for (int x = cur_c; x < j && free; x++) {
                        const VtCell& u = shown.at(i, x);
                        bool plain = u.ch == ' ' && !u.inv;
                        free = (int)u.inv == pen_inv &&
                               (plain || !color || (pr == u.r && pg == u.g && pb == u.b));
                    }
                    if (free) {
                        for (int x = cur_c; x < j; x++) out += shown.at(i, x).ch;
                        cur_c = j;
                    }
                }
                if (cur_r == i && cur_c == j) {}
                else if (cur_r == i && cur_c >= 0 && cur_c < j) {
                    int g = j - cur_c;
                    if (g == 1) out += "\x1b[C";
                    else { std::snprintf(esc, sizeof esc, "\x1b[%dC", g); out += esc; }
                } else {
                    std::snprintf(esc, sizeof esc, "\x1b[%d;%dH", i + 1, j + 1);
                    out += esc;
                }
                if (pen_inv != (int)k.inv) { out += k.inv ? "\x1b[7m" : "\x1b[27m"; pen_inv = k.inv; }
                if (color && !(k.ch == ' ' && !k.inv) && (pr != k.r || pg != k.g || pb != k.b)) {
                    std::snprintf(esc, sizeof esc, "\x1b[38;2;%d;%d;%dm", k.r, k.g, k.b);
                    out += esc;
                    pr = k.r; pg = k.g; pb = k.b;
                }
                out += k.ch;
                sh = k;
                cur_r = i; cur_c = j + 1;
                if (cur_c >= cols) cur_r = cur_c = -1;   // pending wrap: position unknown
            }
        }
        return out;
    }
};

// ---------------------------------------------------------------- the player

inline void play(Scene& scene, const Options& o, int cols, int rows) {
    const Info info = scene.info();
    Display display;
    display.hash_letters = info.hash_letters;
    display.open(o, cols, rows);
    scene.setup(cols, rows - 1);
    display.start();
    while (!display.stopping()) {
        double t = display.time();
        // With --loop the clock just keeps running: the scene carries on
        // from where it was instead of restarting at t = 0.
        if (t >= info.duration && !o.loop) break;
        scene.cycle(t);
        display.show(scene.screen, t, info, o.loop);
        display.end_frame();
    }
}

inline void usage(const char* me) {
    std::fprintf(stderr,
        "usage: %s [--loop] [--no-color] [--still T] [--fps N] [--cols N] [--rows N]\n", me);
}

// Read the command line into o. Returns -1 to carry on, otherwise the exit code
// the program should end with (0 for --help, 2 for a bad option).
inline int parse_options(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--loop") o.loop = true;
        else if (a == "--no-color") o.color = false;
        else if (a == "--still" && i + 1 < argc) { o.still = true; o.still_t = std::atof(argv[++i]); }
        else if (a == "--fps" && i + 1 < argc) o.fps = std::min(std::max(std::atoi(argv[++i]), 1), 240);
        else if (a == "--cols" && i + 1 < argc) o.cols = std::atoi(argv[++i]);
        else if (a == "--rows" && i + 1 < argc) o.rows = std::atoi(argv[++i]);
        else { usage(argv[0]); return a == "--help" || a == "-h" ? 0 : 2; }
    }
    return -1;
}

// The terminal's size, or the options' --cols / --rows. Prints a message and
// returns false if it is smaller than 40 x 20.
inline bool resolve_size(const Options& o, int& cols, int& rows) {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) { ws.ws_col = 100; ws.ws_row = 40; }
    cols = o.cols ? o.cols : ws.ws_col;
    rows = o.rows ? o.rows : ws.ws_row;
    if (cols < 40 || rows < 20) {
        std::fprintf(stderr, "terminal too small (%dx%d); need at least 40x20\n", cols, rows);
        return false;
    }
    return true;
}

// Everything a one-scene program's main() needs: read the options, find the
// terminal size, then play the scene (or print one still frame).
inline int run(Scene& scene, int argc, char** argv) {
    Options o;
    int rc = parse_options(argc, argv, o);
    if (rc >= 0) return rc;
    int cols, rows;
    if (!resolve_size(o, cols, rows)) return 1;

    if (o.still) {
        const Info info = scene.info();
        scene.setup(cols, rows - 1);
        Picture F;
        scene.frame(o.still_t, F);
        std::string s = render_plain(F, rows - 1, cols, o.still_t, o.fps, false, false,
                                     isatty(STDOUT_FILENO), info.hash_letters);
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }
    play(scene, o, cols, rows);
    return 0;
}

}  // namespace vtscreen
