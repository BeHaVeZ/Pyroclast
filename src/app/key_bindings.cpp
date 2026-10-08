#include "key_bindings.hpp"

#include <array>

namespace pyroclast
{
namespace
{
using Kind = UiCommand::Kind;
constexpr std::array bindings{
    KeyBinding{SDLK_SPACE, false, {Kind::trigger}, "restart the group"},
    KeyBinding{SDLK_R, false, {Kind::clear}, "clear"},
    KeyBinding{SDLK_P, false, {Kind::toggle_pause}, "pause"},
    KeyBinding{SDLK_N, false, {Kind::step}, "step"},
    KeyBinding{SDLK_L, false, {Kind::toggle_debug_lines}, "lines"},
    KeyBinding{SDLK_T, false, {Kind::toggle_slow_motion}, "0.25x"},
    KeyBinding{SDLK_G, false, {Kind::toggle_walk}, "walk"},
    KeyBinding{SDLK_F2, false, {Kind::recall_bookmark, 0}, "views"},
    KeyBinding{SDLK_F3, false, {Kind::recall_bookmark, 1}, ""},
    KeyBinding{SDLK_F4, false, {Kind::recall_bookmark, 2}, ""},
    KeyBinding{SDLK_F5, false, {Kind::recall_bookmark, 3}, ""},
    KeyBinding{SDLK_F6, false, {Kind::recall_bookmark, 4}, ""},
    KeyBinding{SDLK_F7, false, {Kind::recall_bookmark, 5}, ""},
    KeyBinding{SDLK_F2, true, {Kind::store_bookmark, 0}, "store a view"},
    KeyBinding{SDLK_F3, true, {Kind::store_bookmark, 1}, ""},
    KeyBinding{SDLK_F4, true, {Kind::store_bookmark, 2}, ""},
    KeyBinding{SDLK_F5, true, {Kind::store_bookmark, 3}, ""},
    KeyBinding{SDLK_F6, true, {Kind::store_bookmark, 4}, ""},
    KeyBinding{SDLK_F7, true, {Kind::store_bookmark, 5}, ""},
};
}

std::span<const KeyBinding> key_bindings()
{
    return bindings;
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
