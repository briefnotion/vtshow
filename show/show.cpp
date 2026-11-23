// show - the scenes in one program, one after the other: bonfire, moonphases,
// starburst, singularity and thunderstorm.
//
// One scene is on screen. After STAY_SECONDS it switches to the next, or at once
// when you press n (or space); q quits. A switch is a FADE_SECONDS cross-fade: the
// two pictures' brightness and colour are mixed cell by cell (vtscreen::fade) and
// the Display picks the letters for the mixture. A press of n during a fade is
// ignored.
//
// h holds the scene that is up: the timer no longer moves on (the status line shows
// [HOLD]). The scene keeps animating, and n still moves to the next one by hand. A
// fade already under way finishes. Press h again to let go; the scene then gets a
// full STAY_SECONDS before the next fade.
//
//   ./build.sh          (compiles this file together with the scenes, with
//                        -DVTSHOW_NO_MAIN so the scenes leave out their own main)
//   ./show/show         options: --fps N  --no-color  --cols N --rows N
//
// All scenes share one clock. Only the scene on screen is computed (and the one
// fading out, during a fade): each scene is a pure function of time, so a scene
// that was not computed for a while is not behind when it comes back.

#include <cstdio>
#include <string>
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
    const int count = (int)scenes.size();

    vtscreen::Display display;
    display.open(o, cols, rows);
    for (auto& s : scenes) s->setup(cols, rows - 1);
    display.start();

    int on = 0;                         // the scene on screen (or fading in)
    int from = 0;                       // the scene fading out
    double switched = 0;                // when the last fade started
    double settled = 0;                 // when the scene on screen was last alone on it
    bool fading = false;
    bool hold = false;                  // h: stay on this scene
    vtscreen::Picture mixed;
    while (!display.stopping()) {
        double t = display.time();

        bool flip = !fading && !hold && t - settled >= STAY_SECONDS;
        for (char key; (key = display.next_key()) != 0;) {
            if (!fading && (key == 'n' || key == 'N' || key == ' ')) flip = true;
            if (key == 'h' || key == 'H') {
                hold = !hold;
                if (!hold) { settled = t; flip = false; }   // let go: a full stay again
            }
        }
        if (count < 2) flip = false;                 // one scene: nothing to switch to
        if (flip) { from = on; on = (on + 1) % count; switched = t; fading = true; }

        double pct = fading ? (t - switched) / FADE_SECONDS : 1.0;
        if (fading && pct >= 1.0) { fading = false; settled = t; }

        // each scene on screen gets its own status bar (title, our hint, time, fps)
        // in its own picture, so a fade mixes the two bars like everything else
        const char* hint = hold ? "  [HOLD]" : "  [n h]";
        scenes[on]->cycle(t);
        display.bar(scenes[on]->screen, scenes[on]->info(), t, true, hint);
        if (fading) {
            scenes[from]->cycle(t);
            display.bar(scenes[from]->screen, scenes[from]->info(), t, true, hint);
        }

        // while fading, the letter style follows the scene that is more visible
        const vtscreen::Scene* lead = (fading && pct < 0.5) ? scenes[from].get() : scenes[on].get();
        display.hash_letters = lead->info().hash_letters;

        if (fading) {
            double ease = pct * pct * (3 - 2 * pct);            // slow at both ends
            vtscreen::fade(scenes[from]->screen, scenes[on]->screen, ease, mixed);
            display.show(mixed, t);
        } else {
            display.show(scenes[on]->screen, t);
        }
        display.end_frame();
    }
    return 0;
}
