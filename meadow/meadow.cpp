// A green meadow in the wind: rolling hills, a lone tree on a ridge, flowers in the grass, and
// clouds drifting across a blue sky. The wind is the point: gusts run across the grass in waves,
// and where a gust passes the blades shimmer faster and catch the light; the same wind moves the
// tree's leaves, and the cloud shadows slide over the hills.
// An original scene (drawn by the vtscreen player in ../vtscreen/vtscreen.h), in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's; this scene
// is not the channel's, it was designed from scratch for this project.
//
// How it is made. Every cell has a colour and a brightness, and a second colour (the alt colour,
// see PicCell in ../vtscreen/vtscreen.h):
//  * the sky and the distant haze are bright cells, drawn as solid blocks, with the letters a soft
//    tone of the block's own colour. The sky is a gradient with a glow where the sun is, and the
//    clouds are noise, lit on the side facing the sun and grey underneath;
//  * the grass is dim cells: the letter is the blade (light, yellow-green, silvery in a gust) and
//    the alt colour is the dark ground between the blades, deeper in shade. Cloud shadows, the
//    tree's shadow and the shaded side of the hills darken the ground and its alt colour;
//  * the wind is made of gusts, each an event in the plane of the ground, seen in perspective: it is
//    born upwind, runs across the field with a sharp leading edge that bows forward and a long soft
//    tail, streaky along the wind, and dies away, with nearly calm grass between. A gust raises
//    the cell's shimmer (the letters change faster), brightens the blades and turns them silver;
//  * the hills are layers, far to near, each fading toward the colour of the sky with distance.
//
//   g++ -O2 -std=c++17 -o meadow meadow.cpp
//   ./meadow              play once (24 s); q or Ctrl-C quits
//   ./meadow --loop       keep going
//   ./meadow --still 6    print the frame at t=6 s as plain text and exit
//   ./meadow --no-color   letters only
//   ./meadow --fps N      frames per second (default 30)
//   ./meadow --cols N --rows N

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

// Where things are, as fractions of the height from the top (Y) or of the width (X).
static const double HORIZON = 0.36;           // where the ground's perspective converges
static const double TREE_X = 0.70;            // the tree, along the ridge
static const double TREE_SIZE = 0.088;        // radius of the crown, as a fraction of the width

// The wind. Gust waves run across the ground: WIND_SPEED is how fast the crests travel, in waves
// per second at the bottom of the picture; GUST_STRENGTH how much a gust changes the grass.
static const double WIND_SPEED = 0.40;
static const double GUST_STRENGTH = 1.0;
static const double SHIMMER_CALM = 0.02, SHIMMER_GUST = 1.8;   // letter speed of still grass and of a gust (1 = normal)
static const double SHIMMER_SKY = 0.03;                        // the sky and the haze: the clouds drift, the letters hardly change
static const double GUST_FLOOR = 0.10, GUST_FULL = 0.60;        // below this a gust is not felt; at this it flickers fully

// The sky and clouds. CLOUD_SPEED: how fast they drift, in noise units per second.
static const double CLOUD_SPEED = 0.020;
static const double CLOUD_COVER = 0.34;       // bigger = fewer clouds
// Brightness of the ground cells (dim cells: they stay below 0.5 so they are letters, not blocks).
static const double GRASS_LUM = 0.27, GRASS_GUST_LUM = 0.16;
// The letter on a bright block (sky, haze) is the block's colour times this: 1 would hide it.
static const double SKY_LETTER = 0.965;
static const double SUN_GLOW = 0.28;          // how far the sky goes toward warm white round the sun (the glow washed the blue out)
// Contrast shows shape. Where the letter colour and the ground behind it are close the picture is
// soft; where they differ it is crisp. So the lit ground has a mid-green behind bright green
// letters, and shade is bright-ish green letters on near black: CONTRAST is how far the ground
// behind the letters goes toward black in shade (0 is the old, murkier look). RELIEF is how
// strongly low hummocks and hollows in the field, lit from the left, show as bands of contrast.
static const double CONTRAST = 1.0;           // at the edges of hills (and the rim of the tree): crisp
static const double CONTRAST_BASE = 0.30;     // everywhere else: soft, so the edges stand out as lines
static const double RELIEF = 5.0;
// Sunlight: where the sun shines well the green goes yellow (0 none, 1 fully yellow at the brightest).
static const double SUN_YELLOW = 0.55;
// The tree sways: how far the top of the crown moves, in column widths, at most (more in a gust).
static const double TREE_SWAY = 1.6;

