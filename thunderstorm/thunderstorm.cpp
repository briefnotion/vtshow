// A thunderstorm of letters. Light arrives at once; thunder crawls at 343 m/s.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's.
// The storm repeats every DURATION seconds, so --loop is seamless.
//
//   g++ -O2 -std=c++17 -o thunderstorm thunderstorm.cpp
//   ./thunderstorm              play once (24 s); q or Ctrl-C quits
//   ./thunderstorm --loop       keep going
//   ./thunderstorm --still 2.6  print the frame at t=2.6 s as plain text and exit
//   ./thunderstorm --no-color   letters only
//   ./thunderstorm --fps N      frames per second (default 30)
//   ./thunderstorm --cols N --rows N

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../scenes.h"
#include "../vtscreen/vtscreen.h"

using vtscreen::Picture;

// Everything below is private to this file (anonymous namespace), so several
// scenes can be linked into one program; make_thunderstorm() at the end is the way in.
namespace {

// ---------------------------------------------------------------- the scene

static const double DURATION = 24.0;

static const double SOUND = 0.343;        // speed of sound, km/s
struct Strike { double t, dist, across; };
// Lightning strikes: time s, distance km, across the screen 0..1.
// Near ones are tall bolts; far ones are small and dim; beyond FAR_KM there is
// no bolt at all, only a glow inside the clouds (and you cannot hear it).
static const Strike STRIKES[] = {{2.6, 2.4, 0.30}, {4.6, 10.5, 0.86}, {6.9, 5.8, 0.68},
                                 {7.6, 13.0, 0.20}, {10.4, 9.0, 0.52}, {12.8, 1.8, 0.45},
                                 {15.1, 12.0, 0.08}, {17.2, 6.5, 0.76}, {20.6, 3.5, 0.20},
                                 {22.3, 11.0, 0.62}};
static const int NSTRIKES = sizeof(STRIKES) / sizeof(STRIKES[0]);
static const double NEAR_KM = 1.5, FAR_KM = 12.0;
// distant rain storms hanging under the far clouds: across 0..1, width 0..1, strength
struct Storm { double across, width, strength; };
static const Storm STORMS[] = {{0.14, 0.07, 0.9}, {0.52, 0.10, 0.7}, {0.84, 0.06, 1.0}};
static const int NSTORMS = sizeof(STORMS) / sizeof(STORMS[0]);
static const double DROPS = 0.03;         // raindrops per letter cell: a drizzle
static const int HUD_ROWS = 3;

struct BoltCell { int idx; double w; };
struct Bolt {
    std::vector<BoltCell> cells;
    double cx = 0, cy = 0, sx = 1, sy = 1, f = 0;     // f: 0 near .. 1 far
};

struct Scene {
    int cols = 0, rows = 0;
    int horizon = 0, rtop = 0, sky = 0;
    std::vector<double> ridge;                        // hill line, per column
    std::vector<Bolt> bolts;                          // per strike
    std::vector<double> x0, sp, ph, thr;              // per drop
    std::vector<int> ln;
} S;

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline double smooth(double v) {
    v = clampd(v, 0.0, 1.0);
    return v * v * (3 - 2 * v);
}

static inline double pymod(double a, double m) {
    double r = std::fmod(a, m);
    if (r < 0) r += m;
    return r;
}

// colour: night blue -> storm blue -> flash white
static void palette(double lum, double out[3]) {
    static const double xs[4] = {0.0, 0.3, 0.7, 1.0};
    static const double cs[4][3] = {{6, 8, 20}, {40, 55, 100},
                                    {150, 170, 230}, {250, 250, 255}};
    lum = clampd(lum, 0.0, 1.0);
    int k = 0;
    while (k < 2 && lum > xs[k + 1]) k++;
    double f = (lum - xs[k]) / (xs[k + 1] - xs[k]);
    for (int c = 0; c < 3; c++) out[c] = cs[k][c] + f * (cs[k + 1][c] - cs[k][c]);
}

static inline double hash2(double x, double y) {
    return pymod(std::sin(x * 12.9898 + y * 78.233) * 43758.5453, 1.0);
}

// smooth value noise
static double vnoise(double x, double y) {
    double ix = std::floor(x), iy = std::floor(y);
    double fx = smooth(x - ix), fy = smooth(y - iy);
    double a = hash2(ix, iy), b = hash2(ix + 1, iy);
    double c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

static double fbm(double x, double y) {
    return 0.55 * vnoise(x, y) + 0.30 * vnoise(2 * x + 5.2, 2 * y + 1.3)
         + 0.15 * vnoise(4 * x + 9.1, 4 * y + 3.7);
}

// setup: the hills, every bolt and every raindrop, fixed once
static void setup(int cols, int rows) {
    S = Scene();
    S.cols = cols;
    S.rows = rows;
    std::mt19937_64 rng(7);
    auto uni = [&](double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(rng);
    };
    S.horizon = (int)(rows * 0.84);
    S.rtop = (int)(rows * 0.24);                       // where the nearest bolts leave the cloud
    S.sky = S.horizon - 2;                             // the horizon line the clouds converge to
    S.ridge.resize(cols);
    for (int j = 0; j < cols; j++)
        S.ridge[j] = S.horizon + 1.6 * std::sin(j * 0.09 + 0.5) + 1.0 * std::sin(j * 0.23 + 2.0)
                   + 1.2 * hash2(j, 3.0);

    std::discrete_distribution<int> jag({5, 20, 50, 20, 5});     // -2 .. +2
    for (int s = 0; s < NSTRIKES; s++) {
        double dist = STRIKES[s].dist;
        double f = clampd((dist - NEAR_KM) / (FAR_KM - NEAR_KM - 1), 0, 1);   // 0 near .. 1 far
        std::vector<double> g((size_t)rows * cols, 0.0);
        auto put = [&](int r, int c, double w) {
            if (r >= 0 && r < rows && c >= 0 && c < cols)
                g[(size_t)r * cols + c] = std::max(g[(size_t)r * cols + c], w);
        };
        int x = (int)(STRIKES[s].across * cols);
        int end = (int)S.ridge[std::min(std::max(x, 0), cols - 1)];
        int tp = (int)(S.sky - (S.sky - S.rtop) * (1 - 0.85 * f));    // far bolts: short, near the horizon
        if (dist < FAR_KM) {
            for (int r = tp; r < end; r++) {
                int nx = x + jag(rng) - 2;
                for (int c = std::min(x, nx); c <= std::max(x, nx); c++) put(r, c, 1.0);
                x = nx;
                if (tp + 3 < r && r < end - 4 && uni(0, 1) < 0.10) {
                    int d = uni(0, 1) < 0.5 ? -1 : 1;
                    int ln = 4 + (int)uni(0, 8);
                    int bx = x, br = r;
                    for (int k = 0; k < ln; k++) {
                        if (uni(0, 1) < 0.7) br++;
                        bx += d;
                        put(br, bx, 0.55 * (1 - (double)k / ln));
                    }
                }
            }
        }
        if (f < 0.5) {                                  // only near bolts have a soft glow
            std::vector<double> base = g;
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++) {
                    double w = base[(size_t)r * cols + c];
                    if (w <= 0) continue;
                    for (int off = 1; off <= 2; off++)
                        for (int sg = -1; sg <= 1; sg += 2)
                            put(r, c + sg * off, (off == 1 ? 0.25 : 0.10) * w);
                }
        }
        Bolt bolt;
        for (size_t p = 0; p < g.size(); p++)
            if (g[p] > 0) bolt.cells.push_back({(int)p, g[p]});
        bolt.cx = STRIKES[s].across * cols;
        bolt.cy = tp + 0.35 * (end - tp);
        bolt.sx = 0.2 * cols * (1 - 0.6 * f);
        bolt.sy = std::max(0.6 * (end - tp), 4.0);
        bolt.f = f;
        S.bolts.push_back(bolt);
    }

    int n = (int)(cols * rows * DROPS);
    S.x0.resize(n); S.sp.resize(n); S.ph.resize(n); S.thr.resize(n); S.ln.resize(n);
    for (double& v : S.x0) v = uni(0, cols + rows * 0.5);
    for (double& v : S.sp) v = uni(18, 30);
    for (double& v : S.ph) v = uni(0, 1);
    for (double& v : S.thr) v = uni(0, 1);
    for (int& v : S.ln) v = 1 + (int)uni(0, 3);
}

// a return stroke, a second one, and a weak third (lightning flickers)
static double envelope(double age) {
    double e = std::exp(-age / 0.05);
    if (age >= 0.13) e += 0.8 * std::exp(-(age - 0.13) / 0.05);
    if (age >= 0.27) e += 0.5 * std::exp(-(age - 0.27) / 0.07);
    return e;
}

// one frame: a pure function of time, no state between frames
static void frame(double t, Picture& F) {
    int R = S.rows, C = S.cols;
    double tc = pymod(t, DURATION);
    double inten = 0.475 + 0.225 * std::sin(2 * M_PI * t / DURATION - 1.2);   // 0.25 .. 0.70
    double mmh = 1 + 7 * (inten - 0.25) / 0.45;

    // lightning: far flashes only glow inside the clouds, near ones light the whole sky
    double amb = 0.0;
    struct Loc { const Bolt* b; double ke; };
    std::vector<Loc> locs;
    std::vector<double> bolt((size_t)R * C, 0.0);
    const double Ts[2] = {tc, tc + DURATION};            // a strike from the end can spill over
    for (double T : Ts)
        for (int s = 0; s < NSTRIKES; s++) {
            double age = T - STRIKES[s].t;
            if (!(age >= 0 && age < 1.2)) continue;
            const Bolt& b = S.bolts[s];
            double e = envelope(age);
            double k = clampd(2.4 / (1 + 0.5 * STRIKES[s].dist), 0.12, 1.3);   // far flashes are dimmer
            amb += k * (1 - 0.65 * b.f) * (0.6 * std::min(e, 1.4) + 0.4 * std::exp(-age / 0.35));
            locs.push_back({&b, k * e});
            if (age < 0.5 && !b.cells.empty()) {
                double I = (0.6 + 0.4 * std::min(e, 1.0)) * smooth((0.5 - age) / 0.12) * (1 - 0.5 * b.f);
                for (const BoltCell& bc : b.cells)
                    bolt[bc.idx] = std::max(bolt[bc.idx], bc.w * I);
            }
        }
    amb = std::min(amb, 1.6);

    // distant rain storms: where each one is this frame
    double storm_cx[NSTORMS];
    for (int q = 0; q < NSTORMS; q++) storm_cx[q] = pymod(STORMS[q].across * C + 0.5 * t, (double)C);

    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);
    for (int i = 0; i < R; i++) {
        // clouds seen from below, in perspective: a flat layer above us that runs
        // off to the horizon, so the pattern shrinks and bunches up with distance
        double d = std::max(S.sky - i, 0) + 1.2;         // rows above the horizon line
        double Z = (R * 0.9) / d;                        // how far away that row of cloud is
        double far = smooth((Z - 3.0) / 12.0);           // 0 overhead .. 1 at the horizon
        for (int j = 0; j < C; j++) {
            double X = (j - C / 2.0) / (C * 0.5) * Z;    // sideways position on the cloud layer
            double n = fbm(X * 1.6 + 0.06 * t, Z * 1.4 + 0.015 * t);
            double dens = clampd((n - 0.22) * 2.2, 0, 1);
            double loc = 0;
            for (const Loc& L : locs) {
                double qx = (j - L.b->cx) / L.b->sx, qy = (i - L.b->cy) / L.b->sy;
                loc += L.ke * std::exp(-(qx * qx + qy * qy));
            }
            loc = std::min(loc, 1.5);
            double lum = 0.012 + 0.17 * far * (0.6 + 0.4 * (1 - dens)) + dens * (0.27 - 0.14 * far)
                       + 0.05 * smooth((i - 0.55 * R) / (0.3 * R))
                       + dens * (0.40 * amb + 0.9 * loc) + (1 - dens) * 0.08 * amb * (1 - (double)i / R);

            // distant rain storms: faint curtains hanging under the far clouds
            for (int q = 0; q < NSTORMS; q++) {
                double dx = std::fabs(j - storm_cx[q]);
                dx = std::min(dx, C - dx);
                double px = std::exp(-std::pow(dx / (STORMS[q].width * C), 2));
                if (px < 1e-3) continue;
                double v = smooth((i - (S.sky - 8)) / 3.0) * (i < S.ridge[j] ? 1.0 : 0.0);
                if (v <= 0) continue;
                double st = vnoise(j * 1.1 - 0.25 * i, 0.9 * t + STORMS[q].across * 50);
                lum += 0.20 * STORMS[q].strength * px * v * (0.4 + 0.8 * st) * (1 + 1.2 * amb);
            }

            // the hills are a dark shape against the sky, lit only a little
            if (i >= S.ridge[j]) lum = 0.004 + 0.07 * amb;
            F.lum[(size_t)i * C + j] = lum;
        }
    }

