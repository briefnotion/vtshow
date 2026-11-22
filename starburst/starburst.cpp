// Fireworks over a night sky, with some real physics: mortars near the middle of
// the ground fire shells that climb slowly on a ballistic arc and burst near the
// top; every spark then flies out fast, is braked by the air, hangs for a moment
// and sinks, cooling from white-hot through its colour to dull ember red. A faint
// haze of cloud drifts across the sky and is lit by each burst.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// The physics used (all in metres and seconds, then scaled to the screen: the sky
// is WORLD_H metres tall):
//  * gravity, g = 9.81 m/s^2. A shell leaves the mortar at v0 = sqrt(2 g H) so that
//    it reaches height H at the top of its arc and bursts a moment before the top,
//    when it is almost standing still (3 to 4 seconds of climb). Its path is a
//    straight-line drift sideways plus the vertical parabola, so shells fired
//    from the middle lean out to wherever they burst.
//  * air drag on the sparks, quadratic in speed: dv/dt = -k |v| v. A spark starts
//    at a speed chosen so that, braked by the drag, it reaches the burst radius in
//    about 1.4 s; it then nearly stops (the hang), and gravity takes over. The
//    drag coefficient k grows as the spark burns down and gets smaller and lighter,
//    so late sparks sink more slowly relative to how fast they fall.
//  * the sparks' velocity has a hidden third component (towards or away from
//    you), which the drag also acts on, so a burst is a shell of sparks, not a disk.
//  * stars burn at close to constant brightness and then die. For the first half
//    second the burst is so dense and hot that it is one blinding white ball; the
//    stars then separate and show their colours, slide a little in hue and shade
//    as they burn, and cool to ember red at the end.
// Nothing is stepped from frame to frame: each spark's flight is integrated from the
// burst up to the time asked for, in fixed 40 ms steps from its own hashed start,
// so frame(t) is still a pure function of t. The launch schedule repeats every
// CYCLE seconds and the cycle before the current one is also drawn, because its
// finale is still falling when the next one begins, so the repeat is seamless.
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
// smooth value noise, 0..1 (for the clouds)
static double vnoise(double x, double y) {
    double fx0 = std::floor(x), fy0 = std::floor(y);
    uint32_t ix = (uint32_t)(int32_t)fx0, iy = (uint32_t)(int32_t)fy0;
    double fx = smooth(x - fx0), fy = smooth(y - fy0);
    double p = rnd(ix, iy, 1), q = rnd(ix + 1, iy, 1);
    double r = rnd(ix, iy + 1, 1), w = rnd(ix + 1, iy + 1, 1);
    return (p * (1 - fx) + q * fx) * (1 - fy) + (r * (1 - fx) + w * fx) * fy;
}
static double fbm(double x, double y) {
    return 0.55 * vnoise(x, y) + 0.30 * vnoise(2 * x + 5.2, 2 * y + 1.3)
         + 0.15 * vnoise(4 * x + 9.1, 4 * y + 3.7);
}

// rotate the hue of an RGB colour (0..255) by `a` radians round the grey axis
static void hue_rotate(double c[3], double a) {
    const double ca = std::cos(a), sa = std::sin(a), t = (1.0 - ca) / 3.0, w = 0.57735026919 * sa;
    double r = c[0], g = c[1], b = c[2];
    c[0] = clampd(r * (ca + t) + g * (t - w) + b * (t + w), 0.0, 255.0);
    c[1] = clampd(r * (t + w) + g * (ca + t) + b * (t - w), 0.0, 255.0);
    c[2] = clampd(r * (t - w) + g * (t + w) + b * (ca + t), 0.0, 255.0);
}

// ---------------------------------------------------------------- the show

static const double G = 9.81;               // m/s^2
static const double WORLD_H = 130.0;        // the sky, bottom to top of the screen, in metres
static const double INTEGRATION_STEP = 0.04;  // seconds; a spark's flight is integrated in these steps
static const int MAX_STEPS = 160;

enum Style { PEONY, CHRYS, RING, WILLOW, DOUBLE };
static const char* STYLE_NAME[] = {"peony", "chrysanthemum", "ring", "willow", "double burst"};

