#include "key_bindings.hpp"

#include <array>

namespace pyroclast
{
namespace
{
using Kind = UiCommand::Kind;
constexpr std::array bindings{
    KeyBinding{SDLK_SPACE, false, {Kind::trigger}},
    KeyBinding{SDLK_R, false, {Kind::clear}},
    KeyBinding{SDLK_P, false, {Kind::toggle_pause}},
    KeyBinding{SDLK_N, false, {Kind::step}},
    KeyBinding{SDLK_L, false, {Kind::toggle_debug_lines}},
    KeyBinding{SDLK_T, false, {Kind::toggle_slow_motion}},
    KeyBinding{SDLK_G, false, {Kind::toggle_walk}},
    KeyBinding{SDLK_F2, false, {Kind::recall_bookmark, 0}},
    KeyBinding{SDLK_F3, false, {Kind::recall_bookmark, 1}},
    KeyBinding{SDLK_F4, false, {Kind::recall_bookmark, 2}},
    KeyBinding{SDLK_F5, false, {Kind::recall_bookmark, 3}},
    KeyBinding{SDLK_F6, false, {Kind::recall_bookmark, 4}},
    KeyBinding{SDLK_F7, false, {Kind::recall_bookmark, 5}},
    KeyBinding{SDLK_F2, true, {Kind::store_bookmark, 0}},
    KeyBinding{SDLK_F3, true, {Kind::store_bookmark, 1}},
    KeyBinding{SDLK_F4, true, {Kind::store_bookmark, 2}},
    KeyBinding{SDLK_F5, true, {Kind::store_bookmark, 3}},
    KeyBinding{SDLK_F6, true, {Kind::store_bookmark, 4}},
    KeyBinding{SDLK_F7, true, {Kind::store_bookmark, 5}},
    KeyBinding{SDLK_1, false, {Kind::select_preset, 0}},
    KeyBinding{SDLK_2, false, {Kind::select_preset, 1}},
    KeyBinding{SDLK_3, false, {Kind::select_preset, 2}},
    KeyBinding{SDLK_4, false, {Kind::select_preset, 3}},
    KeyBinding{SDLK_5, false, {Kind::select_preset, 4}},
    KeyBinding{SDLK_6, false, {Kind::select_preset, 5}},
    KeyBinding{SDLK_7, false, {Kind::select_preset, 6}},
    KeyBinding{SDLK_KP_1, false, {Kind::select_preset, 0}},
    KeyBinding{SDLK_KP_2, false, {Kind::select_preset, 1}},
    KeyBinding{SDLK_KP_3, false, {Kind::select_preset, 2}},
    KeyBinding{SDLK_KP_4, false, {Kind::select_preset, 3}},
    KeyBinding{SDLK_KP_5, false, {Kind::select_preset, 4}},
    KeyBinding{SDLK_KP_6, false, {Kind::select_preset, 5}},
    KeyBinding{SDLK_KP_7, false, {Kind::select_preset, 6}},
    KeyBinding{SDLK_C, false, {Kind::set_count, 0}},
    KeyBinding{SDLK_F, false, {Kind::fire, 0}},
    KeyBinding{SDLK_B, false, {Kind::barrage}},
    KeyBinding{SDLK_V, false, {Kind::toggle_bombardment}},
    KeyBinding{SDLK_F8, false, {Kind::toggle_debug_view, 2}},
    KeyBinding{SDLK_F9, false, {Kind::toggle_debug_view, 3}},
};
}

std::span<const KeyBinding> key_bindings()
{
    return bindings;
}

std::string_view controls_summary()
{
    return "WASD/QE + right mouse: move | click/F: fire | 1-7: preset | C: count | B: barrage | V: bombardment | T: 0.25x | "
           "G: walk | F2-F7: views (Ctrl stores) | F8/F9/L: debug views | Space: group | R: clear | P: pause | N: step";
}

std::optional<UiCommand> key_command(SDL_Keycode key, SDL_Keymod modifiers)
{
    if ((modifiers & SDL_KMOD_ALT) != 0) return std::nullopt;
    const bool ctrl = (modifiers & SDL_KMOD_CTRL) != 0;
    for (const auto& binding : bindings)
    {
        if (binding.key == key && binding.ctrl == ctrl) return binding.command;
    }
    for (const auto& binding : bindings)
    {
        if (binding.key == key && !binding.ctrl) return binding.command;
    }
    return std::nullopt;
}
}