// ---------------------------------------------------------------- small helpers

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline double smooth(double v) {
    v = clampd(v, 0.0, 1.0);
    return v * v * (3 - 2 * v);
}
static inline double sstep(double a, double b, double v) { return smooth((v - a) / (b - a)); }
static inline double flicker(double g) { return sstep(GUST_FLOOR, GUST_FULL, g); }   // 0 in calm grass, 1 in a gust
static inline double lerp(double a, double b, double f) { return a + (b - a) * f; }
static inline void lerp3(const double a[3], const double b[3], double f, double out[3]) {
    for (int c = 0; c < 3; c++) out[c] = a[c] + (b[c] - a[c]) * f;
}

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

// ---------------------------------------------------------------- the scene

class Meadow : public vtscreen::Scene {
public:
    vtscreen::Info info() const override {
        return {"meadow (C++)", DURATION, {130, 200, 100}, true};
    }

    void setup(int cols, int rows) override {
        C = cols;
        R = rows;
        W = cols;                      // the picture in column widths; a row is two of them tall
        H = 2.0 * rows;
        far_r.resize(C); mid_r.resize(C); near_r.resize(C);
        for (int j = 0; j < C; j++) {
            const double x = (j + 0.5) / C;
            // the far ridge: low, hazy hills
            far_r[j] = 0.395 + 0.03 * (fbm(x * 3.0, 1.7) - 0.5) * 2.0 - 0.012 * std::sin(x * 7.0 + 1.0);
            // the middle hills: a big slope on the left running down to the right
            mid_r[j] = 0.315 + 0.235 * sstep(0.0, 0.80, x) + 0.045 * (fbm(x * 2.4, 4.2) - 0.5) * 2.0;
            // the near ridge: the meadow rising to the right, then easing off
            near_r[j] = 0.72 - 0.205 * sstep(0.0, 0.72, x) + 0.075 * sstep(0.80, 1.0, x)
                      + 0.022 * (fbm(x * 3.3, 8.8) - 0.5) * 2.0;
        }
        tree_j = std::min(C - 1, std::max(0, (int)(TREE_X * C)));
        tree_cx = (tree_j + 0.5);
        tree_base = near_r[tree_j] * H + 0.012 * H;           // where the trunk meets the ridge
        tree_rc = TREE_SIZE * W;                              // crown radius, in column widths
        tree_trunk = 0.115 * H;                               // trunk height
    }

    void frame(double t, Picture& F) override;

private:
    int C = 0, R = 0;
    double W = 1, H = 1;
    std::vector<double> far_r, mid_r, near_r;
    int tree_j = 0;
    double tree_cx = 0, tree_base = 0, tree_rc = 1, tree_trunk = 1;

    // ---- the clouds in the sky
    // Cumulus is a pile of round puffs on a flat base. Clouds sit at five distances: a near cloud is
    // big and high in the sky, a far one small and low, flattened against the horizon, and the far
    // ones drift across more slowly, as in perspective. Each is made of puffs, bigger in the middle,
    // and the density is soft where puffs overlap, with a flat bottom. Thin cirrus streaks lie high
    // up. The list is rebuilt every frame (a pure function of time).
    struct Puff { double x, y, r; };
    struct Cumulus { double x0, x1, y0, y1, ybase, ytop; int first, count; };
    std::vector<Puff> puffs_;
    std::vector<Cumulus> cumulus_;

