// show - the scenes in one program, one after the other: bonfire, moonphases,
// starburst, singularity and thunderstorm.
//
// One scene is on screen. After STAY_SECONDS it switches to the next, or at once
// when you press n (or space); q quits. A switch is a FADE_SECONDS cross-fade: the
// two pictures' brightness and colour are mixed cell by cell (vtscreen::fade) and
// the Display picks the letters for the mixture.
//
// Keys: n or space = next scene, p or b = previous, 1-9 = that scene (the status bar
// shows the number, like 3/7), h = hold. They work at any time, in the middle of a
// fade too: the new scene starts fading in over whatever is on screen, so the picture
// never jumps. Up to 3 scenes can be on screen at once; a further press while three
// are up drops the oldest.
//
// h holds the scene that is up: the timer no longer moves on (the status line shows
// HOLD). The scene keeps animating, and the keys above still work. Press h again to
// let go; the scene then gets a full STAY_SECONDS before the next fade.
//
//   ./build.sh          (compiles this file together with the scenes, with
//                        -DVTSHOW_NO_MAIN so the scenes leave out their own main)
//   ./show/show         options: --fps N  --no-color  --cols N --rows N
//
// All scenes share one clock. Only the scenes on screen are computed (one, or
// more during a fade): each scene is a pure function of time, so a scene
// that was not computed for a while is not behind when it comes back.

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../scenes.h"

// A scene on screen on its own, before the next fade starts, and the length of the
// cross-fade. Compile with -DVTSHOW_STAY_SECONDS=5 -DVTSHOW_FADE_SECONDS=2 to try
// other values without editing this file.
#ifndef VTSHOW_STAY_SECONDS
#define VTSHOW_STAY_SECONDS 30.0
#endif
#ifndef VTSHOW_FADE_SECONDS
#define VTSHOW_FADE_SECONDS 10.0
#endif
static const double STAY_SECONDS = VTSHOW_STAY_SECONDS;
static const double FADE_SECONDS = VTSHOW_FADE_SECONDS;

int main(int argc, char** argv) {
    vtscreen::Options o;
    int rc = vtscreen::parse_options(argc, argv, o);
    if (rc >= 0) return rc;
    if (o.still) {
        std::fprintf(stderr, "show: --still is not supported (use the scene's own program)\n");
        return 2;
    }
    int cols, rows;
    if (!vtscreen::resolve_size(o, cols, rows)) return 1;

    std::vector<std::unique_ptr<vtscreen::Scene>> scenes;
    scenes.push_back(make_bonfire());
    scenes.push_back(make_moonphases());
    scenes.push_back(make_starburst());
    scenes.push_back(make_singularity());
    scenes.push_back(make_thunderstorm());
    scenes.push_back(make_meadow());
    scenes.push_back(make_matrix());
    const int count = (int)scenes.size();

    vtscreen::Display display;
    display.open(o, cols, rows);
    for (auto& s : scenes) s->setup(cols, rows - 1);
    display.start();

    // The scenes on screen, oldest first. The first is the base; each later one is
    // fading in over everything before it, from its own start time. Normally there is
    // just one. A key press adds a scene on top, even in the middle of a fade, and the
    // picture is the base mixed with each layer in turn, so nothing jumps.
    struct Layer { int scene; double start; };
    const size_t MAX_LAYERS = 3;                 // when full, the oldest is dropped
    std::vector<Layer> stack{{0, 0.0}};
    double settled = 0;                 // when the scene on screen was last alone on it
    bool hold = false;                  // h: stay on this scene
    vtscreen::Picture mixed, scratch;
    while (!display.stopping()) {
        double t = display.time();

        // keys: n or space = next, p or b = previous, 1-9 = that scene, h = hold
        int target = -1;
        const int top = stack.back().scene;
        for (char key; (key = display.next_key()) != 0;) {
            const int cur = target >= 0 ? target : top;
            if (key == 'n' || key == 'N' || key == ' ') target = (cur + 1) % count;
            else if (key == 'p' || key == 'P' || key == 'b' || key == 'B') target = (cur + count - 1) % count;
            else if (key >= '1' && key <= '9' && key - '1' < count) target = key - '1';
            else if (key == 'h' || key == 'H') {
                hold = !hold;
                if (!hold) settled = t;                  // let go: a full stay again
            }
        }
        if (target < 0 && stack.size() == 1 && !hold && t - settled >= STAY_SECONDS)
            target = (top + 1) % count;                  // the timer
        if (target >= 0 && target != top && count > 1) {
            if (stack.size() >= MAX_LAYERS) stack.erase(stack.begin());
            stack.push_back({target, t});
        }

        // fade progress of each layer (eased: slow at both ends); a layer that is
        // fully in becomes the base and everything under it is gone
        std::vector<double> ease(stack.size(), 1.0);
        for (size_t k = 1; k < stack.size(); k++) {
            double pct = (t - stack[k].start) / FADE_SECONDS;
            ease[k] = pct >= 1.0 ? 1.0 : pct * pct * (3 - 2 * pct);
        }
        size_t base = 0;
        for (size_t k = 1; k < stack.size(); k++) if (ease[k] >= 1.0) base = k;
        if (base > 0) {
            stack.erase(stack.begin(), stack.begin() + base);
            ease.erase(ease.begin(), ease.begin() + base);
            if (stack.size() == 1) settled = t;
        }
        const bool fading = stack.size() > 1;

        // each scene on screen gets its own status bar (its title, its number, our
        // hint, time, fps) in its own picture, so a fade mixes the bars like the rest
        std::vector<bool> done(count, false);
        for (const Layer& L : stack) {
            if (done[L.scene]) continue;                 // the same scene twice: once
            done[L.scene] = true;
            vtscreen::Scene& sc = *scenes[L.scene];
            std::string hint = "  [" + std::to_string(L.scene + 1) + "/" + std::to_string(count) +
                               (hold ? " HOLD]" : " n p h]");
            sc.cycle(t);
            display.bar(sc.screen, sc.info(), t, true, hint);
        }

        // while fading, the letter style follows the scene that is the most visible
        size_t lead = 0;
        double best = -1, rest = 1.0;
        for (size_t k = stack.size(); k-- > 0;) {        // weight of layer k
            double w = rest * ease[k];
            if (w > best) { best = w; lead = k; }
            rest *= 1.0 - ease[k];
        }
        display.hash_letters = scenes[stack[lead].scene]->info().hash_letters;

        if (fading) {
            mixed = scenes[stack[0].scene]->screen;
            for (size_t k = 1; k < stack.size(); k++) {
                vtscreen::fade(mixed, scenes[stack[k].scene]->screen, ease[k], scratch);
                std::swap(mixed, scratch);
            }
            display.show(mixed, t);
        } else {
            display.show(scenes[stack[0].scene]->screen, t);
        }
        display.end_frame();
    }
    return 0;
}
