// A black hole: a dark silhouette, a glowing disk of gas seen at a tilt and turning round it,
// the disk's light bent right round the hole (an arc over the top, a fainter second image
// below, a thin ring at the shadow's edge), and a field of stars bent round it too.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made. Everything is measured in units of the shadow radius (the circle the hole
// blots out; for a hole that does not spin that is 3 sqrt(3) M, M being half the horizon radius):
//  * the paths of light round the hole (Schwarzschild null geodesics) are worked out once, in
//    setup(), into a table (see LightPaths). Each pixel's ray is followed back from the camera
//    through it: it swings round the hole, and wherever it crosses the plane of the disk it
//    picks up that bit of gas. The same ray can cross the plane more than once: the near side
//    of the disk seen directly, the far side bent over the top, and again through light that
//    went round the back, which is the fainter arc below the hole and the thin ring at the
//    edge of the shadow. A ray that falls in shows nothing: that is the shadow;
//  * the disk is a flat ring, tilted towards us. How bright it is comes from physics: the flux of
//    a thin disk of gas on circular orbits (Page and Thorne), which is zero at the innermost
//    stable orbit, peaks at 9.55 M and falls away outward, times streaks and clumps of noise
//    carried round at the Keplerian speed (the inside turns faster than the outside). The
//    temperature goes as the flux to the 1/4 and the colour is the colour of a black body at that
//    temperature (see disk_flux and BlackbodyColors);
//  * the light is shifted: for gas on a circular orbit the shift is g = sqrt(1 - 3M/r) /
//    (1 + Omega b sin(i) cos(theta)), Doppler and gravity together. Temperature is multiplied by g
//    and brightness by g^4, so the side coming towards us is hotter, paler and much brighter, and
//    the receding side is cooler, deeper orange and dimmer. Exactly, that is too lopsided to look
//    good (the film Interstellar toned it down too), so DOPPLER_SCALE shrinks it;
//  * the stars are looked up through a simpler lens: a pixel at distance b from the hole
//    shows the sky at b * (1 - E^2/b^2), so stars bunch up and stretch into arcs
//    near the hole, and a star that drifts behind it is smeared into a ring. They
//    stay at the dimmest letters (stage 1) and twinkle by a slow shimmer, except
//    where the lens magnifies them.
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

static const double R_IN = 0.75;        // inner edge of the gas, in shadow radii. Light that went round
                                        // the hole shows the disk at about this radius right at the edge
                                        // of the shadow (1.03 shows radius 0.68), so the gas has to reach
                                        // in this far for the arcs to start at the shadow, as in a photo
static const double R_OUT = 3.2;        // outer edge (the gas has faded out well before it)
static const double OUTER_FADE = 1.4;   // the gas fades out over this width, up to R_OUT
static const double OMEGA_IN = 1.5;     // radians per second at the inner edge

// The stars stay at stage 1 of the 16 brightness steps (the letters . ` '), so they do not
// clutter the sky. Their twinkle is the letter changing, at a slow shimmer speed of their
// own (1 is the normal rate; see PicCell::shimmer).
static const double STAR_LUM = 0.10;
static const double STAR_SHIMMER_MIN = 0.05, STAR_SHIMMER_MAX = 0.35;

// The lens that bends the stars. A star at distance b (in shadow radii) from the hole is seen
// where the sky is at b - LENS_E2 / b: LENS_E2 is the square of the Einstein radius, where a star
// directly behind the hole is smeared into a ring. Bigger bends more: stars stretch into arcs along
// the circle over a wider zone, and inside the Einstein radius a second, turned-round image shows.
// A lensed star is also brighter, by the lens magnification, up to LENS_MAG_MAX times STAR_LUM.
static const double LENS_E2 = 2.4;
static const double LENS_MAG_MAX = 3.0;