    void build_clouds(double t) {
        cumulus_.clear(); puffs_.clear();
        const double Hs = 0.43 * H;                                   // the horizon, as the sky sees it
        static const double LEVEL[5] = {1.0, 1.7, 2.7, 4.2, 6.5};     // distance: 1 near, 6.5 far
        const double exist = clampd(0.98 - 1.45 * CLOUD_COVER, 0.05, 0.95);
        const double spacing = 0.60;                                  // between clouds, in widths at distance 1
        for (int l = 0; l < 5; l++) {
            const double d = LEVEL[l];
            const double shift = CLOUD_SPEED * 0.35 * t * (1.0 + 0.25 * l);    // drift, in widths at distance 1
            const int m0 = (int)std::floor((-0.5 * d - shift) / spacing) - 3, m1 = (int)std::floor((0.5 * d - shift) / spacing) + 3;
            for (int m = m0; m <= m1; m++) {
                const uint32_t sl = (uint32_t)l * 977u + 5u, sm = (uint32_t)(int32_t)m;
                if (rnd(sl, sm, 1) > exist) continue;
                const double xw = (m + 0.15 + 0.7 * rnd(sl, sm, 2)) * spacing + shift;     // where it is, in widths at distance 1
                const double xc = 0.5 * W + xw * W / d;
                const double halfw = (0.065 + 0.085 * rnd(sl, sm, 3)) * W / std::sqrt(d);    // big near, small far (not quite 1/d)
                const double ybase = Hs - (0.26 + 0.10 * rnd(sl, sm, 4)) * H / d;            // low and flat toward the horizon
                const double hgt = halfw * (0.50 + 0.30 * rnd(sl, sm, 5)) * (l >= 3 ? 0.60 : 1.0);   // far ones are flatter
                Cumulus c;
                c.first = (int)puffs_.size();
                const int np = 10;
                for (int k = 0; k < np; k++) {
                    const double u = -1.0 + 2.0 * (k + rnd(sl, sm, 10 + k)) / np;
                    const double bump = 1.0 - u * u;
                    Puff p;
                    p.x = xc + u * halfw * 0.85;
                    p.r = halfw * (0.20 + 0.20 * rnd(sl, sm, 20 + k)) * (0.50 + 0.60 * bump);
                    p.y = ybase - hgt * (0.20 + 0.70 * bump) * (0.65 + 0.5 * rnd(sl, sm, 30 + k)) + 0.2 * p.r;
                    puffs_.push_back(p);
                }
                c.count = np;
                double top = ybase;                                                    // the real top of the cloud
                for (int k = 0; k < np; k++) top = std::min(top, puffs_[c.first + k].y - puffs_[c.first + k].r);
                c.x0 = xc - halfw * 1.4; c.x1 = xc + halfw * 1.4;
                c.ytop = top; c.ybase = ybase; c.y0 = top - 0.2 * hgt; c.y1 = ybase + 0.25 * hgt;
                cumulus_.push_back(c);
            }
        }
    }

    // cumulus density at a place: 0 (sky) .. 1 (thick); v is how far down the cloud it is (0 top, 1 base)
    double cumulus_at(double X, double Y, double& v) const {
        double dens = 0.0; v = 0.5;
        for (const Cumulus& c : cumulus_) {
            if (X < c.x0 || X > c.x1 || Y < c.y0 || Y > c.y1) continue;
            double sum = 0.0;
            for (int k = 0; k < c.count; k++) {
                const Puff& p = puffs_[c.first + k];
                const double dx = (X - p.x) / p.r, dy = (Y - p.y) / p.r;
                sum += std::exp(-1.7 * (dx * dx + dy * dy));
            }
            if (sum < 0.02) continue;
            const double cut = 1.0 - sstep(0.0, 0.10 * (c.ybase - c.ytop) + 1.0, Y - c.ybase);       // a flat bottom
            const double d = (1.0 - std::exp(-2.0 * sum)) * cut;
            // a ragged edge, so the outline is not made of circles
            const double edge = 0.80 + 0.40 * fbm(X * 0.16 + 3.0, Y * 0.22 + 1.0);
            const double dd = sstep(0.22, 0.80, d * edge);
            if (dd > dens) { dens = dd; v = clampd((Y - c.ytop) / (c.ybase - c.ytop + 1e-6), 0.0, 1.0); }
        }
        return dens;
    }

    // clouds: 0 (clear) .. 1 (thick), at a place in the sky, drifting with the wind
    static double cloud(double X, double Y, double t, double thresh) {
        const double nx = X * 0.021 - t * CLOUD_SPEED, ny = Y * 0.040;
        const double d = 0.65 * fbm(nx * 0.7 + 3.1, ny * 0.8 + 1.3) + 0.35 * vnoise(nx * 2.1 + 0.7, ny * 2.4);
        return sstep(thresh, thresh + 0.26, d);
    }

