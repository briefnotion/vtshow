// scenes.h - one factory function per scene. Each scene's .cpp defines its own
// (and keeps its internals in an anonymous namespace), so several scenes can be
// linked into one program. Compile a scene with -DVTSHOW_NO_MAIN to leave out
// its own main(). The scenes in ports/ are declared in ports/ports.h.
#pragma once

#include <memory>

#include "vtscreen/vtscreen.h"

std::unique_ptr<vtscreen::Scene> make_bonfire();
std::unique_ptr<vtscreen::Scene> make_moonphases();
std::unique_ptr<vtscreen::Scene> make_starburst();
std::unique_ptr<vtscreen::Scene> make_singularity();
std::unique_ptr<vtscreen::Scene> make_thunderstorm();
