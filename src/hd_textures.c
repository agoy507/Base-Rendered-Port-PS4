#define _GNU_SOURCE
#include "hd_textures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <pthread.h>
#endif

#define XXH_INLINE_ALL
#include "xxhash.h"

/* ============================================================================
 * HarvestMoonBTN.exe (ImageBase = 0x140000000) Internal Layout & RVAs
 * ============================================================================ */

typedef struct {
    int32_t x, y, r, g, b, u, v;
} HMVert; /* 28 bytes (0x1c) */

typedef struct {
    int32_t   active;  /* +0x00 */
    int32_t   x, y;    /* +0x04, +0x08 */
    int32_t   w, h;    /* +0x0c, +0x10 */
    int32_t   wide;    /* +0x14 */
    int32_t   scale;   /* +0x18 */
    int32_t   out_w;   /* +0x1c */
    int32_t   out_h;   /* +0x20 */
    int32_t   pad24;   /* +0x24 */
    uint16_t *buf;     /* +0x28 */
    uint64_t  pad30;   /* +0x30 */
} HMCanvas;            /* 56 bytes (0x38) */

typedef struct {
    int32_t   kind;        /* +0x00: 0=tri, 1=sprite/rect, 2=line */
    int32_t   pad04;       /* +0x04 */
    HMCanvas *c;           /* +0x08 */
    int32_t   hx1, hy1;    /* +0x10, +0x14 */
    int32_t   hx2, hy2;    /* +0x18, +0x1c */
    int32_t   mask_set;    /* +0x20 */
    int32_t   mask_check;  /* +0x24 */
    int32_t   tw_mx, tw_my;/* +0x28, +0x2c */
    int32_t   tw_ox, tw_oy;/* +0x30, +0x34 */
    int32_t   gouraud;     /* +0x38 */
    int32_t   textured;    /* +0x3c */
    int32_t   raw_tex;     /* +0x40 */
    int32_t   semi_trans;  /* +0x44 */
    int32_t   semi_mode;   /* +0x48 */
    int32_t   tpage_x;     /* +0x4c */
    int32_t   tpage_y;     /* +0x50 */
    int32_t   texmode;     /* +0x54 (0=4bpp, 1=8bpp, 2=16bpp) */
    int32_t   clut_x;      /* +0x58 */
    int32_t   clut_y;      /* +0x5c */
    HMVert    v[3];        /* +0x60..+0xb3 */
    int32_t   sx, sy;      /* +0xb4, +0xb8 */
    int32_t   sw, sh;      /* +0xbc, +0xc0 */
    int32_t   u0, v0;      /* +0xc4, +0xc8 */
    int32_t   flip_x;      /* +0xcc */
    int32_t   flip_y;      /* +0xd0 */
    int32_t   r, g, b;     /* +0xd4, +0xd8, +0xdc */
} HMHqCmd;                 /* 224 bytes (0xe0) */

#define RVA_GPU_VRAM        0x12ccba0u
#define RVA_GPU_VRAM_WRITES 0x12ccb88u
#define RVA_GPU_WIDE        0x12ccb84u
#define RVA_GPU_PILLARBOX   0x12ccb80u
#define RVA_GPU_DISP_X      0x12ccb94u
#define RVA_GPU_DISP_Y      0x12ccb90u
#define RVA_GPU_DISP_ON     0x02d44c0u
#define RVA_GPU_DISP_W      0x02d44c8u
#define RVA_GPU_DISP_H      0x02d44c4u
#define RVA_GPU_SCALE       0x02d44bcu

#define RVA_NWORKERS        0x16ed3c8u
#define RVA_FLUSH_ONLY      0x16ed460u
#define RVA_HQ_N            0x16ed46cu
#define RVA_HQ_PTR          0x16ed470u
#define RVA_CANV            0x16f04c0u
#define RVA_LUT             0x16ccba0u
#define RVA_OUT_PTR         0x16ecba0u
#define RVA_OUT_SIZE        0x16ecba8u

#define RVA_FN_CANVAS_FILL  0x000c9d0u
#define RVA_FN_CANVAS_TOUCH 0x000cb60u
#define RVA_FN_HQ_FLUSH     0x0011550u
#define RVA_FN_COPY_VRAM    0x00117a0u
#define RVA_FN_CANVAS_FIND  0x0011a70u
#define RVA_FN_GPU_LOAD     0x0012790u
#define RVA_FN_GPU_CLEAR    0x00129a0u
#define RVA_FN_GPU_FRAME    0x0014b00u

#define MAX_CANVASES        6
#define VRAM_WIDTH          1024
#define VRAM_HEIGHT         512
#define NUM_VRAM_PAGES      32

#define HD_CACHE_MAX_BYTES  (256u * 1024u * 1024u)
#define PAL_LRU_SIZE        256
#define MAX_MATCHED_PALS    4

#ifdef _WIN32
#define HM_MS_ABI
#else
#define HM_MS_ABI __attribute__((ms_abi))
#endif

typedef void     (HM_MS_ABI *fn_gpu_load_t)(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *src);
typedef void     (HM_MS_ABI *fn_copy_vram_t)(const uint32_t *w);
typedef void     (HM_MS_ABI *fn_gpu_clear_t)(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b);
typedef void     (HM_MS_ABI *fn_canvas_fill_t)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
typedef void     (HM_MS_ABI *fn_canvas_touch_t)(int32_t x, int32_t y, int32_t w, int32_t h);
typedef void     (HM_MS_ABI *fn_hq_flush_t)(void);
typedef uint32_t*(HM_MS_ABI *fn_gpu_frame_t)(int32_t *out_w, int32_t *out_h);
typedef HMCanvas*(HM_MS_ABI *fn_canvas_find_t)(int32_t x, int32_t y, int32_t w, int32_t h);

/* ============================================================================
 * Global State
 * ============================================================================ */

typedef struct {
    uint8_t  bpp;
    uint16_t pal_words;
    uint32_t first_pal;
    uint32_t count;
} PalSubRange;

typedef struct DecodedTexNode {
    uint32_t *argb;       /* 0xAARRGGBB */
    uint16_t  w, h;
    size_t    byte_size;
    int32_t   tex_id;
    struct DecodedTexNode *prev, *next;
} DecodedTexNode;

typedef struct {
    uint64_t key; /* packed (bpp, tpage_x, tpage_y, clut_x, clut_y) | valid bit */
    uint32_t clut_gen;
    uint32_t last_use;
    uint8_t  num_pals;
    uint32_t pal_idx[MAX_MATCHED_PALS];
    uint8_t  valid;
} PalMatchCacheEntry;

typedef struct {
    HMHqCmd  cmd;
    int32_t  tex_id;
    uint16_t off_x, off_y, orig_w, orig_h;
} SnapCmd;

static struct {
    int            initialized;
    uint8_t       *base;
    uint16_t      *gpu_vram;
    uint32_t      *gpu_vram_writes;
    int32_t       *gpu_pillarbox;
    int32_t       *gpu_wide;
    int32_t       *gpu_disp_x;
    int32_t       *gpu_disp_y;
    int32_t       *gpu_disp_on;
    int32_t       *gpu_disp_w;
    int32_t       *gpu_disp_h;
    int32_t       *gpu_scale;
    int32_t       *nworkers;
    HMCanvas     **flush_only;
    int32_t       *hq_n;
    HMHqCmd      **hq_ptr;
    HMCanvas      *canv;
    uint32_t      *lut;
    uint32_t     **out_ptr;
    size_t        *out_size;
    fn_canvas_find_t fn_canvas_find;

    /* Loaded HDPak image in RAM */
    uint8_t          *pak_buf;
    size_t            pak_size;
    HDPakHeader       hdr;
    const HDPakTex   *texs;
    const HDPakPal   *pals;
    const HDPakRect  *rects;
    const HDPakEntry *entries;
    const uint8_t    *qoi_blob;

    PalSubRange       subranges[64];
    uint32_t          num_subranges;

    /* VRAM generation counters per 64x256 VRAM page */
    uint32_t          vram_page_gen[NUM_VRAM_PAGES];
    uint32_t          last_vram_writes;

    /* Decoded QOI LRU cache */
    DecodedTexNode  **tex_nodes; /* size hdr.num_textures */
    DecodedTexNode   *lru_head;
    DecodedTexNode   *lru_tail;
    size_t            lru_bytes;

    /* Palette bucket lookup cache */
    PalMatchCacheEntry pal_cache[PAL_LRU_SIZE];
    uint32_t           pal_clock;

    /* Per-canvas 32-bit HD overlay framebuffers */
    uint32_t         *hd_buf[MAX_CANVASES];
    int32_t           hd_w[MAX_CANVASES];
    int32_t           hd_h[MAX_CANVASES];
    uint8_t           hd_has_pixels[MAX_CANVASES];

    /* Queue snapshot scratch buffer */
    SnapCmd          *snap;
    int32_t           snap_cap;

    /* Trampoline page */
    uint8_t          *tramp_mem;
    size_t            tramp_used;

    fn_gpu_load_t     orig_gpu_load;
    fn_copy_vram_t    orig_copy_vram;
    fn_gpu_clear_t    orig_gpu_clear;
    fn_canvas_fill_t  orig_canvas_fill;
    fn_canvas_touch_t orig_canvas_touch;
    fn_hq_flush_t     orig_hq_flush;
    fn_gpu_frame_t    orig_gpu_frame;
} g_hd;

/* ============================================================================
 * QOI Decoder (RGBA -> 0xAARRGGBB)
 * ============================================================================ */

