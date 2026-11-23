// Digital rain: columns of green glyphs falling down a dark screen, each with a bright head and a
// long fading trail, the glyphs in a trail changing now and then. The classic look, with a few quiet
// touches that use the colour the player can draw:
//  * a soft glow behind the head of each stream (the alt colour, see PicCell in ../vtscreen/vtscreen.h);
//  * every column leans a hair toward teal or toward yellow-green, and the whole rain drifts slowly
//    between the two;
//  * three layers of depth: the far streams are slower, dimmer, a touch bluer;
//  * a faint vignette, and a slow wave of light that sweeps down the screen now and then;
//  * now and then a pale cyan "data" stream.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made. Each column has up to three streams, one per layer. A stream repeats with its own
// period: in each pass it starts after a random delay, its head falls at a fixed speed, and its trail
// has a random length. The head and trail are text cells: the scene chooses the glyph (half-width
// katakana, digits and a few symbols, set by USE_KATAKANA) and the colour. Each glyph changes at its
// own random slow pace; the one at the head changes quickly. The very faint end of a trail is drawn as
// ordinary dim cells instead, so it fades out to dots, and also in the --no-color mode.
//
//   g++ -O2 -std=c++17 -o matrix matrix.cpp
//   ./matrix              play once (24 s); q or Ctrl-C quits
//   ./matrix --loop       keep going
//   ./matrix --still 6    print the frame at t=6 s as plain text and exit
//   ./matrix --no-color   letters only
//   ./matrix --fps N      frames per second (default 30)
//   ./matrix --cols N --rows N

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../scenes.h"
#include "../vtscreen/vtscreen.h"

using vtscreen::Picture;

namespace {

static const double DURATION = 24.0;

// ---------------------------------------------------------------- knobs

// The glyphs: true is half-width katakana (one column wide in most terminals; the font needs it),
// digits and a few symbols, as in the film. false is plain ASCII, which always works.
static const bool USE_KATAKANA = true;

static const int LAYERS = 3;
// Per layer: how fast it falls (times the column's own speed), how bright, how much of the columns
// have a stream in it, and how far it leans toward blue.
static const double LAYER_SPEED[LAYERS] = {1.00, 0.72, 0.50};
static const double LAYER_BRIGHT[LAYERS] = {1.00, 0.58, 0.34};
static const double LAYER_DENSITY[LAYERS] = {0.80, 0.66, 0.55};
static const double LAYER_BLUE[LAYERS] = {0.00, 0.10, 0.22};

static const double SPEED_MIN = 6.5, SPEED_MAX = 17.0;        // rows per second, front layer
static const double TRAIL_MIN = 12.0, TRAIL_MAX = 44.0;       // rows
static const double GAP_MAX = 2.6;                            // seconds of quiet at most between passes
static const double TEXT_ABOVE = 0.18;                        // brighter than this is a glyph; fainter is a dot
static const double HEAD_GLOW = 1.0;                          // the glow behind the head (0 none)
static const double VIGNETTE = 0.40;                          // how much darker the edges are
static const double CYAN_STREAMS = 0.045;                     // the share of streams that are pale cyan
static const double SCAN_PERIOD = 19.0;                       // seconds between waves of light
static const double SCAN_STRENGTH = 0.45;

// ---------------------------------------------------------------- small helpers

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline double smooth(double v) {
    v = clampd(v, 0.0, 1.0);
    return v * v * (3 - 2 * v);
}
static inline double lerp(double a, double b, double f) { return a + (b - a) * f; }

static inline uint32_t mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}
static inline uint32_t hash4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return mix(a * 0x9E3779B1u ^ mix(b + 0x85ebca6bu) ^ mix(c * 0xc2b2ae35u + 0x27d4eb2fu) ^ mix(d * 0x165667b1u + 0x9e3779b9u));
}
static inline double rnd(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    return (hash4(a, b, c, d) >> 8) * (1.0 / 16777216.0);
}

// the glyphs
static std::vector<char32_t> make_glyphs() {
    std::vector<char32_t> g;
    if (USE_KATAKANA) {
        for (char32_t c = 0xFF66; c <= 0xFF9D; c++) g.push_back(c);     // half-width katakana, ｦ .. ﾝ
        for (int k = 0; k < 2; k++) for (char32_t c = U'0'; c <= U'9'; c++) g.push_back(c);   // digits, twice as common
        for (char32_t c : std::u32string(U":.=*+-<>|\"")) g.push_back(c);
    } else {
        for (char32_t c = U'0'; c <= U'9'; c++) g.push_back(c);
        for (char32_t c = U'A'; c <= U'Z'; c++) g.push_back(c);
        for (char32_t c : std::u32string(U":.=*+-<>|\"")) g.push_back(c);
    }
    return g;
}