    // drizzle: a few faint short streaks drifting down
    int rtop = S.rtop, hz = S.horizon;
    double span = hz - rtop;
    double slope = 0.20 + 0.08 * std::sin(0.4 * t);
    int nd = (int)S.x0.size();
    for (int dd = 0; dd < nd; dd++) {
        if (!(S.thr[dd] < inten)) continue;
        double pos = std::fmod(S.ph[dd] * span + S.sp[dd] * t, span);
        for (int k = 0; k < 3 && k < S.ln[dd]; k++) {
            double row = rtop + pos - k;
            int col = (int)std::nearbyint(S.x0[dd] - slope * (row - rtop));
            int r = (int)std::nearbyint(row);
            if (r < rtop || r >= R || col < 0 || col >= C) continue;
            if (!(r < S.ridge[col])) continue;
            double w = (0.30 - 0.09 * k) * smooth((row - rtop) / 4.0) * (1 + 0.35 * std::min(amb, 1.0));
            double& cell = F.lum[(size_t)r * C + col];
            cell = std::max(cell, w);
        }
    }

    double fade = 0.5 + 0.5 * smooth(t / 1.0);                          // fade in
    for (size_t p = 0; p < F.lum.size(); p++) {
        double v = std::max(F.lum[p], bolt[p]);
        F.lum[p] = clampd(v * fade, 0, 1);
        palette(F.lum[p], &F.rgb[p * 3]);
    }

