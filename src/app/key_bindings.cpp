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
