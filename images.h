// images.h - ask the terminal whether it can show Sixel pictures, and tell vtscreen.
//
// Scenes can add pictures made of pixels (line art, say) over their letters. That only works on a terminal that
// understands Sixel, so a program that runs scenes calls vtshow_enable_images() once, before it opens a Display.
// It uses vtpen's probe (https://github.com/briefnotion/vtpen) to ask, and says so to vtscreen::set_image_support.
// It does nothing, and everything stays letters, when:
//   - vtscreen is too old to have pictures made of pixels (no VTSCREEN_IMAGES),
//   - vtpen's headers are not on the include path (compile with -I<vtpen>/include),
//   - the program is not on a Unix terminal, or its input or output is not a terminal,
//   - the terminal does not answer or does not do Sixel, or
//   - the environment variable VTSHOW_NO_IMAGES is set (to compare with and without).
#pragma once

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
#endif
}
