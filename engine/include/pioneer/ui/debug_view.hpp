#pragma once
// Pioneer Engine - draws the debug shapes (core/debug_draw.hpp) over a view
// with ImGui: lines and text on top of everything, so they show through
// hills and walls. The caller says how a world point lands on the screen.

#include <functional>

#include "pioneer/core/debug_draw.hpp"

struct ImDrawList;

namespace pioneer::ui {

// World point -> screen pixels; false when it can't be seen (behind the eye).
using ProjectFn = std::function<bool(Vec3f world, float& x, float& y)>;
// Returns how many shapes were drawn.
int drawDebugShapes(const debug::DebugDraw& shapes, ImDrawList* list, const ProjectFn& project);

}  // namespace pioneer::ui