    // The gusts, in the plane of the ground (wx across, zd depth), 0 (still) .. 1 (the middle of a
    // gust). The wind blows along (WIND_X, WIND_Z). A gust is an event: it is born somewhere
    // upwind, runs downwind with a sharp leading edge that bows forward in the middle and a long
    // soft tail behind it, and dies away. Inside it the grass is streaky, in lines along the
    // wind. Between gusts the field is nearly calm, with only a faint ripple. Several gust
    // slots with different periods overlap, and each time a slot comes round it makes a new gust
    // (place, speed, width, strength, or none at all), so the wind never repeats in an obvious way.
    static constexpr double WIND_X = 0.92, WIND_Z = 0.39;
    static double gust(double wx, double zd, double t) {
        const double along = wx * WIND_X + zd * WIND_Z;            // downwind
        const double cross = -wx * WIND_Z + zd * WIND_X;           // across the wind
        double calm = 1.0;                                          // the chance that no gust is here
        for (int k = 0; k < 6; k++) {
            const double period = 6.5 + 1.9 * k;
            const double tt = t + 3.1 * k;
            const double n = std::floor(tt / period);
            const double u = tt - n * period;                       // seconds into this gust
            const uint32_t sk = (uint32_t)k * 7919u + 13u, sn = (uint32_t)(int32_t)n;
            if (rnd(sk, sn, 1) > 0.88) continue;                    // this time round, no gust
            const double a0 = -3.0 + 1.6 * rnd(sk, sn, 2);          // where it starts, upwind
            const double v = (0.55 + 0.55 * rnd(sk, sn, 3)) * WIND_SPEED * 2.5;   // how fast it runs
            const double c0 = -2.0 + 4.0 * rnd(sk, sn, 4);          // across the field
            const double wc = 0.30 + 0.55 * rnd(sk, sn, 5);         // how wide
            const double tail = 0.9 + 1.5 * rnd(sk, sn, 6);         // how long its tail is
            const double power = 0.65 + 0.35 * rnd(sk, sn, 7);
            const double life = sstep(0.0, 0.10 * period, u) * (1.0 - sstep(0.78 * period, period, u));
            const double dc = cross - c0;
            const double da = along - (a0 + v * u) - 0.35 * dc * dc / wc;     // the front bows forward in the middle
            const double front = da > 0.0 ? std::exp(-(da / 0.10) * (da / 0.10)) : std::exp(da / tail);
            const double side = std::exp(-(dc / wc) * (dc / wc));
            calm *= 1.0 - power * life * front * side;
        }
        double g = 1.0 - calm;
        // inside a gust the grass is streaky, in lines along the wind; the streaks drift with it
        const double streak = fbm(cross * 5.5 + 3.0, (along - 1.1 * t * WIND_SPEED * 2.5) * 0.9);
        g *= 0.45 + 1.05 * streak;
        // a faint ripple all the time
        const double ripple = fbm((along - 0.9 * t * WIND_SPEED * 2.5) * 2.4 + 5.0, cross * 2.4);
        g += 0.10 * ripple;
        return clampd(g * GUST_STRENGTH, 0.0, 1.0);
    }
};

