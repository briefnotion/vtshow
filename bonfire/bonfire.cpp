// A bonfire of letters: flames that lick and sway in a breeze, a warm glow on the
// ground, sparks that rise and fade, and a few specks of ash in the dark.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made: the flame is a tall soft blob (narrower and fainter the higher
// you look) that is bent sideways by the breeze, shaken by value noise and cut into
// tongues by more noise that scrolls upwards. Brightness is then mapped to a fire
// palette (dark red, orange, yellow, white). Sparks and ash are not simulated:
// each one is a "slot" that starts a new life every few seconds, with its start
// position, speed and drift taken from a hash of (slot, life number), so where it
// is at time t can be worked out directly.
//
//   g++ -O2 -std=c++17 -o bonfire bonfire.cpp
//   ./bonfire              play once (24 s); q or Ctrl-C quits
//   ./bonfire --loop       keep going
//   ./bonfire --still 6    print the frame at t=6 s as plain text and exit
//   ./bonfire --no-color   letters only
//   ./bonfire --fps N      frames per second (default 30)
//   ./bonfire --cols N --rows N

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
static const int HUD_ROWS = 3;

// ---------------------------------------------------------------- small helpers

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline double smooth(double v) {
    v = clampd(v, 0.0, 1.0);
    return v * v * (3 - 2 * v);
}
static inline double lerp(double a, double b, double f) { return a + (b - a) * f; }

// integer hash -> 0..1; the same on every machine, and fine for any (a, b, c)
static inline uint32_t mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352dU;
    h ^= h >> 15; h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}
static inline double rnd(uint32_t a, uint32_t b = 0, uint32_t c = 0) {
    uint32_t h = mix(a * 0x9E3779B1u ^ mix(b + 0x85ebca6bu) ^ mix(c * 0xc2b2ae35u + 0x27d4eb2fu));
    return (h >> 8) * (1.0 / 16777216.0);
}

// smooth value noise, 0..1
static double vnoise(double x, double y) {
    double fx0 = std::floor(x), fy0 = std::floor(y);
    uint32_t ix = (uint32_t)(int32_t)fx0, iy = (uint32_t)(int32_t)fy0;
    double fx = smooth(x - fx0), fy = smooth(y - fy0);
    double a = rnd(ix, iy, 1), b = rnd(ix + 1, iy, 1);
    double c = rnd(ix, iy + 1, 1), d = rnd(ix + 1, iy + 1, 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
static double fbm(double x, double y) {
    return 0.55 * vnoise(x, y) + 0.30 * vnoise(2 * x + 5.2, 2 * y + 1.3)
         + 0.15 * vnoise(4 * x + 9.1, 4 * y + 3.7);
}
static double vn1(double x) { return vnoise(x, 11.5); }      // noise along one line

// fire palette: lum 0 .. 1 -> darkness, dark red, red-orange, orange, yellow, hot white
static void palette(double lum, double out[3]) {
    static const double xs[6] = {0.0, 0.18, 0.38, 0.58, 0.80, 1.0};
    static const double cs[6][3] = {{6, 2, 2}, {85, 12, 6}, {185, 38, 8},
                                    {255, 105, 18}, {255, 190, 60}, {255, 246, 200}};
    lum = clampd(lum, 0.0, 1.0);
    int k = 0;
    while (k < 4 && lum > xs[k + 1]) k++;
    double f = (lum - xs[k]) / (xs[k + 1] - xs[k]);
    for (int c = 0; c < 3; c++) out[c] = lerp(cs[k][c], cs[k + 1][c], f);
}

// the breeze at time t: roughly -1 .. 1, slow, a little gusty
static double breeze(double t) {
    return 0.55 * std::sin(0.5 * t + 0.7) + 0.6 * (vn1(t * 0.35 + 9.0) - 0.5) * 2.0;
}

// ---------------------------------------------------------------- the scene

class Bonfire : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"bonfire (C++)", DURATION, {255, 140, 40}, true};
    }

    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        ground = rows * 0.84;                       // the row where the fire sits
        cx = cols * 0.5;
        // Heights are measured in column widths (a cell is about twice as tall as
        // wide), so that circles come out round: y = 2 * rows above the ground.
        Hc = rows * 0.62 * 2.0;                     // tallest the flame gets
        W0 = std::min(0.10 * cols, 0.30 * Hc);      // half width of the flame at its foot
        logH = std::max(2.2, 0.075 * Hc);           // the pile of logs
        nspark = (int)clampd(cols * rows / 60.0, 24, 160);
        nash = (int)clampd(cols * rows / 120.0, 12, 120);
    }

    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0, nspark = 0, nash = 0;
    double ground = 0, cx = 0, Hc = 1, W0 = 1, logH = 1;
};

