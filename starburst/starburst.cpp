// Fireworks over a night sky: shells climb with a glowing trail, burst into
// sparks that slow in the air, droop under gravity and cool from white through
// their colour to ember red; some glitter at the end.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made: nothing is simulated step by step. A launch schedule repeats
// every CYCLE seconds; each shell has a time, a place, a colour and a burst style.
// A spark's position at any moment is a closed-form formula (air drag plus
// gravity), with its direction, speed and lifetime taken from a hash of
// (cycle, shell, spark). So frame(t) only has to ask where every live spark is.
// The cycle before the current one is also drawn, because its finale is still
// falling when the next cycle starts, which makes the repeat seamless.
//
//   g++ -O2 -std=c++17 -o starburst starburst.cpp
//   ./starburst              play once (24 s); q or Ctrl-C quits
//   ./starburst --loop       keep going
//   ./starburst --still 6    print the frame at t=6 s as plain text and exit
//   ./starburst --no-color   letters only
//   ./starburst --fps N      frames per second (default 30)
//   ./starburst --cols N --rows N

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
static const double CYCLE = 24.0;
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

// ---------------------------------------------------------------- the show

enum Style { PEONY, CHRYS, RING, WILLOW, DOUBLE };
static const char* STYLE_NAME[] = {"peony", "chrysanthemum", "ring", "willow", "double burst"};

// burst colours
static const double COLORS[8][3] = {{255, 60, 50},  {255, 190, 60}, {90, 255, 120}, {80, 170, 255},
                                    {255, 80, 220}, {225, 232, 255}, {170, 110, 255}, {255, 125, 30}};

struct Shell {
    double t;          // launch time in the cycle, seconds
    double x;          // across the sky, 0..1
    double h;          // burst height, 0..1 of the usable sky
    Style style;
    int color;
    double size;       // 1 = ordinary
};
static const Shell SHELLS[] = {
    {0.6, 0.30, 0.62, PEONY, 0, 1.0},   {2.4, 0.70, 0.55, RING, 3, 0.9},
    {4.2, 0.48, 0.72, CHRYS, 1, 1.1},   {6.3, 0.20, 0.50, PEONY, 2, 0.85},
    {7.0, 0.80, 0.62, PEONY, 4, 0.95},  {9.4, 0.55, 0.68, WILLOW, 1, 1.2},
    {11.6, 0.35, 0.58, DOUBLE, 3, 1.0}, {12.4, 0.72, 0.50, RING, 7, 1.0},
    {14.0, 0.50, 0.70, CHRYS, 6, 1.1},  {16.0, 0.25, 0.55, PEONY, 5, 0.9},
    {16.4, 0.78, 0.60, PEONY, 0, 0.9},
    // the finale
    {18.0, 0.40, 0.60, CHRYS, 1, 1.0},  {18.5, 0.62, 0.66, PEONY, 3, 1.0},
    {19.0, 0.50, 0.74, DOUBLE, 4, 1.2}, {19.4, 0.22, 0.56, RING, 2, 1.0},
    {19.8, 0.80, 0.58, RING, 6, 1.0},   {20.2, 0.35, 0.70, PEONY, 7, 1.0},
    {20.5, 0.65, 0.72, WILLOW, 1, 1.2}, {20.9, 0.50, 0.78, DOUBLE, 0, 1.35},
    {21.2, 0.28, 0.64, CHRYS, 5, 1.1},  {21.4, 0.72, 0.66, CHRYS, 5, 1.1}};
static const int NSHELLS = sizeof(SHELLS) / sizeof(SHELLS[0]);

static const double MAX_LIFE = 4.8;          // no spark lives longer than this

// the numbers a style needs
// sparks: how many per cell of burst area (a ring counts its circumference instead)
struct StyleInfo { double sparks, drag, life, gravity, trails, glitter; };
static StyleInfo style_info(Style s) {
    switch (s) {
        case PEONY:  return {0.20, 1.7, 2.6, 0.55, 0, 0.25};
        case CHRYS:  return {0.16, 1.6, 3.0, 0.60, 1, 0.20};
        case RING:   return {0.8, 1.7, 2.6, 0.50, 0, 0.30};
        case WILLOW: return {0.12, 0.9, 4.0, 1.10, 1, 0.10};
        case DOUBLE: return {0.26, 1.7, 2.8, 0.55, 0, 0.30};
    }
    return {0.2, 1.7, 2.6, 0.55, 0, 0.2};
}

