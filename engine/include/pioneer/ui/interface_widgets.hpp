#pragma once
// Pioneer Engine - the widgets interface scripts draw with (core/interface_lua.hpp),
// made of Dear ImGui.

#include <memory>

#include "pioneer/core/interface_lua.hpp"

namespace pioneer::ui {
std::unique_ptr<InterfaceWidgets> makeInterfaceWidgets();
}
