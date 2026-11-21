#ifndef HMBTN_PIXEL_H
#define HMBTN_PIXEL_H

#include <stdint.h>

uint32_t hmbtn_argb_to_abgr(uint32_t argb);
uint32_t hmbtn_blend_argb_over_abgr(uint32_t source_argb, uint32_t destination_abgr);

#endif
