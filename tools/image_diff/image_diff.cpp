#include "image_compare.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

std::optional<Image> load(const char* path)
{
    SDL_Surface* loaded = SDL_LoadPNG(path);
    SDL_Surface* converted = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(loaded);
    if (!converted)
    {
        std::fprintf(stderr, "Cannot load %s: %s\n", path, SDL_GetError());
        return std::nullopt;
    }
    Image image{converted->w, converted->h, {}};
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    for (int row = 0; row < image.height; ++row)
    {
        const auto* source = static_cast<const std::uint8_t*>(converted->pixels) + static_cast<std::size_t>(row) * converted->pitch;
        std::copy(source, source + static_cast<std::size_t>(image.width) * 4,
            image.rgba.begin() + static_cast<std::ptrdiff_t>(row) * image.width * 4);
    }
    SDL_DestroySurface(converted);
    return image;
}

bool save_difference(const Image& first, const Image& second, const std::string& path)
{
    std::vector<std::uint8_t> pixels(first.rgba.size());
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        const int level = std::abs(int{first.rgba[index]} - int{second.rgba[index]});
        pixels[index] = index % 4 == 3 ? 255 : static_cast<std::uint8_t>(std::min(255, level * 40));
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(first.width, first.height, SDL_PIXELFORMAT_RGBA32, pixels.data(), first.width * 4);
    const bool saved = surface && SDL_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}

template <typename Number>
bool parse_option(std::string_view argument, std::string_view name, Number& value)
{
    if (!argument.starts_with(name)) return false;
    const auto text = argument.substr(name.size());
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}

int main(int argc, char** argv)
{
    unsigned max_level = 0;
    unsigned max_outliers = 0;
    double min_within_one = 1.0;
    if (argc < 3)
    {
        std::fputs("Usage: ImageDiff a.png b.png [--max-level=N] [--max-outliers=N] [--min-within-one=F] [--difference=out.png]\n"
            "  --max-level=N       Largest allowed channel difference (default 0).\n"
            "  --max-outliers=N    Pixels allowed to exceed --max-level (default 0).\n"
            "  --min-within-one=F  Required fraction of pixels whose channels differ by at most one (default 1).\n"
            "  --difference=FILE   Write the per-channel difference, scaled by 40, as a PNG.\n", stderr);
        return 2;
    }
    std::string difference_path;
    for (int index = 3; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument.starts_with("--difference="))
        {
            difference_path = argument.substr(std::string_view("--difference=").size());
            continue;
        }
        if (!parse_option(argument, "--max-level=", max_level) && !parse_option(argument, "--max-outliers=", max_outliers) &&
            !parse_option(argument, "--min-within-one=", min_within_one))
        {
            std::fprintf(stderr, "Unknown or malformed option '%s'.\n", argv[index]);
            return 2;
        }
    }
    const auto first = load(argv[1]);
    const auto second = load(argv[2]);
    if (!first || !second) return 2;
    if (first->width != second->width || first->height != second->height)
    {
        std::fprintf(stderr, "Size mismatch: %dx%d vs %dx%d\n", first->width, first->height, second->width, second->height);
        return 2;
    }
    const auto difference = pyroclast::tools::compare_rgba(first->rgba, second->rgba);
    if (!difference_path.empty() && !save_difference(*first, *second, difference_path))
    {
        std::fprintf(stderr, "Cannot write %s: %s\n", difference_path.c_str(), SDL_GetError());
        return 2;
    }
    const double within_one = 1.0 - static_cast<double>(difference.beyond_one_level) / static_cast<double>(difference.pixels);
    const auto outliers = difference.above(max_level);
    const bool pass = outliers <= max_outliers && within_one >= min_within_one;
    std::printf("%s size=%dx%d differing=%llu max=%u mean=%.4f p99=%u within_one=%.5f outliers=%llu\n", pass ? "PASS" : "FAIL",
        first->width, first->height, static_cast<unsigned long long>(difference.differing_pixels),
        difference.max_level, difference.mean_level, difference.p99_level, within_one, static_cast<unsigned long long>(outliers));
    return pass ? 0 : 1;
}
