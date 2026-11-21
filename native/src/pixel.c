#include "hmbtn/pixel.h"

uint32_t hmbtn_argb_to_abgr(uint32_t argb) {
    return (argb & 0xFF00FF00u) |
           ((argb & 0x00FF0000u) >> 16u) |
           ((argb & 0x000000FFu) << 16u);
}

static uint32_t blend_channel(uint32_t source, uint32_t destination, uint32_t alpha) {
    return (source * alpha + destination * (255u - alpha) + 127u) / 255u;
}

uint32_t hmbtn_blend_argb_over_abgr(uint32_t source_argb, uint32_t destination_abgr) {
    const uint32_t alpha = source_argb >> 24u;
    if (alpha == 0u) {
        return destination_abgr;
    }
    if (alpha == 255u) {
        return hmbtn_argb_to_abgr(source_argb);
    }

    const uint32_t source_red = (source_argb >> 16u) & 0xFFu;
    const uint32_t source_green = (source_argb >> 8u) & 0xFFu;
    const uint32_t source_blue = source_argb & 0xFFu;
    const uint32_t destination_red = destination_abgr & 0xFFu;
    const uint32_t destination_green = (destination_abgr >> 8u) & 0xFFu;
    const uint32_t destination_blue = (destination_abgr >> 16u) & 0xFFu;

    const uint32_t red = blend_channel(source_red, destination_red, alpha);
    const uint32_t green = blend_channel(source_green, destination_green, alpha);
    const uint32_t blue = blend_channel(source_blue, destination_blue, alpha);

    return 0xFF000000u | (blue << 16u) | (green << 8u) | red;
}
