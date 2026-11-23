// A moon on a starry night, going through all its phases in 24 seconds: new,
// crescent, quarter, gibbous, full, and back again.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made: every cell on the disc is a point on a sphere. The sun's
// direction turns once around the moon per cycle; the lit part of a point is its
// surface normal against that direction (with the moon's own, nearly flat,
// brightness law, so a full moon has no dark edge), which draws the terminator.
// The dark side gets a faint blue earthshine. Dark seas are a handful of soft
// ellipses with noisy edges, craters are bowls with a rim that catch the light
// near the terminator, and one of them has rays. The stars are a hash of the
// cell, each with its own twinkle rhythm.
//
//   g++ -O2 -std=c++17 -o moonphases moonphases.cpp
//   ./moonphases              play once (24 s); q or Ctrl-C quits
//   ./moonphases --loop       keep going
//   ./moonphases --still 12   print the frame at t=12 s as plain text and exit
//   ./moonphases --no-color   letters only
//   ./moonphases --fps N      frames per second (default 30)
//   ./moonphases --cols N --rows N

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
static const double TILT = 0.35;           // radians: how far the line of the horns leans

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

// ---------------------------------------------------------------- the moon's face
// Coordinates on the disc: u to the right, v downwards, the rim at distance 1.

struct Sea { double u, v, a, b; };          // a soft ellipse: centre and two radii
static const Sea SEAS[] = {{-0.30, -0.30, 0.30, 0.20}, {0.05, -0.45, 0.20, 0.15},
                           {0.38, -0.22, 0.20, 0.18},  {0.02, -0.05, 0.18, 0.13},
                           {0.15, 0.25, 0.15, 0.17},   {-0.50, 0.10, 0.10, 0.15},
                           {0.62, 0.18, 0.08, 0.07},   {-0.15, 0.58, 0.12, 0.08}};
static const int NSEAS = sizeof(SEAS) / sizeof(SEAS[0]);

struct Crater { double u, v, r; };
static const Crater CRATERS[] = {{0.30, 0.52, 0.11},  {-0.55, 0.42, 0.08}, {-0.12, -0.68, 0.07},
                                 {0.66, -0.10, 0.06}, {-0.74, -0.12, 0.055}, {0.18, 0.02, 0.045},
                                 {-0.28, 0.62, 0.05}, {0.52, -0.52, 0.05},  {-0.40, -0.05, 0.04},
                                 {0.80, 0.38, 0.05},  {0.05, 0.80, 0.045}};
static const int NCRATERS = sizeof(CRATERS) / sizeof(CRATERS[0]);
static const int RAYED = 0;                  // the first crater throws bright rays

// how dark-sea the point is, 0 (highland) .. 1 (sea)
static double sea_amount(double u, double v) {
    double wob = (fbm(u * 7.0 + 3.0, v * 7.0) - 0.5) * 0.9;
    double s = 0;
    for (int k = 0; k < NSEAS; k++) {
        double du = (u - SEAS[k].u) / SEAS[k].a, dv = (v - SEAS[k].v) / SEAS[k].b;
        s = std::max(s, smooth((1.0 - (du * du + dv * dv)) * 1.5 + wob));
    }
    return s;
}

// height of the ground: bowls with a raised rim
static double height(double u, double v) {
    double h = 0;
    for (int k = 0; k < NCRATERS; k++) {
        double du = u - CRATERS[k].u, dv = v - CRATERS[k].v;
        double d = std::sqrt(du * du + dv * dv) / CRATERS[k].r;
        if (d > 2.0) continue;
        double bowl = d < 1.0 ? -0.6 * (1.0 - d * d) : 0.0;
        double rim = 0.5 * std::exp(-((d - 1.0) / 0.18) * ((d - 1.0) / 0.18));
        h += (bowl + rim) * CRATERS[k].r * 6.0;
    }
    return h;
}

// ---------------------------------------------------------------- the scene

class Moonphases : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"moonphases (C++)", DURATION, {150, 175, 235}, true};
    }
    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        Rm = std::min(cols * 0.27, rows * 2.0 * 0.34);   // radius in column widths
    }
    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double Rm = 1;
};

static const char* phase_name(double p) {          // p: 0 new .. 0.5 full .. 1 new
    if (p < 0.03 || p >= 0.97) return "new moon";
    if (p < 0.22) return "waxing crescent";
    if (p < 0.28) return "first quarter";
    if (p < 0.47) return "waxing gibbous";
    if (p < 0.53) return "full moon";
    if (p < 0.72) return "waning gibbous";
    if (p < 0.78) return "last quarter";
    return "waning crescent";
}