// colours
static const double COLORS[8][3] = {{255, 60, 50},  {255, 190, 60}, {90, 255, 120}, {80, 170, 255},
                                    {255, 80, 220}, {225, 232, 255}, {170, 110, 255}, {255, 125, 30}};
static const double WHITE_HOT[3] = {255, 255, 250};
static const double EMBER[3] = {215, 75, 22};
static const double GOLD[3] = {255, 205, 120};

struct Shell {
    double t;          // when it bursts, seconds into the cycle
    double x;          // where it bursts across the sky, 0..1
    double h;          // how high, 0..1
    Style style;
    int color;         // theme colour
    int accent;        // the colour of the sparks that are not the theme colour
    double size;       // 1 = ordinary
};
static const Shell SHELLS[] = {
    {4.4, 0.30, 0.60, PEONY, 0, 1, 1.0},   {6.4, 0.68, 0.55, RING, 3, 5, 0.9},
    {8.0, 0.48, 0.72, CHRYS, 1, 7, 1.1},   {9.6, 0.26, 0.50, PEONY, 2, 3, 0.85},
    {10.8, 0.74, 0.62, PEONY, 4, 6, 0.95}, {12.4, 0.52, 0.66, WILLOW, 1, 7, 1.2},
    {14.0, 0.36, 0.58, DOUBLE, 3, 0, 1.0}, {14.8, 0.70, 0.52, RING, 7, 1, 1.0},
    {16.2, 0.50, 0.70, CHRYS, 6, 4, 1.1},  {17.2, 0.28, 0.54, PEONY, 5, 3, 0.9},
    {17.8, 0.74, 0.60, PEONY, 0, 6, 0.9},
    // the finale
    {19.0, 0.40, 0.62, CHRYS, 1, 0, 1.0},  {19.5, 0.62, 0.68, PEONY, 3, 5, 1.0},
    {20.0, 0.50, 0.76, DOUBLE, 4, 1, 1.2}, {20.4, 0.30, 0.56, RING, 2, 1, 1.0},
    {20.8, 0.72, 0.60, RING, 6, 3, 1.0},   {21.2, 0.38, 0.70, PEONY, 7, 4, 1.0},
    {21.6, 0.62, 0.72, WILLOW, 1, 5, 1.2}, {22.1, 0.50, 0.80, DOUBLE, 0, 1, 1.35},
    {22.4, 0.27, 0.64, CHRYS, 5, 3, 1.1},  {22.7, 0.73, 0.66, CHRYS, 5, 6, 1.1}};
static const int NSHELLS = sizeof(SHELLS) / sizeof(SHELLS[0]);

static const double MAX_LIFE = 4.8;          // no spark lives longer than this
static const double MAX_FLIGHT = 4.2;        // no shell climbs for longer than this
static const double FIREBALL = 0.55;         // seconds of white-hot, blinding burst