    // which flash does the readout talk about: the latest one so far
    double bt = -1e9, bd = 0;
    for (int s = 0; s < NSTRIKES; s++)
        for (int kk = -1; kk <= 0; kk++) {
            double ts = STRIKES[s].t + 24 * kk;
            if (ts <= tc && ts > bt) { bt = ts; bd = STRIKES[s].dist; }
        }
    double wait = bd / SOUND - (tc - bt);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s %3.1f mm/h", mmh < 3 ? "drizzle" : "light rain", mmh);
    F.hud.resize(HUD_ROWS);
    F.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "flash %4.1f km away", bd);
    F.hud[1] = buf;
    if (bd >= FAR_KM) std::snprintf(buf, sizeof buf, "too far to hear");
    else if (wait > 0) std::snprintf(buf, sizeof buf, "thunder in %4.1f s", wait);
    else std::snprintf(buf, sizeof buf, "%s", wait > -1.5 ? "thunder!" : "quiet");
    F.hud[2] = buf;
}

// the scene as the vtscreen player sees it
class Thunderstorm : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"thunderstorm (C++)", DURATION, {120, 160, 255}, true};
    }
    void setup(int cols, int rows) override { ::setup(cols, rows); }
    void frame(double t, Picture& F) override { ::frame(t, F); }
};

}  // namespace

std::unique_ptr<vtscreen::Scene> make_thunderstorm() { return std::make_unique<Thunderstorm>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Thunderstorm scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