void Meadow::frame(double t, Picture& F) {
    F.init(R, C);

    const double Yh = HORIZON * H;                                  // the horizon, in column widths
    // the sun is up and to the left: light comes from there
    static const double SUN_X = 0.12, SUN_Y = 0.07;

    // colours
    static const double SKY_TOP[3] = {52, 112, 204}, SKY_HOR[3] = {160, 206, 240};
    static const double CLOUD_LIT[3] = {254, 252, 246}, CLOUD_SHADE[3] = {160, 178, 206};
    static const double HAZE[3] = {128, 160, 172};                  // the far hills
    static const double GRASS_SHADE_LETTER[3] = {96, 166, 52}, GRASS_LIT[3] = {176, 222, 74}, GRASS_SILVER[3] = {214, 238, 150};
    static const double GROUND_SHADE[3] = {16, 48, 18}, GROUND_LIT[3] = {50, 108, 28};   // the alt colour: the ground between the blades
    static const double NEAR_BLACK[3] = {3, 9, 4};
    static const double LEAF_DARK[3] = {62, 126, 42}, LEAF_LIT[3] = {138, 176, 52};

    // the tree's trunk and crown, and how far the wind has swayed it: a little all the time, more in a gust
    const double crown_cy = tree_base - tree_trunk - 0.72 * tree_rc;
    const double dyT = std::max(2.0, tree_base - Yh);
    const double tree_g = gust((tree_cx - 0.5 * W) / dyT, 0.5 * H / dyT, t);
    const double sway = TREE_SWAY * (0.30 + 0.90 * tree_g) * (0.6 * std::sin(t * 1.25 + 0.7) + 0.4 * std::sin(t * 2.05 + 2.1));
    const double cx_s = tree_cx + sway;                                // the middle of the crown now

    double gust_sum = 0, gust_n = 0, cloud_sum = 0, cloud_n = 0;
    build_clouds(t);

    for (int i = 0; i < R; i++) {
        const double Y = 2.0 * (i + 0.5);
        const double fy = Y / H;
        for (int j = 0; j < C; j++) {
            const double X = j + 0.5;
            const double fx = X / W;
            auto& pc = F.at(i, j);

            const double far_y = far_r[j] * H, mid_y = mid_r[j] * H, near_y = near_r[j] * H;
            int layer;                                            // 0 sky, 1 far hills, 2 middle, 3 near
            if (Y >= near_y) layer = 3;
            else if (Y >= mid_y) layer = 2;
            else if (Y >= far_y) layer = 1;
            else layer = 0;

            // each cell has its own slow rate, so a patch of still sky or grass does not change letters all at once
            const double jit = 0.25 + 1.75 * rnd((uint32_t)j, (uint32_t)i, 40);
            double lum, col[3], alt[3], shimmer = SHIMMER_SKY * jit;
            bool bright = false;                                  // a solid block with a soft letter, or a dim letter on a dark ground
            double tone = SKY_LETTER;                             // the letter on a block, as a multiple of the block's colour

            if (layer == 0) {
                // ---- the sky
                const double ts = sstep(0.0, 0.42 * H, Y);
                lerp3(SKY_TOP, SKY_HOR, ts, col);
                lum = 0.72 + 0.04 * ts;
                const double gdx = (fx - SUN_X) * W, gdy = (fy - SUN_Y) * H;    // the glow round the sun
                const double glow = std::exp(-(gdx * gdx + gdy * gdy) / (2.0 * (0.30 * W) * (0.30 * W)));
                static const double SUN_COL[3] = {255, 243, 214};
                lerp3(col, SUN_COL, SUN_GLOW * glow, col);
                lum += 0.05 * glow;
                // cumulus: bright on top toward the sun, bluish grey underneath, shaded by the cloud toward the sun
                double v, vs;
                const double dens_c = cumulus_at(X, Y, v);
                // cirrus: thin streaks high in the sky, long along the wind
                const double cir = 0.38 * sstep(0.52, 0.82, fbm(X * 0.010 - t * CLOUD_SPEED * 0.8, Y * 0.085 + 3.0))
                                 * (1.0 - sstep(0.06 * H, 0.30 * H, Y));
                const double dens = std::max(dens_c, cir);
                cloud_sum += dens > 0.25; cloud_n += 1;
                if (dens > 0.0) {
                    double lit;
                    if (dens_c >= cir) {
                        const double dsun = cumulus_at(X - 3.0, Y - 2.5, vs);                  // the cloud a little toward the sun
                        lit = 1.0 - 0.55 * std::pow(v, 1.2)                                       // the base is in shade
                            + 1.3 * (dens - dsun) - 0.08;                                         // the side facing the sun is brighter
                    } else {
                        lit = 0.85;                                                               // thin cirrus catches the sun
                    }
                    lit = clampd(lit, 0.12, 1.0);
                    double cc[3];
                    lerp3(CLOUD_SHADE, CLOUD_LIT, lit, cc);
                    lerp3(col, cc, dens, col);
                    lum = lerp(lum, 0.76 + 0.14 * lit, dens);
                }
                bright = true;
            } else if (layer == 1) {
                // ---- the far hills: hazy, close to the sky's colour
                const double h = clampd((Y - far_y) / (0.10 * H), 0.0, 1.0);
                const double hz[3] = {HAZE[0] + 20 * (1 - h), HAZE[1] + 20 * (1 - h), HAZE[2] + 14 * (1 - h)};
                lerp3(hz, HAZE, h, col);
                const double n = fbm(X * 0.05, Y * 0.09);
                for (int c = 0; c < 3; c++) col[c] *= 0.90 + 0.18 * n;
                lum = 0.66 + 0.05 * n;
                bright = true;
                // patches of shadow: a darker block with lighter letters, a mix of bright and dark
                const double patch = std::max(sstep(0.46, 0.82, fbm(X * 0.026 + 40.0, Y * 0.055 + 9.0)),
                                              0.8 * cloud(X * 0.85 + 60.0, Y * 1.5 + 90.0, t, CLOUD_COVER + 0.04));
                for (int c = 0; c < 3; c++) col[c] *= 1.0 - 0.36 * patch;
                tone = 1.0 + 0.80 * patch;
                {                                                                                // a line along the top of the far hills
                    const double yc = Y - far_y;
                    const double edge = std::exp(-(yc / (0.014 * H)) * (yc / (0.014 * H)));
                    for (int c = 0; c < 3; c++) col[c] *= 1.0 - 0.45 * edge;
                    tone = std::max(tone, 1.0 + 1.0 * edge);
                }
            } else {
                // ---- the meadow: layer 2 (middle hills) and 3 (the near field)
                const double ridge = layer == 3 ? near_y : mid_y;
                const double depth = clampd((Y - Yh) / (H - Yh), 0.0, 1.0);        // 0 far .. 1 near
                // across and away, in the plane of the ground
                const double dy = std::max(2.0, Y - Yh);
                const double wx = (X - 0.5 * W) / dy, zd = 0.5 * H / dy;
                const double g = gust(wx, zd, t);
                gust_sum += g; gust_n += 1;

                // how lit the ground is: the hills' shape, the crest catching the sun, big soft bumps
                const double slope = (layer == 3 ? (near_r[std::min(C - 1, j + 2)] - near_r[std::max(0, j - 2)])
                                                 : (mid_r[std::min(C - 1, j + 2)] - mid_r[std::max(0, j - 2)])) * H / 4.0;
                double light = 0.52 + 0.34 * clampd(-slope * 2.2, -1.0, 1.0)               // slopes facing the sun
                             + 0.10 * (1.0 - smooth((Y - ridge) / (0.12 * H)))            // the crest
                             + 0.55 * (fbm(X * 0.028, Y * 0.050 + 3.0) - 0.5);
                // hidden shapes in the field: low hummocks and hollows, lit from the left (the height's
                // slope toward the light), stronger the nearer the ground
                {
                    const double h1 = fbm((X - 2.5) * 0.022 + 20.0, Y * 0.055 + 7.0), h2 = fbm((X + 2.5) * 0.022 + 20.0, Y * 0.055 + 7.0);
                    light += RELIEF * (h1 - h2) * (0.5 + 0.9 * depth);
                }
                // the crest of the hill: a thin bright line at the very top, a darker band just under it
                {
                    const double yc = Y - ridge;
                    const double a = yc / (0.010 * H), b = (yc - 0.040 * H) / (0.030 * H);
                    light += 0.30 * std::exp(-a * a) - 0.24 * std::exp(-b * b);
                }
                // cloud shadows slide over the ground
                const double shade_cloud = cloud(X * 0.85 + 60.0, Y * 1.5 + 90.0, t, CLOUD_COVER + 0.04);
                light -= 0.42 * shade_cloud;
                // the tree's shadow, thrown to the right
                {
                    const double sx = (X - (tree_cx + 0.55 * tree_rc)) / (1.7 * tree_rc);
                    const double sy = (Y - (tree_base + 0.04 * H)) / (0.050 * H + 0.25 * tree_rc);
                    light -= 0.45 * std::exp(-(sx * sx + sy * sy) * 1.4);
                }
                // sunlit patches and tufts in the field: bigger and brighter toward us
                double tuft = 0.0;
                {
                    const double sp = sstep(0.50, 0.78, fbm(wx * 2.6 + 11.0, zd * 2.6 + 4.0));          // patches where the sun falls well
                    light += (0.10 + 0.20 * depth) * sp * (1.0 - 0.8 * shade_cloud);
                    tuft = sstep(0.56, 0.80, fbm(wx * 9.0 + 3.0, zd * 9.0 + 8.0)) * (0.35 + 0.65 * depth);
                }
                light = clampd(light, 0.0, 1.0);

                // the blades: colour from the light, silver in a gust
                double blade[3];
                lerp3(GRASS_SHADE_LETTER, GRASS_LIT, light, blade);
                lerp3(blade, GRASS_SILVER, 0.88 * g * (0.45 + 0.55 * light), blade);
                {                                                                             // yellow where the sun shines well
                    static const double YELLOW[3] = {238, 228, 84};
                    lerp3(blade, YELLOW, SUN_YELLOW * sstep(0.58, 0.98, light) * (1.0 - 0.5 * g), blade);
                }
                const double tex = fbm(X * 0.30, Y * 0.30);                                  // fine patchiness
                {                                                                             // tufts: lighter, yellower green
                    static const double TUFT[3] = {198, 232, 98};
                    lerp3(blade, TUFT, 0.55 * tuft * (0.4 + 0.6 * light), blade);
                }
                for (int c = 0; c < 3; c++) col[c] = blade[c] * (0.86 + 0.28 * tex);
                lum = GRASS_LUM + 0.10 * light + GRASS_GUST_LUM * g + 0.05 * (tex - 0.5) + 0.05 * tuft;
                shimmer = SHIMMER_CALM * jit + SHIMMER_GUST * flicker(g) * (0.5 + 0.5 * tex);
                {                                                                             // the ground between the blades:
                    double murky[3], crisp[3];                                                // mid-green in the light, near black in shade
                    lerp3(GROUND_SHADE, GROUND_LIT, light, murky);
                    lerp3(NEAR_BLACK, GROUND_LIT, std::pow(light, 0.85), crisp);
                    // the contrast is strongest along the top of the hill, where the ground turns away from
                    // us: a line that separates this hill from what is behind it
                    const double yc = Y - ridge;
                    const double edge = std::exp(-(yc / (0.045 * H)) * (yc / (0.045 * H)));
                    lerp3(murky, crisp, CONTRAST_BASE + (CONTRAST - CONTRAST_BASE) * edge, alt);
                }

                // distance: the farther middle hills fade toward haze and the sky
                const double haze = layer == 2 ? 0.55 * (1.0 - depth) * (1.0 - depth) + 0.10 : 0.0;
                if (haze > 0.0) {
                    lerp3(col, HAZE, haze, col);
                    for (int c = 0; c < 3; c++) alt[c] = lerp(alt[c], HAZE[c] * 0.55, haze);
                    lum = lerp(lum, 0.45, haze);
                    {                                                      // just before they turn into blocks, the ground behind the
                        const double k = sstep(0.14, 0.30, haze);          // letters takes the block's colour, so the join is soft
                        for (int c = 0; c < 3; c++) alt[c] = lerp(alt[c], col[c] * 0.97, k);
                    }
                    if (haze > 0.30) {                                    // far enough to be a soft block, like the far hills
                        bright = true;
                        shimmer = SHIMMER_SKY * jit;
                        lum = 0.60 + 0.08 * light;
                        // shade on the hazy hills: a darker block with lighter letters
                        const double patch = clampd((0.55 - light) * 1.6, 0.0, 1.0) * (0.5 + 0.5 * (1.0 - haze));
                        for (int c = 0; c < 3; c++) col[c] *= 1.0 - 0.40 * patch;
                        tone = 1.0 + 0.90 * patch;
                        {                                                                        // a line along the top of the middle hills
                            const double yc = Y - mid_y;
                            const double edge = std::exp(-(yc / (0.016 * H)) * (yc / (0.016 * H)));
                            for (int c = 0; c < 3; c++) col[c] *= 1.0 - 0.45 * edge;
                            tone = std::max(tone, 1.0 + 1.0 * edge);
                        }
                        {                                                                        // a warm tint where the sun shines on them
                            static const double WARM[3] = {232, 228, 150};
                            lerp3(col, WARM, 0.30 * sstep(0.55, 0.9, light) * (1.0 - haze), col);
                        }
                    }
                }

                // flowers in the grass, mostly in the near field
                // flowers come in clusters (in the plane of the ground), most in the near field
                const double cluster = 0.25 + 2.4 * sstep(0.50, 0.75, fbm(wx * 4.0 + 30.0, zd * 4.0 + 17.0));
                const double pf = (layer == 3 ? 0.050 : 0.014) * (0.4 + 1.0 * depth) * cluster;
                if (!bright && rnd((uint32_t)j, (uint32_t)i, 5) < pf) {
                    const double w = rnd((uint32_t)j, (uint32_t)i, 6);
                    static const double YEL[3] = {248, 214, 64}, WHT[3] = {246, 246, 236}, PNK[3] = {232, 150, 190};
                    const double* fc = w < 0.68 ? YEL : (w < 0.93 ? WHT : PNK);
                    for (int c = 0; c < 3; c++) col[c] = fc[c] * (0.80 + 0.20 * light);
                    lum = 0.38;
                    shimmer = SHIMMER_CALM * jit + 0.6 * SHIMMER_GUST * flicker(g);
                }
            }

            // ---- the tree, over everything behind it
            if (std::fabs(X - cx_s) < 1.5 * tree_rc + 2.0 && Y > crown_cy - 1.5 * tree_rc && Y < tree_base + 2.0) {
                // trunk: slightly leaning, tapering upward
                const double ty = (tree_base - Y) / tree_trunk;                              // 0 at the ground, 1 at the crown
                if (ty >= -0.02 && ty <= 1.15) {
                    const double lean = 0.012 * W * ty + 0.7 * sway * ty;          // the trunk bends toward the top
                    const double halfw = (0.0085 + 0.0030 * (1.0 - ty)) * W;
                    if (std::fabs(X - (tree_cx + lean)) < halfw) {
                        const double sh = 0.5 + 0.5 * clampd((X - (tree_cx + lean)) / halfw, -1.0, 1.0);   // lit on the left
                        col[0] = 118 - 52 * sh; col[1] = 84 - 38 * sh; col[2] = 52 - 24 * sh;
                        lum = 0.30; shimmer = SHIMMER_CALM * jit;
                        alt[0] = 22; alt[1] = 16; alt[2] = 10;
                        bright = false;
                    }
                }
                // crown: a lumpy blob, lit from the upper left
                const double cdx = (X - cx_s) / 1.12, cdy = Y - crown_cy;
                const double ang = std::atan2(cdy, cdx);
                const double lump = 1.0 + 0.20 * (fbm(std::cos(ang) * 2.2 + 3.0, std::sin(ang) * 2.2 + 1.0) - 0.5) * 2.0;
                const double ragged = 1.0 + 0.16 * (fbm(X * 0.30 + 11.0, Y * 0.30 + 5.0) - 0.5) * 2.0;     // leaves along the edge
                const double rad = std::sqrt(cdx * cdx + cdy * cdy) / (tree_rc * lump * ragged * (cdy > 0 ? 0.90 : 1.0));
                if (rad < 1.0) {
                    const double gt = tree_g;
                    const double flutter = 2.5 * gt * std::sin(t * 2.3 + X * 0.1);                // the leaves ruffle in a gust
                    const double leaf = fbm((X + flutter) * 0.28, (Y + 0.5 * flutter) * 0.34);
                    const double n = leaf - 0.5;
                    // lit from the sun: up and to the left
                    double light = 0.50 + 0.55 * (-(cdx * 0.62 + cdy * 0.78) / tree_rc) + 0.9 * n;
                    light -= 0.30 * sstep(0.15, 0.90, cdy / tree_rc);                              // the underside is in shade
                    light = clampd(light, 0.0, 1.0);
                    lerp3(LEAF_DARK, LEAF_LIT, light, col);
                    {                                                                             // the sunny side of the crown is yellower
                        static const double LEAF_SUN[3] = {222, 214, 78};
                        lerp3(col, LEAF_SUN, 0.55 * SUN_YELLOW * sstep(0.62, 1.0, light), col);
                    }
                    lum = 0.30 + 0.13 * light + 0.06 * n;
                    shimmer = SHIMMER_CALM * jit + 1.1 * SHIMMER_GUST * flicker(gt) * (0.4 + 0.6 * (n + 0.5));
                    static const double LEAF_BACK_LIT[3] = {52, 104, 30}, LEAF_BACK_OLD[3] = {14, 40, 16};
                    double murky[3], crisp[3];
                    lerp3(LEAF_BACK_OLD, LEAF_BACK_LIT, light, murky);
                    lerp3(NEAR_BLACK, LEAF_BACK_LIT, std::pow(light, 1.2), crisp);
                    const double rim = sstep(0.72, 1.0, rad);                                    // the outline of the crown is crisp
                    lerp3(murky, crisp, CONTRAST_BASE + (CONTRAST - CONTRAST_BASE) * (0.30 + 0.70 * rim), alt);
                    // a few leaves catch the light
                    if (rnd((uint32_t)j, (uint32_t)i, 9) < 0.10 * light) { col[0] = 206; col[1] = 228; col[2] = 120; lum += 0.04; }
                    bright = false;
                }
            }

            // ---- put it in the picture
            pc.lum = clampd(lum, 0.0, 0.999);
            pc.r = col[0]; pc.g = col[1]; pc.b = col[2];
            pc.shimmer = shimmer;
            if (bright) {                                         // a block: the letter on it is a soft tone of the block
                pc.alt_lum = 1.0;
                pc.alt_r = col[0] * tone; pc.alt_g = col[1] * tone; pc.alt_b = col[2] * tone;
            } else {                                              // a letter: the alt colour is the ground behind it
                pc.alt_lum = 1.0;
                pc.alt_r = alt[0]; pc.alt_g = alt[1]; pc.alt_b = alt[2];
            }
        }
    }

    // the readout: written over the sky, so its background is the sky's colour and the letters dark
    auto hud = [&](int row, const std::string& txt) {
        double back[3][64];
        const int len = (int)std::min<size_t>(txt.size(), 64);
        for (int k = 0; k < len && k < C; k++) { auto& q = F.at(row, k); back[0][k] = q.r; back[1][k] = q.g; back[2][k] = q.b; }
        F.text(row, 0, txt, 24, 44, 76);
        for (int k = 0; k < len && k < C; k++) {
            auto& q = F.at(row, k);
            q.alt_lum = 1.0; q.alt_r = back[0][k]; q.alt_g = back[1][k]; q.alt_b = back[2][k];
        }
    };
    char buf[64];
    const double avg = gust_n > 0 ? gust_sum / gust_n : 0.0;
    std::snprintf(buf, sizeof buf, "wind %.0f km/h", 6.0 + 30.0 * avg);
    hud(0, buf);
    std::snprintf(buf, sizeof buf, "gusts %.0f%%", 100.0 * avg);
    hud(1, buf);
    std::snprintf(buf, sizeof buf, "cloud cover %.0f%%", cloud_n > 0 ? 100.0 * cloud_sum / cloud_n : 0.0);
    hud(2, buf);
}

}  // namespace

std::unique_ptr<vtscreen::Scene> make_meadow() { return std::make_unique<Meadow>(); }

#ifndef VTSHOW_NO_MAIN
int main(int argc, char** argv) {
    Meadow scene;
    return vtscreen::run(scene, argc, argv);
}
#endif
