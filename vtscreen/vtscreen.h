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
//     + text cells (the readout)       turns brightness into a letter: 16 steps,
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

// On Windows (_WIN32) the terminal, keyboard and timing code uses the Windows
// console API; elsewhere it uses POSIX (termios, poll). The Windows branch needs
// Windows 10 or later, where the console understands escape codes. Defining
// VTSCREEN_FORCE_WINDOWS selects it on any system (for compile-checking only).
#if defined(_WIN32) || defined(VTSCREEN_FORCE_WINDOWS)
#define VTSCREEN_WINDOWS 1
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef VTSCREEN_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX                  // keep std::min and std::max working
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#ifdef near                       // old Windows headers define these as empty macros
#undef near
#endif
#ifdef far
#undef far
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef M_PI                      // Visual C++ only defines it with _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

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

// ---------------------------------------------------------------- characters

// A cell holds one Unicode code point (char32_t); text goes in and out as UTF-8.
// Every character is taken to be one column wide: wide characters (Chinese, emoji)
// and combining marks are not handled, and would shift the rest of their line.
namespace detail {
// the next code point of a UTF-8 string, moving i past it; a bad sequence gives '?'
inline char32_t next_utf8(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int n;
    char32_t cp;
    if (c >= 0xC2 && c <= 0xDF)      { n = 1; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
    else return U'?';
    for (int k = 0; k < n; k++) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return U'?';
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    const char32_t least = n == 1 ? 0x80 : (n == 2 ? 0x800 : 0x10000);
    if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return U'?';
    return cp;
}

inline std::u32string to_u32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) out += next_utf8(s, i);
    return out;
}

inline void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) { out += (char)c; return; }
    if (c < 0x800) {
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += (char)(0xE0 | (c >> 12));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    } else {
        out += (char)(0xF0 | (c >> 18));
        out += (char)(0x80 | ((c >> 12) & 0x3F));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

inline std::string to_utf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) append_utf8(out, c);
    return out;
}
}  // namespace detail

// ---------------------------------------------------------------- what a scene gives us

// One cell of a scene's picture. Normally: how bright it is, what colour, and how
// fast its letter shimmers; the Display picks the letter. lum is 0..1; r, g, b are
// 0..255; shimmer is a speed, 1 = the normal rate (the letter inside its group
// changes once every SHIMMER_FRAMES frames), 0 = frozen, 3 = three times as fast.
//
// The alt colour is the other colour of the pair every cell has. The primary
// colour r, g, b is the letter in a dim cell and the solid block in a bright (inverse)
// cell; the alt colour is what is behind the letter in a dim cell and the letter
// itself in a bright one. alt_lum is its brightness, 0..1: the colour shown is
// alt_r, alt_g, alt_b scaled by alt_lum, and 0 means no alt colour at all (the
// terminal's own colour is left alone, and nothing extra is sent). With --no-color
// it is not used.
//
// A text cell (text = true) is different: it is a letter the scene chose itself.
// The Display draws ch, in inverse video if inv is set, in the colour r, g, b, and
// ignores lum and shimmer. Text is how the readout in the corner is drawn.
struct PicCell {
    double lum = 0;
    double r = 0, g = 0, b = 0;
    double shimmer = 1;
    double alt_lum = 0;
    double alt_r = 0, alt_g = 0, alt_b = 0;
    bool text = false;
    char32_t ch = U' ';
    bool inv = false;
};

// What a scene draws on: a grid of PicCells, rows * cols, row by row, the same
// shape as a VtScreen (which holds the finished letters), so it is the whole
// terminal. The Display turns it into letters. The last row is the status bar.
struct Picture {
    int rows = 0, cols = 0;
    std::vector<PicCell> cell;

    // Size it and clear every cell to the default. R is the rows of the picture
    // proper (what setup() was given); the status bar's row comes underneath, so
    // rows ends up R + 1.
    void init(int R, int C) { rows = R + 1; cols = C; cell.assign((size_t)(R + 1) * C, PicCell()); }
    int bar_row() const { return rows - 1; }
    PicCell& at(int i, int j) { return cell[(size_t)i * cols + j]; }
    const PicCell& at(int i, int j) const { return cell[(size_t)i * cols + j]; }

