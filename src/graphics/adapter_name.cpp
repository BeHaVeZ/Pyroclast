#include "adapter_name.hpp"

#include <algorithm>
#include <cctype>

namespace pyroclast
{
namespace
{
bool same_letter(char left, char right)
{
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
}
}

bool adapter_matches(std::string_view adapter_name, std::string_view requested)
{
    return std::search(adapter_name.begin(), adapter_name.end(), requested.begin(), requested.end(), same_letter) != adapter_name.end();
}

bool is_warp_request(std::string_view requested)
{
    return requested.size() == 4 && adapter_matches(requested, "warp");
}
}