// The disk's light, from physics (see disk_flux, BlackbodyColors and the shift g in frame()).
// The gas moves on circular orbits, so its light is shifted by g (Doppler and gravity together):
// its temperature is multiplied by g and its brightness by g^SHIFT_POWER, so the side coming towards
// us is hotter, bluer and much brighter. Done exactly that is far too lopsided to look good (the
// makers of the film Interstellar found the same and toned theirs down), so DOPPLER_SCALE shrinks
// it: 1 is exact, 0 is no shift at all. DISK_GAIN is the overall brightness. T_PEAK is the
// temperature of the hottest ring in kelvin, and T_MIN the lowest the gas gets. Gas dimmer than
// GAS_FLOOR is not drawn.
static const double DOPPLER_SCALE = 0.40;
static const double SHIFT_POWER = 4.0;
static const double DISK_GAIN = 2.2;
static const double FLUX_SHARPEN = 2.4;     // brightness goes as flux to this power: the outer gas fades faster
static const double T_PEAK = 3800.0, T_MIN = 1700.0;
static const double GAS_FLOOR = 0.09;
static const double FLUX_PEAK = 1.7188e-4;  // the largest value of disk_flux (at r = 9.55 M)
// disk_flux ends at 6 M. Scaling the radius by this makes the gas start at R_IN shadow radii instead.
static const double RADIUS_SCALE = 6.0 / (R_IN * 5.196152422706632);
// The thin bright ring of light that has gone round the hole, just outside the shadow edge.
static const double RING_BRIGHT = 0.5, RING_RADIUS = 1.03, RING_WIDTH = 0.025;
// A bright cell (0.5 and up) is a solid block with a letter on it. The letter is the block's own
// colour times this, a soft darker tone, so a bright area is calm and not black holes like static.
static const double LETTER_TONE = 0.62;
// The disk is seen at TILT_BASE degrees from face-on, breathing by +-TILT_SWING over the 24 s
// (90 would be edge on; the photo this was matched to is nearly edge on).
static const double TILT_BASE = 74.0, TILT_SWING = 6.0;

// How close the view is: 1 fits the whole disk across the screen, 1.2 is 20% closer, so the
// hole is bigger and stands out on a small screen (the faint outer edge of the disk is cut off).
static const double ZOOM = 1.2;

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

#if 0   // retired: a colour ramp made up by eye, and a tint for the shift; the colour now comes from temperature
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

// The colour of gas whose light is shifted: g above 1 (coming towards us, or moving to us)
// is bluer and whiter, g below 1 (receding, or climbing out of the hole's gravity) redder.
static void doppler_tint(double c[3], double g) {
    if (g >= 1.0) {
        const double k = clampd((g - 1.0) * TINT_BLUE, 0.0, 0.7);
        c[0] = lerp(c[0], 190, k); c[1] = lerp(c[1], 215, k); c[2] = lerp(c[2], 255, k);
    } else {
        const double k = clampd((1.0 - g) * TINT_RED, 0.0, 0.8);
        c[1] *= 1.0 - 0.45 * k; c[2] *= 1.0 - 0.85 * k;
    }
}
#endif

// ---------------------------------------------------------------- the disk's physics
//
// Brightness. The flux leaving a thin disk of gas on circular orbits round a hole that does not
// spin (Page and Thorne): with r in M and the disk ending at the innermost stable orbit, 6 M,
//   F(r) = 3/(8 pi) * [ sqrt(r) - sqrt(6) + (sqrt(3)/2) ln( (sqrt(r)+sqrt(3))(sqrt(6)-sqrt(3)) /
//                                                         ((sqrt(r)-sqrt(3))(sqrt(6)+sqrt(3))) ) ]
//        / ( r^(5/2) (r - 3) )          (per unit accretion rate, M = 1)
// It is zero at 6 M (growing as (r-6)^2 there), peaks at 9.55 M and falls as 1/r^3. This comes
// from integrating the conserved energy and angular momentum of the orbits: the integral
// of (E - Omega L) dL/dr collapses to the log above. (A copy of this formula in an open-source
// reproduction of Luminet's famous 1979 picture has sqrt(3)/3 and a log base 10; both are wrong:
// checked against the integral, the sqrt(3)/2 and the natural log agree to 1e-10.)
static double disk_flux(double r) {
    if (r <= 6.0) return 0.0;
    const double s = std::sqrt(r), s3 = 1.7320508075688772, s6 = 2.449489742783178;
    const double bracket = s - s6 + 0.5 * s3 * std::log(((s + s3) * (s6 - s3)) / ((s - s3) * (s6 + s3)));
    return 1.5 * bracket / (std::pow(r, 2.5) * (r - 3.0));
}