// ---------------------------------------------------------------- the scene

class Matrix : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"matrix (C++)", DURATION, {60, 220, 100}, true};
    }

    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        glyphs = make_glyphs();
        stream.assign((size_t)C * LAYERS, Stream());
        for (int j = 0; j < C; j++) {
            for (int L = 0; L < LAYERS; L++) {
                Stream& s = stream[(size_t)j * LAYERS + L];
                const uint32_t sj = (uint32_t)j * 7u + (uint32_t)L * 1009u;
                s.present = rnd(sj, 1) < LAYER_DENSITY[L];
                s.speed = lerp(SPEED_MIN, SPEED_MAX, rnd(sj, 2)) * LAYER_SPEED[L];
                s.tmax = lerp(TRAIL_MIN, TRAIL_MAX, rnd(sj, 3));
                s.period = (R + s.tmax) / s.speed + GAP_MAX;
                s.phase = rnd(sj, 4) * s.period;
            }
        }
    }

    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    std::vector<char32_t> glyphs;
    struct Stream { bool present = false; double speed = 10, tmax = 20, period = 10, phase = 0; };
    std::vector<Stream> stream;
};

void Matrix::frame(double t, Picture& F) {
    F.init(R, C);

    // the slow drift of the whole rain, between a little teal and a little yellow-green
    const double drift = std::sin(t * 0.11) * 0.5 + std::sin(t * 0.047 + 1.3) * 0.5;       // -1 .. 1
    // the wave of light: where it is now (rows), 0 while it is away
    const double sp = std::fmod(t, SCAN_PERIOD) / SCAN_PERIOD;
    const double scan_y = sp < 0.55 ? (sp / 0.55) * (R + 24) - 12 : -1000.0;

    long streams_now = 0;
    const double nglyph = (double)glyphs.size();

    for (int j = 0; j < C; j++) {
        // each column's own lean: negative toward teal, positive toward yellow-green
        const double lean = (rnd((uint32_t)j, 91) - 0.5) * 0.9 + 0.35 * drift;
        const double fx = (j + 0.5) / C * 2.0 - 1.0;
        for (int i = 0; i < R; i++) {
            const double fy = (i + 0.5) / R * 2.0 - 1.0;
            const double vig = 1.0 - VIGNETTE * clampd(0.55 * fx * fx + 0.45 * fy * fy, 0.0, 1.0);
            double best = 0.0, best_head = 0.0, best_glow = 0.0, best_blue = 0.0, best_cyan = 0.0;
            int best_layer = 0;
            for (int L = 0; L < LAYERS; L++) {
                const Stream& s = stream[(size_t)j * LAYERS + L];
                if (!s.present) continue;
                const double u0 = t + s.phase;
                const long k = (long)std::floor(u0 / s.period);
                const double u = u0 - k * s.period;
                const uint32_t sj = (uint32_t)j * 7u + (uint32_t)L * 1009u, sk = (uint32_t)(int32_t)k;
                const double delay = rnd(sj, 5, sk) * GAP_MAX;
                const double trail = lerp(TRAIL_MIN, s.tmax, 0.35 + 0.65 * rnd(sj, 6, sk));
                const double yh = (u - delay) * s.speed - 1.0;                          // the head's row
                const double d = yh - (i + 0.5);                                        // rows behind the head
                if (yh < -2.0 || d < -0.5 || d > trail) continue;
                double v;                                                                // 0..1 brightness
                double head = 0.0;
                if (d < 0.5) { v = 1.0; head = 1.0; }
                else v = std::pow(1.0 - d / trail, 1.7);
                v *= LAYER_BRIGHT[L];
                if (v > best) {
                    best = v; best_layer = L; best_head = head;
                    best_glow = d < 4.0 ? (1.0 - d / 4.0) : 0.0;
                    best_blue = LAYER_BLUE[L];
                    best_cyan = rnd(sj, 8, sk) < CYAN_STREAMS ? 1.0 : 0.0;
                }
            }
            if (best <= 0.012) continue;

            // glyphs are fixed where the stream laid them, and change now and then at their own slow pace
            const uint32_t ci = (uint32_t)i, cj = (uint32_t)j, cl = (uint32_t)best_layer;
            const double per = 0.45 + 2.6 * rnd(cj, ci, 31 + cl);
            const double ph = rnd(cj, ci, 32 + cl) * per;
            const long epoch = best_head > 0.5 ? (long)std::floor(t * 14.0) : (long)std::floor((t + ph) / per);
            const uint32_t gi = hash4(cj, ci, 33 + cl, (uint32_t)(int32_t)epoch) % (uint32_t)nglyph;

            // brightness: a flicker of its own, the vignette, the wave of light
            double inten = best * (0.82 + 0.18 * rnd(cj, ci, 34 + cl, (uint32_t)(int32_t)epoch));
            inten *= vig;
            const double band = std::exp(-((i - scan_y) / 4.5) * ((i - scan_y) / 4.5));
            inten *= 1.0 + SCAN_STRENGTH * band;
            inten = clampd(inten, 0.0, 1.0);

            // colour: a green that goes from dark through bright to a pale head
            double col[3];
            const double a = clampd(inten, 0.0, 1.0);
            if (best_head > 0.5) {                                              // the head: nearly white, a little green
                col[0] = 226; col[1] = 255; col[2] = 236;
            } else {
                const double hi = smooth((a - 0.55) / 0.45);                    // the top of the trail goes pale
                col[0] = lerp(0.0, 150.0, hi) + 0.0;
                col[1] = lerp(34.0 + 190.0 * a, 255.0, hi);
                col[2] = lerp(10.0 + 50.0 * a, 190.0, hi);
            }
            // the lean of the column and of the layer: teal (more blue) or yellow-green (more red)
            const double teal = clampd(-lean, 0.0, 1.0) + best_blue, yel = clampd(lean, 0.0, 1.0);
            col[2] = col[2] + (255.0 - col[2]) * 0.55 * teal * (best_head > 0.5 ? 0.3 : 1.0) * 0.45;
            col[0] = col[0] + (210.0 - col[0]) * 0.65 * yel * (best_head > 0.5 ? 0.3 : 1.0) * 0.55;
            col[1] *= 1.0 - 0.10 * teal;
            if (best_cyan > 0.5 && best_head < 0.5) {                           // a pale cyan data stream
                col[0] = lerp(col[0], 120.0, 0.6); col[1] = lerp(col[1], 235.0, 0.5); col[2] = lerp(col[2], 255.0, 0.8);
            }
            // brightness of the colour itself follows the trail
            const double dim = best_head > 0.5 ? 1.0 : clampd(0.18 + 1.05 * a, 0.0, 1.0);
            for (int c = 0; c < 3; c++) col[c] *= dim;

            auto& pc = F.at(i, j);
            if (inten >= TEXT_ABOVE || best_head > 0.5) {
                pc.text = true; pc.ch = glyphs[gi]; pc.inv = false;
                pc.r = col[0]; pc.g = col[1]; pc.b = col[2];
                if (best_glow > 0.0 && HEAD_GLOW > 0.0) {                       // the glow behind the head
                    pc.alt_lum = 1.0;
                    const double gl = HEAD_GLOW * best_glow * best_glow * (0.25 + 0.75 * vig);
                    pc.alt_r = 6 + 30 * gl * (best_cyan > 0.5 ? 0.4 : 1.0); pc.alt_g = 22 + 84 * gl; pc.alt_b = 10 + 34 * gl + 24 * gl * best_blue * 4.0;
                    
                }
            } else {                                                            // the faint end of a trail: dots
                pc.lum = 0.065 + 0.055 * clampd(inten / TEXT_ABOVE, 0.0, 1.0);
                pc.r = col[0]; pc.g = col[1]; pc.b = col[2];
                pc.shimmer = 0.05 + 0.25 * rnd(cj, ci, 36);
            }
            if (best_head > 0.5) streams_now++;
        }
    }

    // the readout
    char buf[64];
    int active = 0;
    for (const Stream& s : stream) if (s.present) active++;
    std::snprintf(buf, sizeof buf, "streams %d", (int)streams_now);
    F.text(0, 0, buf, 90, 190, 110);
    std::snprintf(buf, sizeof buf, "layers %d, glyphs %d", LAYERS, (int)glyphs.size());
    F.text(1, 0, buf, 90, 190, 110);
    std::snprintf(buf, sizeof buf, "columns %d of %d", std::min(active, C), C);
    F.text(2, 0, buf, 90, 190, 110);
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_matrix() { return std::make_unique<Matrix>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Matrix scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
