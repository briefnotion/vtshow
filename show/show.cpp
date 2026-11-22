// show - the scenes in one program, one after the other: with the scenes in
// ports/ present, campfire, moon, fireworks, black hole and thunderstorm; without
// them, just the thunderstorm.
//
// One scene is on screen. After STAY_SECONDS it switches to the next, or at once
// when you press n (or space); q quits. A switch is a FADE_SECONDS cross-fade: the two pictures' brightness and colour are mixed cell by cell
// (vtscreen::fade) and the Display picks the letters for the mixture. A press of
// n during a fade is ignored.
//
//   ./build.sh          (compiles this file together with the scenes, with
//                        -DVTSHOW_NO_MAIN so the scenes leave out their own main,
//                        and -DVTSHOW_PORTS when the ports/ folder is there)
//   ./show/show         options: --fps N  --no-color  --cols N --rows N
//
// All scenes share one clock. Only the scene on screen is computed (and the one
// fading out, during a fade): each scene is a pure function of time, so a scene
// that was not computed for a while is not behind when it comes back.

#include <cstdio>
#include <string>
#include <vector>

#include "../scenes.h"
#ifdef VTSHOW_PORTS
#include "../ports/ports.h"
#endif

static const double STAY_SECONDS = 30.0;   // a scene on screen on its own, before the next fade starts
static const double FADE_SECONDS = 10.0;   // the cross-fade between two scenes

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
#ifdef VTSHOW_PORTS
    scenes.push_back(make_campfire());
    scenes.push_back(make_moon());
    scenes.push_back(make_fireworks());
    scenes.push_back(make_blackhole());
#endif
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
    vtscreen::Picture mixed;
    while (!display.stopping()) {
        double t = display.time();

        bool flip = !fading && t - settled >= STAY_SECONDS;
        for (char key; (key = display.next_key()) != 0;)
            if (!fading && (key == 'n' || key == 'N' || key == ' ')) flip = true;
        if (count < 2) flip = false;                 // one scene: nothing to switch to
        if (flip) { from = on; on = (on + 1) % count; switched = t; fading = true; }

        double pct = fading ? (t - switched) / FADE_SECONDS : 1.0;
        if (fading && pct >= 1.0) { fading = false; settled = t; }

        scenes[on]->cycle(t);
        if (fading) scenes[from]->cycle(t);

        // while fading, the status line and letter style follow the scene that
        // is more visible
        const vtscreen::Scene* lead = (fading && pct < 0.5) ? scenes[from].get() : scenes[on].get();
        vtscreen::Info info = lead->info();
        std::string title = std::string(info.title) + "  [n]";
        info.title = title.c_str();
        display.hash_letters = info.hash_letters;

        if (fading) {
            double ease = pct * pct * (3 - 2 * pct);            // slow at both ends
            vtscreen::fade(scenes[from]->screen, scenes[on]->screen, ease, mixed);
            display.show(mixed, t, info, true);
        } else {
            display.show(scenes[on]->screen, t, info, true);
        }
        display.end_frame();
    }
    return 0;
}