// Colour. The colour of a black body at a temperature, worked out once into a table: the Planck
// spectrum is turned into the colour the eye sees with the CIE colour matching functions (the
// multi-lobe fits of Wyman, Sloan and Shirley, 2013), then into sRGB. Only the hue is kept (the
// brightest channel is 1); how bright the gas is is set separately. at() gives sRGB 0..255.
struct BlackbodyColors {
    static constexpr int N = 256;
    static constexpr double TMIN = 1000.0, TMAX = 20000.0;
    float rgb[N][3];

    static double lobe(double l, double mu, double s1, double s2) {
        const double t = (l - mu) / (l < mu ? s1 : s2);
        return std::exp(-0.5 * t * t);
    }
    // x, y chromaticity of a black body, for testing
    static void chromaticity(double T, double& cx, double& cy, double xyz[3] = nullptr) {
        double X = 0, Y = 0, Z = 0;
        for (double l = 380.0; l <= 780.0; l += 5.0) {
            const double planck = 1.0 / (std::pow(l, 5.0) * (std::exp(1.4388e7 / (l * T)) - 1.0));
            X += planck * (1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2));
            Y += planck * (0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1));
            Z += planck * (1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8));
        }
        cx = X / (X + Y + Z); cy = Y / (X + Y + Z);
        if (xyz) { xyz[0] = X; xyz[1] = Y; xyz[2] = Z; }
    }
    void build() {
        for (int k = 0; k < N; k++) {
            const double T = TMIN * std::pow(TMAX / TMIN, k / (N - 1.0));
            double cx, cy, xyz[3];
            chromaticity(T, cx, cy, xyz);
            const double X = xyz[0], Y = xyz[1], Z = xyz[2];
            double lin[3] = {3.2406 * X - 1.5372 * Y - 0.4986 * Z,
                             -0.9689 * X + 1.8758 * Y + 0.0415 * Z,
                             0.0557 * X - 0.2040 * Y + 1.0570 * Z};
            double mx = 1e-30;
            for (double& v : lin) { if (v < 0) v = 0; mx = std::max(mx, v); }
            for (int c = 0; c < 3; c++) {
                const double v = lin[c] / mx;                                   // hue only
                rgb[k][c] = (float)(255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055));
            }
        }
    }
    void at(double T, double out[3]) const {
        double f = std::log(std::min(std::max(T, TMIN), TMAX) / TMIN) / std::log(TMAX / TMIN) * (N - 1);
        const int k = std::min((int)f, N - 2);
        f -= k;
        for (int c = 0; c < 3; c++) out[c] = rgb[k][c] * (1 - f) + rgb[k + 1][c] * f;
    }
};

// ---------------------------------------------------------------- light paths round the hole
//
// The paths of light round a black hole that does not spin (Schwarzschild), worked out once.
// Units: M = 1 (half the horizon radius). A ray reaching the camera at impact parameter b
// (how far it passes from the hole) is followed backwards from the camera, round the hole.
// With psi the angle it has swept round the hole, u = 1/r obeys u'' + u = 3 u^2 (Binet's
// equation for light); the ray starts at u = 0, u' = 1/b. A ray with b below the critical
// 3 sqrt(3) falls in; above it, it swings past and escapes. In the scene b is measured in
// units of that critical value, so the shadow has radius 1.
//
// The table holds u and du/dpsi for many b and psi, so a pixel can ask: "following my ray
// back, how far from the hole is it after sweeping psi?" The disk is hit where the ray crosses
// the disk's plane, which happens at psi, psi + pi, psi + 2 pi: the first is the disk seen
// more or less directly (the near side, or the far side bent over the top), the later ones
// are the same disk seen again through light that has gone round the back of the hole.
struct LightPaths {
    static constexpr int NX = 420, NP = 790;
    static constexpr double BC = 5.196152422706632;       // critical impact parameter, in M
    static constexpr double DPSI = 0.0125;
    static constexpr double SCALE = 0.02, XMIN = 0.05, XMAX = 7.0;
    std::vector<float> u, du;                              // [NX][NP]
    double w0 = 0, dw = 1;

