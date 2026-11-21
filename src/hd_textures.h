#ifndef HM_HD_TEXTURES_H
#define HM_HD_TEXTURES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HDPK_MAGIC   0x4B504448u /* 'HDPK' */
#define HDPK_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_textures;
    uint32_t num_palettes;
    uint32_t num_rects;
    uint32_t num_entries;
    uint64_t qoi_blob_size;
} HDPakHeader; /* 32 bytes */

typedef struct {
    uint64_t qoi_offset;
    uint32_t qoi_size;
    uint16_t hd_w;
    uint16_t hd_h;
} HDPakTex; /* 16 bytes */

typedef struct {
    uint64_t pal_hash;
    uint32_t first_rect;
    uint16_t rect_count;
    uint8_t  bpp;
    uint8_t  pal_min;
    uint16_t pal_words;
    uint16_t pad;
    uint32_t pad2;
} HDPakPal; /* 24 bytes */

typedef struct {
    uint32_t first_entry;
    uint16_t entry_count;
    uint16_t off_x;
    uint16_t off_y;
    uint16_t orig_w;
    uint16_t orig_h;
    uint16_t pad;
} HDPakRect; /* 16 bytes */

typedef struct {
    uint64_t tex_hash;
    uint32_t tex_id;
    uint32_t pad;
} HDPakEntry; /* 16 bytes */

/* Load hd_textures.pak into memory and patch HarvestMoonBTN.exe runtime hooks.
 * Returns 0 on success, negative on error (non-fatal: game renders normally without HD). */
int hd_textures_init(const char *pak_path, uint8_t *mapped_base);

/* Release allocated HD pack buffers and LRU caches. */
void hd_textures_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* HM_HD_TEXTURES_H */
