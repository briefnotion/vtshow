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
// The dark side gets a faint blue earthshine. The dark seas (maria) and the craters
// are the real ones, at their real latitude, longitude and size (see the tables below):
// the seas are ellipses with noisy shores, the craters are bowls with a rim that catch
// the light near the terminator, and the young ones are bright and throw rays (Tycho's
// reach across the disc). The stars are a hash of the cell, kept at the dimmest
// letters, each twinkling at a slow shimmer of its own. The moon is seen at its highest point
// from the northern hemisphere, and its whole face (and so its shadow) is turned in the sky by
// the angle the ecliptic leans from celestial north, as the real moon is; see SUN_LONGITUDE_DEG.
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

#include "../images.h"
#include "../scenes.h"
#include "../vtscreen/vtscreen.h"

// Line art drawn over the letters, with vtpen, when the terminal can show Sixel pictures (see ../images.h).
// Without vtpen's headers on the include path, or with a vtscreen that has no pictures made of pixels, the
// scene is exactly what it was: letters only.
#if defined(VTSCREEN_IMAGES) && __has_include("vtpen.h") && __has_include("vtpen_drawing.h")
#define MOON_PEN 1
#include "vtpen.h"
#include "vtpen_drawing.h"
#endif

using vtscreen::Picture;

namespace {

static const double DURATION = 24.0;
#ifdef MOON_PEN
// How many times a second the line art over the letters is drawn again (it costs bytes every time: see overlay()).
// The environment variable VTSHOW_PEN_FPS tries another value without compiling.
static const double PEN_FPS_DEFAULT = 15.0;
static double pen_fps() {
    static const double v = [] {
        const char* e = std::getenv("VTSHOW_PEN_FPS");
        const double x = e ? std::atof(e) : 0.0;
        return x > 0.0 ? std::min(x, 240.0) : PEN_FPS_DEFAULT;
    }();
    return v;
}
// How soft the lines' edges are (vtpen's set_edge_levels): 1 is hard edges (smallest), 4 is the smoothest. Every step costs
// bytes: 2 is about a third smaller than 4 and still looks smooth. VTSHOW_PEN_AA=N tries another value.
static int pen_edge_levels() {
    static const int v = [] { const char* e = std::getenv("VTSHOW_PEN_AA"); const int x = e ? std::atoi(e) : 0; return x > 0 ? std::min(x, 8) : 2; }();
    return v;
}
// How often a letter under the line art may change (vtscreen's ImagePlacement::letter_rate). A letter written into a cell
// takes that cell's pixels with it, so a shimmering letter under a line means the line's piece is sent again each time.
// -1 lets them change as often as the scene says (the best looks, the most bytes). A rate holds the cells back so each one
// changes only that many times a second: it saves bytes, but the cells under a line show the moon a little late, and a very
// low rate shows stale patches. VTSHOW_PEN_LETTERS=N tries a rate (a number of 0 or more), -1 for free.
static double pen_letter_rate() {
    static const double v = [] { const char* e = std::getenv("VTSHOW_PEN_LETTERS"); return e ? std::atof(e) : -1.0; }();
    return v;
}
#endif
// The sky. The moon is seen from the northern hemisphere at its highest point (due south of
// the zenith), so "up" on screen is celestial north. The moon's own north pole points
// (nearly) at the north pole of the ecliptic, which leans away from celestial north by up to
// 23.4 degrees, to one side or the other, depending on where the moon is on the ecliptic. So the
// whole face is turned by that angle. The sun always lights the face along its equator (within
// 1.5 degrees), so the shadow is turned with it: that is the tilt of the shadow.
// Which way and how far depends on the season. SUN_LONGITUDE_DEG is the sun's ecliptic
// longitude: 0 spring equinox, 90 summer solstice, 180 autumn equinox, 270 winter solstice.
// SKY_TURN scales the turning: 1 is the real thing, 0 is no tilt at all.
static const double OBLIQUITY_DEG = 23.44;
static const double SUN_LONGITUDE_DEG = 180.0;
static const double SKY_TURN = 1.0;

// The stars stay at stage 1 of the 16 brightness steps (the letters . ` '), so they do
// not clutter the sky. Their twinkle is the letter changing, at a slow shimmer speed of
// their own (1 is the normal rate; see PicCell::shimmer).
static const double STAR_LUM = 0.10;       // brightness of a star, 1/16 to 2/16 is stage 1
static const double STAR_SHIMMER_MIN = 0.05, STAR_SHIMMER_MAX = 0.35;

// How bright the lit ground is: brightness = SURFACE_BASE + SURFACE_GAIN * albedo. The
// highlands (albedo about 0.8) come out near 0.68, so they land on the middle of the
// brightness steps and show letters, instead of all being solid blocks at the very top.
static const double SURFACE_BASE = 0.14, SURFACE_GAIN = 0.70;
// How much darker a dark sea is than the highlands, in albedo (the real maria are about
// half as bright as the highlands).
static const double SEA_DEPTH = 0.40;
// Fine texture of the ground, in albedo: its size in disc units^-1, and its strength.
static const double GRAIN_FREQ = 20.0, GRAIN_STRENGTH = 0.22;
// A bright cell (0.5 and up) is drawn as a solid block with the letter on it. The letter
// is the cell's alt colour: the block's own colour times this, so the letters read as a soft
// darker texture on the block instead of black holes. 1 would hide the letters.
static const double LETTER_TONE = 0.66;

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
//
// Every place is given as in an atlas: selenographic latitude and longitude (degrees,
// north and east positive) and a size in km. The moon is seen from the north side, as
// from the northern hemisphere: north up, Mare Crisium on the right. The positions and
// sizes are from the USGS gazetteer (as listed on Wikipedia); the shapes of the
// irregular seas are approximated by overlapping ellipses.
//
// A point of the disc is a point p on the unit sphere in the disc frame: x to the
// right, y downward, z toward us. A "spot" is a feature's centre with its own east and
// north directions, so any size and shape is measured on the sphere, in moon radii,
// and is squashed toward the rim as on the real moon.

static const double MOON_KM = 1737.4;

struct Spot {
    double c[3], e[3], n[3];                 // centre, east and north tangents
};
static Spot make_spot(double lat_deg, double lon_deg) {
    const double la = lat_deg * M_PI / 180.0, lo = lon_deg * M_PI / 180.0;
    Spot s;
    s.c[0] = std::cos(la) * std::sin(lo); s.c[1] = -std::sin(la); s.c[2] = std::cos(la) * std::cos(lo);
    s.e[0] = std::cos(lo);                s.e[1] = 0.0;           s.e[2] = -std::sin(lo);
    s.n[0] = -std::sin(la) * std::sin(lo); s.n[1] = -std::cos(la); s.n[2] = -std::sin(la) * std::cos(lo);
    return s;
}
// where p is relative to a spot: east and north in moon radii; false if p is on the far side of it
static inline bool local(const Spot& s, double x, double y, double z, double& east, double& north) {
    if (x * s.c[0] + y * s.c[1] + z * s.c[2] <= 0.0) return false;
    east = x * s.e[0] + y * s.e[1] + z * s.e[2];
    north = x * s.n[0] + y * s.n[1] + z * s.n[2];
    return true;
}

// A dark sea, or part of one: an ellipse with half-widths a (east-west) and b (north-south)
// in km, turned by ang degrees. edge: how sharp the shore is (the bigger, the sharper).
// dark: how dark the sea is, 0..1.
struct SeaDef { double lat, lon, a_km, b_km, ang, edge, dark; };
static const SeaDef SEA_DEFS[] = {
    // lat      lon     a_km  b_km  ang  edge dark
    { 34.7,  -14.9,  572,  572,   0, 5.0, 0.90},   // Mare Imbrium
    { 27.3,   18.4,  337,  337,   0, 5.0, 1.00},   // Mare Serenitatis
    { 10.0,   28.0,  300,  260,   0, 4.5, 1.00},   // Mare Tranquillitatis (three parts)
    {  5.0,   38.0,  190,  170,   0, 4.5, 1.00},
    {  3.0,   22.0,  130,  110,   0, 4.5, 1.00},
    { 16.2,   59.1,  300,  235,   0, 6.0, 1.00},   // Mare Crisium
    { -8.0,   53.5,  230,  330,   0, 4.0, 0.90},   // Mare Fecunditatis (two parts)
    {-14.0,   44.0,  130,  120,   0, 4.0, 0.90},
    {-15.2,   34.6,  170,  170,   0, 5.0, 1.00},   // Mare Nectaris
    {-20.6,  -17.3,  370,  340,   0, 3.5, 0.85},   // Mare Nubium
    {-24.5,  -38.6,  210,  200,   0, 5.0, 1.00},   // Mare Humorum
    { 13.2,    4.1,  120,  120,   0, 4.0, 0.80},   // Mare Vaporum
    { 59.0,    0.0,  260,   85,   0, 3.0, 0.70},   // Mare Frigoris (three parts)
    { 57.0,   25.0,  170,   75,   0, 3.0, 0.70},
    { 57.5,  -25.0,  170,   75,   0, 3.0, 0.70},
    { 20.0,  -57.0,  800,  850,   0, 1.8, 0.80},   // Oceanus Procellarum
    { 50.0,  -50.0,  260,  200,   0, 2.2, 0.75},   // Sinus Roris, its northern lobe
    {-10.5,  -22.3,  175,  175,   0, 3.0, 0.85},   // Mare Cognitum
    {  7.8,  -30.6,  255,  255,   0, 3.0, 0.85},   // Mare Insularum
    {  1.6,    1.0,  140,  140,   0, 3.0, 0.60},   // Sinus Medii
    { 12.1,   -8.3,  160,  160,   0, 3.0, 0.70},   // Sinus Aestuum
    { 37.6,   30.8,  210,  190,   0, 3.5, 0.75},   // Lacus Somniorum
    { -1.7,   87.0,  190,  190,   0, 5.0, 1.00},   // Mare Smythii (on the east limb)
    { 12.7,   86.5,  180,  180,   0, 4.5, 0.90},   // Mare Marginis
    { 56.9,   81.5,  115,  115,   0, 5.0, 1.00},   // Mare Humboldtianum
};
static const int NSEAS = sizeof(SEA_DEFS) / sizeof(SEA_DEFS[0]);

// A crater at its true size. ray_km: how far its bright rays reach (0: none). bright:
// how bright and fresh it is, 0..1. floor: how much darker its floor is than its surroundings.
struct CraterDef { double lat, lon, d_km, ray_km, bright, floor; };
static const CraterDef CRATER_DEFS[] = {
    // lat     lon     d_km   ray_km bright floor
    {-43.3,  -11.2,   85.3,  1400,  1.00, 0.20},   // Tycho
    {  9.6,  -20.1,   96.1,   450,  0.70, 0.25},   // Copernicus
    {  8.1,  -38.0,   29.5,   250,  0.80, 0.20},   // Kepler
    { 23.7,  -47.5,   40.0,   120,  1.00, 0.10},   // Aristarchus
    { 51.6,   -9.4,  100.7,     0,  0.00, 0.55},   // Plato, a dark floor
    {-58.6,  -14.7,  230.8,     0,  0.00, 0.20},   // Clavius
    { -9.2,   -1.8,  153.7,     0,  0.00, 0.15},   // Ptolemaeus
    { -8.9,   61.0,  132.0,     0,  0.00, 0.20},   // Langrenus
    {-25.4,   60.8,  184.1,     0,  0.00, 0.20},   // Petavius
    { -5.4,  -68.4,  173.5,     0,  0.00, 0.50},   // Grimaldi, a dark floor
    { 16.1,   46.9,   26.9,   180,  0.90, 0.20},   // Proclus
    { 29.7,   -4.0,   81.0,     0,  0.00, 0.30},   // Archimedes
    { 14.5,  -11.3,   58.8,     0,  0.30, 0.20},   // Eratosthenes
};
static const int NCRATERS = sizeof(CRATER_DEFS) / sizeof(CRATER_DEFS[0]);

// the features as spots, in moon radii, worked out once
struct Sea { Spot s; double a, b, ca, sa, edge, dark; };
struct Crater { Spot s; double r, ray, bright, floor; };
static Sea SEAS[NSEAS];
static Crater CRATERS[NCRATERS];
static void build_face() {
    for (int k = 0; k < NSEAS; k++) {
        const SeaDef& d = SEA_DEFS[k];
        Sea& q = SEAS[k];
        q.s = make_spot(d.lat, d.lon);
        q.a = d.a_km / MOON_KM; q.b = d.b_km / MOON_KM;
        q.ca = std::cos(d.ang * M_PI / 180.0); q.sa = std::sin(d.ang * M_PI / 180.0);
        q.edge = d.edge; q.dark = d.dark;
    }
    for (int k = 0; k < NCRATERS; k++) {
        const CraterDef& d = CRATER_DEFS[k];
        Crater& q = CRATERS[k];
        q.s = make_spot(d.lat, d.lon);
        q.r = d.d_km / 2.0 / MOON_KM; q.ray = d.ray_km / MOON_KM;
        q.bright = d.bright; q.floor = d.floor;
    }
}

// how dark-sea the point is, 0 (highland) .. 1 (full, dark sea). wob: a slow wobble that
// roughens the shores. p is a point on the sphere in the disc frame.
static double sea_amount(double x, double y, double z, double wob) {
    double best = 0;
    for (int k = 0; k < NSEAS; k++) {
        const Sea& q = SEAS[k];
        double east, north;
        if (!local(q.s, x, y, z, east, north)) continue;
        const double ex = (east * q.ca + north * q.sa) / q.a, ny = (north * q.ca - east * q.sa) / q.b;
        const double inside = 1.0 - (ex * ex + ny * ny);        // 1 at the centre, 0 on the shore
        if (inside < -0.6) continue;
        best = std::max(best, q.dark * smooth((inside + wob) * q.edge));
    }
    return best;
}

// height of the ground: bowls with a raised rim. Only the craters big enough to show as a
// shape at this size; the small ones are only bright spots.
static double height(double us, double vs, double cT, double sT) {
    const double z = std::sqrt(std::max(0.0, 1.0 - us * us - vs * vs));
    const double u = us * cT - vs * sT, v = us * sT + vs * cT;       // the same point on the turned face
    double h = 0;
    for (int k = 0; k < NCRATERS; k++) {
        const Crater& c = CRATERS[k];
        if (c.r < 0.02) continue;
        double east, north;
        if (!local(c.s, u, v, z, east, north)) continue;
        double d = std::sqrt(east * east + north * north) / c.r;
        if (d > 2.0) continue;
        double bowl = d < 1.0 ? -0.6 * (1.0 - d * d) : 0.0;
        double rim = 0.5 * std::exp(-((d - 1.0) / 0.18) * ((d - 1.0) / 0.18));
        h += (bowl + rim) * c.r * 6.0;
    }
    return h;
}

// How far the moon's face is turned on screen, in radians, counter-clockwise, when the moon is
// elong radians east of the sun along the ecliptic (0 new, pi full). It is the position angle,
// from celestial north toward east (which is to the left in the sky), of the ecliptic's north
// pole as seen from the moon, which is on the ecliptic.
static double face_turn(double elong) {
    const double eps = OBLIQUITY_DEG * M_PI / 180.0;
    const double lam = SUN_LONGITUDE_DEG * M_PI / 180.0 + elong;     // the moon's ecliptic longitude
    // the moon as a unit vector in equatorial coordinates (ecliptic latitude 0)
    const double mx = std::cos(lam), my = std::sin(lam) * std::cos(eps), mz = std::sin(lam) * std::sin(eps);
    // celestial north and east at the moon
    const double nl = std::sqrt(std::max(1e-12, 1.0 - mz * mz));
    const double nx = -mz * mx / nl, ny = -mz * my / nl, nz = (1.0 - mz * mz) / nl;
    const double el = std::sqrt(std::max(1e-12, mx * mx + my * my));
    const double ex = -my / el, ey = mx / el;
    // the ecliptic's north pole
    const double px = 0.0, py = -std::sin(eps), pz = std::cos(eps);
    return std::atan2(px * ex + py * ey, px * nx + py * ny + pz * nz);
}

#if 0   // retired: the first invented face (8 soft ellipses and 11 craters, in disc coordinates)

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
#endif

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
        build_face();
#ifdef MOON_PEN
        canvas_.reset();
        raster_.reset();
        pen_step_ = -1;
#endif
    }
    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double Rm = 1;