// ---------------------------------------------------------------- the scene

class Starburst : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"starburst (C++)", DURATION, {255, 190, 70}, true};
    }

    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        Hc = rows * 2.0;                             // sky height, in column widths
        ground = rows * 0.93;                        // the row where the skyline starts
        Rb = std::min(0.28 * Hc, 0.30 * cols);       // burst radius of an ordinary shell
        g_unit = 0.09 * Hc;                          // gravity scale
        ridge.assign(cols, ground);
        // a low skyline: trees and a few roofs, fixed once
        double hgt = 0;
        int left = 0;
        for (int j = 0; j < cols; j++) {
            if (left == 0) {
                left = 2 + (int)(rnd(j, 1, 900) * 6);
                hgt = rnd(j, 2, 900) * rows * 0.06;
            }
            left--;
            ridge[j] = ground - hgt - 0.6 * std::sin(j * 0.31) - 0.4;
        }
    }

    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double Hc = 1, ground = 0, Rb = 1, g_unit = 1;
    std::vector<double> ridge;

    struct Burst { double x, y, age, size; int color; };      // for the sky flash

    void put(Picture& F, int row, int col, double l, const double c[3]) const {
        if (row < 0 || row >= R || col < 0 || col >= C) return;
        if (row + 0.5 >= ridge[col]) return;                   // hidden behind the skyline
        size_t p = (size_t)row * C + col;
        if (l > F.lum[p]) {
            F.lum[p] = l;
            for (int k = 0; k < 3; k++) F.rgb[p * 3 + k] = c[k];
        }
    }
};