// the numbers a style needs: sparks per cell of burst area (a ring counts its
// circumference instead), drag coefficient k0 in 1/m, life in s, trails, glitter fraction,
// the radius it reaches in metres (before the size factor), the time to get there
struct StyleInfo { double sparks, k0, life, trails, glitter, radius, reach; };
static StyleInfo style_info(Style s) {
    switch (s) {
        case PEONY:  return {0.30, 0.055, 2.8, 0, 0.25, 36, 1.4};
        case CHRYS:  return {0.24, 0.050, 3.2, 1, 0.20, 38, 1.4};
        case RING:   return {1.2,  0.060, 2.8, 0, 0.30, 36, 1.4};
        case WILLOW: return {0.20, 0.100, 4.2, 1, 0.10, 28, 1.7};
        case DOUBLE: return {0.38, 0.055, 3.0, 0, 0.30, 38, 1.4};
    }
    return {0.2, 0.055, 2.8, 0, 0.2, 30, 1.4};
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
        gh = (R - ground) * 2.0;                     // height of the ground line above the bottom
        sc = (Hc - gh) / WORLD_H;                    // column widths per metre
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
    double Hc = 1, ground = 0, gh = 0, sc = 1;
    std::vector<double> ridge;

    struct Burst { double x, y, age, size; const double* color; };      // lights the sky and the clouds

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

// where a shell is, u seconds after the mortar fired it: ballistic, no drag
struct Flight {
    double xl, xb;           // columns: where it was fired, where it bursts
    double apex;             // metres: top of the arc
    double v0, tb;           // launch speed m/s, burst time after launch s
    double x(double u) const { return xl + (xb - xl) * u / tb; }
    double y(double u) const { return v0 * u - 0.5 * G * u * u; }          // metres
};

void Starburst::frame(double t, Picture& F) {
    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);

    const long k = (long)std::floor(t / CYCLE);

    // ---- which shells are in the air: this cycle and the one before it
    struct Live { const Shell* s; long K; int idx; double tc, flight, tl; Flight f; int color, accent; };
    std::vector<Live> live;
    int fired = (int)(k * NSHELLS);
    const Shell* latest = nullptr;
    double latest_t = -1e9, latest_apex = 0;
    for (long K = k - 1; K <= k; K++) {
        const double tc = t - K * CYCLE;
        for (int n = 0; n < NSHELLS; n++) {
            const Shell& s = SHELLS[n];
            const uint32_t seed = (uint32_t)(K * 64 + n + 4096);
            Live L;
            L.s = &s; L.K = K; L.idx = n; L.tc = tc;
            L.f.apex = 40.0 + 55.0 * s.h;
            double t_top = std::sqrt(2.0 * L.f.apex / G);               // time to the top of the arc
            L.f.tb = 0.96 * t_top;
            L.f.v0 = G * t_top;
            L.f.xb = clampd(s.x + (rnd(seed, 1, 1) - 0.5) * 0.06, 0.1, 0.9) * C;
            L.f.xl = (0.5 + (rnd(seed, 3, 1) - 0.5) * 0.16) * C;          // mortars near the middle
            L.flight = L.f.tb;
            L.tl = s.t - L.flight;                                        // when it was fired
            L.color = (s.color + (int)(rnd(seed, 2, 1) * 2.0 * (K % 3 != 0))) % 8;   // some variety from one cycle to the next
            L.accent = (s.accent + (L.color - s.color + 8)) % 8;
            if (K == k && tc >= L.tl) fired++;
            if (tc < L.tl || tc > s.t + MAX_LIFE + 0.5) continue;
            live.push_back(L);
            if (tc >= s.t && s.t + K * CYCLE > latest_t) { latest = &s; latest_t = s.t + K * CYCLE; latest_apex = L.f.apex; }
        }
    }

    // ---- bursts that are lighting the sky
    std::vector<Burst> flashes;
    for (const Live& L : live) {
        double age = L.tc - L.s->t;
        if (age >= 0 && age < MAX_LIFE) {
            double Yb = gh + L.f.y(L.flight) * sc;
            flashes.push_back({L.f.xb, Yb, age, L.s->size, COLORS[L.color]});
        }
    }

    // ---- the sky: haze at the horizon, drifting clouds, dim stars, and the light of the bursts
    // The sky brightness is far below one letter step, so a plain threshold would show
    // contour bands. A fixed random dither (the same for every frame) turns each band
    // edge into a smooth scatter of dim dots instead.
    const double step_w = 1.0 / (2.0 * (double)vtscreen::LEVELS.size());
    for (int i = 0; i < R; i++) {
        const double y = (R - (i + 0.5)) * 2.0;                 // height above the bottom
        for (int j = 0; j < C; j++) {
            double lum = 0, cw = 0, col[3] = {0, 0, 0};          // brightness, colour weight, colour sum
            auto add = [&](double a, const double c[3]) {
                if (a <= 0) return;
                lum += a; cw += a;
                for (int q = 0; q < 3; q++) col[q] += a * c[q];
            };
            static const double HAZE[3] = {26, 38, 84}, CLOUD[3] = {40, 48, 78};
            // horizon haze
            double hz = std::max(0.0, y - gh) / (0.16 * Hc);
            add(0.045 * std::exp(-hz), HAZE);
            // clouds: slow, wide, mostly invisible until a burst lights them
            double cn = fbm(j * 0.030 + 0.025 * t, y * 0.060 + 3.0) * 0.7 + 0.3 * fbm(j * 0.07 - 0.04 * t, y * 0.11 + 11.0);
            double cd = smooth((cn - 0.42) / 0.30);
            add(0.011 * cd, CLOUD);
            double lit = 0, litc[3] = {0, 0, 0};                  // light from all the bursts together
            for (const Burst& b : flashes) {
                double dx = j + 0.5 - b.x, dy = y - b.y;
                double d2 = (dx * dx + dy * dy) / (0.55 * Hc * 0.55 * Hc);
                if (d2 > 7) continue;
                // a quick flash and then a long glow from the sparks hanging in the air
                double amp = (1.1 * std::exp(-b.age / 0.2) + 0.35 * std::exp(-b.age / 1.3)) * b.size;
                double fall = std::exp(-d2) * smooth((MAX_LIFE - b.age) / 1.5);
                double tint[3];
                for (int q = 0; q < 3; q++) tint[q] = lerp(0.7 * b.color[q] + 40, 255.0, 0.75 * std::exp(-b.age / 0.25));   // white at first
                double a = (0.04 + 0.30 * cd) * amp * fall;       // the clouds take much more of it than the open sky
                lit += a;
                for (int q = 0; q < 3; q++) litc[q] += a * tint[q];
            }
            if (lit > 0) {                                        // several bursts at once must not blow it out
                double comp = 0.26 * (1.0 - std::exp(-lit / 0.26));
                for (int q = 0; q < 3; q++) litc[q] /= lit;
                add(comp, litc);
            }
            if (cw > 0) {
                for (int q = 0; q < 3; q++) col[q] /= cw;
                if (lum > 0.0015) lum += rnd(j, i, 77) * step_w;   // the dither
            }
            // stars, very faint, twinkling slowly
            if (rnd(j, i, 3) < 0.018 && i + 0.5 < ridge[j]) {
                double s = (0.08 + 0.16 * rnd(j, i, 4)) * (0.6 + 0.4 * std::sin(2 * M_PI * (0.2 + 0.5 * rnd(j, i, 5)) * t + 6.28 * rnd(j, i, 6)));
                if (s > lum) { lum = s; col[0] = 170; col[1] = 185; col[2] = 235; }
            }
            size_t p = (size_t)i * C + j;
            F.lum[p] = std::min(lum, 1.0);
            for (int c = 0; c < 3; c++) F.rgb[p * 3 + c] = col[c];
        }
    }

    // ---- shells and sparks
    long alive = 0;
    static const double trail_col[3] = {255, 200, 120};
    for (const Live& L : live) {
        const Shell& s = *L.s;
        const StyleInfo si = style_info(s.style);
        const uint32_t seed = (uint32_t)(L.K * 64 + L.idx + 4096);

        if (L.tc < s.t) {                                       // climbing
            const double u = L.tc - L.tl;                       // seconds since it was fired
            // the lift charge leaves a trail, and sheds a few sparks that fall behind
            for (int m = 0; m < 12; m++) {
                double uu = u - m * 0.06;
                if (uu < 0) break;
                double l = m == 0 ? 0.95 : 0.65 * std::pow(0.82, m);
                double col[3] = {255, lerp(225, 130, m / 11.0), lerp(160, 50, m / 11.0)};
                put(F, (int)std::floor(R - (gh + L.f.y(uu) * sc) / 2.0), (int)std::floor(L.f.x(uu)), l, col);
            }
            for (int j = 0; j < 14; j++) {
                double ub = (j + 1) * 0.27;                     // when this one was shed
                double a = u - ub;
                if (a < 0 || a > 0.9 || ub > L.flight) continue;
                double jx = (rnd(seed, j, 31) - 0.5) * 2.0;     // sideways, metres
                double ym = L.f.y(ub) - 2.4 * a - 0.5 * 1.5 * a * a;    // sinks, held up by the air
                double l = 0.5 * (1.0 - a / 0.9);
                put(F, (int)std::floor(R - (gh + ym * sc) / 2.0), (int)std::floor(L.f.x(ub) + jx * sc), l, trail_col);
            }
            continue;
        }

        const double tau = L.tc - s.t;                          // seconds since the burst
        const double Xb = L.f.xb;
        const double Yb = gh + L.f.y(L.flight) * sc;            // column widths above the bottom
        // a burst radius that fits the screen
        const double Rm = std::min({si.radius * s.size, 0.34 * C / sc, WORLD_H - 6.0 - L.f.apex});
        const double Rcol = Rm * sc;

        // The first half second: everything is so dense and hot that the burst is one
        // blinding white ball that swells to the burst radius and then thins into sparks.
        if (tau < FIREBALL) {
            const double k0n = si.k0;
            const double v0n = (std::exp(k0n * Rm) - 1.0) / (k0n * si.reach);
            const double front = clampd(std::log(1.0 + k0n * v0n * tau) / k0n / Rm, 0.0, 1.0) * Rcol;
            const double rr_ = std::max(front, Rcol * 0.12);
            const double life_w = std::pow(1.0 - tau / FIREBALL, 0.6);     // fades as the sparks separate
            const double* theme_c = COLORS[L.color];
            int r0 = (int)std::floor(R - Yb / 2.0), c0 = (int)std::floor(Xb);
            int rows_ = (int)std::ceil(rr_ / 2.0) + 1, cols_ = (int)std::ceil(rr_) + 1;
            for (int a = -rows_; a <= rows_; a++)
                for (int b = -cols_; b <= cols_; b++) {
                    double d = std::sqrt((double)b * b + (double)a * 2 * a * 2) / rr_;
                    if (d > 1.0) continue;
                    double w = life_w * (1.0 - 0.6 * d * d);              // brightest at the middle
                    double ph = tau / FIREBALL;
                    double tint = clampd(ph * ph * 0.7 + d * 0.12, 0.0, 0.8);   // the rim picks up the colour first
                    double col[3];
                    for (int c = 0; c < 3; c++) col[c] = lerp(WHITE_HOT[c], theme_c[c], tint * (1.0 - 0.5 * w));
                    put(F, r0 + a, c0 + b, 0.3 + 0.7 * w, col);
                }
        }

        // how many sparks: by area, so a small screen is not a solid blob
        const int nsp = std::max(44, (int)(1.15 * si.sparks * (s.style == RING ? 2.0 * M_PI * Rcol : M_PI * Rcol * Rcol / 2.0)));
        const bool changer = rnd(seed, 555, 1) < 0.3;            // the theme sparks turn into the accent colour
        const double ring_tilt = 0.35 + 0.5 * rnd(seed, 99, 13), ring_rot = 6.28 * rnd(seed, 98, 13);
        const double* theme = COLORS[L.color];
        const double* accent = COLORS[L.accent];
        double xs[MAX_STEPS + 2], ys[MAX_STEPS + 2];

        for (int n = 0; n < nsp; n++) {
            // each spark: how long it burns, which way it goes, how fast
            const double life = si.life * (0.75 + 0.5 * rnd(seed, n, 11));
            if (tau >= life) continue;
            alive++;
            double ang = 2.0 * M_PI * rnd(seed, n, 12);
            double dx, dy, dz, sf, rscale = 1.0;
            int kind = 0;                                         // 0 theme, 1 accent, 2 warm glitter
            double u = rnd(seed, n, 19);
            if (u > 0.58) kind = u < 0.86 ? 1 : 2;
            double reach = si.reach;
            switch (s.style) {
                case RING: {                                      // a ring: a circle tilted away from us
                    double ex = std::cos(ang), ey = std::sin(ang) * ring_tilt;
                    dx = ex * std::cos(ring_rot) - ey * std::sin(ring_rot);
                    dy = ex * std::sin(ring_rot) + ey * std::cos(ring_rot);
                    dz = std::sin(ang) * std::sqrt(1.0 - ring_tilt * ring_tilt);
                    sf = 0.97 + 0.03 * rnd(seed, n, 14);
                    break;
                }
                default: {                                        // a sphere seen from the side
                    double z = 2.0 * rnd(seed, n, 15) - 1.0, q = std::sqrt(1.0 - z * z);
                    dx = q * std::cos(ang); dy = q * std::sin(ang); dz = z;
                    sf = s.style == WILLOW ? 0.55 + 0.4 * rnd(seed, n, 14) : 0.90 + 0.10 * rnd(seed, n, 14);
                    if (s.style == DOUBLE && n % 5 < 2) { rscale = 0.55; kind = 1; }
                    break;
                }
            }
            const double k0 = si.k0 * (1.0 + 0.3 * (rnd(seed, n, 20) - 0.5));
            // speed that carries it (braked by the drag) to the radius in `reach` seconds
            const double v0 = (std::exp(k0 * Rm * rscale * sf) - 1.0) / (k0 * reach);
            double vx = v0 * dx, vy = v0 * dy, vz = v0 * dz;

            // fly it: quadratic drag that grows as the star burns down, gravity, 40 ms steps
            const int nsteps = std::min((int)std::ceil(tau / INTEGRATION_STEP) + 1, MAX_STEPS);
            double x = 0, y = 0;
            xs[0] = ys[0] = 0;
            for (int i = 1; i <= nsteps; i++) {
                double f = i * INTEGRATION_STEP / life;
                double kk = k0 * (1.0 + 0.9 * clampd(f, 0.0, 1.0));
                double sp = std::sqrt(vx * vx + vy * vy + vz * vz);
                double damp = 1.0 / (1.0 + kk * sp * INTEGRATION_STEP);
                vx *= damp; vz *= damp;
                vy = (vy - G * INTEGRATION_STEP) * damp;
                x += vx * INTEGRATION_STEP; y += vy * INTEGRATION_STEP;
                xs[i] = x; ys[i] = y;
            }

            const bool glitter = rnd(seed, n, 16) < si.glitter;
            const double flick_seed = rnd(seed, n, 17);
            const double tone = rnd(seed, n, 21);                 // some sparks run a little hotter, some cooler
            const double drift = 2.0 * rnd(seed, n, 22) - 1.0;    // which way its hue slides as it burns
            const double* base = kind == 0 ? theme : (kind == 1 ? accent : GOLD);
            const double* base2 = kind == 0 && changer ? accent : base;

            int ntr = si.trails > 0 ? 4 : 3;
            for (int m = 0; m < ntr; m++) {
                double ta = tau - m * 0.045;
                if (ta < 0) break;
                double fi = ta / INTEGRATION_STEP;
                int i0 = std::min((int)fi, nsteps - 1);
                double fr = fi - i0;
                double px = Xb + (lerp(xs[i0], xs[i0 + 1], fr)) * sc;
                double py = Yb + (lerp(ys[i0], ys[i0 + 1], fr)) * sc;
                double f = ta / life;
                // a star burns steadily and then dies
                double b = std::pow(clampd((1.0 - f) / 0.45, 0.0, 1.0), 1.1);
                if (glitter && f > 0.5)                          // glitter: on and off at random
                    b *= rnd((uint32_t)(flick_seed * 1e6), (uint32_t)(ta * 22.0), 18) < 0.5 ? 1.25 : 0.12;
                b *= std::pow(0.55, m);
                double l = clampd(b, 0.0, 1.0);
                if (l < 0.07) continue;
                // colour: white-hot at ignition (the first half second), then the star's own
                // colour (turning into the accent half way for colour-changers). While it burns
                // the hue slides a little, its shade shimmers, and at the end it cools to ember.
                double hot = 1.0 - smooth(ta / FIREBALL);
                double cool = smooth((f - 0.6) / 0.4);
                double mixc = smooth((f - 0.38) / 0.2);
                double col[3];
                for (int c = 0; c < 3; c++) col[c] = lerp(base[c], base2[c], mixc);
                hue_rotate(col, drift * 0.5 * smooth(f * 1.3));              // up to about 30 degrees
                double shade = 1.0 + 0.10 * std::sin(ta * (6.0 + 8.0 * tone) + 6.28 * flick_seed);
                for (int c = 0; c < 3; c++) {
                    double v1 = lerp(col[c], 255.0, 0.22 * (tone - 0.5) + 0.04);        // hotter or cooler by a little
                    v1 = lerp(v1, WHITE_HOT[c], 0.95 * hot);
                    v1 = lerp(v1, EMBER[c], 0.7 * cool);
                    col[c] = clampd(v1 * shade * (0.88 + 0.12 * l) * (1.0 - 0.1 * cool), 0, 255);
                }
                put(F, (int)std::floor(R - py / 2.0), (int)std::floor(px),
                    std::min(1.0, 0.22 + 0.62 * l + 0.5 * hot), col);
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
    if (latest) std::snprintf(buf, sizeof buf, "%s at %.0f m", STYLE_NAME[latest->style], latest_apex);
    else std::snprintf(buf, sizeof buf, "-");
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