    static double asinh_(double v) { return std::log(v + std::sqrt(v * v + 1.0)); }

    void build() {
        if (!u.empty()) return;
        w0 = asinh_((XMIN - 1.0) / SCALE);
        const double w1 = asinh_((XMAX - 1.0) / SCALE);
        dw = (w1 - w0) / (NX - 1);
        u.assign((size_t)NX * NP, 0.0f);
        du.assign((size_t)NX * NP, 0.0f);
        for (int k = 0; k < NX; k++) {
            const double x = 1.0 + SCALE * std::sinh(w0 + k * dw);
            const double b = x * BC;
            double y = 0.0, v = 1.0 / b;
            int state = 0;                                 // 0 flying, 1 fell in, 2 escaped
            for (int j = 0; j < NP; j++) {
                float* U = &u[(size_t)k * NP + j];
                float* V = &du[(size_t)k * NP + j];
                if (state == 0) {
                    *U = (float)y; *V = (float)v;
                    for (int sub = 0; sub < 2; sub++) {    // two RK4 steps of DPSI / 2
                        const double h = DPSI / 2;
                        auto f = [](double yy, double vv, double& dy, double& dv) { dy = vv; dv = -yy + 3.0 * yy * yy; };
                        double a1, b1, a2, b2, a3, b3, a4, b4;
                        f(y, v, a1, b1);
                        f(y + 0.5 * h * a1, v + 0.5 * h * b1, a2, b2);
                        f(y + 0.5 * h * a2, v + 0.5 * h * b2, a3, b3);
                        f(y + h * a3, v + h * b3, a4, b4);
                        y += h / 6.0 * (a1 + 2 * a2 + 2 * a3 + a4);
                        v += h / 6.0 * (b1 + 2 * b2 + 2 * b3 + b4);
                    }
                    if (y >= 0.5) state = 1;
                    else if (j > 2 && y <= 0.0) state = 2;
                } else if (state == 1) {
                    *U = 1.0f; *V = 0.0f;                  // inside the horizon
                } else {
                    *U = 0.0f; *V = 0.0f;                  // gone to infinity
                }
            }
        }
    }

    // 0: found (r_M = distance from the hole in M, dr = dr/dpsi), 1: the ray fell in before this
    // angle, 2: the ray has already escaped. x is b in units of the critical value.
    int lookup(double x, double psi, double& r_M, double& dr) const {
        if (psi < 0 || psi >= (NP - 1) * DPSI) return 2;
        if (x < XMIN) x = XMIN;
        if (x > XMAX - 1e-6) x = XMAX - 1e-6;
        double fk = (asinh_((x - 1.0) / SCALE) - w0) / dw;
        fk = std::min(std::max(fk, 0.0), NX - 1.000001);
        const int k = (int)fk;
        const double ft = fk - k;
        const double fj = psi / DPSI;
        const int j = (int)fj;
        const double fs = fj - j;
        const size_t i00 = (size_t)k * NP + j, i10 = i00 + NP;
        const float a = u[i00], b = u[i00 + 1], c = u[i10], d = u[i10 + 1];
        if (a >= 0.99f || b >= 0.99f || c >= 0.99f || d >= 0.99f) return 1;
        if (a <= 1e-6f && b <= 1e-6f && c <= 1e-6f && d <= 1e-6f) return 2;
        const double uu = (a * (1 - fs) + b * fs) * (1 - ft) + (c * (1 - fs) + d * fs) * ft;
        const double vv = (du[i00] * (1 - fs) + du[i00 + 1] * fs) * (1 - ft) + (du[i10] * (1 - fs) + du[i10 + 1] * fs) * ft;
        if (uu < 1e-5) return 2;
        r_M = 1.0 / uu;
        dr = -vv / (uu * uu);
        return 0;
    }