void Bonfire::frame(double t, Picture& F) {
    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);

    // the fire's mood at this moment
    const double flick = 1.0 + 0.05 * std::sin(t * 9.1) + 0.04 * std::sin(t * 15.7 + 1.0)
                       + 0.16 * (vn1(t * 2.3) - 0.5);
    const double Ht = Hc * (0.90 + 0.07 * std::sin(t * 2.0) + 0.28 * (vn1(t * 0.9 + 4.0) - 0.5));
    const double wind = breeze(t);
    const double bend = wind * W0 * 0.9;            // how far the tip leans, in columns

    for (int i = 0; i < R; i++) {
        const double y = (ground - (i + 0.5)) * 2.0;     // height above the ground line
        for (int j = 0; j < C; j++) {
            const double x = j + 0.5 - cx;
            double lum, col[3];

            // ---- the dark, lit only by the fire: ground below the line, haze above
            if (y < 0) {
                double gx = x / (W0 * 2.4), gd = -y / (W0 * 1.1);
                double glow = std::exp(-(gx * gx + gd * gd)) * flick;
                double tex = 0.75 + 0.5 * vnoise(j * 0.30, i * 0.9);
                lum = 0.012 + 0.55 * glow * glow * tex;
            } else {
                double hx = x / (W0 * 2.6), hy = y / (Ht * 0.95);
                lum = 0.17 * std::exp(-(hx * hx + hy * hy)) * flick * (0.6 + 0.4 * vnoise(j * 0.2, i * 0.3 - t * 0.5));
            }
            palette(lum, col);

            // ---- the flame
            const double hn = y / Ht;                       // 0 at the foot, 1 at full height
            if (y > -0.5 && hn < 1.6 && std::fabs(x) < 3.0 * W0 + std::fabs(bend)) {
                double h1 = clampd(hn, 0.0, 1.0);
                double xs = x - bend * std::pow(h1, 1.6);             // the breeze bends it
                // shake it sideways, more towards the tip
                xs += (fbm(x / (W0 * 1.1), y / (W0 * 1.4) - t * 1.5 + 17.0) - 0.5)
                      * 0.7 * W0 * std::pow(h1, 0.8);
                // Each column has its own flame height: a hump across the fire, times a
                // noise that changes quickly with time, so tongues rise and drop.
                double u = xs / (W0 * 1.05);
                double hump = std::pow(std::max(0.0, 1.0 - u * u), 1.0);
                double tongue = 0.2 + 1.6 * fbm(xs / (W0 * 0.30), t * 1.7 + 40.0);
                double hcol = (Ht * 1.0 * tongue + logH) * hump;
                double tex = 0.7 + 0.6 * fbm(xs / (W0 * 0.5), y / (W0 * 1.0) - t * 2.2);   // flow, rising
                double v = (1.0 - std::pow(y / hcol, 1.5)) * tex;
                if (hcol < 0.5 || y >= hcol) v = 0.0;
                else v = clampd(v, 0.0, 1.0) * (1.05 - 0.2 * h1) * smooth((1.0 - std::fabs(u)) * 2.5);   // soft sides
                v = clampd(v * flick, 0.0, 1.0) * (0.35 + 0.65 * smooth(y / (logH * 1.3)));   // rises out of the logs
                if (v > lum) { lum = v; palette(v, col); }
            }

            // ---- the pile of logs at the foot, with embers breathing in them
            if (y > -1.4 && y < logH * 1.6) {
                double half = W0 * 1.15;
                double top = logH * (1.0 - smooth(std::fabs(x) / half));
                if (y < top && std::fabs(x) < half) {
                    double stripe = 0.5 + 0.5 * std::sin((x * 0.9 + y * 1.5) / (logH * 0.5));
                    double ember = clampd((vnoise(x / (W0 * 0.18), y / (W0 * 0.12) + 3.0) - 0.45) * 3.5, 0, 1)
                                 * (0.75 + 0.25 * std::sin(t * 1.7 + x * 0.3));
                    double lg = 0.17 + 0.07 * stripe + 0.5 * ember;
                    if (lg > lum) {
                        lum = lg;
                        for (int c = 0; c < 3; c++) {
                            static const double bark[3] = {52, 30, 18}, hot[3] = {255, 110, 25};
                            col[c] = lerp(bark[c] * (0.7 + 0.5 * stripe), hot[c], ember);
                        }
                    }
                }
            }
            F.lum[(size_t)i * C + j] = lum;
            for (int c = 0; c < 3; c++) F.rgb[((size_t)i * C + j) * 3 + c] = col[c];
        }
    }

    // ---- ash: grey specks rising slowly through the dark, brighter near the fire
    for (int a = 0; a < nash; a++) {
        double sp = 0.5 + 1.2 * rnd(a, 1, 50);                  // rows per second
        double ph = rnd(a, 2, 50);
        double span = R * 1.1;
        double pos = std::fmod(ph * span + sp * t, span);
        double frac = pos / span;
        double xcol = rnd(a, 3, 50) * C + std::sin(t * (0.25 + 0.4 * rnd(a, 4, 50)) + 6.0 * ph) * 2.5
                    + wind * 3.0 * frac * R * 0.1;
        double row = ground - pos;
        int ci = (int)std::floor(xcol), ri = (int)std::floor(row);
        if (ci < 0 || ci >= C || ri < 0 || ri >= R) continue;
        double hx = (xcol - cx) / (W0 * 3.5), hy = (ground - row) * 2.0 / (Hc * 1.1);
        double near = std::exp(-(hx * hx + hy * hy));
        double tw = 0.65 + 0.35 * std::sin(t * 2.0 + 17.0 * ph);
        double l = (0.085 + 0.10 * near) * std::sin(M_PI * frac) * tw * 1.6;
        size_t p = (size_t)ri * C + ci;
        if (l > F.lum[p]) {
            F.lum[p] = l;
            double k = 0.55 + 0.9 * near;
            F.rgb[p * 3 + 0] = clampd(150 * k, 0, 255);
            F.rgb[p * 3 + 1] = clampd(125 * k, 0, 255);
            F.rgb[p * 3 + 2] = clampd(110 * k, 0, 255);
        }
    }

    // ---- sparks: each slot starts a new life every few seconds
    int visible = 0;
    for (int s = 0; s < nspark; s++) {
        double life = 2.2 + 2.8 * rnd(s, 1, 100);
        double shift = rnd(s, 2, 100) * life;
        long gen = (long)std::floor((t + shift) / life);
        double age = (t + shift) - gen * life;
        uint32_t g = (uint32_t)gen;
        if (rnd(s, g, 101) > 0.65) continue;                    // not every life makes a spark
        double x0 = (rnd(s, g, 102) - 0.5) * W0 * 1.1;
        double y0 = Ht * (0.05 + 0.25 * rnd(s, g, 103));
        double vy = Hc * (0.18 + 0.35 * rnd(s, g, 104));
        double y = y0 + vy * age * (1.0 - 0.1 * age);
        double wob = rnd(s, g, 105);
        double x = x0 + wind * W0 * 0.5 * age + std::sin(age * (1.5 + 2.0 * wob) + 6.0 * wob) * W0 * 0.22 * wob
                 + bend * 0.5 * clampd(y / Hc, 0, 1);
        double k = age / life;
        double bright = std::pow(1.0 - k, 1.2) * (0.75 + 0.25 * std::sin(age * 23.0 + 9.0 * wob));
        int ci = (int)std::floor(cx + x), ri = (int)std::floor(ground - y / 2.0);
        if (ci < 0 || ci >= C || ri < 0 || ri >= R || bright <= 0.03) continue;
        visible++;
        // white-yellow when new, orange, then dull red as it cools
        double col[3] = {lerp(255, 175, smooth(k * 1.3)), lerp(238, 45, std::pow(k, 0.8)), lerp(165, 12, std::sqrt(k))};
        for (int pass = 0; pass < 2; pass++) {              // pass 1: a dimmer trail just below
            int rr = ri + pass;
            if (rr >= R) break;
            double l = (0.42 + 0.58 * bright) * (pass ? 0.45 : 1.0);
            size_t p = (size_t)rr * C + ci;
            if (l > F.lum[p]) {
                F.lum[p] = l;
                for (int c = 0; c < 3; c++) F.rgb[p * 3 + c] = col[c] * (pass ? 0.8 : 1.0);
            }
        }
    }

    const double fade = 0.4 + 0.6 * smooth(t / 1.5);
    for (double& v : F.lum) v = clampd(v * fade, 0, 1);

    char buf[64];
    F.hud.resize(HUD_ROWS);
    std::snprintf(buf, sizeof buf, "flame %.1f m", 2.2 * Ht / Hc);
    F.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "sparks %d", visible);
    F.hud[1] = buf;
    std::snprintf(buf, sizeof buf, "breeze %.0f km/h %s", 4.0 + 6.0 * std::fabs(wind),
                  wind > 0.05 ? "->" : (wind < -0.05 ? "<-" : "--"));
    F.hud[2] = buf;
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_bonfire() { return std::make_unique<Bonfire>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Bonfire scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
