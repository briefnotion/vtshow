// A black hole: a dark silhouette, a glowing disk of gas seen at a tilt and
// turning round it, light from the far side of the disk bent over the top and
// under the bottom, and a field of twinkling stars bent round the hole.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made (an approximation, not a ray tracer). Everything is measured in
// units of the shadow radius (the circle the hole blots out):
//  * the disk is a flat ring, tilted towards us, between an inner and an outer
//    radius. Each pixel is mapped back onto the disk plane to find which bit of
//    the ring it shows; the part in front of the hole is drawn over the shadow,
//    the part behind it is hidden by it;
//  * the disk's brightness falls off with radius, is streaked by noise carried
//    round at the Keplerian speed (the inside turns faster than the outside), and
//    is brighter on the side coming towards us (Doppler beaming);
//  * light from the far side of the disk is bent over the hole: a halo hugs the
//    shadow all the way round (over the top, under the bottom), made from the far
//    half of the disk and thinning outwards, with a thin bright "photon ring";
//  * the stars are looked up through a lens: a pixel at distance b from the hole
//    shows the sky at b * (1 - E^2/b^2), so stars bunch up and stretch into arcs
//    near the hole, and a star that drifts behind it is smeared into a ring.
// The tilt breathes slowly with a period of the whole 24 s, and the sky drifts.
//
//   g++ -O2 -std=c++17 -o singularity singularity.cpp
//   ./singularity              play once (24 s); q or Ctrl-C quits
//   ./singularity --loop       keep going
//   ./singularity --still 6    print the frame at t=6 s as plain text and exit
//   ./singularity --no-color   letters only
//   ./singularity --fps N      frames per second (default 30)
//   ./singularity --cols N --rows N

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

static const double R_IN = 1.35;        // inner edge of the disk, in shadow radii
static const double R_OUT = 4.0;        // outer edge
static const double OMEGA_IN = 1.5;     // radians per second at the inner edge

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

// gas colour from brightness: dark violet, maroon, orange, gold, white with a hint of blue
static void palette(double v, double out[3]) {
    static const double xs[6] = {0.0, 0.18, 0.42, 0.68, 0.88, 1.0};
    static const double cs[6][3] = {{8, 3, 18}, {75, 18, 65}, {205, 70, 38},
                                    {255, 160, 70}, {255, 228, 185}, {238, 244, 255}};
    v = clampd(v, 0.0, 1.0);
    int k = 0;
    while (k < 4 && v > xs[k + 1]) k++;
    double f = (v - xs[k]) / (xs[k + 1] - xs[k]);
    for (int c = 0; c < 3; c++) out[c] = lerp(cs[k][c], cs[k + 1][c], f);
}

// ---------------------------------------------------------------- the scene

class Singularity : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"singularity (C++)", DURATION, {255, 150, 90}, true};
    }

    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        // the shadow radius in column widths: the whole disk (R_OUT of them) has to
        // fit across, and its halo above and below
        bc = std::min(0.115 * cols, 0.19 * rows * 2.0);
    }
    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double bc = 1;

    // how bright the gas is at disk radius r (in shadow radii), azimuth a already
    // advanced by the rotation, before beaming
    static double gas(double r, double a_rot, double t) {
        double inner = smooth((r - R_IN) / 0.18);
        double outer = smooth((R_OUT - r) / 1.0);
        double base = std::pow(R_IN / r, 2.1) * inner * outer;
        // streaks and clumps carried round with the gas: seen as bands that wind
        // up because the inside turns faster
        double ca = std::cos(a_rot), sa = std::sin(a_rot);
        double n = fbm(ca * 2.6 + r * 1.3, sa * 2.6 - r * 1.1);
        double arms = 0.5 + 0.5 * std::sin(3.0 * a_rot + 4.5 * std::log(r));
        double ring = 0.5 + 0.5 * std::sin(r * 9.0 + 2.0 * n);                 // faint concentric bands
        double tex = 0.45 + 0.9 * n * (0.65 + 0.35 * arms) * (0.8 + 0.2 * ring);
        (void)t;
        return base * tex;
    }
};