    // how far a ray at b (in critical units) is turned in all, in radians, 0 for no bending;
    // -1 if it falls in. Used for testing, and to check the table.
    double bending(double x) const {
        double fk = (asinh_((x - 1.0) / SCALE) - w0) / dw;
        fk = std::min(std::max(fk, 0.0), NX - 1.000001);
        const int k = (int)fk;
        for (int j = 3; j < NP - 1; j++) {
            const float a = u[(size_t)k * NP + j], b = u[(size_t)k * NP + j + 1];
            if (a >= 0.99f) return -1.0;
            if (a > 1e-6f && b <= 1e-6f)                   // it crosses u = 0 between j and j + 1
                return j * DPSI + a / -du[(size_t)k * NP + j] - M_PI;
        }
        return -2.0;                                       // still going round at the end of the table
    }
};

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
        bc = ZOOM * std::min(0.115 * cols, 0.19 * rows * 2.0);
        lp.build();
        bb.build();
    }
    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double bc = 1;
    LightPaths lp;

    BlackbodyColors bb;

    // The texture of the gas, around 1: clumps and arms carried round with the gas (r in shadow
    // radii, a_rot the azimuth already turned by the rotation: the inside turns faster, so they
    // wind up), and fine irregular streaks that run round the hole (they change quickly with radius
    // and slowly round it; bent round the hole they become the arcs).
    static double turbulence(double r, double a_rot) {
        const double ca = std::cos(a_rot), sa = std::sin(a_rot);
        const double n = fbm(ca * 2.6 + r * 1.3, sa * 2.6 - r * 1.1);
        const double arms = 0.5 + 0.5 * std::sin(3.0 * a_rot + 4.5 * std::log(r));
        const double streak = fbm(r * 12.0 + 3.0, a_rot * 0.9);
        return (0.45 + 0.9 * n * (0.65 + 0.35 * arms) * (0.7 + 0.7 * streak)) * 1.25;
    }

#if 0   // retired: brightness and colour made up by eye
    // how bright the gas is at disk radius r (in shadow radii), azimuth a already
    // advanced by the rotation, before beaming
    static double gas(double r, double a_rot, double t) {
        double inner = smooth((r - R_IN) / 0.18);
        double outer = smooth((R_OUT - r) / OUTER_FADE);
        double base = std::pow(R_IN / r, DISK_FALLOFF) * inner * outer;
        // streaks and clumps carried round with the gas: seen as bands that wind
        // up because the inside turns faster
        double ca = std::cos(a_rot), sa = std::sin(a_rot);
        double n = fbm(ca * 2.6 + r * 1.3, sa * 2.6 - r * 1.1);
        double arms = 0.5 + 0.5 * std::sin(3.0 * a_rot + 4.5 * std::log(r));
        // fine streaks that run round the hole: they change quickly with radius and slowly round it,
        // irregular, so they are not bands (bent round the hole they become the arcs in the halo)
        double streak = fbm(r * 12.0 + 3.0, a_rot * 0.9);
        double tex = 0.45 + 0.9 * n * (0.65 + 0.35 * arms) * (0.7 + 0.7 * streak);
        (void)t;
        return base * tex;
    }
#endif
};