static uint32_t *qoi_decode_argb(const uint8_t *data, uint32_t size, uint16_t exp_w, uint16_t exp_h) {
    if (!data || size < 22) return NULL;
    if (data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f') return NULL;

    uint32_t w = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                 ((uint32_t)data[6] << 8)  |  (uint32_t)data[7];
    uint32_t h = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                 ((uint32_t)data[10] << 8) |  (uint32_t)data[11];
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return NULL;
    if ((exp_w && w != exp_w) || (exp_h && h != exp_h)) return NULL;

    size_t npix = (size_t)w * (size_t)h;
    uint32_t *out = (uint32_t *)malloc(npix * sizeof(uint32_t));
    if (!out) return NULL;

    uint8_t index[64][4];
    memset(index, 0, sizeof(index));

    uint8_t r = 0, g = 0, b = 0, a = 255;
    size_t p = 14;
    size_t end = (size >= 8) ? (size_t)(size - 8) : size;
    size_t px_pos = 0;

    while (px_pos < npix) {
        if (p < end) {
            uint8_t b1 = data[p++];
            if (b1 == 0xFE) {
                if (p + 3 > end) break;
                r = data[p++]; g = data[p++]; b = data[p++];
                uint32_t idx = (r * 3u + g * 5u + b * 7u + a * 11u) & 63u;
                index[idx][0] = r; index[idx][1] = g; index[idx][2] = b; index[idx][3] = a;
                out[px_pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else if (b1 == 0xFF) {
                if (p + 4 > end) break;
                r = data[p++]; g = data[p++]; b = data[p++]; a = data[p++];
                uint32_t idx = (r * 3u + g * 5u + b * 7u + a * 11u) & 63u;
                index[idx][0] = r; index[idx][1] = g; index[idx][2] = b; index[idx][3] = a;
                out[px_pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                uint8_t tag = b1 & 0xC0u;
                if (tag == 0x00u) {
                    uint8_t idx = b1 & 0x3Fu;
                    r = index[idx][0]; g = index[idx][1]; b = index[idx][2]; a = index[idx][3];
                    out[px_pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                } else if (tag == 0x40u) {
                    r = (uint8_t)(r + ((int)((b1 >> 4) & 3u) - 2));
                    g = (uint8_t)(g + ((int)((b1 >> 2) & 3u) - 2));
                    b = (uint8_t)(b + ((int)(b1 & 3u) - 2));
                    uint32_t idx = (r * 3u + g * 5u + b * 7u + a * 11u) & 63u;
                    index[idx][0] = r; index[idx][1] = g; index[idx][2] = b; index[idx][3] = a;
                    out[px_pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                } else if (tag == 0x80u) {
                    if (p >= end) break;
                    uint8_t b2 = data[p++];
                    int vg = (int)(b1 & 0x3Fu) - 32;
                    int dr = vg + ((int)((b2 >> 4) & 0x0Fu) - 8);
                    int db = vg + ((int)(b2 & 0x0Fu) - 8);
                    r = (uint8_t)(r + dr);
                    g = (uint8_t)(g + vg);
                    b = (uint8_t)(b + db);
                    uint32_t idx = (r * 3u + g * 5u + b * 7u + a * 11u) & 63u;
                    index[idx][0] = r; index[idx][1] = g; index[idx][2] = b; index[idx][3] = a;
                    out[px_pos++] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                } else {
                    uint32_t run = (uint32_t)(b1 & 0x3Fu) + 1u;
                    uint32_t px = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    while (run-- && px_pos < npix) {
                        out[px_pos++] = px;
                    }
                }
            }
        } else {
            uint32_t px = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            while (px_pos < npix) out[px_pos++] = px;
            break;
        }
    }

    if (px_pos != npix) {
        free(out);
        return NULL;
    }
    return out;
}

/* ============================================================================
 * Decoded Texture LRU Cache
 * ============================================================================ */

static void lru_unlink(DecodedTexNode *n) {
    if (n->prev) n->prev->next = n->next; else g_hd.lru_head = n->next;
    if (n->next) n->next->prev = n->prev; else g_hd.lru_tail = n->prev;
    n->prev = NULL;
    n->next = NULL;
}

static void lru_push_front(DecodedTexNode *n) {
    n->prev = NULL;
    n->next = g_hd.lru_head;
    if (g_hd.lru_head) g_hd.lru_head->prev = n;
    g_hd.lru_head = n;
    if (!g_hd.lru_tail) g_hd.lru_tail = n;
}

static const uint32_t *get_decoded_texture(int32_t tex_id, uint16_t *out_w, uint16_t *out_h) {
    if (tex_id < 0 || (uint32_t)tex_id >= g_hd.hdr.num_textures) return NULL;
    DecodedTexNode *n = g_hd.tex_nodes[tex_id];
    if (n) {
        if (n != g_hd.lru_head) {
            lru_unlink(n);
            lru_push_front(n);
        }
        *out_w = n->w;
        *out_h = n->h;
        return n->argb;
    }

    const HDPakTex *t = &g_hd.texs[tex_id];
    if (t->qoi_offset + t->qoi_size > g_hd.hdr.qoi_blob_size) return NULL;
    uint32_t *argb = qoi_decode_argb(g_hd.qoi_blob + t->qoi_offset, t->qoi_size, t->hd_w, t->hd_h);
    if (!argb) return NULL;

    size_t sz = (size_t)t->hd_w * (size_t)t->hd_h * sizeof(uint32_t);
    while (g_hd.lru_bytes + sz > HD_CACHE_MAX_BYTES && g_hd.lru_tail) {
        DecodedTexNode *vic = g_hd.lru_tail;
        lru_unlink(vic);
        g_hd.lru_bytes -= vic->byte_size;
        g_hd.tex_nodes[vic->tex_id] = NULL;
        free(vic->argb);
        free(vic);
    }

    n = (DecodedTexNode *)malloc(sizeof(DecodedTexNode));
    if (!n) {
        free(argb);
        return NULL;
    }
    n->argb = argb;
    n->w = t->hd_w;
    n->h = t->hd_h;
    n->byte_size = sz;
    n->tex_id = tex_id;
    n->prev = n->next = NULL;
    lru_push_front(n);
    g_hd.lru_bytes += sz;
    g_hd.tex_nodes[tex_id] = n;

    *out_w = n->w;
    *out_h = n->h;
    return n->argb;
}

/* ============================================================================
 * VRAM Dirty Tracking & DuckStation Texture Matcher
 * ============================================================================ */

static void bump_vram_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = x + w - 1;
    int32_t y1 = y + h - 1;
    if (x1 >= VRAM_WIDTH) x1 = VRAM_WIDTH - 1;
    if (y1 >= VRAM_HEIGHT) y1 = VRAM_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return;

    int32_t px0 = x0 >> 6, px1 = x1 >> 6;
    int32_t py0 = y0 >> 8, py1 = y1 >> 8;
    for (int32_t py = py0; py <= py1; py++) {
        for (int32_t px = px0; px <= px1; px++) {
            g_hd.vram_page_gen[py * 16 + px]++;
        }
    }
}

static void bump_all_vram_pages(void) {
    for (int i = 0; i < NUM_VRAM_PAGES; i++) {
        g_hd.vram_page_gen[i]++;
    }
}

static void sync_vram_generation(void) {
    if (!g_hd.gpu_vram_writes) return;
    uint32_t cur = *g_hd.gpu_vram_writes;
    if (cur != g_hd.last_vram_writes) {
        bump_all_vram_pages();
        g_hd.last_vram_writes = cur;
    }
}

static int bsearch_pal(uint32_t first, uint32_t count, uint64_t pal_hash) {
    uint32_t lo = first, hi = first + count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        uint64_t mh = g_hd.pals[mid].pal_hash;
        if (mh < pal_hash) lo = mid + 1;
        else if (mh > pal_hash) hi = mid;
        else return (int)mid;
    }
    return -1;
}

static int bsearch_entry(uint32_t first, uint16_t count, uint64_t tex_hash) {
    uint32_t lo = first, hi = first + (uint32_t)count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        uint64_t mh = g_hd.entries[mid].tex_hash;
        if (mh < tex_hash) lo = mid + 1;
        else if (mh > tex_hash) hi = mid;
        else return (int)g_hd.entries[mid].tex_id;
    }
    return -1;
}

static const PalMatchCacheEntry *lookup_pal_buckets(uint8_t bpp, uint16_t clut_x, uint16_t clut_y) {
    if (clut_x >= VRAM_WIDTH || clut_y >= VRAM_HEIGHT) return NULL;
    uint32_t clut_page = ((uint32_t)(clut_y >> 8) * 16u) + (uint32_t)(clut_x >> 6);
    uint32_t cgen = g_hd.vram_page_gen[clut_page];

    uint64_t key = ((uint64_t)bpp << 32) | ((uint64_t)clut_y << 16) | (uint64_t)clut_x;
    PalMatchCacheEntry *empty_or_old = &g_hd.pal_cache[0];
    uint32_t best_lru = UINT32_MAX;

    for (int i = 0; i < PAL_LRU_SIZE; i++) {
        PalMatchCacheEntry *e = &g_hd.pal_cache[i];
        if (e->valid && e->key == key) {
            if (e->clut_gen == cgen) {
                e->last_use = ++g_hd.pal_clock;
                return e->num_pals > 0 ? e : NULL;
            }
            empty_or_old = e;
            break;
        }
        if (!e->valid) {
            empty_or_old = e;
            best_lru = 0;
        } else if (best_lru != 0 && e->last_use < best_lru) {
            best_lru = e->last_use;
            empty_or_old = e;
        }
    }

    const uint16_t *pal_ptr = &g_hd.gpu_vram[(size_t)clut_y * VRAM_WIDTH + (size_t)clut_x];
    uint32_t avail_words = VRAM_WIDTH - clut_x;

    PalMatchCacheEntry *dst = empty_or_old;
    dst->valid = 1;
    dst->key = key;
    dst->clut_gen = cgen;
    dst->last_use = ++g_hd.pal_clock;
    dst->num_pals = 0;

    for (uint32_t s = 0; s < g_hd.num_subranges; s++) {
        const PalSubRange *sr = &g_hd.subranges[s];
        if (sr->bpp != bpp) continue;
        if (sr->pal_words > avail_words) continue;
        uint64_t ph = XXH3_64bits(pal_ptr, (size_t)sr->pal_words * sizeof(uint16_t));
        int pidx = bsearch_pal(sr->first_pal, sr->count, ph);
        if (pidx >= 0 && dst->num_pals < MAX_MATCHED_PALS) {
            dst->pal_idx[dst->num_pals++] = (uint32_t)pidx;
        }
    }

    return dst->num_pals > 0 ? dst : NULL;
}

static uint64_t hash_vram_rect(uint8_t bpp, int32_t tpage_x, int32_t tpage_y,
                               uint16_t off_x, uint16_t off_y, uint16_t orig_w, uint16_t orig_h) {
    uint32_t hw_x = (bpp == 4) ? ((uint32_t)off_x >> 2) : ((uint32_t)off_x >> 1);
    uint32_t hw_w = (bpp == 4) ? ((uint32_t)orig_w >> 2) : ((uint32_t)orig_w >> 1);
    int32_t left = tpage_x + (int32_t)hw_x;
    int32_t top  = tpage_y + (int32_t)off_y;
    if (left < 0 || left + (int32_t)hw_w > VRAM_WIDTH) return 0;
    if (top  < 0 || top  + (int32_t)orig_h > VRAM_HEIGHT) return 0;

    XXH3_state_t st;
    XXH3_64bits_reset(&st);
    const uint16_t *row = &g_hd.gpu_vram[(size_t)top * VRAM_WIDTH + (size_t)left];
    size_t row_bytes = (size_t)hw_w * sizeof(uint16_t);
    for (uint16_t y = 0; y < orig_h; y++) {
        XXH3_64bits_update(&st, row, row_bytes);
        row += VRAM_WIDTH;
    }
    return XXH3_64bits_digest(&st);
}

static void compute_cmd_uv_bounds(const HMHqCmd *cmd, int32_t *u_lo, int32_t *v_lo, int32_t *u_hi, int32_t *v_hi) {
    int32_t x0, x1, y0, y1;
    if (cmd->kind == 1) {
        if (cmd->tw_mx) {
            int32_t mask = (~(cmd->tw_mx * 8)) & 0xFF;
            int32_t base = (cmd->tw_ox & cmd->tw_mx) * 8;
            x0 = base;
            x1 = base | mask;
        } else {
            x0 = cmd->flip_x ? (cmd->u0 - (cmd->sw - 1)) : cmd->u0;
            x1 = cmd->flip_x ? cmd->u0 : (cmd->u0 + cmd->sw - 1);
        }
        if (cmd->tw_my) {
            int32_t mask = (~(cmd->tw_my * 8)) & 0xFF;
            int32_t base = (cmd->tw_oy & cmd->tw_my) * 8;
            y0 = base;
            y1 = base | mask;
        } else {
            y0 = cmd->flip_y ? (cmd->v0 - (cmd->sh - 1)) : cmd->v0;
            y1 = cmd->flip_y ? cmd->v0 : (cmd->v0 + cmd->sh - 1);
        }
    } else {
        x0 = x1 = cmd->v[0].u;
        y0 = y1 = cmd->v[0].v;
        for (int i = 1; i < 3; i++) {
            if (cmd->v[i].u < x0) x0 = cmd->v[i].u;
            if (cmd->v[i].u > x1) x1 = cmd->v[i].u;
            if (cmd->v[i].v < y0) y0 = cmd->v[i].v;
            if (cmd->v[i].v > y1) y1 = cmd->v[i].v;
        }
        if (cmd->tw_mx) {
            int32_t mask = (~(cmd->tw_mx * 8)) & 0xFF;
            int32_t base = (cmd->tw_ox & cmd->tw_mx) * 8;
            x0 = base; x1 = base | mask;
        }
        if (cmd->tw_my) {
            int32_t mask = (~(cmd->tw_my * 8)) & 0xFF;
            int32_t base = (cmd->tw_oy & cmd->tw_my) * 8;
            y0 = base; y1 = base | mask;
        }
    }
    if (x0 < 0) x0 = 0;
    if (x1 > 255) x1 = 255;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    *u_lo = x0; *v_lo = y0; *u_hi = x1; *v_hi = y1;
}

typedef struct {
    uint32_t rect_idx;
    int32_t  area;
} RectCand;

static int32_t resolve_cmd_hd_texture(const HMHqCmd *cmd, uint16_t *out_ox, uint16_t *out_oy,
                                      uint16_t *out_ow, uint16_t *out_oh) {
    if (!cmd->textured || cmd->texmode > 1) return -1;
    if (cmd->kind != 0 && cmd->kind != 1) return -1;
    if (cmd->kind == 1 && (cmd->sw <= 0 || cmd->sh <= 0)) return -1;

    uint8_t bpp = (cmd->texmode == 0) ? 4 : 8;
    const PalMatchCacheEntry *pm = lookup_pal_buckets(bpp, (uint16_t)cmd->clut_x, (uint16_t)cmd->clut_y);
    if (!pm) return -1;

    int32_t u_lo, v_lo, u_hi, v_hi;
    compute_cmd_uv_bounds(cmd, &u_lo, &v_lo, &u_hi, &v_hi);
    if (u_lo > u_hi || v_lo > v_hi) return -1;

    RectCand cands[256];
    int n_cands = 0;

    for (uint8_t pi = 0; pi < pm->num_pals; pi++) {
        const HDPakPal *pal = &g_hd.pals[pm->pal_idx[pi]];
        uint32_t end_r = pal->first_rect + pal->rect_count;
        for (uint32_t ri = pal->first_rect; ri < end_r; ri++) {
            const HDPakRect *r = &g_hd.rects[ri];
            int32_t rx0 = r->off_x, ry0 = r->off_y;
            int32_t rx1 = rx0 + r->orig_w - 1;
            int32_t ry1 = ry0 + r->orig_h - 1;
            int32_t ix0 = (u_lo > rx0) ? u_lo : rx0;
            int32_t iy0 = (v_lo > ry0) ? v_lo : ry0;
            int32_t ix1 = (u_hi < rx1) ? u_hi : rx1;
            int32_t iy1 = (v_hi < ry1) ? v_hi : ry1;
            if (ix0 <= ix1 && iy0 <= iy1) {
                if (n_cands < 256) {
                    cands[n_cands].rect_idx = ri;
                    cands[n_cands].area = (ix1 - ix0 + 1) * (iy1 - iy0 + 1);
                    n_cands++;
                }
            }
        }
    }

    if (n_cands == 0) return -1;

    /* Sort by descending overlap area (typically < 16 candidates) */
    for (int i = 1; i < n_cands; i++) {
        RectCand tmp = cands[i];
        int j = i - 1;
        while (j >= 0 && cands[j].area < tmp.area) {
            cands[j + 1] = cands[j];
            j--;
        }
        cands[j + 1] = tmp;
    }

    /* Prioritize larger page rects first; try matching tex_hash in VRAM */
    for (int i = 0; i < n_cands; i++) {
        const HDPakRect *r = &g_hd.rects[cands[i].rect_idx];
        uint64_t th = hash_vram_rect(bpp, cmd->tpage_x, cmd->tpage_y,
                                     r->off_x, r->off_y, r->orig_w, r->orig_h);
        if (th == 0) continue;
        int32_t tid = bsearch_entry(r->first_entry, r->entry_count, th);
        if (tid >= 0) {
            *out_ox = r->off_x;
            *out_oy = r->off_y;
            *out_ow = r->orig_w;
            *out_oh = r->orig_h;
            return tid;
        }
    }
    return -1;
}

/* ============================================================================
 * PS1 VRAM Texel Sampling & Blending Helpers
 * ============================================================================ */

static inline uint16_t read_ps1_texel16(const HMHqCmd *cmd, int32_t u, int32_t v) {
    u &= 0xFF;
    v &= 0xFF;
    if (cmd->tw_mx) u = (u & ~(cmd->tw_mx * 8)) | ((cmd->tw_ox & cmd->tw_mx) * 8);
    if (cmd->tw_my) v = (v & ~(cmd->tw_my * 8)) | ((cmd->tw_oy & cmd->tw_my) * 8);
    int32_t vy = (cmd->tpage_y + v) & 511;

    if (cmd->texmode == 0) {
        int32_t vx = (cmd->tpage_x + (u >> 2)) & 1023;
        uint16_t word = g_hd.gpu_vram[(size_t)vy * VRAM_WIDTH + (size_t)vx];
        uint32_t cidx = (word >> ((u & 3) * 4)) & 0xFu;
        int32_t cx = (cmd->clut_x + (int32_t)cidx) & 1023;
        return g_hd.gpu_vram[(size_t)(cmd->clut_y & 511) * VRAM_WIDTH + (size_t)cx];
    } else if (cmd->texmode == 1) {
        int32_t vx = (cmd->tpage_x + (u >> 1)) & 1023;
        uint16_t word = g_hd.gpu_vram[(size_t)vy * VRAM_WIDTH + (size_t)vx];
        uint32_t cidx = (word >> ((u & 1) * 8)) & 0xFFu;
        int32_t cx = (cmd->clut_x + (int32_t)cidx) & 1023;
        return g_hd.gpu_vram[(size_t)(cmd->clut_y & 511) * VRAM_WIDTH + (size_t)cx];
    } else {
        int32_t vx = (cmd->tpage_x + u) & 1023;
        return g_hd.gpu_vram[(size_t)vy * VRAM_WIDTH + (size_t)vx];
    }
}

static inline uint32_t apply_semi_mode(uint32_t bg, uint32_t sr, uint32_t sg, uint32_t sb, int32_t mode) {
    int32_t br = (int32_t)((bg >> 16) & 0xFFu);
    int32_t bgc = (int32_t)((bg >> 8) & 0xFFu);
    int32_t bb = (int32_t)(bg & 0xFFu);
    int32_t fr = (int32_t)sr, fg = (int32_t)sg, fb = (int32_t)sb;
    int32_t r, g, b;

    switch (mode & 3) {
        case 0: r = (br + fr) >> 1; g = (bgc + fg) >> 1; b = (bb + fb) >> 1; break;
        case 1: r = br + fr; g = bgc + fg; b = bb + fb; break;
        case 2: r = br - fr; g = bgc - fg; b = bb - fb; break;
        default: r = br + (fr >> 2); g = bgc + (fg >> 2); b = bb + (fb >> 2); break;
    }
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline uint32_t lerp_argb_over_bg(uint32_t bg, uint32_t fg, uint32_t alpha) {
    if (alpha >= 255u) return fg | 0xFF000000u;
    if (alpha == 0u)   return bg;
    uint32_t inv = 255u - alpha;
    uint32_t br = (bg >> 16) & 0xFFu, bgc = (bg >> 8) & 0xFFu, bb = bg & 0xFFu;
    uint32_t fr = (fg >> 16) & 0xFFu, fgc = (fg >> 8) & 0xFFu, fb = fg & 0xFFu;
    uint32_t r = (br * inv + fr * alpha + 127u) / 255u;
    uint32_t g = (bgc * inv + fgc * alpha + 127u) / 255u;
    uint32_t b = (bb * inv + fb * alpha + 127u) / 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Premultiplied-alpha bilinear sample inside [0..hd_w-1] x [0..hd_h-1] (prevents black fringes) */
static inline uint32_t sample_hd_bilinear(const uint32_t *tex, int32_t hd_w, int32_t hd_h, float fx, float fy) {
    if (fx < 0.0f) fx = 0.0f; else if (fx > (float)(hd_w - 1)) fx = (float)(hd_w - 1);
    if (fy < 0.0f) fy = 0.0f; else if (fy > (float)(hd_h - 1)) fy = (float)(hd_h - 1);

    int32_t x0 = (int32_t)fx;
    int32_t y0 = (int32_t)fy;
    int32_t x1 = (x0 + 1 < hd_w) ? (x0 + 1) : x0;
    int32_t y1 = (y0 + 1 < hd_h) ? (y0 + 1) : y0;

    int32_t wx = (int32_t)((fx - (float)x0) * 256.0f + 0.5f);
    int32_t wy = (int32_t)((fy - (float)y0) * 256.0f + 0.5f);
    int32_t iwx = 256 - wx, iwy = 256 - wy;

    uint32_t p00 = tex[(size_t)y0 * hd_w + x0];
    uint32_t p10 = tex[(size_t)y0 * hd_w + x1];
    uint32_t p01 = tex[(size_t)y1 * hd_w + x0];
    uint32_t p11 = tex[(size_t)y1 * hd_w + x1];

    uint32_t a00 = p00 >> 24, a10 = p10 >> 24, a01 = p01 >> 24, a11 = p11 >> 24;
    if ((a00 | a10 | a01 | a11) == 0u) return 0u;
    if ((p00 & p10 & p01 & p11) >> 24 == 255u && p00 == p10 && p00 == p01 && p00 == p11) return p00;

    uint32_t w00 = (uint32_t)(iwx * iwy);
    uint32_t w10 = (uint32_t)(wx  * iwy);
    uint32_t w01 = (uint32_t)(iwx * wy);
    uint32_t w11 = (uint32_t)(wx  * wy);

    uint32_t wa_sum = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11;
    uint32_t a_out = (wa_sum + 32768u) >> 16;
    if (a_out == 0u) return 0u;

    uint32_t kw00 = w00 * a00, kw10 = w10 * a10, kw01 = w01 * a01, kw11 = w11 * a11;
    uint32_t r_sum = kw00 * ((p00 >> 16) & 0xFFu) + kw10 * ((p10 >> 16) & 0xFFu)
                   + kw01 * ((p01 >> 16) & 0xFFu) + kw11 * ((p11 >> 16) & 0xFFu);
    uint32_t g_sum = kw00 * ((p00 >> 8) & 0xFFu)  + kw10 * ((p10 >> 8) & 0xFFu)
                   + kw01 * ((p01 >> 8) & 0xFFu)  + kw11 * ((p11 >> 8) & 0xFFu);
    uint32_t b_sum = kw00 * (p00 & 0xFFu)         + kw10 * (p10 & 0xFFu)
                   + kw01 * (p01 & 0xFFu)         + kw11 * (p11 & 0xFFu);

    uint32_t half = wa_sum >> 1;
    uint32_t r_out = (r_sum + half) / wa_sum;
    uint32_t g_out = (g_sum + half) / wa_sum;
    uint32_t b_out = (b_sum + half) / wa_sum;
    if (r_out > 255u) r_out = 255u;
    if (g_out > 255u) g_out = 255u;
    if (b_out > 255u) b_out = 255u;
    if (a_out > 255u) a_out = 255u;

    return (a_out << 24) | (r_out << 16) | (g_out << 8) | b_out;
}

/* ============================================================================
 * 32-bit HD Canvas Compositor (hd_buf[0..5])
 * ============================================================================ */

static int get_canvas_index(const HMCanvas *c) {
    if (!c || c < g_hd.canv || c >= g_hd.canv + MAX_CANVASES) return -1;
    return (int)(c - g_hd.canv);
}

static uint32_t *ensure_hd_buf(int cidx, const HMCanvas *c) {
    if (cidx < 0 || cidx >= MAX_CANVASES || !c || c->out_w <= 0 || c->out_h <= 0) return NULL;
    if (!g_hd.hd_buf[cidx] || g_hd.hd_w[cidx] != c->out_w || g_hd.hd_h[cidx] != c->out_h) {
        free(g_hd.hd_buf[cidx]);
        size_t bytes = (size_t)c->out_w * (size_t)c->out_h * sizeof(uint32_t);
        g_hd.hd_buf[cidx] = (uint32_t *)calloc(1, bytes);
        if (!g_hd.hd_buf[cidx]) {
            g_hd.hd_w[cidx] = g_hd.hd_h[cidx] = 0;
            g_hd.hd_has_pixels[cidx] = 0;
            return NULL;
        }
        g_hd.hd_w[cidx] = c->out_w;
        g_hd.hd_h[cidx] = c->out_h;
        g_hd.hd_has_pixels[cidx] = 0;
    }
    return g_hd.hd_buf[cidx];
}

static void hd_clear_canvas_rect(int32_t vx, int32_t vy, int32_t vw, int32_t vh) {
    if (vw <= 0 || vh <= 0 || !g_hd.canv) return;
    int32_t vx2 = vx + vw, vy2 = vy + vh;
    for (int i = 0; i < MAX_CANVASES; i++) {
        if (!g_hd.hd_has_pixels[i] || !g_hd.hd_buf[i]) continue;
        const HMCanvas *c = &g_hd.canv[i];
        if (!c->active || c->scale <= 0) continue;
        int32_t x0 = (vx  - c->x + c->wide) * c->scale;
        int32_t y0 = (vy  - c->y) * c->scale;
        int32_t x1 = (vx2 - c->x + c->wide) * c->scale;
        int32_t y1 = (vy2 - c->y) * c->scale;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > g_hd.hd_w[i]) x1 = g_hd.hd_w[i];
        if (y1 > g_hd.hd_h[i]) y1 = g_hd.hd_h[i];
        if (x0 >= x1 || y0 >= y1) continue;

        if (x0 == 0 && x1 == g_hd.hd_w[i] && y0 == 0 && y1 == g_hd.hd_h[i]) {
            memset(g_hd.hd_buf[i], 0, (size_t)g_hd.hd_w[i] * (size_t)g_hd.hd_h[i] * sizeof(uint32_t));
            g_hd.hd_has_pixels[i] = 0;
        } else {
            size_t span = (size_t)(x1 - x0) * sizeof(uint32_t);
            for (int32_t y = y0; y < y1; y++) {
                memset(&g_hd.hd_buf[i][(size_t)y * g_hd.hd_w[i] + x0], 0, span);
            }
        }
    }
}

static inline void write_hd_pixel(const HMHqCmd *cmd, uint32_t *hd_px, uint16_t ps1_bg16,
                                  uint32_t hd_argb, int32_t tr, int32_t tg, int32_t tb,
                                  int32_t u_ps1, int32_t v_ps1) {
    uint32_t a = hd_argb >> 24;
    if (a == 0u) return;

    uint32_t sr = (hd_argb >> 16) & 0xFFu;
    uint32_t sg = (hd_argb >> 8)  & 0xFFu;
    uint32_t sb = hd_argb & 0xFFu;

    if (!cmd->raw_tex) {
        sr = (sr * (uint32_t)tr) >> 7; if (sr > 255u) sr = 255u;
        sg = (sg * (uint32_t)tg) >> 7; if (sg > 255u) sg = 255u;
        sb = (sb * (uint32_t)tb) >> 7; if (sb > 255u) sb = 255u;
    }

    uint32_t fg = 0xFF000000u | (sr << 16) | (sg << 8) | sb;
    if (cmd->semi_trans) {
        uint16_t t16 = read_ps1_texel16(cmd, u_ps1, v_ps1);
        if (t16 & 0x8000u) {
            uint32_t bg = (*hd_px != 0u) ? *hd_px : g_hd.lut[ps1_bg16 & 0x7FFFu];
            uint32_t blended = apply_semi_mode(bg, sr, sg, sb, cmd->semi_mode);
            *hd_px = (a == 255u) ? blended : lerp_argb_over_bg(bg, blended, a);
            return;
        }
    }

    if (a == 255u) {
        *hd_px = fg;
    } else {
        uint32_t bg = (*hd_px != 0u) ? *hd_px : g_hd.lut[ps1_bg16 & 0x7FFFu];
        *hd_px = lerp_argb_over_bg(bg, fg, a);
    }
}

static void clip_cmd_bounds(const HMHqCmd *cmd, const HMCanvas *c,
                            int32_t *cx0, int32_t *cy0, int32_t *cx1, int32_t *cy1) {
    int32_t x0 = cmd->hx1, y0 = cmd->hy1, x1 = cmd->hx2, y1 = cmd->hy2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->out_w) x1 = c->out_w;
    if (y1 > c->out_h) y1 = c->out_h;
    *cx0 = x0; *cy0 = y0; *cx1 = x1; *cy1 = y1;
}

static void raster_hd_sprite(const SnapCmd *sc, int cidx) {
    const HMHqCmd *cmd = &sc->cmd;
    const HMCanvas *c = cmd->c;
    if (!c || !c->buf || c->scale <= 0 || cmd->sw <= 0 || cmd->sh <= 0) return;

    uint16_t hd_w = 0, hd_h = 0;
    const uint32_t *tex = get_decoded_texture(sc->tex_id, &hd_w, &hd_h);
    if (!tex) return;

    uint32_t *hd_buf = ensure_hd_buf(cidx, c);
    if (!hd_buf) return;

    int32_t scx0, scy0, scx1, scy1;
    clip_cmd_bounds(cmd, c, &scx0, &scy0, &scx1, &scy1);

    int32_t dx0 = (cmd->sx - c->x + c->wide) * c->scale;
    int32_t dy0 = (cmd->sy - c->y) * c->scale;
    int32_t dx1 = dx0 + cmd->sw * c->scale;
    int32_t dy1 = dy0 + cmd->sh * c->scale;

    int32_t x0 = (dx0 > scx0) ? dx0 : scx0;
    int32_t y0 = (dy0 > scy0) ? dy0 : scy0;
    int32_t x1 = (dx1 < scx1) ? dx1 : scx1;
    int32_t y1 = (dy1 < scy1) ? dy1 : scy1;
    if (x0 >= x1 || y0 >= y1) return;

    float inv_scale = 1.0f / (float)c->scale;
    float uscale = (float)hd_w / (float)sc->orig_w;
    float vscale = (float)hd_h / (float)sc->orig_h;
    int32_t ox = (int32_t)sc->off_x, oy = (int32_t)sc->off_y;
    int32_t u_wrap_mask = cmd->tw_mx ? (~(cmd->tw_mx * 8)) : ~0;
    int32_t u_wrap_off  = cmd->tw_mx ? ((cmd->tw_ox & cmd->tw_mx) * 8) : 0;
    int32_t v_wrap_mask = cmd->tw_my ? (~(cmd->tw_my * 8)) : ~0;
    int32_t v_wrap_off  = cmd->tw_my ? ((cmd->tw_oy & cmd->tw_my) * 8) : 0;

    g_hd.hd_has_pixels[cidx] = 1;

    for (int32_t y = y0; y < y1; y++) {
        int32_t py_rel = y - dy0;
        float fy_ps1 = ((float)py_rel + 0.5f) * inv_scale;
        float tv_raw = cmd->flip_y ? ((float)(cmd->v0 + 1) - fy_ps1) : ((float)cmd->v0 + fy_ps1);
        int32_t iv = (int32_t)tv_raw;
        float vf = tv_raw - (float)iv;
        if (cmd->tw_my) iv = (iv & v_wrap_mask) | v_wrap_off;
        iv &= 0xFF;
        float fy_hd = (((float)iv + vf - (float)oy) * vscale) - 0.5f;
        int32_t py_ps1_int = py_rel / c->scale;
        int32_t v_ps1 = cmd->flip_y ? (cmd->v0 - py_ps1_int) : (cmd->v0 + py_ps1_int);

        size_t row_off = (size_t)y * (size_t)c->out_w;
        uint32_t *hd_row = hd_buf + row_off;
        const uint16_t *ps1_row = c->buf + row_off;

        for (int32_t x = x0; x < x1; x++) {
            int32_t px_rel = x - dx0;
            float fx_ps1 = ((float)px_rel + 0.5f) * inv_scale;
            float tu_raw = cmd->flip_x ? ((float)(cmd->u0 + 1) - fx_ps1) : ((float)cmd->u0 + fx_ps1);
            int32_t iu = (int32_t)tu_raw;
            float uf = tu_raw - (float)iu;
            if (cmd->tw_mx) iu = (iu & u_wrap_mask) | u_wrap_off;
            iu &= 0xFF;
            float fx_hd = (((float)iu + uf - (float)ox) * uscale) - 0.5f;

            uint32_t hd_px = sample_hd_bilinear(tex, hd_w, hd_h, fx_hd, fy_hd);
            if (hd_px == 0u) continue;

            int32_t px_ps1_int = px_rel / c->scale;
            int32_t u_ps1 = cmd->flip_x ? (cmd->u0 - px_ps1_int) : (cmd->u0 + px_ps1_int);
            write_hd_pixel(cmd, &hd_row[x], ps1_row[x], hd_px, cmd->r, cmd->g, cmd->b, u_ps1, v_ps1);
        }
    }
}

static void raster_hd_tri(const SnapCmd *sc, int cidx) {
    const HMHqCmd *cmd = &sc->cmd;
    const HMCanvas *c = cmd->c;
    if (!c || !c->buf || c->scale <= 0) return;

    HMVert v0 = cmd->v[0], v1 = cmd->v[1], v2 = cmd->v[2];
    int64_t x0 = (int64_t)(v0.x - c->x + c->wide) * c->scale;
    int64_t y0 = (int64_t)(v0.y - c->y) * c->scale;
    int64_t x1 = (int64_t)(v1.x - c->x + c->wide) * c->scale;
    int64_t y1 = (int64_t)(v1.y - c->y) * c->scale;
    int64_t x2 = (int64_t)(v2.x - c->x + c->wide) * c->scale;
    int64_t y2 = (int64_t)(v2.y - c->y) * c->scale;

    int64_t area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0) return;
    if (area < 0) {
        HMVert tv = v1; v1 = v2; v2 = tv;
        int64_t tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty;
        area = -area;
    }

    int32_t scx0, scy0, scx1, scy1;
    clip_cmd_bounds(cmd, c, &scx0, &scy0, &scx1, &scy1);

    int64_t minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
    int64_t miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
    int64_t maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
    int64_t maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;

    int32_t bx0 = (minx > scx0) ? (int32_t)minx : scx0;
    int32_t by0 = (miny > scy0) ? (int32_t)miny : scy0;
    int32_t bx1 = (maxx + 1 < scx1) ? (int32_t)(maxx + 1) : scx1;
    int32_t by1 = (maxy + 1 < scy1) ? (int32_t)(maxy + 1) : scy1;
    if (bx0 >= bx1 || by0 >= by1) return;

    uint16_t hd_w = 0, hd_h = 0;
    const uint32_t *tex = get_decoded_texture(sc->tex_id, &hd_w, &hd_h);
    if (!tex) return;

    uint32_t *hd_buf = ensure_hd_buf(cidx, c);
    if (!hd_buf) return;
    g_hd.hd_has_pixels[cidx] = 1;

    int64_t a01 = y1 - y2, b01 = x2 - x1;
    int64_t a12 = y2 - y0, b12 = x0 - x2;
    int64_t a20 = y0 - y1, b20 = x1 - x0;

    float inv_area = 1.0f / (float)area;
    float uscale = (float)hd_w / (float)sc->orig_w;
    float vscale = (float)hd_h / (float)sc->orig_h;
    int32_t ox = (int32_t)sc->off_x, oy = (int32_t)sc->off_y;
    int32_t u_wrap_mask = cmd->tw_mx ? (~(cmd->tw_mx * 8)) : ~0;
    int32_t u_wrap_off  = cmd->tw_mx ? ((cmd->tw_ox & cmd->tw_mx) * 8) : 0;
    int32_t v_wrap_mask = cmd->tw_my ? (~(cmd->tw_my * 8)) : ~0;
    int32_t v_wrap_off  = cmd->tw_my ? ((cmd->tw_oy & cmd->tw_my) * 8) : 0;

    int64_t w0_row = (x2 - x1) * ((int64_t)by0 - y1) - (y2 - y1) * ((int64_t)bx0 - x1);
    int64_t w1_row = (x0 - x2) * ((int64_t)by0 - y2) - (y0 - y2) * ((int64_t)bx0 - x2);
    int64_t w2_row = (x1 - x0) * ((int64_t)by0 - y0) - (y1 - y0) * ((int64_t)bx0 - x0);

    for (int32_t y = by0; y < by1; y++) {
        int64_t w0 = w0_row, w1 = w1_row, w2 = w2_row;
        size_t row_off = (size_t)y * (size_t)c->out_w;
        uint32_t *hd_row = hd_buf + row_off;
        const uint16_t *ps1_row = c->buf + row_off;

        for (int32_t x = bx0; x < bx1; x++) {
            if ((w0 | w1 | w2) >= 0) {
                float bw0 = (float)w0 * inv_area;
                float bw1 = (float)w1 * inv_area;
                float bw2 = (float)w2 * inv_area;

                float u_f = bw0 * (float)v0.u + bw1 * (float)v1.u + bw2 * (float)v2.u;
                float v_f = bw0 * (float)v0.v + bw1 * (float)v1.v + bw2 * (float)v2.v;

                int32_t iu = (int32_t)u_f, iv = (int32_t)v_f;
                float uf = u_f - (float)iu, vf = v_f - (float)iv;
                int32_t iu_ps1 = iu, iv_ps1 = iv;
                if (cmd->tw_mx) iu = (iu & u_wrap_mask) | u_wrap_off;
                if (cmd->tw_my) iv = (iv & v_wrap_mask) | v_wrap_off;
                iu &= 0xFF; iv &= 0xFF;

                float fx_hd = (((float)iu + uf - (float)ox) * uscale) - 0.5f;
                float fy_hd = (((float)iv + vf - (float)oy) * vscale) - 0.5f;

                uint32_t hd_px = sample_hd_bilinear(tex, hd_w, hd_h, fx_hd, fy_hd);
                if (hd_px != 0u) {
                    int32_t tr = v0.r, tg = v0.g, tb = v0.b;
                    if (cmd->gouraud) {
                        tr = (int32_t)(bw0 * v0.r + bw1 * v1.r + bw2 * v2.r + 0.5f);
                        tg = (int32_t)(bw0 * v0.g + bw1 * v1.g + bw2 * v2.g + 0.5f);
                        tb = (int32_t)(bw0 * v0.b + bw1 * v1.b + bw2 * v2.b + 0.5f);
                    }
                    write_hd_pixel(cmd, &hd_row[x], ps1_row[x], hd_px, tr, tg, tb, iu_ps1, iv_ps1);
                }
            }
            w0 += a01; w1 += a12; w2 += a20;
        }
        w0_row += b01; w1_row += b12; w2_row += b20;
    }
}

/* Handle non-HD command (or unreplaced textured font/icon) over existing hd_buf[cidx] */
static void occlude_or_blend_non_hd(const HMHqCmd *cmd, int cidx) {
    if (!g_hd.hd_has_pixels[cidx] || !g_hd.hd_buf[cidx]) return;
    const HMCanvas *c = cmd->c;
    if (!c || !c->buf || c->scale <= 0) return;

    int32_t scx0, scy0, scx1, scy1;
    clip_cmd_bounds(cmd, c, &scx0, &scy0, &scx1, &scy1);
    uint32_t *hd_buf = g_hd.hd_buf[cidx];

    if (cmd->kind == 1) {
        if (cmd->sw <= 0 || cmd->sh <= 0) return;
        int32_t dx0 = (cmd->sx - c->x + c->wide) * c->scale;
        int32_t dy0 = (cmd->sy - c->y) * c->scale;
        int32_t dx1 = dx0 + cmd->sw * c->scale;
        int32_t dy1 = dy0 + cmd->sh * c->scale;
        int32_t x0 = (dx0 > scx0) ? dx0 : scx0;
        int32_t y0 = (dy0 > scy0) ? dy0 : scy0;
        int32_t x1 = (dx1 < scx1) ? dx1 : scx1;
        int32_t y1 = (dy1 < scy1) ? dy1 : scy1;
        if (x0 >= x1 || y0 >= y1) return;

        if (!cmd->textured) {
            if (!cmd->semi_trans) {
                size_t span = (size_t)(x1 - x0) * sizeof(uint32_t);
                for (int32_t y = y0; y < y1; y++) {
                    memset(&hd_buf[(size_t)y * c->out_w + x0], 0, span);
                }
            } else {
                for (int32_t y = y0; y < y1; y++) {
                    uint32_t *row = hd_buf + (size_t)y * c->out_w;
                    for (int32_t x = x0; x < x1; x++) {
                        if (row[x] != 0u) {
                            row[x] = apply_semi_mode(row[x], (uint32_t)cmd->r, (uint32_t)cmd->g,
                                                     (uint32_t)cmd->b, cmd->semi_mode);
                        }
                    }
                }
            }
        } else {
            /* Unreplaced PS1 sprite (e.g. Indonesian UI font glyph) over HD background */
            for (int32_t y = y0; y < y1; y++) {
                uint32_t *row = hd_buf + (size_t)y * c->out_w;
                int32_t py_ps1 = (y - dy0) / c->scale;
                int32_t v_ps1 = cmd->flip_y ? (cmd->v0 - py_ps1) : (cmd->v0 + py_ps1);
                for (int32_t x = x0; x < x1; x++) {
                    if (row[x] == 0u) continue;
                    int32_t px_ps1 = (x - dx0) / c->scale;
                    int32_t u_ps1 = cmd->flip_x ? (cmd->u0 - px_ps1) : (cmd->u0 + px_ps1);
                    uint16_t t16 = read_ps1_texel16(cmd, u_ps1, v_ps1);
                    if (t16 == 0u) continue; /* transparent PS1 texel preserves HD bg */
                    if (cmd->semi_trans && (t16 & 0x8000u)) {
                        uint32_t argb = g_hd.lut[t16 & 0x7FFFu];
                        uint32_t sr = (argb >> 16) & 0xFFu, sg = (argb >> 8) & 0xFFu, sb = argb & 0xFFu;
                        if (!cmd->raw_tex) {
                            sr = (sr * (uint32_t)cmd->r) >> 7; if (sr > 255u) sr = 255u;
                            sg = (sg * (uint32_t)cmd->g) >> 7; if (sg > 255u) sg = 255u;
                            sb = (sb * (uint32_t)cmd->b) >> 7; if (sb > 255u) sb = 255u;
                        }
                        row[x] = apply_semi_mode(row[x], sr, sg, sb, cmd->semi_mode);
                    } else {
                        /* Clear HD pixel so orig_hq_flush_real's PS1 pixel in c->buf shows through */
                        row[x] = 0u;
                    }
                }
            }
        }
    } else if (cmd->kind == 0) {
        HMVert v0 = cmd->v[0], v1 = cmd->v[1], v2 = cmd->v[2];
        int64_t x0 = (int64_t)(v0.x - c->x + c->wide) * c->scale;
        int64_t y0 = (int64_t)(v0.y - c->y) * c->scale;
        int64_t x1 = (int64_t)(v1.x - c->x + c->wide) * c->scale;
        int64_t y1 = (int64_t)(v1.y - c->y) * c->scale;
        int64_t x2 = (int64_t)(v2.x - c->x + c->wide) * c->scale;
        int64_t y2 = (int64_t)(v2.y - c->y) * c->scale;

        int64_t area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (area == 0) return;
        if (area < 0) {
            HMVert tv = v1; v1 = v2; v2 = tv;
            int64_t tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty;
            area = -area;
        }
        int64_t minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
        int64_t miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
        int64_t maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
        int64_t maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;

        int32_t bx0 = (minx > scx0) ? (int32_t)minx : scx0;
        int32_t by0 = (miny > scy0) ? (int32_t)miny : scy0;
        int32_t bx1 = (maxx + 1 < scx1) ? (int32_t)(maxx + 1) : scx1;
        int32_t by1 = (maxy + 1 < scy1) ? (int32_t)(maxy + 1) : scy1;
        if (bx0 >= bx1 || by0 >= by1) return;

        int64_t a01 = y1 - y2, b01 = x2 - x1;
        int64_t a12 = y2 - y0, b12 = x0 - x2;
        int64_t a20 = y0 - y1, b20 = x1 - x0;
        float inv_area = 1.0f / (float)area;

        int64_t w0_row = (x2 - x1) * ((int64_t)by0 - y1) - (y2 - y1) * ((int64_t)bx0 - x1);
        int64_t w1_row = (x0 - x2) * ((int64_t)by0 - y2) - (y0 - y2) * ((int64_t)bx0 - x2);
        int64_t w2_row = (x1 - x0) * ((int64_t)by0 - y0) - (y1 - y0) * ((int64_t)bx0 - x0);

        for (int32_t y = by0; y < by1; y++) {
            int64_t w0 = w0_row, w1 = w1_row, w2 = w2_row;
            uint32_t *row = hd_buf + (size_t)y * c->out_w;
            for (int32_t x = bx0; x < bx1; x++) {
                if ((w0 | w1 | w2) >= 0 && row[x] != 0u) {
                    if (!cmd->textured) {
                        if (!cmd->semi_trans) {
                            row[x] = 0u;
                        } else {
                            int32_t cr = v0.r, cg = v0.g, cb = v0.b;
                            if (cmd->gouraud) {
                                float bw0 = (float)w0 * inv_area, bw1 = (float)w1 * inv_area, bw2 = (float)w2 * inv_area;
                                cr = (int32_t)(bw0 * v0.r + bw1 * v1.r + bw2 * v2.r + 0.5f);
                                cg = (int32_t)(bw0 * v0.g + bw1 * v1.g + bw2 * v2.g + 0.5f);
                                cb = (int32_t)(bw0 * v0.b + bw1 * v1.b + bw2 * v2.b + 0.5f);
                            }
                            row[x] = apply_semi_mode(row[x], (uint32_t)cr, (uint32_t)cg, (uint32_t)cb, cmd->semi_mode);
                        }
                    } else {
                        float bw0 = (float)w0 * inv_area, bw1 = (float)w1 * inv_area, bw2 = (float)w2 * inv_area;
                        int32_t u_ps1 = (int32_t)(bw0 * v0.u + bw1 * v1.u + bw2 * v2.u);
                        int32_t v_ps1 = (int32_t)(bw0 * v0.v + bw1 * v1.v + bw2 * v2.v);
                        uint16_t t16 = read_ps1_texel16(cmd, u_ps1, v_ps1);
                        if (t16 != 0u) {
                            if (cmd->semi_trans && (t16 & 0x8000u)) {
                                uint32_t argb = g_hd.lut[t16 & 0x7FFFu];
                                uint32_t sr = (argb >> 16) & 0xFFu, sg = (argb >> 8) & 0xFFu, sb = argb & 0xFFu;
                                if (!cmd->raw_tex) {
                                    int32_t cr = v0.r, cg = v0.g, cb = v0.b;
                                    if (cmd->gouraud) {
                                        cr = (int32_t)(bw0 * v0.r + bw1 * v1.r + bw2 * v2.r + 0.5f);
                                        cg = (int32_t)(bw0 * v0.g + bw1 * v1.g + bw2 * v2.g + 0.5f);
                                        cb = (int32_t)(bw0 * v0.b + bw1 * v1.b + bw2 * v2.b + 0.5f);
                                    }
                                    sr = (sr * (uint32_t)cr) >> 7; if (sr > 255u) sr = 255u;
                                    sg = (sg * (uint32_t)cg) >> 7; if (sg > 255u) sg = 255u;
                                    sb = (sb * (uint32_t)cb) >> 7; if (sb > 255u) sb = 255u;
                                }
                                row[x] = apply_semi_mode(row[x], sr, sg, sb, cmd->semi_mode);
                            } else {
                                row[x] = 0u;
                            }
                        }
                    }
                }
                w0 += a01; w1 += a12; w2 += a20;
            }
            w0_row += b01; w1_row += b12; w2_row += b20;
        }
    }
}

/* ============================================================================
 * Engine Hooks (ms_abi)
 * ============================================================================ */

static void HM_MS_ABI hook_gpu_load(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *src) {
    if (g_hd.orig_gpu_load) g_hd.orig_gpu_load(x, y, w, h, src);
    bump_vram_rect(x, y, w, h);
    if (g_hd.gpu_vram_writes) g_hd.last_vram_writes = *g_hd.gpu_vram_writes;
}

static void HM_MS_ABI hook_copy_vram(const uint32_t *w) {
    if (g_hd.orig_copy_vram) g_hd.orig_copy_vram(w);
    if (w) {
        int32_t dx = (int32_t)(w[2] & 0x3FFu);
        int32_t dy = (int32_t)((w[2] >> 16) & 0x1FFu);
        int32_t bw = (int32_t)(w[3] & 0xFFFFu);
        int32_t bh = (int32_t)(w[3] >> 16);
        bump_vram_rect(dx, dy, bw, bh);
        if (g_hd.gpu_vram_writes) g_hd.last_vram_writes = *g_hd.gpu_vram_writes;
    }
}

static void HM_MS_ABI hook_gpu_clear(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b) {
    if (g_hd.orig_gpu_clear) g_hd.orig_gpu_clear(x, y, w, h, r, g, b);
    bump_vram_rect(x, y, w, h);
    if (g_hd.gpu_vram_writes) g_hd.last_vram_writes = *g_hd.gpu_vram_writes;
}

static void HM_MS_ABI hook_canvas_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    if (g_hd.orig_canvas_fill) g_hd.orig_canvas_fill(x, y, w, h, color);
    hd_clear_canvas_rect(x, y, w, h);
}

static void HM_MS_ABI hook_canvas_touch(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (g_hd.orig_canvas_touch) g_hd.orig_canvas_touch(x, y, w, h);
    hd_clear_canvas_rect(x, y, w, h);
}

static void HM_MS_ABI hook_hq_flush_real(void) {
    sync_vram_generation();

    int32_t n = g_hd.hq_n ? *g_hd.hq_n : 0;
    const HMHqCmd *hq = g_hd.hq_ptr ? *g_hd.hq_ptr : NULL;
    HMCanvas *only = g_hd.flush_only ? *g_hd.flush_only : NULL;

    int32_t snap_n = 0;
    bool any_hd = false;
    bool target_has_hd = false;

    if (n > 0 && hq) {
        if (n > g_hd.snap_cap) {
            int32_t new_cap = (n + 255) & ~255;
            SnapCmd *ns = (SnapCmd *)realloc(g_hd.snap, (size_t)new_cap * sizeof(SnapCmd));
            if (ns) {
                g_hd.snap = ns;
                g_hd.snap_cap = new_cap;
            }
        }
        if (g_hd.snap && g_hd.snap_cap >= n) {
            for (int32_t i = 0; i < n; i++) {
                const HMHqCmd *cmd = &hq[i];
                if (only && cmd->c != only) continue;
                int cidx = get_canvas_index(cmd->c);
                if (cidx < 0) continue;
                if (g_hd.hd_has_pixels[cidx]) target_has_hd = true;

                SnapCmd *sc = &g_hd.snap[snap_n++];
                sc->cmd = *cmd;
                sc->tex_id = -1;
                sc->off_x = sc->off_y = sc->orig_w = sc->orig_h = 0;

                if (cmd->textured) {
                    int32_t tid = resolve_cmd_hd_texture(cmd, &sc->off_x, &sc->off_y,
                                                         &sc->orig_w, &sc->orig_h);
                    if (tid >= 0) {
                        sc->tex_id = tid;
                        any_hd = true;
                    }
                }
            }
        }
    }

    if (g_hd.orig_hq_flush) g_hd.orig_hq_flush();

    if (!any_hd && !target_has_hd) return;

    for (int32_t i = 0; i < snap_n; i++) {
        const SnapCmd *sc = &g_hd.snap[i];
        int cidx = get_canvas_index(sc->cmd.c);
        if (cidx < 0) continue;

        if (sc->tex_id >= 0) {
            if (sc->cmd.kind == 1) raster_hd_sprite(sc, cidx);
            else if (sc->cmd.kind == 0) raster_hd_tri(sc, cidx);
        } else if (g_hd.hd_has_pixels[cidx]) {
            occlude_or_blend_non_hd(&sc->cmd, cidx);
        }
    }
}

static uint32_t * HM_MS_ABI hook_gpu_frame(int32_t *out_w, int32_t *out_h) {
    uint32_t *out = g_hd.orig_gpu_frame ? g_hd.orig_gpu_frame(out_w, out_h) : NULL;
    if (!out || !out_w || !out_h) return out;
    if (!g_hd.gpu_disp_on || *g_hd.gpu_disp_on == 0) return out;
    if ((!g_hd.gpu_scale || *g_hd.gpu_scale <= 1) && (!g_hd.gpu_wide || *g_hd.gpu_wide <= 0)) return out;

    int32_t dw = *g_hd.gpu_disp_w;
    int32_t dh = *g_hd.gpu_disp_h;
    if (dw > 640) dw = 640;
    if (dh > 512) dh = 512;

    const HMCanvas *c = g_hd.fn_canvas_find(*g_hd.gpu_disp_x, *g_hd.gpu_disp_y, dw, dh);
    int cidx = get_canvas_index(c);
    if (cidx < 0 || !c->buf || !g_hd.hd_has_pixels[cidx] || !g_hd.hd_buf[cidx]) return out;

    int32_t w = *out_w, h = *out_h;
    if (w != c->out_w || h != c->out_h || w != g_hd.hd_w[cidx] || h != g_hd.hd_h[cidx]) return out;
    if (g_hd.out_size && *g_hd.out_size < (size_t)w * (size_t)h) return out;

    int32_t x0 = 0, x1 = w;
    if (c->wide && g_hd.gpu_pillarbox && *g_hd.gpu_pillarbox) {
        int32_t pad = c->wide * c->scale;
        if (pad > 0) {
            x0 = pad;
            x1 = w - pad;
        }
    }
    if (x0 >= x1) return out;

    const uint32_t *hd_buf = g_hd.hd_buf[cidx];
    for (int32_t y = 0; y < h; y++) {
        size_t roff = (size_t)y * (size_t)w;
        const uint32_t *hd_row = hd_buf + roff;
        uint32_t *out_row = out + roff;
        for (int32_t x = x0; x < x1; x++) {
            uint32_t px = hd_row[x];
            if (px == 0u) continue;
            uint32_t a = px >> 24;
            out_row[x] = (a == 255u) ? px : lerp_argb_over_bg(out_row[x], px, a);
        }
    }
    return out;
}

/* ============================================================================
 * Inline Hook & Trampoline Installer
 * ============================================================================ */

static void emit_jmp64(uint8_t *dst, uint64_t target) {
    dst[0] = 0xFF;
    dst[1] = 0x25;
    dst[2] = 0x00;
    dst[3] = 0x00;
    dst[4] = 0x00;
    dst[5] = 0x00;
    memcpy(dst + 6, &target, 8);
}

static void *alloc_rwx(size_t sz) {
#ifdef _WIN32
    return VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
#endif
}

static void *install_hook_simple(uint8_t *fn_addr, size_t steal_len, void *hook_fn) {
    if (!g_hd.tramp_mem || g_hd.tramp_used + steal_len + 16 > 4096) return NULL;
    uint8_t *tramp = g_hd.tramp_mem + g_hd.tramp_used;
    g_hd.tramp_used += (steal_len + 14 + 15) & ~(size_t)15;

    memcpy(tramp, fn_addr, steal_len);
    emit_jmp64(tramp + steal_len, (uint64_t)(uintptr_t)(fn_addr + steal_len));
    emit_jmp64(fn_addr, (uint64_t)(uintptr_t)hook_fn);
    return tramp;
}

/* Special relocator for hq_flush_real.part.0:
 * bytes 0..7: 55 57 56 53 48 83 ec 28 (8 bytes)
 * bytes 8..13: 8b 0d <disp32> (movl nworkers(%rip), %ecx -> 6 bytes)
 */
static void *install_hook_hq_flush(uint8_t *fn_addr, void *hook_fn) {
    if (!g_hd.tramp_mem || g_hd.tramp_used + 48 > 4096) return NULL;
    uint8_t *tramp = g_hd.tramp_mem + g_hd.tramp_used;
    g_hd.tramp_used += 48;

    size_t pos = 0;
    memcpy(tramp + pos, fn_addr, 8);
    pos += 8;

    /* movabs &nworkers, %rcx (48 b9 <u64>) */
    tramp[pos++] = 0x48;
    tramp[pos++] = 0xB9;
    uint64_t nw_addr = (uint64_t)(uintptr_t)g_hd.nworkers;
    memcpy(tramp + pos, &nw_addr, 8);
    pos += 8;

    /* movl (%rcx), %ecx (8b 09) */
    tramp[pos++] = 0x8B;
    tramp[pos++] = 0x09;

    emit_jmp64(tramp + pos, (uint64_t)(uintptr_t)(fn_addr + 14));
    emit_jmp64(fn_addr, (uint64_t)(uintptr_t)hook_fn);
    return tramp;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

static int parse_and_index_pak(uint8_t *buf, size_t size) {
    if (!buf || size < sizeof(HDPakHeader)) return -1;
    memcpy(&g_hd.hdr, buf, sizeof(HDPakHeader));
    if (g_hd.hdr.magic != HDPK_MAGIC || g_hd.hdr.version != HDPK_VERSION) return -2;

    size_t off = sizeof(HDPakHeader);
    size_t tex_sz   = (size_t)g_hd.hdr.num_textures * sizeof(HDPakTex);
    size_t pal_sz   = (size_t)g_hd.hdr.num_palettes * sizeof(HDPakPal);
    size_t rect_sz  = (size_t)g_hd.hdr.num_rects    * sizeof(HDPakRect);
    size_t ent_sz   = (size_t)g_hd.hdr.num_entries  * sizeof(HDPakEntry);
    size_t total    = off + tex_sz + pal_sz + rect_sz + ent_sz + (size_t)g_hd.hdr.qoi_blob_size;
    if (size < total) return -3;

    g_hd.texs     = (const HDPakTex *)(buf + off);        off += tex_sz;
    g_hd.pals     = (const HDPakPal *)(buf + off);        off += pal_sz;
    g_hd.rects    = (const HDPakRect *)(buf + off);       off += rect_sz;
    g_hd.entries  = (const HDPakEntry *)(buf + off);      off += ent_sz;
    g_hd.qoi_blob = buf + off;

    g_hd.num_subranges = 0;
    for (uint32_t i = 0; i < g_hd.hdr.num_palettes; ) {
        uint8_t bpp = g_hd.pals[i].bpp;
        uint16_t pw = g_hd.pals[i].pal_words;
        uint32_t j = i + 1;
        while (j < g_hd.hdr.num_palettes &&
               g_hd.pals[j].bpp == bpp &&
               g_hd.pals[j].pal_words == pw) {
            j++;
        }
        if (g_hd.num_subranges < 64) {
            PalSubRange *sr = &g_hd.subranges[g_hd.num_subranges++];
            sr->bpp = bpp;
            sr->pal_words = pw;
            sr->first_pal = i;
            sr->count = j - i;
        }
        i = j;
    }

    /* Reorder subranges so full 16 / 256 palettes are tested first */
    for (uint32_t i = 0; i < g_hd.num_subranges; i++) {
        for (uint32_t j = i + 1; j < g_hd.num_subranges; j++) {
            if (g_hd.subranges[j].pal_words > g_hd.subranges[i].pal_words) {
                PalSubRange tmp = g_hd.subranges[i];
                g_hd.subranges[i] = g_hd.subranges[j];
                g_hd.subranges[j] = tmp;
            }
        }
    }

    g_hd.tex_nodes = (DecodedTexNode **)calloc(g_hd.hdr.num_textures, sizeof(DecodedTexNode *));
    if (!g_hd.tex_nodes) return -4;

    return 0;
}

int hd_textures_init(const char *pak_path, uint8_t *mapped_base) {
    if (g_hd.initialized) return 0;
    memset(&g_hd, 0, sizeof(g_hd));
    if (!pak_path || !mapped_base) return -1;

    FILE *f = fopen(pak_path, "rb");
    if (!f) {
        fprintf(stderr, "[HD] Could not open %s (falling back to original textures)\n", pak_path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= (long)sizeof(HDPakHeader)) {
        fclose(f);
        return -2;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)fsz);
    if (!buf) {
        fclose(f);
        return -3;
    }
    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) {
        free(buf);
        fclose(f);
        return -4;
    }
    fclose(f);

    g_hd.pak_buf = buf;
    g_hd.pak_size = (size_t)fsz;
    if (parse_and_index_pak(buf, (size_t)fsz) != 0) {
        fprintf(stderr, "[HD] Invalid HDPak format in %s\n", pak_path);
        hd_textures_shutdown();
        return -5;
    }

    g_hd.base            = mapped_base;
    g_hd.gpu_vram        = (uint16_t *)(mapped_base + RVA_GPU_VRAM);
    g_hd.gpu_vram_writes = (uint32_t *)(mapped_base + RVA_GPU_VRAM_WRITES);
    g_hd.gpu_pillarbox   = (int32_t *) (mapped_base + RVA_GPU_PILLARBOX);
    g_hd.gpu_wide        = (int32_t *) (mapped_base + RVA_GPU_WIDE);
    g_hd.gpu_disp_x      = (int32_t *) (mapped_base + RVA_GPU_DISP_X);
    g_hd.gpu_disp_y      = (int32_t *) (mapped_base + RVA_GPU_DISP_Y);
    g_hd.gpu_disp_on     = (int32_t *) (mapped_base + RVA_GPU_DISP_ON);
    g_hd.gpu_disp_w      = (int32_t *) (mapped_base + RVA_GPU_DISP_W);
    g_hd.gpu_disp_h      = (int32_t *) (mapped_base + RVA_GPU_DISP_H);
    g_hd.gpu_scale       = (int32_t *) (mapped_base + RVA_GPU_SCALE);
    g_hd.nworkers        = (int32_t *) (mapped_base + RVA_NWORKERS);
    g_hd.flush_only      = (HMCanvas **)(mapped_base + RVA_FLUSH_ONLY);
    g_hd.hq_n            = (int32_t *) (mapped_base + RVA_HQ_N);
    g_hd.hq_ptr          = (HMHqCmd **)(mapped_base + RVA_HQ_PTR);
    g_hd.canv            = (HMCanvas *)(mapped_base + RVA_CANV);
    g_hd.lut             = (uint32_t *)(mapped_base + RVA_LUT);
    g_hd.out_ptr         = (uint32_t **)(mapped_base + RVA_OUT_PTR);
    g_hd.out_size        = (size_t *)   (mapped_base + RVA_OUT_SIZE);
    g_hd.fn_canvas_find  = (fn_canvas_find_t)(mapped_base + RVA_FN_CANVAS_FIND);

#ifndef HD_TEST_NO_HOOKS
    g_hd.tramp_mem = (uint8_t *)alloc_rwx(4096);
    if (!g_hd.tramp_mem) {
        hd_textures_shutdown();
        return -6;
    }

    g_hd.orig_gpu_load     = (fn_gpu_load_t)    install_hook_simple(mapped_base + RVA_FN_GPU_LOAD,     16, (void *)hook_gpu_load);
    g_hd.orig_copy_vram    = (fn_copy_vram_t)   install_hook_simple(mapped_base + RVA_FN_COPY_VRAM,    16, (void *)hook_copy_vram);
    g_hd.orig_gpu_clear    = (fn_gpu_clear_t)   install_hook_simple(mapped_base + RVA_FN_GPU_CLEAR,    14, (void *)hook_gpu_clear);
    g_hd.orig_canvas_fill  = (fn_canvas_fill_t) install_hook_simple(mapped_base + RVA_FN_CANVAS_FILL,  16, (void *)hook_canvas_fill);
    g_hd.orig_canvas_touch = (fn_canvas_touch_t)install_hook_simple(mapped_base + RVA_FN_CANVAS_TOUCH, 16, (void *)hook_canvas_touch);
    g_hd.orig_hq_flush     = (fn_hq_flush_t)    install_hook_hq_flush(mapped_base + RVA_FN_HQ_FLUSH,       (void *)hook_hq_flush_real);
    g_hd.orig_gpu_frame    = (fn_gpu_frame_t)   install_hook_simple(mapped_base + RVA_FN_GPU_FRAME,    16, (void *)hook_gpu_frame);

    if (!g_hd.orig_gpu_load || !g_hd.orig_copy_vram || !g_hd.orig_gpu_clear ||
        !g_hd.orig_canvas_fill || !g_hd.orig_canvas_touch || !g_hd.orig_hq_flush ||
        !g_hd.orig_gpu_frame) {
        hd_textures_shutdown();
        return -7;
    }
#endif

    g_hd.initialized = 1;
    fprintf(stderr, "[HD] Loaded %s: %u textures, %u palettes, %u rects, %u entries (%.2f MB)\n",
            pak_path, g_hd.hdr.num_textures, g_hd.hdr.num_palettes,
            g_hd.hdr.num_rects, g_hd.hdr.num_entries, (double)g_hd.pak_size / 1048576.0);
    return 0;
}

void hd_textures_shutdown(void) {
    DecodedTexNode *n = g_hd.lru_head;
    while (n) {
        DecodedTexNode *nxt = n->next;
        free(n->argb);
        free(n);
        n = nxt;
    }
    free(g_hd.tex_nodes);
    for (int i = 0; i < MAX_CANVASES; i++) {
        free(g_hd.hd_buf[i]);
    }
    free(g_hd.snap);
    free(g_hd.pak_buf);
    memset(&g_hd, 0, sizeof(g_hd));
}
