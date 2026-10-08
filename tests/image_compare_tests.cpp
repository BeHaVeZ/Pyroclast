#include "image_diff/image_compare.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    using pyroclast::tools::compare_rgba;
    int failures = 0;
    const auto check = [&failures](bool value, const char* name) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };

    std::vector<std::uint8_t> first(100 * 4, 10);
    auto second = first;
    const auto same = compare_rgba(first, second);
    check(same.pixels == 100 && same.differing_pixels == 0 && same.max_level == 0 && same.mean_level == 0 && same.p99_level == 0,
        "identical images");

    second[3] = 200;
    check(compare_rgba(first, second).differing_pixels == 0, "alpha is ignored");

    second[0] = 11;
    second[4 * 7 + 2] = 13;
    const auto small = compare_rgba(first, second);
    check(small.differing_pixels == 2 && small.beyond_one_level == 1 && small.max_level == 3, "counts and maximum");
    check(std::abs(small.mean_level - 4.0 / 300.0) < 1e-12, "mean over RGB channels");
    check(small.p99_level == 1, "p99 includes the second largest of 100 pixels");
    check(small.histogram[0] == 98 && small.histogram[1] == 1 && small.histogram[3] == 1 && small.above(1) == 1 &&
        small.above(2) == 1 && small.above(3) == 0, "pixels counted by their largest difference");

    second = first;
    second[0] = 250;
    check(compare_rgba(first, second).p99_level == 0, "one outlier stays above p99");
    check(compare_rgba({}, {}).pixels == 0, "empty images");

    using pyroclast::tools::box_blur_rgba;
    std::vector<std::uint8_t> dot(3 * 3 * 4, 0);
    dot[(1 * 3 + 1) * 4] = 90;
    for (int pixel = 0; pixel < 9; ++pixel) dot[pixel * 4 + 3] = 255;
    const auto spread = box_blur_rgba(dot, 3, 3, 1);
    check(spread[(1 * 3 + 1) * 4] == 10 && spread[0] == 23 && spread[(0 * 3 + 1) * 4] == 15 && spread[3] == 255 && spread[1] == 0,
        "a dot spreads over its clipped window");
    check(box_blur_rgba(first, 10, 10, 2) == first, "a flat image stays flat");
    check(box_blur_rgba(dot, 3, 3, 0) == dot, "radius 0 copies");
    return failures == 0 ? 0 : 1;
}
