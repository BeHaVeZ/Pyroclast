#include "image_compare.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdio>
#include <optional>
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
    double min_within_one = 1.0;
    if (argc < 3)
    {
        std::fputs("Usage: ImageDiff a.png b.png [--max-level=N] [--min-within-one=F]\n"
            "  --max-level=N       Largest allowed channel difference (default 0).\n"
            "  --min-within-one=F  Required fraction of pixels whose channels differ by at most one (default 1).\n", stderr);
        return 2;
    }
    for (int index = 3; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (!parse_option(argument, "--max-level=", max_level) && !parse_option(argument, "--min-within-one=", min_within_one))
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
    const double within_one = 1.0 - static_cast<double>(difference.beyond_one_level) / static_cast<double>(difference.pixels);
    const bool pass = difference.max_level <= max_level && within_one >= min_within_one;
    std::printf("%s size=%dx%d differing=%llu max=%u mean=%.4f p99=%u within_one=%.5f\n", pass ? "PASS" : "FAIL",
        first->width, first->height, static_cast<unsigned long long>(difference.differing_pixels),
        difference.max_level, difference.mean_level, difference.p99_level, within_one);
    return pass ? 0 : 1;
}