void Singularity::frame(double t, Picture& F) {
    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);

    // the disk tilts: 90 degrees would be edge on, 0 face on
    const double tilt = (70.0 + 8.0 * std::sin(2 * M_PI * t / DURATION)) * M_PI / 180.0;
    const double ci = std::cos(tilt), si = std::sin(tilt);
    const double cx = C * 0.5, cy = R * 0.5;
    const double drift = 0.9 * t;                           // the sky slides slowly sideways (columns per second)
    const double E2 = 0.80;                                 // the lens: source = apparent * (1 - E2 / b^2), b in shadow radii

    for (int i = 0; i < R; i++) {
        const double Y = -((i + 0.5) - cy) * 2.0 / bc;      // up, in shadow radii
        for (int j = 0; j < C; j++) {
            const double X = (j + 0.5 - cx) / bc;
            const double b2 = X * X + Y * Y;
            const double b = std::sqrt(b2);
            double lum = 0, col[3] = {0, 0, 0};

            // ---- the stars, seen through the lens
            if (b > 0.98) {
                double sc = 1.0 - E2 / b2;
                // where in the sky this pixel looks, in cells of the lattice (one cell
                // of the screen when there is no lens): a star in a few of them
                double gx = X * sc * bc + drift, gy = Y * sc * bc * 0.5;
                int32_t ix = (int32_t)std::floor(gx), iy = (int32_t)std::floor(gy);
                uint32_t ux = (uint32_t)ix, uy = (uint32_t)iy;
                if (rnd(ux, uy, 21) < 0.04) {
                    double base = 0.25 + 0.75 * std::pow(rnd(ux, uy, 22), 2.0);
                    double rate = 0.3 + 0.9 * rnd(ux, uy, 23);
                    double tw = 0.6 + 0.4 * std::sin(2 * M_PI * rate * t + 6.28 * rnd(ux, uy, 24));
                    double mag = clampd(1.0 / std::fabs(1.0 - 0.8 * E2 * E2 / (b2 * b2)), 1.0, 2.2);   // brighter in the lensed zone
                    lum = clampd(base * tw * 0.9 * (0.75 + 0.25 * mag), 0.0, 1.0);
                    double w = rnd(ux, uy, 25);
                    col[0] = lerp(165, 255, w); col[1] = lerp(190, 235, w); col[2] = lerp(255, 190, w);
                    // reddened and dimmed right at the edge of the shadow
                    double edge = smooth((b - 0.98) / 0.35);
                    lum *= edge;
                }
            }

            // ---- the disk, seen at a tilt: map the pixel back onto the disk plane
            //      (disk x = X, disk y = Y / cos(tilt); y > 0 is the far side)
            const double yd = Y / ci;
            const double rd = std::sqrt(X * X + yd * yd);
            double disk = 0;
            if (rd > R_IN * 0.8 && rd < R_OUT * 1.05) {
                bool front = yd < 0;
                if (front || b > 1.0) {                           // the far side hides behind the shadow
                    double ang = std::atan2(yd, X);
                    double a_rot = ang - OMEGA_IN * std::pow(R_IN / rd, 1.5) * t;
                    double v = 0.30 * std::pow(R_IN / rd, 0.5);         // orbital speed as a fraction of c, roughly
                    double los = -X / rd * si;                           // towards us on the left
                    double beam = std::pow(1.0 / (1.0 - v * los), 2.0) * (1.0 - v * v);
                    disk = gas(rd, a_rot, t) * beam * 1.2;
                    if (!front) disk *= smooth((b - 1.0) / 0.25);       // soft edge against the shadow
                }
            }

            // ---- the halo: the far side of the disk, bent over and under the hole
            double halo = 0;
            if (b > 1.0 && b < 2.6) {
                double rr = R_IN * (1.0 + (b - 1.0) * 1.7);           // which radius of the disk it shows
                double th = std::atan2(Y, X);
                double a_far = std::atan2(std::fabs(std::sin(th)), std::cos(th));   // always the far half
                double a_rot = a_far - OMEGA_IN * std::pow(R_IN / rr, 1.5) * t;
                double v = 0.30 * std::pow(R_IN / rr, 0.5);
                double los = -std::cos(th) * si;
                double beam = std::pow(1.0 / (1.0 - v * los), 2.0) * (1.0 - v * v);
                double fall = std::exp(-(b - 1.0) * 2.4);
                halo = gas(rr, a_rot, t) * beam * 1.2 * 1.2 * fall;
                // the thin bright ring of light that has gone round the hole
                double pr = std::exp(-std::pow((b - 1.025) / 0.03, 2.0));
                halo += 0.55 * pr * (0.5 + 0.5 * beam * 0.6);
            }

            double gasv = std::max(disk, halo);
            if (b <= 1.0 && !(yd < 0 && rd > R_IN * 0.8 && rd < R_OUT)) {
                lum = 0;                                              // the shadow
                col[0] = col[1] = col[2] = 0;
            }
            if (gasv > 0 && (b > 1.0 || (yd < 0))) {
                double gl = clampd(gasv, 0.0, 1.0);
                // gas hides the stars behind it
                double cover = smooth(gl / 0.12);
                double pc[3];
                palette(gl, pc);
                if (gl > lum * 0.0 + 0.0 && (cover > 0.0)) {
                    lum = lerp(lum, gl, cover);
                    for (int c = 0; c < 3; c++) col[c] = lerp(col[c], pc[c], cover);
                }
            }
            size_t p = (size_t)i * C + j;
            F.lum[p] = clampd(lum, 0.0, 1.0);
            for (int c = 0; c < 3; c++) F.rgb[p * 3 + c] = col[c];
        }
    }

    char buf[64];
    F.hud.resize(HUD_ROWS);
    std::snprintf(buf, sizeof buf, "tilt %.1f deg", tilt * 180.0 / M_PI);
    F.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "inner spin %.1f s/turn", 2 * M_PI / OMEGA_IN);
    F.hud[1] = buf;
    std::snprintf(buf, sizeof buf, "hole r %.0f cols", bc);
    F.hud[2] = buf;
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_singularity() { return std::make_unique<Singularity>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Singularity scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
