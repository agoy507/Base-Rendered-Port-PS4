#ifndef PORT_BASE_PIXEL_H
#define PORT_BASE_PIXEL_H

#include <stdint.h>

uint32_t port_base_argb_to_abgr(uint32_t argb);
uint32_t port_base_blend_argb_over_abgr(uint32_t source_argb, uint32_t destination_abgr);

#endif