#ifdef MOON_PEN
    // The line art: the limb, a graticule on the face, the terminator, the moon's axis, and the sun going round its
    // orbit. It is drawn into a canvas the size of the block of cells round the moon, and handed to the Picture as pixels.
    void overlay(double t, double phi, double theta, double mcx, double mcy, Picture& F);
    long pen_step_ = -1;                       // which redraw of the line art the raster holds
    std::unique_ptr<vtpen::Canvas> canvas_;
    std::shared_ptr<vtscreen::Raster> raster_;
    int pen_cw_ = 0, pen_ch_ = 0, pen_row0_ = 0, pen_col0_ = 0, pen_rows_ = 0, pen_cols_ = 0;
#endif
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

#ifdef MOON_PEN
void Moonphases::overlay(double t, double phi, double theta, double mcx, double mcy, Picture& F) {
    const int cw = vtscreen::g_image_cell_w, ch = vtscreen::g_image_cell_h;
    if (cw <= 0 || ch <= 0) return;                          // the terminal cannot show pictures: letters only

    // The block of cells round the moon. The moon only drifts a little, so the block stays where it is.
    const double reach = 1.32 * Rm;                          // in column widths, from the middle
    const int col0 = std::max(0, (int)std::floor(C * 0.5 - reach - C * 0.035) - 1);
    const int col1 = std::min(C - 1, (int)std::ceil(C * 0.5 + reach + C * 0.035) + 1);
    const int row0 = std::max(0, (int)std::floor(R * 0.5 - reach / 2 - R * 0.025) - 1);
    const int row1 = std::min(R - 1, (int)std::ceil(R * 0.5 + reach / 2 + R * 0.025) + 1);
    const int cols = col1 - col0 + 1, rows = row1 - row0 + 1;
    if (cols < 1 || rows < 1) return;
    if (!canvas_ || pen_cw_ != cw || pen_ch_ != ch || pen_cols_ != cols || pen_rows_ != rows || pen_col0_ != col0 || pen_row0_ != row0) {
        canvas_ = std::make_unique<vtpen::Canvas>(cols * cw, rows * ch);      // transparent: pixels not drawn on stay empty
        raster_ = std::make_shared<vtscreen::Raster>();
        raster_->init(cols * cw, rows * ch);
        pen_cw_ = cw; pen_ch_ = ch; pen_cols_ = cols; pen_rows_ = rows; pen_col0_ = col0; pen_row0_ = row0;
    }
    // Every line moves a little every frame (the moon drifts, the sun goes round), so redrawing them every frame sends every
    // cell they cross every frame. They are drawn PEN_FPS times a second instead, and the frames in between keep the picture.
    const long step = (long)std::floor(t * pen_fps());
    if (step == pen_step_) { F.place_image(raster_, row0, col0, pen_letter_rate()); return; }
    pen_step_ = step;

    // The lines are described to a Drawing (shapes in pixel units of the block, here) and replayed onto the canvas, which holds
    // the pixels. The same Drawing could as well be shown some other way.
    vtpen::Canvas& cv = *canvas_;
    cv.set_edge_levels(pen_edge_levels());
    cv.clear();
    vtpen::Drawing c(cols * cw, rows * ch);

    // The moon's disc in pixels. The letters treat a row as two column widths, so the disc is as wide as Rm columns and
    // Rm / 2 rows tall, and the pixels follow that exactly.
    const double cx = (mcx - col0) * cw, cy = (mcy - row0) * ch;
    const double rx = Rm * cw, ry = Rm * ch * 0.5;
    auto P = [&](double u, double v) { return vtpen::Point{cx + u * rx, cy + v * ry}; };
    const double cT = std::cos(theta), sT = std::sin(theta);
    const double sn = std::sin(phi), cs = std::cos(phi);
    const double Lx = sn * cT, Ly = -sn * sT, Lz = -cs;     // the light, as in frame()

    // The graticule: lines of latitude and longitude on the face, turned with it. Only the side facing us.
    auto face = [&](double lat_deg, double lon_deg, double& u, double& v, double& pz) {
        const double la = lat_deg * M_PI / 180.0, lo = lon_deg * M_PI / 180.0;
        const double uf = std::cos(la) * std::sin(lo), vf = -std::sin(la);
        pz = std::cos(la) * std::cos(lo);
        u = uf * cT + vf * sT;
        v = -uf * sT + vf * cT;
    };
    auto trace = [&](bool meridian, double fixed) {
        std::vector<vtpen::Point> run;
        for (int k = -90; k <= 90; k += 3) {
            double u, v, pz;
            if (meridian) face(k, fixed, u, v, pz); else face(fixed, k, u, v, pz);
            if (pz > 0.01) run.push_back(P(u, v));
            else { if (run.size() > 1) c.polyline(run); run.clear(); }
        }
        if (run.size() > 1) c.polyline(run);
    };
    c.set_colour({105, 135, 200});
    c.set_thickness(1.0);
    for (int g = -60; g <= 60; g += 30) { trace(true, g); trace(false, g); }

    // The limb.
    c.set_colour({215, 225, 255});
    c.set_thickness(1.0);
    c.ellipse(cx, cy, rx, ry);

    // The terminator: the great circle at right angles to the light, the half we can see.
    double ax = Ly, ay = -Lx, az = 0.0;                      // a = L x (0, 0, 1)
    const double an = std::hypot(ax, ay);
    if (an < 1e-3) { ax = 1.0; ay = 0.0; } else { ax /= an; ay /= an; }
    const double bx = Ly * az - Lz * ay, by = Lz * ax - Lx * az, bz = Lx * ay - Ly * ax;   // b = L x a
    {
        c.set_colour({255, 238, 170});
        c.set_thickness(2.0);
        std::vector<vtpen::Point> run;
        for (int k = 0; k <= 360; k += 3) {
            const double a = k * M_PI / 180.0;
            const double px = std::cos(a) * ax + std::sin(a) * bx, py = std::cos(a) * ay + std::sin(a) * by;
            const double pz = std::cos(a) * az + std::sin(a) * bz;
            if (pz >= -0.002) run.push_back(P(px, py));
            else { if (run.size() > 1) c.polyline(run); run.clear(); }
        }
        if (run.size() > 1) c.polyline(run);
    }

    // The axis: short ticks beyond the limb, the north one longer.
    c.set_colour({150, 175, 235});
    c.set_thickness(1.0);
    c.line(P(-sT * 1.03, -cT * 1.03), P(-sT * 1.20, -cT * 1.20));
    c.line(P(sT * 1.03, cT * 1.03), P(sT * 1.10, cT * 1.10));

    // The sun's orbit, seen from a little above (dashes), and the sun on it, with its rays.
    const double tilt = 25.0 * M_PI / 180.0, ring = 1.28;
    auto orbit = [&](double ph, double& u, double& v) {
        const double ox = std::sin(ph) * cT, oy = -std::sin(ph) * sT, oz = -std::cos(ph);
        u = ring * ox;
        v = ring * (oy * std::cos(tilt) + oz * std::sin(tilt));
    };
    c.set_colour({90, 110, 170});
    c.set_thickness(1.0);
    for (int k = 0; k < 360; k += 10) {
        double u0, v0, u1, v1, um, vm;
        orbit(k * M_PI / 180.0, u0, v0);
        orbit((k + 3) * M_PI / 180.0, um, vm);
        orbit((k + 6) * M_PI / 180.0, u1, v1);
        c.polyline({P(u0, v0), P(um, vm), P(u1, v1)});
    }
    double su, sv;
    orbit(phi, su, sv);
    const vtpen::Point sun = P(su, sv);
    const double sr = std::max(4.0, 0.045 * Rm * cw);
    c.set_colour({255, 215, 90});
    c.set_thickness(1.5);
    c.circle(sun.x, sun.y, sr);
    for (int k = 0; k < 8; k++) {
        const double a = k * M_PI / 4.0 + phi;               // the rays turn slowly
        c.line({sun.x + std::cos(a) * sr * 1.5, sun.y + std::sin(a) * sr * 1.5}, {sun.x + std::cos(a) * sr * 2.5, sun.y + std::sin(a) * sr * 2.5});
    }
    c.set_colour({150, 125, 60});
    c.set_thickness(1.0);                                    // a thin line of light toward the moon
    c.line(sun, {cx + su * rx * 0.2, cy + sv * ry * 0.2});

    // Over to vtscreen: a pixel the canvas did not draw on is empty.
    c.draw(cv);
    const vtpen::Rgb bg = cv.background();
    for (int y = 0; y < raster_->h; y++) {
        const vtpen::Rgb* src = cv.row(y);
        vtscreen::Pixel* dst = &raster_->at(0, y);
        for (int x = 0; x < raster_->w; x++)
            dst[x] = src[x] == bg ? vtscreen::Pixel() : vtscreen::Pixel{src[x].r, src[x].g, src[x].b, 255};
    }
    F.place_image(raster_, row0, col0, pen_letter_rate());
}
#endif