void Singularity::frame(double t, Picture& F) {
    F.init(R, C);

    // the disk tilts: 90 degrees would be edge on, 0 face on
    const double tilt = (TILT_BASE + TILT_SWING * std::sin(2 * M_PI * t / DURATION)) * M_PI / 180.0;
    const double ci = std::cos(tilt), si = std::sin(tilt);
    const double cx = C * 0.5, cy = R * 0.5;
    const double drift = 0.9 * t;                           // the sky slides slowly sideways (columns per second)
    const double E2 = LENS_E2;                              // the lens: source = apparent * (1 - E2 / b^2), b in shadow radii

    for (int i = 0; i < R; i++) {
        const double Y = -((i + 0.5) - cy) * 2.0 / bc;      // up, in shadow radii
        for (int j = 0; j < C; j++) {
            const double X = (j + 0.5 - cx) / bc;
            const double b2 = X * X + Y * Y;
            const double b = std::sqrt(b2);
            double lum = 0, col[3] = {0, 0, 0};
            double shimmer = 1.0;

            // ---- the stars, seen through the lens
            if (b > 1.0) {
                double sc = 1.0 - E2 / b2;
                // where in the sky this pixel looks, in cells of the lattice (one cell
                // of the screen when there is no lens): a star in a few of them
                double gx = X * sc * bc + drift, gy = Y * sc * bc * 0.5;
                int32_t ix = (int32_t)std::floor(gx), iy = (int32_t)std::floor(gy);
                uint32_t ux = (uint32_t)ix, uy = (uint32_t)iy;
                if (rnd(ux, uy, 21) < 0.04) {
                    // the lens makes a star brighter too, most near the Einstein ring
                    double mu = clampd(1.0 / std::fabs(1.0 - E2 * E2 / (b2 * b2)), 1.0, LENS_MAG_MAX);
                    lum = STAR_LUM * mu;
                    shimmer = lerp(STAR_SHIMMER_MIN, STAR_SHIMMER_MAX, rnd(ux, uy, 23));
                    double w = rnd(ux, uy, 25);
                    col[0] = lerp(165, 255, w); col[1] = lerp(190, 235, w); col[2] = lerp(255, 190, w);
                    // reddened and dimmed right at the edge of the shadow
                    double edge = smooth((b - 0.98) / 0.35);
                    lum *= edge;
                }
            }

            // ---- the disk, traced through the light paths round the hole. The ray of this pixel is
            //      followed back from the camera; it crosses the disk's plane at psi_a, psi_a + pi,
            //      psi_a + 2 pi (the disk seen directly, then again through light that went round
            //      the back of the hole). Farthest first, so the nearer ones paint over it.
            const double th = std::atan2(Y, X);
            const double cth = std::cos(th), sth = std::sin(th);
            const double psi_a = std::atan2(ci, -sth * si);
            auto paint = [&](double bimp, double& l, double c3[3], double& sh) {
                const double b_M = bimp * LightPaths::BC;                // impact parameter, in M
                for (int n = 2; n >= 0; n--) {
                    const double psi = psi_a + n * M_PI;
                    double r_M, dr;
                    if (lp.lookup(bimp, psi, r_M, dr) != 0) continue;
                    const double rs = r_M / LightPaths::BC;             // distance from the hole, in shadow radii
                    if (rs > R_OUT * 1.05) continue;
                    // how bright the gas is there: the disk's flux, with the radius scaled so the
                    // flux starts at R_IN (the real inner edge, 6 M, is 1.16 shadow radii)
                    const double F = disk_flux(r_M * RADIUS_SCALE) / FLUX_PEAK;
                    if (F <= 0.0) continue;
                    // where on the disk the ray lands (the disk's own x to the right, y away from us)
                    const double sp = std::sin(psi), cp = std::cos(psi);
                    const double phi = std::atan2(sp * sth * ci - cp * si, sp * cth);
                    // the shift: light from gas on a circular orbit at r reaches us shifted by
                    //   g = sqrt(1 - 3M/r) / (1 + Omega b sin(i) cos(theta)),  Omega = r^(-3/2) (M = 1)
                    // (Doppler and gravity together; checked against the photon's direction at the gas)
                    const double den = std::max(0.15, 1.0 + std::pow(r_M, -1.5) * b_M * si * cth);
                    const double g = std::sqrt(std::max(0.05, 1.0 - 3.0 / r_M)) / den;
                    const double ge = std::pow(g, DOPPLER_SCALE);
                    const double a_rot = phi - OMEGA_IN * std::pow(R_IN / rs, 1.5) * t;
                    const double outer = smooth((R_OUT - rs) / OUTER_FADE);
                    const double I = std::pow(F, FLUX_SHARPEN) * std::pow(ge, SHIFT_POWER) * turbulence(rs, a_rot) * outer * DISK_GAIN;
                    const double gl = 1.0 - std::exp(-I);               // brightness, 0..1
                    if (gl < GAS_FLOOR) continue;
                    // the colour of a black body at the shifted temperature (T goes as flux^(1/4))
                    double pc[3];
                    bb.at(std::max(T_MIN, T_PEAK * std::pow(F, 0.25)) * ge, pc);
                    const double dim = 0.40 + 0.60 * clampd(gl * 1.6, 0.0, 1.0);   // faint gas is darker too
                    for (int c = 0; c < 3; c++) pc[c] *= dim;
                    const double cover = smooth(gl / 0.12);            // gas hides what is behind it
                    l = lerp(l, gl, cover);
                    for (int c = 0; c < 3; c++) c3[c] = lerp(c3[c], pc[c], cover);
                    sh = lerp(sh, 1.0, cover);                         // the gas is no star
                }
            };
            // the ring: brighter and hotter on the side coming towards us, like the gas
            {
                const double d = (b - RING_RADIUS) / RING_WIDTH;
                if (d > -4.0 && d < 4.0) {
                    const double ge = std::pow(1.0 / (1.0 + 0.4 * si * cth), DOPPLER_SCALE);
                    const double gl = clampd(RING_BRIGHT * std::exp(-d * d) * std::pow(ge, SHIFT_POWER) * 0.8, 0.0, 1.0);
                    if (gl > GAS_FLOOR) {
                        double pc[3];
                        bb.at(T_PEAK * 1.15 * ge, pc);
                        const double cover = smooth(gl / 0.12);
                        lum = lerp(lum, gl, cover);
                        for (int c = 0; c < 3; c++) col[c] = lerp(col[c], pc[c], cover);
                        shimmer = lerp(shimmer, 1.0, cover);
                    }
                }
            }
            if (b < 1.12) {
                // right at the shadow's edge the rings of light are thinner than a cell: average a few
                // radii across the cell, so they come out as a smooth thin ring and not a few dots
                double sl = 0, sc[3] = {0, 0, 0}, ss = 0;
                const double step = 1.0 / bc;                           // one cell, in shadow radii
                for (int k = 0; k < 5; k++) {
                    double l = lum, c3[3] = {col[0], col[1], col[2]}, sh = shimmer;
                    paint(b + (k - 2) * 0.25 * step, l, c3, sh);
                    sl += l; ss += sh;
                    for (int c = 0; c < 3; c++) sc[c] += c3[c];
                }
                lum = sl / 5; shimmer = ss / 5;
                for (int c = 0; c < 3; c++) col[c] = sc[c] / 5;
            } else {
                paint(b, lum, col, shimmer);
            }

#if 0       // retired: the first version, a flat disk plus a thin halo made up to look like bent light
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
                    shimmer = lerp(shimmer, 1.0, cover);                  // the gas is no star
                }
            }
#endif

            size_t p = (size_t)i * C + j;
            F.cell[p].lum = clampd(lum, 0.0, 1.0);
            F.cell[p].r = col[0]; F.cell[p].g = col[1]; F.cell[p].b = col[2];
            F.cell[p].shimmer = shimmer;
            if (F.cell[p].lum >= 0.5) {                            // a block: set the colour of its letter
                F.cell[p].alt_lum = 1.0;
                F.cell[p].alt_r = col[0] * LETTER_TONE; F.cell[p].alt_g = col[1] * LETTER_TONE; F.cell[p].alt_b = col[2] * LETTER_TONE;
            }
        }
    }

    char buf[64];
    std::snprintf(buf, sizeof buf, "tilt %.1f deg", tilt * 180.0 / M_PI);
    F.text(0, 0, buf);
    std::snprintf(buf, sizeof buf, "inner spin %.1f s/turn", 2 * M_PI / OMEGA_IN);
    F.text(1, 0, buf);
    std::snprintf(buf, sizeof buf, "hole r %.0f cols", bc);
    F.text(2, 0, buf);
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_singularity() { return std::make_unique<Singularity>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Singularity scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
