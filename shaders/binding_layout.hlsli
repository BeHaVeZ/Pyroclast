// The binding model every pipeline shares. Included by C++ (src/graphics/binding_layout.hpp) and
// parsed by cmake/Shaders.cmake for the Vulkan shifts: only `static const uint` declarations belong here.

static const uint draw_constant_words = 16;
static const uint frame_constant_bytes = 32768;
static const uint read_buffer_slots = 8;
static const uint texture_slots = 2;
static const uint write_buffer_slots = 6;

static const uint vulkan_b_shift = 0;
static const uint vulkan_t_shift = 2;
static const uint vulkan_u_shift = 12;
static const uint vulkan_s_shift = 18;
