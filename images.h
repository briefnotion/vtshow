// images.h - ask the terminal whether it can show Sixel or ReGIS pictures, and tell vtscreen.
//
// Scenes can add pictures over their letters: made of pixels (a Raster), or of shapes (a vtpen Drawing: line art, say). That only
// works on a terminal that understands Sixel or ReGIS, so a program that runs scenes calls vtshow_enable_images() once, before
// it opens a Display. It uses vtpen's probe (https://github.com/briefnotion/vtpen) to ask, and says so to
// vtscreen::set_image_support (Sixel) and set_regis_support (ReGIS, which draws shapes as lines).
// It does nothing, and everything stays letters, when:
//   - vtscreen is too old to have pictures made of pixels (no VTSCREEN_IMAGES),
//   - vtpen's headers are not on the include path (compile with -I<vtpen>/include),
//   - the program is not on a Unix terminal, or its input or output is not a terminal,
//   - the terminal does not answer or does not do Sixel or ReGIS, or
//   - the environment variable VTSHOW_NO_IMAGES is set (to compare with and without).
// VTSHOW_NO_REGIS=1 keeps drawings as Sixel pixels on a terminal that also does ReGIS (to compare the two).
#pragma once

#include <algorithm>
#include <cstdlib>

#include "vtscreen/vtscreen.h"

#if defined(VTSCREEN_IMAGES) && !defined(_WIN32) && __has_include("vtpen_probe_posix.h")
#include <unistd.h>

#include "vtpen_probe_posix.h"
#define VTSHOW_CAN_PROBE 1
#endif

inline void vtshow_enable_images() {
#ifdef VTSHOW_CAN_PROBE
    if (std::getenv("VTSHOW_NO_IMAGES")) return;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return;
    const vtpen::Capabilities cap = vtpen::probe_terminal(STDIN_FILENO, STDOUT_FILENO, 1000);
    if (cap.usable()) vtscreen::set_image_support(cap.cell_w, cap.cell_h);
#ifdef VTSCREEN_VECTORS
    // How soft the edges of drawings shown as pixels are (1 hard, 4 smoothest; every step costs bytes: 2 is about a third
    // smaller than 4 and still looks smooth). VTSHOW_PEN_AA=N tries another value.
    if (const char* e = std::getenv("VTSHOW_PEN_AA")) {
        const int x = std::atoi(e);
        if (x > 0) vtscreen::g_vector_edge_levels = std::min(x, 8);
    }
    // Drawings made of shapes go as ReGIS when the terminal lists it (VTSHOW_NO_REGIS=1: as pixels, to compare). The ReGIS screen
    // is as big as the terminal says its pictures may be, at most the window; the terminal needs to say how big a cell is.
    if (cap.regis && cap.cell_w > 0 && cap.cell_h > 0 && !std::getenv("VTSHOW_NO_REGIS")) {
        int w = cap.max_width > 0 ? cap.max_width : 800, h = cap.max_height > 0 ? cap.max_height : 480;
        if (cap.window_w > 0) w = std::min(w, cap.window_w);
        if (cap.window_h > 0) h = std::min(h, cap.window_h);
        vtscreen::set_regis_support(cap.cell_w, cap.cell_h, w, h);
    }
#endif
#endif
}
