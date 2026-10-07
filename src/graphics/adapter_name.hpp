#pragma once

#include <string_view>

namespace pyroclast
{
// Case-insensitive substring match used to pick a GPU by GraphicsDeviceOptions::adapter.
bool adapter_matches(std::string_view adapter_name, std::string_view requested);
bool is_warp_request(std::string_view requested);
}