    // Write s (UTF-8) as text cells along row, starting at col, over whatever is
    // there. The default colour is the readout grey. What does not fit on the
    // picture is left off.
    void text(int row, int col, const std::string& s, double r = 170, double g = 170,
              double b = 170, bool inv = false) {
        if (row < 0 || row >= rows) return;
        const std::u32string u = detail::to_u32(s);
        for (size_t k = 0; k < u.size(); k++) {
            const int j = col + (int)k;
            if (j < 0 || j >= cols) continue;
            PicCell& c = at(row, j);
            c = PicCell();
            c.text = true; c.ch = u[k]; c.inv = inv;
            c.r = r; c.g = g; c.b = b;
        }
    }
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
// between is a cross-fade. Brightness, colour and shimmer are mixed cell by cell,
// before any letters are picked, so the Display picks the letters once for the
// mixture. Text cannot be mixed, so a cell that is text in either picture takes
// the letter from the picture that is ahead (a up to the halfway point, b after
// it). If both are text the colours are mixed as well; if only one is, the cell
// is that picture's cell as it is.
inline void fade(const Picture& a, const Picture& b, double pct, Picture& out) {
    pct = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
    const double wa = 1.0 - pct, wb = pct;
    out.rows = a.rows; out.cols = a.cols;
    out.cell.resize(a.cell.size());
    for (size_t i = 0; i < a.cell.size(); i++) {
        const PicCell& p = a.cell[i];
        const PicCell& q = b.cell[i];
        PicCell& o = out.cell[i];
        if (p.text || q.text) {
            o = pct < 0.5 ? p : q;
            if (p.text && q.text) {
                o.r = p.r * wa + q.r * wb;
                o.g = p.g * wa + q.g * wb;
                o.b = p.b * wa + q.b * wb;
                o.alt_lum = p.alt_lum * wa + q.alt_lum * wb;
                o.alt_r = p.alt_r * wa + q.alt_r * wb;
                o.alt_g = p.alt_g * wa + q.alt_g * wb;
                o.alt_b = p.alt_b * wa + q.alt_b * wb;
            }
            continue;
        }
        o = PicCell();
        o.lum = p.lum * wa + q.lum * wb;
        o.r = p.r * wa + q.r * wb;
        o.g = p.g * wa + q.g * wb;
        o.b = p.b * wa + q.b * wb;
        o.shimmer = p.shimmer * wa + q.shimmer * wb;
        o.alt_lum = p.alt_lum * wa + q.alt_lum * wb;
        o.alt_r = p.alt_r * wa + q.alt_r * wb;
        o.alt_g = p.alt_g * wa + q.alt_g * wb;
        o.alt_b = p.alt_b * wa + q.alt_b * wb;
    }
}

// ---------------------------------------------------------------- letters and colours

namespace detail {
inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// a colour channel (0..255) as the nearest whole number, not rounded to colour levels
inline int byte(double v) { return (int)(clampd(v, 0, 255) + 0.5); }

inline bool stdout_is_tty() {
#ifdef VTSCREEN_WINDOWS
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
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
// shimmer is the cell's speed (PicCell::shimmer): 1 = the normal rate, 0 = the
// letter stays put, 3 = three times as fast. Below 0 counts as 0.
inline char pick_char(double lum, int i, int j, double t, int fps, bool& inv,
                      bool hash = false, double shimmer = 1.0) {
    int n = (int)LEVELS.size();
    int step = std::min(std::max((int)(lum * 2 * n), 0), 2 * n - 1);
    inv = step >= n;
    const std::string& g = LEVELS[inv ? 2 * n - 1 - step : step];
    const long ticks = (long)(t * fps / SHIMMER_FRAMES * (shimmer > 0 ? shimmer : 0.0));
    if (hash) {
        uint32_t h = (uint32_t)i * 73856093u ^ (uint32_t)j * 19349663u ^
                     (uint32_t)ticks * 83492791u;
        // Mix the bits before taking the remainder: the low bits of the products above
        // repeat every few cells, which tiled the letters of a flat area.
        h ^= h >> 16; h *= 0x7feb352dU;
        h ^= h >> 15; h *= 0x846ca68bU;
        h ^= h >> 16;
        return g[h % g.size()];
    }
    long shuffle = (long)i * 7 + (long)j * 13 + ticks;
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
        bool alt_on = false;                   // a background colour currently set
        int lastalt[3] = {-1, -1, -1};
        for (int j = 0; j < C; j++) {
            const PicCell& pc = F.cell[i * C + j];
            bool inv;
            char32_t ch;
            if (pc.text) { ch = pc.ch; inv = pc.inv; }
            else ch = pick_char(pc.lum, i, j, t, fps, inv, hash_letters, pc.shimmer);
            inv = inv && vt_inv;
            if (inv != on) { out += inv ? "\x1b[7m" : "\x1b[27m"; on = inv; }
            if (color) {                                  // the alt colour (background)
                const bool alt = pc.alt_lum > 0;
                const double al = detail::clampd(pc.alt_lum, 0, 1);
                int ap[3] = {0, 0, 0};
                if (alt) {
                    if (pc.text) { ap[0] = detail::byte(pc.alt_r * al); ap[1] = detail::byte(pc.alt_g * al); ap[2] = detail::byte(pc.alt_b * al); }
                    else { ap[0] = quant(pc.alt_r * al); ap[1] = quant(pc.alt_g * al); ap[2] = quant(pc.alt_b * al); }
                }
                if (alt && (!alt_on || ap[0] != lastalt[0] || ap[1] != lastalt[1] || ap[2] != lastalt[2])) {
                    std::snprintf(esc, sizeof esc, "\x1b[48;2;%d;%d;%dm", ap[0], ap[1], ap[2]);
                    out += esc;
                    alt_on = true;
                    std::memcpy(lastalt, ap, sizeof lastalt);
                } else if (!alt && alt_on) {
                    out += "\x1b[49m";
                    alt_on = false;
                }
            }
            if (ch == U' ' && !inv) { out += ' '; continue; }
            if (color) {
                int px[3];
                if (pc.text) { px[0] = detail::byte(pc.r); px[1] = detail::byte(pc.g); px[2] = detail::byte(pc.b); }
                else { px[0] = quant(pc.r); px[1] = quant(pc.g); px[2] = quant(pc.b); }
                if (px[0] != last[0] || px[1] != last[1] || px[2] != last[2]) {
                    std::snprintf(esc, sizeof esc, "\x1b[38;2;%d;%d;%dm", px[0], px[1], px[2]);
                    out += esc;
                    std::memcpy(last, px, sizeof last);
                }
            }
            detail::append_utf8(out, ch);
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
    char32_t ch = U' ';
    bool inv = false;
    unsigned char r = 0, g = 0, b = 0;
    bool alt = false;                         // an alt colour is set (see PicCell)
    unsigned char ar = 0, ag = 0, ab = 0;     // it, already scaled by its brightness

    bool operator==(const VtCell& o) const {
        return ch == o.ch && inv == o.inv && r == o.r && g == o.g && b == o.b &&
               alt == o.alt && ar == o.ar && ag == o.ag && ab == o.ab;
    }
    bool operator!=(const VtCell& o) const { return !(*this == o); }
};

inline VtCell make_cell(char32_t ch, bool inv, int r, int g, int b, bool alt = false,
                        int ar = 0, int ag = 0, int ab = 0) {
    if (ch == U' ' && !inv) r = g = b = 0;     // a plain space has no visible letter colour
    VtCell k;
    k.ch = ch; k.inv = inv;
    k.r = (unsigned char)r; k.g = (unsigned char)g; k.b = (unsigned char)b;
    if (alt) {
        k.alt = true;
        k.ar = (unsigned char)ar; k.ag = (unsigned char)ag; k.ab = (unsigned char)ab;
    }
    return k;
}

struct VtScreen {
    int rows = 0, cols = 0;
    std::vector<VtCell> cell;                 // rows * cols, row by row

    void init(int R, int C) { rows = R; cols = C; cell.assign((size_t)R * C, VtCell()); }
    void set(int i, int j, char32_t ch, bool inv, int r, int g, int b, bool alt = false,
             int ar = 0, int ag = 0, int ab = 0) {
        cell[(size_t)i * cols + j] = make_cell(ch, inv, r, g, b, alt, ar, ag, ab);
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
#ifdef VTSCREEN_WINDOWS
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    while (left > 0) {
        DWORD want = (DWORD)(left > 65536 ? 65536 : left), n = 0;
        if (!WriteFile(h, p, want, &n, nullptr) || n == 0) break;
        p += n;
        left -= (size_t)n;
    }
#else
    while (left > 0) {
        ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n <= 0) break;
        p += n;
        left -= (size_t)n;
    }
#endif
}

// The status line text (UTF-8), exactly cols characters wide. actual = frames really drawn per
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
    const std::u32string left = detail::to_u32(info.title);
    const std::u32string r32 = detail::to_u32(right);
    int pad = std::max(1, cols - (int)left.size() - (int)r32.size());
    std::u32string line = left + std::u32string((size_t)pad, U' ') + r32;
    line.resize((size_t)cols, U' ');
    return detail::to_utf8(line);
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

    // pic_rows is the height of the pictures it will be given (Picture::init's R);
    // the status bar is one more row underneath, as in a Picture.
    void init(int pic_rows, int cols) {
        shown.init(pic_rows + 1, cols);
        target.init(pic_rows + 1, cols);
    }

    // clear the terminal and forget everything: the next render sends it all
    std::string refresh() {
        for (VtCell& k : shown.cell) k = VtCell();
        pen_inv = 0; pr = pg = pb = -1; pen_alt = false; par = pag = pab = -1;
        cur_r = cur_c = -1;
        return "\x1b[0m\x1b[H\x1b[2J";
    }

    // pic: a whole screen, as made by Picture::init (the bar row included). t:
    // seconds (drives the shimmer). Returns the bytes to write to the terminal.
    std::string render(const Picture& pic, double t) {
        to_letters(pic, t);
        return send();
    }

    // Write the default status bar into the bottom row of pic: the scene's title
    // (plus title_suffix, for a program to add its own hint), then the time and the
    // measured / target fps, in the scene's accent colour, in inverse video (plain
    // with --no-color). It is text cells like any other, so two bars cross-fade.
    // A scene that wrote its own text in the bar row keeps it.
    void bar(Picture& pic, const Info& info, double t, bool loop,
             const std::string& title_suffix = "") const {
        const int row = pic.bar_row();
        for (int j = 0; j < pic.cols; j++)
            if (pic.at(row, j).text) return;
        const std::string title = std::string(info.title) + title_suffix;
        Info shown_info = info;
        shown_info.title = title.c_str();
        pic.text(row, 0, status_text(shown_info, pic.cols, t, loop, actual_, fps),
                 info.accent[0], info.accent[1], info.accent[2], color);
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
#ifdef VTSCREEN_WINDOWS
        hout_ = GetStdHandle(STD_OUTPUT_HANDLE);
        hin_ = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        out_console_ = GetConsoleMode(hout_, &mode) != 0;
        if (out_console_) {                    // let the console understand escape codes
            old_out_mode_ = mode;
            if (!SetConsoleMode(hout_, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                std::fprintf(stderr, "this console does not understand terminal escape codes; "
                                     "use Windows 10 or later (Windows Terminal works best)\n");
                std::exit(1);
            }
        }
        old_cp_ = GetConsoleOutputCP();        // letters beyond ASCII go out as UTF-8
        SetConsoleOutputCP(CP_UTF8);
        tty_in = GetConsoleMode(hin_, &mode) != 0;
        std::signal(SIGINT, on_sigint);
        std::signal(SIGTERM, on_sigint);
        // a high-resolution timer for the wait between frames (Windows 10 1803 and
        // later), else an ordinary one, which is coarser (about 15 ms)
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);

        write_all("\x1b[?1049h\x1b[?25l\x1b[2J");
#else
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
#endif
        init(rows - 1, cols);                  // the picture; the status line goes underneath
        write_all(refresh());
        is_open = true;
    }

    void close() {
        if (!is_open) return;
        write_all("\x1b[0m\x1b[?25h\x1b[?1049l");
#ifdef VTSCREEN_WINDOWS
        if (out_console_) SetConsoleMode(hout_, old_out_mode_);
        if (old_cp_) SetConsoleOutputCP(old_cp_);
        if (tty_in) FlushConsoleInputBuffer(hin_);      // keys typed meanwhile are not for the shell
        if (timer_) { CloseHandle(timer_); timer_ = nullptr; }
#else
        if (tty_in) tcsetattr(STDIN_FILENO, TCSADRAIN, &old_tio);
#endif
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
    // render() and write. Call bar() on the picture first to give it a status bar.
    void show(const Picture& pic, double t) {
        std::string pre;
        if (t - last_refresh_ >= REFRESH_SECONDS) { pre = refresh(); last_refresh_ = t; }
        write_all(pre + render(pic, t));
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
#ifdef VTSCREEN_WINDOWS
        const double due = n_ * dt;
        for (;;) {
            double remaining = due - time();
            if (remaining <= 0 || g_stop) break;
            HANDLE objs[2];
            DWORD count = 0;
            if (timer_) {
                LARGE_INTEGER rel;
                rel.QuadPart = -(LONGLONG)(remaining * 1e7);    // negative: relative, in 100 ns units
                SetWaitableTimer(timer_, &rel, 0, nullptr, nullptr, FALSE);
                objs[count++] = timer_;
            }
            if (tty_in) objs[count++] = hin_;                    // wake early when a key arrives
            DWORD ms = timer_ ? INFINITE : (DWORD)(remaining * 1000 + 0.5);
            if (count > 0) WaitForMultipleObjects(count, objs, FALSE, ms);
            else Sleep(ms);
            read_keys();
        }
        read_keys();
#else
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
#endif
    }

private:
    using clk = std::chrono::steady_clock;
    bool is_open = false, tty_in = false;
#ifdef VTSCREEN_WINDOWS
    HANDLE hout_ = nullptr, hin_ = nullptr, timer_ = nullptr;
    DWORD old_out_mode_ = 0;
    UINT old_cp_ = 0;
    bool out_console_ = false;

    // take every key press waiting in the console: q quits, the rest are kept
    void read_keys() {
        if (!tty_in) return;
        INPUT_RECORD rec[64];
        DWORD avail = 0, got = 0;
        while (GetNumberOfConsoleInputEvents(hin_, &avail) && avail > 0) {
            if (!ReadConsoleInputW(hin_, rec, 64, &got)) break;
            for (DWORD i = 0; i < got; i++) {
                if (rec[i].EventType != KEY_EVENT || !rec[i].Event.KeyEvent.bKeyDown) continue;
                wchar_t w = rec[i].Event.KeyEvent.uChar.UnicodeChar;
                if (w <= 0 || w >= 128) continue;
                char c = (char)w;
                if (c == 'q' || c == 'Q') g_stop = 1;
                else keys_.push_back(c);
            }
        }
    }
#else
    termios old_tio{};
#endif
    int cols_ = 0, rows_ = 0;
    std::string keys_;                            // keys pressed, not yet read
    clk::time_point start_ = clk::now();
    long n_ = 0;                                  // frames due so far
    long fps_frames_ = 0;                         // frames drawn in the current second
    double fps_start_ = 0, actual_ = -1, last_refresh_ = 0;

    VtScreen shown;                               // what the terminal is showing
    VtScreen target;                              // the picture as letters, this frame
    int pen_inv = 0, pr = -1, pg = -1, pb = -1;   // what the terminal will use next
    bool pen_alt = false;                         // ... and its background colour (none, or
    int par = -1, pag = -1, pab = -1;             //     this one)
    int cur_r = -1, cur_c = -1;                   // where its cursor is (-1: unknown)

    // brightness and colour -> letter, inverse flag and rounded colour per cell
    void to_letters(const Picture& F, double t) {
        const int R = target.rows, C = target.cols;
        for (int i = 0; i < R; i++) {
            for (int j = 0; j < C; j++) {
                const PicCell& pc = F.cell[i * C + j];
                const bool alt = color && pc.alt_lum > 0;
                const double al = detail::clampd(pc.alt_lum, 0, 1);
                if (pc.text) {                  // a letter the scene chose: as it is
                    target.set(i, j, pc.ch, pc.inv, detail::byte(pc.r), detail::byte(pc.g),
                               detail::byte(pc.b), alt, detail::byte(pc.alt_r * al),
                               detail::byte(pc.alt_g * al), detail::byte(pc.alt_b * al));
                    continue;
                }
                bool inv;
                char ch = pick_char(pc.lum, i, j, t, fps, inv, hash_letters, pc.shimmer);
                int px[3] = {0, 0, 0};
                if (color) {
                    px[0] = quant(pc.r); px[1] = quant(pc.g); px[2] = quant(pc.b);
                }
                target.set(i, j, ch, inv, px[0], px[1], px[2], alt, quant(pc.alt_r * al),
                           quant(pc.alt_g * al), quant(pc.alt_b * al));
            }
        }
    }

    // compare target with shown and write the differences
    std::string send() {
        const int rows = shown.rows, cols = shown.cols;
        std::string out;
        char esc[40];
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                const VtCell k = target.at(i, j);
                VtCell& sh = shown.cell[(size_t)i * cols + j];
                if (k == sh) continue;
                // A short gap of unchanged cells is cheaper to write again than to
                // skip with a cursor move, when no colour or inverse code is needed.
                if (cur_r == i && cur_c >= 0 && j - cur_c <= 3) {
                    bool free = true;
                    for (int x = cur_c; x < j && free; x++) {
                        const VtCell& u = shown.at(i, x);
                        bool plain = u.ch == U' ' && !u.inv;
                        free = (int)u.inv == pen_inv &&
                               (plain || !color || (pr == u.r && pg == u.g && pb == u.b)) &&
                               (!color || (u.alt ? pen_alt && par == u.ar && pag == u.ag && pab == u.ab
                                                 : !pen_alt));
                    }
                    if (free) {
                        for (int x = cur_c; x < j; x++) detail::append_utf8(out, shown.at(i, x).ch);
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
                if (color) {
                    if (k.alt) {
                        if (!pen_alt || par != k.ar || pag != k.ag || pab != k.ab) {
                            std::snprintf(esc, sizeof esc, "\x1b[48;2;%d;%d;%dm", k.ar, k.ag, k.ab);
                            out += esc;
                            pen_alt = true; par = k.ar; pag = k.ag; pab = k.ab;
                        }
                    } else if (pen_alt) {
                        out += "\x1b[49m";
                        pen_alt = false;
                    }
                }
                if (color && !(k.ch == U' ' && !k.inv) && (pr != k.r || pg != k.g || pb != k.b)) {
                    std::snprintf(esc, sizeof esc, "\x1b[38;2;%d;%d;%dm", k.r, k.g, k.b);
                    out += esc;
                    pr = k.r; pg = k.g; pb = k.b;
                }
                detail::append_utf8(out, k.ch);
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
        display.bar(scene.screen, info, t, o.loop);
        display.show(scene.screen, t);
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
    int tcols = 100, trows = 40;               // used when the output is not a terminal
#ifdef VTSCREEN_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        tcols = info.srWindow.Right - info.srWindow.Left + 1;
        trows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col != 0) { tcols = ws.ws_col; trows = ws.ws_row; }
#endif
    cols = o.cols ? o.cols : tcols;
    rows = o.rows ? o.rows : trows;
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
                                     detail::stdout_is_tty(), info.hash_letters);
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }
    play(scene, o, cols, rows);
    return 0;
}

}  // namespace vtscreen
