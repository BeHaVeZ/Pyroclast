#pragma once

#include "../tools/debug_ui.hpp"

#include <SDL3/SDL_keycode.h>

#include <optional>
#include <span>
#include <string_view>

namespace pyroclast
{
struct KeyBinding
{
    SDL_Keycode key = 0;
    bool ctrl = false;
    UiCommand command{};
    std::string_view label;
};

std::span<const KeyBinding> key_bindings();

std::string_view controls_summary();

std::optional<UiCommand> key_command(SDL_Keycode key, SDL_Keymod modifiers);
}