void Starburst::frame(double t, Picture& F) {
    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);

    const long k = (long)std::floor(t / CYCLE);
    std::vector<Burst> flashes;

    // ---- which shells are in the air: this cycle and the one before it
    struct Live { const Shell* s; long K; int idx; double tc, x, tflight, ybase; int color; };
    std::vector<Live> live;
    int fired = (int)(k * NSHELLS);
    const Shell* latest = nullptr;
    double latest_t = -1e9;
    for (long K = k - 1; K <= k; K++) {
        double tc = t - K * CYCLE;
        for (int n = 0; n < NSHELLS; n++) {
            const Shell& s = SHELLS[n];
            uint32_t seed = (uint32_t)(K * 64 + n + 4096);
            double flight = 1.3 + 0.8 * s.h;
            if (K == k && tc >= s.t) fired++;
            if (tc < s.t || tc > s.t + flight + MAX_LIFE + 0.5) continue;
            Live L;
            L.s = &s; L.K = K; L.idx = n; L.tc = tc;
            L.x = clampd(s.x + (rnd(seed, 1, 1) - 0.5) * 0.08, 0.1, 0.9);
            L.tflight = flight;
            L.color = (s.color + (int)(rnd(seed, 2, 1) * 2.0 * (K % 3 != 0))) % 8;   // some variety from one cycle to the next
            live.push_back(L);
            if (tc >= s.t + flight && s.t + K * CYCLE > latest_t) { latest = &s; latest_t = s.t + K * CYCLE; }
        }
    }

    // ---- the sky: a faint haze at the horizon and a few dim stars
    // (the flashes are added to it below, so they are collected first)
    for (const Live& L : live) {
        double age = L.tc - (L.s->t + L.tflight);
        if (age >= 0 && age < 0.9) {
            double Yb = L.s->h * 0.70 * Hc + (R - ground) * 2.0;     // burst height above the bottom, column widths
            flashes.push_back({L.x * C, Yb, age, L.s->size, L.color});
        }
    }

    for (int i = 0; i < R; i++) {
        const double y = (R - (i + 0.5)) * 2.0;                 // height above the bottom
        for (int j = 0; j < C; j++) {
            double lum = 0, col[3] = {0, 0, 0};
            // horizon haze
            double hz = std::max(0.0, y - (R - ground) * 2.0) / (0.16 * Hc);
            double haze = 0.075 * std::exp(-hz);
            if (haze > 0) {
                lum = haze; col[0] = 24; col[1] = 36; col[2] = 80;
            }
            // stars, very faint, twinkling slowly
            if (rnd(j, i, 3) < 0.018 && i + 0.5 < ridge[j]) {
                double s = (0.08 + 0.16 * rnd(j, i, 4)) * (0.6 + 0.4 * std::sin(2 * M_PI * (0.2 + 0.5 * rnd(j, i, 5)) * t + 6.28 * rnd(j, i, 6)));
                if (s > lum) { lum = s; col[0] = 170; col[1] = 185; col[2] = 235; }
            }
            // flashes lighting the sky at each burst
            for (const Burst& b : flashes) {
                double dx = j + 0.5 - b.x, dy = y - b.y;
                double d2 = (dx * dx + dy * dy) / (0.42 * Hc * 0.42 * Hc);
                if (d2 > 6) continue;
                double a = 0.15 * b.size * std::exp(-b.age / 0.22) * std::exp(-d2);
                const double* bc = COLORS[b.color];
                double w = a / (lum + a);
                lum += a;
                for (int c = 0; c < 3; c++) col[c] = lerp(col[c], 0.75 * bc[c] + 25, w);
            }
            size_t p = (size_t)i * C + j;
            F.lum[p] = lum;
            for (int c = 0; c < 3; c++) F.rgb[p * 3 + c] = col[c];
        }
    }

    // ---- shells and sparks
    long alive = 0;
    for (const Live& L : live) {
        const Shell& s = *L.s;
        const StyleInfo si = style_info(s.style);
        const uint32_t seed = (uint32_t)(L.K * 64 + L.idx + 4096);
        const double ybot = (R - ground) * 2.0;                 // the launch height, above the bottom
        const double Xb = L.x * C;
        const double Yb = ybot + s.h * 0.70 * Hc;
        const double tl = s.t, flight = L.tflight;

        if (L.tc < tl + flight) {                               // climbing
            double u = (L.tc - tl) / flight;
            for (int m = 0; m < 9; m++) {                       // head and trail
                double uu = u - m * 0.025;
                if (uu < 0) break;
                double y = ybot + (Yb - ybot) * (1.0 - (1.0 - uu) * (1.0 - uu));
                double wob = std::sin(uu * 20.0 + L.idx) * 0.25 * m * 0.1;
                double l = m == 0 ? 0.95 : 0.7 * std::pow(0.78, m);
                double col[3] = {255, lerp(225, 120, m / 8.0), lerp(160, 40, m / 8.0)};
                put(F, (int)std::floor(R - y / 2.0), (int)std::floor(Xb + wob), l, col);
            }
            continue;
        }

        const double tau = L.tc - (tl + flight);                // seconds since the burst
        const double Rs = Rb * s.size;
        const double k_drag = si.drag;
        // gravity is stronger for the long-lived willow, weaker for others
        const double grav = si.gravity * g_unit * 0.55 * (s.style == WILLOW ? 1.0 : 0.8);

        // bright core at the moment of the burst
        if (tau < 0.16) {
            double rc = Rs * 0.14 * (0.5 + tau / 0.16);
            double l = 1.0 - tau / 0.18;
            double col[3] = {255, 250, 235};
            int r0 = (int)std::floor(R - Yb / 2.0), c0 = (int)std::floor(Xb);
            int rr = (int)std::ceil(rc / 2.0) + 1, rc_ = (int)std::ceil(rc) + 1;
            for (int a = -rr; a <= rr; a++)
                for (int b = -rc_; b <= rc_; b++) {
                    double d = std::sqrt((double)b * b + (double)a * 2 * a * 2);
                    if (d <= rc) put(F, r0 + a, c0 + b, l * (1.0 - 0.5 * d / (rc + 1e-9)), col);
                }
        }

        // how many sparks: by area, so a small screen is not a solid blob
        const int nsp = std::max(36, (int)(si.sparks * (s.style == RING ? 2.0 * M_PI * Rs : M_PI * Rs * Rs / 2.0)));
        for (int n = 0; n < nsp; n++) {
            // each spark: direction, speed factor and lifetime
            double life = si.life * (0.75 + 0.5 * rnd(seed, n, 11));
            if (tau >= life) continue;
            alive++;
            double ang = 2.0 * M_PI * rnd(seed, n, 12);
            double dx, dy, sf, rscale = 1.0;
            int colorIdx = L.color;
            switch (s.style) {
                case RING: {
                    double tilt = 0.35 + 0.5 * rnd(seed, 99, 13), rot = 6.28 * rnd(seed, 98, 13);
                    double ex = std::cos(ang), ey = std::sin(ang) * tilt;
                    dx = ex * std::cos(rot) - ey * std::sin(rot);
                    dy = ex * std::sin(rot) + ey * std::cos(rot);
                    sf = 0.96 + 0.04 * rnd(seed, n, 14);
                    break;
                }
                case WILLOW: {
                    double z = 2.0 * rnd(seed, n, 15) - 1.0, q = std::sqrt(1.0 - z * z);
                    dx = q * std::cos(ang); dy = q * std::sin(ang);
                    sf = 0.55 + 0.35 * rnd(seed, n, 14);
                    break;
                }
                default: {                                      // a sphere seen from the side
                    double z = 2.0 * rnd(seed, n, 15) - 1.0, q = std::sqrt(1.0 - z * z);
                    dx = q * std::cos(ang); dy = q * std::sin(ang);
                    sf = 0.88 + 0.12 * rnd(seed, n, 14);
                    if (s.style == DOUBLE && n % 5 < 2) { rscale = 0.55; colorIdx = (L.color + 3) % 8; }
                    break;
                }
            }
            double v = Rs * rscale * sf * k_drag / (1.0 - std::exp(-k_drag * si.life));
            const double* base = COLORS[colorIdx];
            bool glitter = rnd(seed, n, 16) < si.glitter;
            double flick_seed = rnd(seed, n, 17);

            int ntr = si.trails > 0 ? 4 : 2;
            for (int m = 0; m < ntr; m++) {
                double ta = tau - m * 0.045;
                if (ta < 0) break;
                double e = 1.0 - std::exp(-k_drag * ta);
                double px = Xb + dx * v / k_drag * e;
                double py = Yb + dy * v / k_drag * e - grav / k_drag * (ta - e / k_drag);
                double f = ta / life;
                double b = std::pow(1.0 - f, 0.9);
                if (glitter && f > 0.5)                          // glitter: on and off at random
                    b *= rnd((uint32_t)(flick_seed * 1e6), (uint32_t)(ta * 22.0), 18) < 0.5 ? 1.25 : 0.12;
                b *= std::pow(0.55, m);
                if (ta < 0.12) b = std::max(b, 0.95);
                double l = clampd(b, 0.0, 1.0);
                if (l < 0.07) continue;
                double hot = smooth(ta / 0.4);
                double cool = smooth((f - 0.55) / 0.45);
                double col[3];
                for (int c = 0; c < 3; c++) {
                    double v1 = lerp(250, base[c], hot);
                    double ember = c == 0 ? 210 : (c == 1 ? 70 : 20);
                    col[c] = lerp(v1, ember, cool) * (0.55 + 0.45 * clampd(l * 1.4, 0, 1));
                }
                put(F, (int)std::floor(R - py / 2.0), (int)std::floor(px),
                    0.08 + 0.55 * l + 0.30 * std::max(0.0, 1.0 - ta / 0.4), col);
            }
        }
    }

    // ---- the readout
    char buf[64];
    F.hud.resize(HUD_ROWS);
    std::snprintf(buf, sizeof buf, "shells fired %d", fired);
    F.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "sparks alive %ld", alive);
    F.hud[1] = buf;
    std::snprintf(buf, sizeof buf, "%s", latest ? STYLE_NAME[latest->style] : "-");
    F.hud[2] = buf;
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_starburst() { return std::make_unique<Starburst>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Starburst scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