void Moonphases::frame(double t, Picture& F) {
    F.init(R, C);

    // where in the cycle: 0 = new, 0.5 = full. t = 0 is a day or so after new moon,
    // so a play of 24 s ends on the new moon it began after.
    const double p = std::fmod(std::fmod(t + 1.0, DURATION) + DURATION, DURATION) / DURATION;
    const double phi = 2.0 * M_PI * p;
    const double ill = 0.5 - 0.5 * std::cos(phi);   // lit fraction
    // sun direction: behind the moon at new, in front at full, to the right while waxing
    const double sn = std::sin(phi), cs = std::cos(phi);
    const double Lx = sn * std::cos(TILT), Ly = -sn * std::sin(TILT), Lz = -cs;

    // the moon drifts a little along an arc
    const double mcx = C * (0.5 + 0.035 * std::sin(2 * M_PI * t / DURATION - 1.2));
    const double mcy = R * (0.5 - 0.025 * std::cos(2 * M_PI * t / DURATION - 1.2));

    for (int i = 0; i < R; i++) {
        const double Y = (i + 0.5 - mcy) * 2.0;
        for (int j = 0; j < C; j++) {
            const double X = j + 0.5 - mcx;
            const double dist = std::sqrt(X * X + Y * Y);       // in column widths
            double lum = 0, col[3] = {0, 0, 0};

            // ---- the sky: a faint glow round the moon, and stars
            const double halo = dist > Rm ? ill * 0.08 * std::exp(-(dist - Rm) / Rm * 2.6) : 0.0;
            if (halo > 0) {
                lum = halo;
                col[0] = 80; col[1] = 105; col[2] = 170;
            }
            if (rnd(j, i, 7) < 0.035) {
                double base = 0.22 + 0.78 * std::pow(rnd(j, i, 8), 2.0);
                double rate = 0.25 + 0.8 * rnd(j, i, 9);
                double tw = 0.62 + 0.38 * std::sin(2 * M_PI * rate * t + 6.28 * rnd(j, i, 10));
                double dim = 1.0 - clampd(halo * 6.0, 0.0, 0.95);        // the glow drowns stars
                double s = base * tw * dim;
                if (s > lum) {
                    double w = rnd(j, i, 11);                               // blue-white .. yellow-white
                    lum = s;
                    col[0] = lerp(190, 255, w); col[1] = lerp(205, 240, w); col[2] = lerp(255, 200, w);
                }
            }

            // ---- the moon
            double cov = clampd((Rm - dist) + 0.5, 0.0, 1.0);          // soft edge, about a cell wide
            if (cov > 0) {
                double r2 = std::min((X * X + Y * Y) / (Rm * Rm), 0.9999);
                double u = X / Rm, v = Y / Rm, pz = std::sqrt(1.0 - r2);
                double d = u * Lx + v * Ly + pz * Lz;                   // sun against the surface

                // the ground: highlands, dark seas, craters, rays
                double sea = sea_amount(u, v);
                double alb = 0.80 - 0.36 * sea + (fbm(u * 5.0 + 20.0, v * 5.0) - 0.5) * 0.22;
                for (int k = 0; k < NCRATERS; k++) {
                    double du = u - CRATERS[k].u, dv = v - CRATERS[k].v;
                    double q = std::sqrt(du * du + dv * dv) / CRATERS[k].r;
                    if (q < 1.0) alb -= 0.26 * (1.0 - q * q);               // a darker floor
                    else if (q < 1.5) alb += 0.20 * (1.5 - q);              // a brighter rim
                    if (k == RAYED && q > 0.8) {
                        double rd = q * CRATERS[k].r;                       // disc units
                        if (rd < 0.5) {
                            double ang = std::atan2(dv, du);
                            double ray = clampd((fbm(std::cos(ang) * 2.4 + 8.0, std::sin(ang) * 2.4) - 0.42) * 3.0, 0, 1);
                            alb += 0.45 * ray * std::pow(1.0 - rd / 0.5, 1.3);
                        }
                    }
                }
                alb = clampd(alb, 0.2, 1.15);

                // sunlight: nothing past the terminator, a flat brightness elsewhere
                double sh = 0;
                if (d > 0) sh = smooth(d / 0.07) * std::min(1.0, 2.0 * d / (d + pz + 0.02));
                // relief: crater rims catch low light, mostly near the terminator
                const double e = 0.012;
                double gx = (height(u + e, v) - height(u - e, v)) / (2 * e);
                double gy = (height(u, v + e) - height(u, v - e)) / (2 * e);
                double relief = clampd(0.06 * (gx * Lx + gy * Ly), -0.35, 0.35);
                double sun = clampd(sh * (0.30 + 0.70 * alb) * (1.0 + relief) * 1.02, 0.0, 1.0);

                // earthshine: the dark side is faintly lit by the Earth, most when the moon is thin
                double es = (0.012 + 0.060 * std::pow(1.0 - ill, 0.8)) * (0.5 + 0.8 * alb) * (1.0 - sh) * (0.6 + 0.4 * pz);

                double k = std::pow(clampd(sun / 0.85, 0.0, 1.0), 0.85);
                double ke = clampd(es / 0.075, 0.0, 1.0) * (1.0 - k);
                double ac = clampd(alb, 0.0, 1.0);
                double sunCol[3] = {lerp(206, 246, ac), lerp(210, 240, ac), lerp(218, 214, ac)};
                double mlum = sun + es;
                double mcol[3];
                const double earth[3] = {64, 88, 142};
                for (int c = 0; c < 3; c++) mcol[c] = sunCol[c] * k + earth[c] * ke;

                lum = lerp(lum, mlum, cov);
                for (int c = 0; c < 3; c++) col[c] = lerp(col[c], mcol[c], cov);
            }
            auto& pc = F.at(i, j);
            pc.lum = clampd(lum, 0.0, 1.0); pc.r = col[0]; pc.g = col[1]; pc.b = col[2];
        }
    }

    char buf[64];
    F.text(0, 0, phase_name(p));
    std::snprintf(buf, sizeof buf, "%3.0f%% lit", ill * 100.0);
    F.text(1, 0, buf);
    std::snprintf(buf, sizeof buf, "day %4.1f of 29.5", p * 29.5);
    F.text(2, 0, buf);
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_moonphases() { return std::make_unique<Moonphases>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Moonphases scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