void Moonphases::frame(double t, Picture& F) {
    F.init(R, C);

    // where in the cycle: 0 = new, 0.5 = full. t = 0 is a day or so after new moon,
    // so a play of 24 s ends on the new moon it began after.
    const double p = std::fmod(std::fmod(t + 1.0, DURATION) + DURATION, DURATION) / DURATION;
    const double phi = 2.0 * M_PI * p;
    const double ill = 0.5 - 0.5 * std::cos(phi);   // lit fraction
    // sun direction: behind the moon at new, in front at full, to the right while waxing
    const double sn = std::sin(phi), cs = std::cos(phi);
    // the face is turned by theta in the sky, and the light with it (it lies along the equator)
    const double theta = SKY_TURN * face_turn(phi);
    const double cT = std::cos(theta), sT = std::sin(theta);
    const double Lx = sn * cT, Ly = -sn * sT, Lz = -cs;

    // the moon drifts a little along an arc
    const double mcx = C * (0.5 + 0.035 * std::sin(2 * M_PI * t / DURATION - 1.2));
    const double mcy = R * (0.5 - 0.025 * std::cos(2 * M_PI * t / DURATION - 1.2));

    for (int i = 0; i < R; i++) {
        const double Y = (i + 0.5 - mcy) * 2.0;
        for (int j = 0; j < C; j++) {
            const double X = j + 0.5 - mcx;
            const double dist = std::sqrt(X * X + Y * Y);       // in column widths
            double lum = 0, col[3] = {0, 0, 0};
            double shimmer = 1.0;

            // ---- the sky: a faint glow round the moon, and stars
            const double halo = dist > Rm ? ill * 0.08 * std::exp(-(dist - Rm) / Rm * 2.6) : 0.0;
            if (halo > 0) {
                lum = halo;
                col[0] = 80; col[1] = 105; col[2] = 170;
            }
            if (rnd(j, i, 7) < 0.035) {
                double dim = 1.0 - clampd(halo * 6.0, 0.0, 0.95);        // the glow drowns stars
                double s = STAR_LUM * dim;
                if (s > lum) {
                    double w = rnd(j, i, 11);                               // blue-white .. yellow-white
                    lum = s;
                    shimmer = lerp(STAR_SHIMMER_MIN, STAR_SHIMMER_MAX, rnd(j, i, 9));
                    col[0] = lerp(190, 255, w); col[1] = lerp(205, 240, w); col[2] = lerp(255, 200, w);
                }
            }

            // ---- the moon
            double cov = clampd((Rm - dist) + 0.5, 0.0, 1.0);          // soft edge, about a cell wide
            if (cov > 0) {
                double r2 = std::min((X * X + Y * Y) / (Rm * Rm), 0.9999);
                double u = X / Rm, v = Y / Rm, pz = std::sqrt(1.0 - r2);
                const double uf = u * cT - v * sT, vf = u * sT + v * cT;   // the same point on the turned face
                double d = u * Lx + v * Ly + pz * Lz;                   // sun against the surface

                // the ground: highlands, dark seas, craters, rays
                double wob = (fbm(uf * 7.0 + 3.0, vf * 7.0) - 0.5) * 0.6;
                double sea = sea_amount(uf, vf, pz, wob);
                double alb = 0.80 - SEA_DEPTH * sea + (fbm(uf * 5.0 + 20.0, vf * 5.0) - 0.5) * 0.22;
                alb += (fbm(uf * GRAIN_FREQ + 60.0, vf * GRAIN_FREQ + 11.0) - 0.5) * GRAIN_STRENGTH * (1.0 - 0.6 * sea);
                for (int k = 0; k < NCRATERS; k++) {
                    const Crater& c = CRATERS[k];
                    double east, north;
                    if (!local(c.s, uf, vf, pz, east, north)) continue;
                    const double rd = std::sqrt(east * east + north * north);    // moon radii
                    const double q = rd / c.r;
                    if (q < 1.0) alb -= c.floor * (1.0 - q * q);                  // a darker floor
                    else if (q < 1.5) alb += 0.20 * (1.5 - q);                    // a brighter rim
                    if (c.bright > 0 && q < 3.0) alb += 0.45 * c.bright * std::exp(-q * q / 3.0);   // fresh ejecta
                    if (c.ray > 0 && q > 0.8 && rd < c.ray) {
                        double ang = std::atan2(north, east);
                        double ray = clampd((fbm(std::cos(ang) * 3.5 + 8.0 + 13.0 * k, std::sin(ang) * 3.5 + 7.0 * k) - 0.42) * 3.0, 0, 1);
                        alb += 0.45 * c.bright * ray * std::pow(1.0 - rd / c.ray, 1.3);
                    }
                }
                alb = clampd(alb, 0.2, 1.15);

                // sunlight: nothing past the terminator, a flat brightness elsewhere
                double sh = 0;
                if (d > 0) sh = smooth(d / 0.07) * std::min(1.0, 2.0 * d / (d + pz + 0.02));
                // relief: crater rims catch low light, mostly near the terminator
                const double e = 0.012;
                double gx = (height(u + e, v, cT, sT) - height(u - e, v, cT, sT)) / (2 * e);
                double gy = (height(u, v + e, cT, sT) - height(u, v - e, cT, sT)) / (2 * e);
                double relief = clampd(0.06 * (gx * Lx + gy * Ly), -0.35, 0.35);
                double sun = clampd(sh * (SURFACE_BASE + SURFACE_GAIN * alb) * (1.0 + relief) * 1.02, 0.0, 1.0);

                // earthshine: the dark side is faintly lit by the Earth, most when the moon is thin
                double es = (0.012 + 0.060 * std::pow(1.0 - ill, 0.8)) * (0.5 + 0.8 * alb) * (1.0 - sh) * (0.6 + 0.4 * pz);

                double k = std::pow(clampd(sun / 0.72, 0.0, 1.0), 0.85);
                double ke = clampd(es / 0.075, 0.0, 1.0) * (1.0 - k);
                double ac = clampd(alb, 0.0, 1.0);
                double sunCol[3] = {lerp(206, 246, ac), lerp(210, 240, ac), lerp(218, 214, ac)};
                double mlum = sun + es;
                double mcol[3];
                const double earth[3] = {64, 88, 142};
                for (int c = 0; c < 3; c++) mcol[c] = sunCol[c] * k + earth[c] * ke;

                lum = lerp(lum, mlum, cov);
                for (int c = 0; c < 3; c++) col[c] = lerp(col[c], mcol[c], cov);
                shimmer = lerp(shimmer, 1.0, cov);                          // the moon's edge is no star
            }
            auto& pc = F.at(i, j);
            pc.lum = clampd(lum, 0.0, 1.0); pc.r = col[0]; pc.g = col[1]; pc.b = col[2];
            pc.shimmer = shimmer;
            if (pc.lum >= 0.5) {                                   // a block: set the colour of its letter
                pc.alt_lum = 1.0;
                pc.alt_r = col[0] * LETTER_TONE; pc.alt_g = col[1] * LETTER_TONE; pc.alt_b = col[2] * LETTER_TONE;
            }
        }
    }

    char buf[64];
    F.text(0, 0, phase_name(p));
    std::snprintf(buf, sizeof buf, "%3.0f%% lit", ill * 100.0);
    F.text(1, 0, buf);
    std::snprintf(buf, sizeof buf, "day %4.1f of 29.5", p * 29.5);
    F.text(2, 0, buf);
#ifdef MOON_PEN
    overlay(t, phi, theta, mcx, mcy, F);
#endif
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_moonphases() { return std::make_unique<Moonphases>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    vtshow_enable_images();
    Moonphases scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
