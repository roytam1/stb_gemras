/* stb_gemras.h - v1.0 - Digital Research GEM Raster (IMG) decoder in stb_image style */
/* Public domain. See end of file for license. */
/*
   This is a single-file C89 decoder for Digital Research GEM VDI Bit Image,
   also known as GEM Raster / GEM IMG.

   Usage:
      #define STB_GEMRAS_IMPLEMENTATION
      #include "stb_gemras.h"

   API is modeled after stb_image.h but only for GEM Raster:

      unsigned char *stb_gemras_load_from_memory(
          const unsigned char *buffer, int len,
          int *x, int *y, int *comp, int req_comp);

      unsigned char *stb_gemras_load(
          const char *filename,
          int *x, int *y, int *comp, int req_comp);

      int stb_gemras_info_from_memory(
          const unsigned char *buffer, int len,
          int *x, int *y, int *comp);

      int stb_gemras_info(
          const char *filename, int *x, int *y, int *comp);

      int stb_gemras_is_from_memory(
          const unsigned char *buffer, int len);

      int stb_gemras_is_file(const char *filename);

      const char *stb_gemras_failure_reason(void);

      void stb_gemras_free(void *data);

   req_comp follows stb_image convention: 0 means keep native component
   count, 1 means gray, 2 means gray+alpha, 3 means RGB, 4 means RGBA.
   Native component count is 1 for monochrome GEM images and 3 otherwise.

   Supported variants (matching deark reference):
     - 8-word and 9-word headers, 1-8 planes (mono, grayscale, 3/4-plane color)
     - 25-word headers with Atari ST palette (Hyperpaint and similar)
     - XIMG extended headers with 0-1000 RGB palette
   Unsupported (load fails):
     - 24-plane / 16-plane / 32-plane truecolor, STTT, TIMG and other
       header sizes, invalid opcodes, truncated headers.

   Decompression implements the four GEM opcodes: solid run, literal run
   (0x80), pattern run (0x00 nn) and scanline repeat (0x00 0x00 0xFF nn).

   This header is C89 clean. Define STB_GEMRAS_NO_STDIO to remove the
   filename based APIs. Define STB_GEMRAS_STATIC to make all functions
   static. You may define STB_GEMRAS_MALLOC, STB_GEMRAS_REALLOC and
   STB_GEMRAS_FREE for custom allocation.
*/
#ifndef STB_GEMRAS_H
#define STB_GEMRAS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBGEMDEF
#ifdef STB_GEMRAS_STATIC
#define STBGEMDEF static
#else
#define STBGEMDEF extern
#endif
#endif

STBGEMDEF const char *stb_gemras_failure_reason(void);
STBGEMDEF int stb_gemras_info_from_memory(const unsigned char *data, int len, int *x, int *y, int *comp);
STBGEMDEF unsigned char *stb_gemras_load_from_memory(const unsigned char *data, int len, int *x, int *y, int *comp, int req_comp);
STBGEMDEF int stb_gemras_is_from_memory(const unsigned char *data, int len);
STBGEMDEF void stb_gemras_free(void *data);

#ifndef STB_GEMRAS_NO_STDIO
STBGEMDEF int stb_gemras_info(const char *filename, int *x, int *y, int *comp);
STBGEMDEF unsigned char *stb_gemras_load(const char *filename, int *x, int *y, int *comp, int req_comp);
STBGEMDEF int stb_gemras_is_file(const char *filename);
#endif

#ifdef __cplusplus
}
#endif

#endif /* STB_GEMRAS_H */

/* Implementation */
#ifdef STB_GEMRAS_IMPLEMENTATION

#ifndef STB_GEMRAS_H_IMPLEMENTED
#define STB_GEMRAS_H_IMPLEMENTED

#include <stdlib.h>
#include <string.h>
#ifndef STB_GEMRAS_NO_STDIO
#include <stdio.h>
#endif

#ifndef STB_GEMRAS_MALLOC
#define STB_GEMRAS_MALLOC(sz) malloc(sz)
#endif
#ifndef STB_GEMRAS_REALLOC
#define STB_GEMRAS_REALLOC(p,sz) realloc(p,sz)
#endif
#ifndef STB_GEMRAS_FREE
#define STB_GEMRAS_FREE(p) free(p)
#endif

/* failure reason */
static const char *stb_gemras__g_failure = "No error";

static void stb_gemras__err(const char *msg)
{
    stb_gemras__g_failure = msg;
}

STBGEMDEF const char *stb_gemras_failure_reason(void)
{
    return stb_gemras__g_failure;
}

STBGEMDEF void stb_gemras_free(void *data)
{
    if (data) {
        STB_GEMRAS_FREE(data);
    }
}

static unsigned int stb_gemras__get16(const unsigned char *d)
{
    return ((unsigned int)d[0] << 8) | (unsigned int)d[1];
}

static unsigned char stb_gemras__rev8(unsigned char b)
{
    unsigned char r;
    int i;
    r = 0;
    for (i = 0; i < 8; i++) {
        r <<= 1;
        r |= (unsigned char)(b & 1);
        b >>= 1;
    }
    return r;
}

static unsigned char stb_gemras__scale_1000(int x)
{
    if (x <= 0) return 0;
    if (x >= 1000) return 255;
    return (unsigned char)((x * 255 + 500) / 1000);
}

static unsigned char stb_gemras__scale_7(int x)
{
    if (x <= 0) return 0;
    if (x >= 7) return 255;
    return (unsigned char)((x * 255 + 3) / 7);
}

static unsigned char stb_gemras__scale_n(int n, int x)
{
    if (x <= 0) return 0;
    if (x >= n) return 255;
    return (unsigned char)((x * 255 + n / 2) / n);
}

/* header description */
struct stb_gemras__hdr {
    int ver;
    int hdr_words;
    int hdr_bytes;
    int nplanes;
    int patlen;
    int pixw;
    int pixh;
    int npwidth;
    int height;
    int is_ximg;
    int ext_word0;
};

static int stb_gemras__parse_header(const unsigned char *data, int len, struct stb_gemras__hdr *h)
{
    if (len < 16) {
        stb_gemras__err("truncated header");
        return 0;
    }
    h->ver = (int)stb_gemras__get16(data + 0);
    h->hdr_words = (int)stb_gemras__get16(data + 2);
    h->nplanes = (int)stb_gemras__get16(data + 4);
    h->patlen = (int)stb_gemras__get16(data + 6);
    h->pixw = (int)stb_gemras__get16(data + 8);
    h->pixh = (int)stb_gemras__get16(data + 10);
    h->npwidth = (int)stb_gemras__get16(data + 12);
    h->height = (int)stb_gemras__get16(data + 14);
    h->hdr_bytes = h->hdr_words * 2;
    h->is_ximg = 0;
    h->ext_word0 = 0;

    if (h->ver < 0 || h->ver > 2) {
        stb_gemras__err("unsupported version");
        return 0;
    }
    if (h->hdr_words < 8) {
        stb_gemras__err("bad header size");
        return 0;
    }
    if (h->hdr_words > 1024) {
        stb_gemras__err("bad header size");
        return 0;
    }
    if (h->hdr_bytes > len) {
        stb_gemras__err("truncated header");
        return 0;
    }
    if (h->hdr_bytes < 16) {
        stb_gemras__err("bad header size");
        return 0;
    }
    if (h->nplanes < 1 || h->nplanes > 32) {
        stb_gemras__err("bad plane count");
        return 0;
    }
    if (h->patlen < 1 || h->patlen > 16) {
        stb_gemras__err("bad pattern length");
        return 0;
    }
    if (h->npwidth < 1 || h->height < 1) {
        stb_gemras__err("bad dimensions");
        return 0;
    }
    if (h->npwidth > 65535 || h->height > 65535) {
        stb_gemras__err("bad dimensions");
        return 0;
    }
    if (h->hdr_words >= 9 && len >= 18) {
        h->ext_word0 = (int)stb_gemras__get16(data + 16);
    }
    if (h->hdr_words >= 10 && len >= 20) {
        if (data[16] == 88 && data[17] == 73 && data[18] == 77 && data[19] == 71) {
            h->is_ximg = 1;
        }
    }
    return 1;
}

/* 0 = unsupported variant, 1 = supported */
static int stb_gemras__is_supported(struct stb_gemras__hdr *h)
{
    if (h->is_ximg) {
        if (h->nplanes < 1 || h->nplanes > 8) {
            stb_gemras__err("unsupported plane count");
            return 0;
        }
        return 1;
    }
    if (h->hdr_words == 25) {
        if (h->nplanes < 1 || h->nplanes > 8) {
            stb_gemras__err("unsupported plane count");
            return 0;
        }
        return 1;
    }
    if (h->hdr_words == 8 || h->hdr_words == 9) {
        if (h->nplanes < 1 || h->nplanes > 8) {
            stb_gemras__err("unsupported plane count");
            return 0;
        }
        return 1;
    }
    stb_gemras__err("unsupported GEM variant");
    return 0;
}

static int stb_gemras__pad_width(int w)
{
    return ((w + 7) / 8) * 8;
}

/* Atari ST palette at pal_pos (16 entries, 2 bytes each) */
static void stb_gemras__read_atari_pal(const unsigned char *data, int len, int pal_pos,
    unsigned char *pr, unsigned char *pg, unsigned char *pb)
{
    unsigned int vals[16];
    int i;
    int bit3;
    int nib3;
    int use12;
    (void)len;
    for (i = 0; i < 16; i++) {
        vals[i] = stb_gemras__get16(data + pal_pos + i * 2);
    }
    bit3 = 0;
    nib3 = 0;
    for (i = 0; i < 16; i++) {
        if (vals[i] & 0x0888U) bit3 = 1;
        if (vals[i] & 0xF000U) nib3 = 1;
    }
    use12 = 0;
    if (bit3 && !nib3) use12 = 1;
    for (i = 0; i < 16; i++) {
        if (use12) {
            unsigned int n;
            int r1, g1, b1;
            n = vals[i];
            r1 = (int)((n >> 7) & 0x0EU);
            if (n & 0x0800U) r1++;
            g1 = (int)((n >> 3) & 0x0EU);
            if (n & 0x0080U) g1++;
            b1 = (int)((n << 1) & 0x0EU);
            if (n & 0x0008U) b1++;
            pr[i] = (unsigned char)(r1 * 17);
            pg[i] = (unsigned char)(g1 * 17);
            pb[i] = (unsigned char)(b1 * 17);
        } else {
            unsigned int n;
            int r1, g1, b1;
            n = vals[i];
            r1 = (int)((n >> 8) & 7U);
            g1 = (int)((n >> 4) & 7U);
            b1 = (int)(n & 7U);
            pr[i] = stb_gemras__scale_7(r1);
            pg[i] = stb_gemras__scale_7(g1);
            pb[i] = stb_gemras__scale_7(b1);
        }
    }
}

/* XIMG palette at offset 22. pal arrays size 256. returns entries read. */
static int stb_gemras__read_ximg_pal(const unsigned char *data, int len,
    int hdr_bytes, int nplanes,
    unsigned char *pr, unsigned char *pg, unsigned char *pb)
{
    int i;
    int maxneed;
    int avail;
    int toread;
    int off;
    for (i = 0; i < 256; i++) {
        pr[i] = 0;
        pg[i] = 0;
        pb[i] = 0;
    }
    if (hdr_bytes < 22) return 0;
    if (nplanes < 1 || nplanes > 8) return 0;
    maxneed = 1 << nplanes;
    if (hdr_bytes <= 22) {
        avail = 0;
    } else {
        avail = (hdr_bytes - 22) / 6;
    }
    if (avail < 0) avail = 0;
    toread = maxneed;
    if (toread > avail) toread = avail;
    if (toread > 256) toread = 256;
    for (i = 0; i < toread; i++) {
        int r1, g1, b1;
        off = 22 + i * 6;
        if (off + 6 > hdr_bytes) break;
        if (off + 6 > len) break;
        r1 = (int)stb_gemras__get16(data + off);
        g1 = (int)stb_gemras__get16(data + off + 2);
        b1 = (int)stb_gemras__get16(data + off + 4);
        pr[i] = stb_gemras__scale_1000(r1);
        pg[i] = stb_gemras__scale_1000(g1);
        pb[i] = stb_gemras__scale_1000(b1);
    }
    if (nplanes == 1) {
        if (pr[0] == pr[1] && pg[0] == pg[1] && pb[0] == pb[1]) {
            pr[0] = 255; pg[0] = 255; pb[0] = 255;
            pr[1] = 0; pg[1] = 0; pb[1] = 0;
        }
    }
    return toread;
}

/* decompress planar data into unc (size rowspan_total*h, zeroed).
   Returns 1 on success (possibly truncated with zero padding), 0 on error. */
static int stb_gemras__decompress(const unsigned char *data, int len,
    int hdr_bytes, int nplanes, int patlen,
    int pdwidth, int h,
    unsigned char *unc)
{
    int rowspan_per_plane;
    int rowspan_total;
    int curpos;
    int rownum;
    unsigned char *linebuf;
    int i;

    rowspan_per_plane = pdwidth / 8;
    rowspan_total = rowspan_per_plane * nplanes;
    if (rowspan_per_plane <= 0 || rowspan_total <= 0) {
        stb_gemras__err("bad dimensions");
        return 0;
    }

    linebuf = (unsigned char *)STB_GEMRAS_MALLOC((size_t)(rowspan_total + 16));
    if (!linebuf) {
        stb_gemras__err("out of memory");
        return 0;
    }

    curpos = hdr_bytes;
    rownum = 0;

    while (rownum < h) {
        int plane;
        int repeat_count;
        int row_ok;
        for (i = 0; i < rowspan_total + 16; i++) linebuf[i] = 0;

        repeat_count = 0;
        row_ok = 1;

        for (plane = 0; plane < nplanes; plane++) {
            int prev_ipos;
            int start_off;
            int end_off;
            int wpos;
            int total_written;
            int pos1;

            if (curpos >= len) {
                row_ok = 0;
                break;
            }
            prev_ipos = curpos;
            start_off = plane * rowspan_per_plane;
            end_off = (plane + 1) * rowspan_per_plane;
            wpos = start_off;
            if (plane == 0) {
                repeat_count = 1;
            }
            pos1 = curpos;

            while (wpos < end_off) {
                unsigned char b0;
                if (curpos >= len) break;
                b0 = data[curpos++];
                if (b0 == 0) {
                    unsigned char b1;
                    if (curpos >= len) break;
                    b1 = data[curpos++];
                    if (b1 > 0) {
                        int k, j;
                        int opcode_ok;
                        opcode_ok = 1;
                        if (patlen <= 0 || patlen > 16) {
                            stb_gemras__err("bad pattern length");
                            STB_GEMRAS_FREE(linebuf);
                            return 0;
                        }
                        if (curpos + patlen > len) {
                            break;
                        }
                        for (k = 0; k < (int)b1; k++) {
                            for (j = 0; j < patlen; j++) {
                                unsigned char v;
                                v = data[curpos + j];
                                if (wpos < end_off) {
                                    linebuf[wpos] = v;
                                } else if (wpos < end_off + 16) {
                                    linebuf[wpos] = v;
                                }
                                wpos++;
                                if (wpos > end_off + 64) {
                                    opcode_ok = 0;
                                    break;
                                }
                            }
                            if (!opcode_ok) break;
                        }
                        curpos += patlen;
                    } else {
                        unsigned char flagbyte;
                        unsigned char cnt;
                        int opcode_pos;
                        opcode_pos = curpos - 2;
                        if (curpos >= len) break;
                        flagbyte = data[curpos++];
                        if (flagbyte != 0xFF) {
                            stb_gemras__err("corrupt data");
                            STB_GEMRAS_FREE(linebuf);
                            return 0;
                        }
                        if (plane != 0 || opcode_pos != pos1) {
                            stb_gemras__err("corrupt data");
                            STB_GEMRAS_FREE(linebuf);
                            return 0;
                        }
                        if (curpos >= len) break;
                        cnt = data[curpos++];
                        if (cnt == 0) {
                            stb_gemras__err("corrupt data");
                            STB_GEMRAS_FREE(linebuf);
                            return 0;
                        }
                        repeat_count = (int)cnt;
                    }
                } else if (b0 == 0x80) {
                    unsigned char cnt;
                    int k;
                    if (curpos >= len) break;
                    cnt = data[curpos++];
                    if (curpos + (int)cnt > len) {
                        break;
                    }
                    for (k = 0; k < (int)cnt; k++) {
                        unsigned char v;
                        v = data[curpos++];
                        if (wpos < end_off + 16) {
                            if (wpos < rowspan_total + 16) {
                                linebuf[wpos] = v;
                            }
                        }
                        wpos++;
                        if (wpos > end_off + 64) break;
                    }
                } else {
                    unsigned char v;
                    int cnt;
                    int k;
                    v = (b0 & 0x80) ? 0xFF : 0x00;
                    cnt = (int)(b0 & 0x7F);
                    for (k = 0; k < cnt; k++) {
                        if (wpos < end_off + 16) {
                            if (wpos < rowspan_total + 16) {
                                linebuf[wpos] = v;
                            }
                        }
                        wpos++;
                        if (wpos > end_off + 64) break;
                    }
                }
                if (wpos > end_off + 64) break;
            }

            if (curpos <= prev_ipos) {
                row_ok = 0;
                break;
            }
            if (wpos < start_off) {
                wpos = start_off;
            }
            total_written = wpos - start_off;
            if (wpos < end_off) {
                /* truncated plane row: treat as end of data */
                if (curpos >= len) {
                    row_ok = 0;
                    break;
                }
                /* otherwise pad with zeros */
                for (i = wpos; i < end_off; i++) linebuf[i] = 0;
            } else if (total_written > rowspan_per_plane + 4) {
                stb_gemras__err("corrupt data");
                STB_GEMRAS_FREE(linebuf);
                return 0;
            }
            /* truncate extra bytes (ignore beyond end_off) */
        }

        if (!row_ok) {
            break;
        }
        if (repeat_count <= 0) repeat_count = 1;
        if (repeat_count > 255) repeat_count = 255;
        for (i = 0; i < repeat_count; i++) {
            int dst;
            int k;
            if (rownum >= h) break;
            dst = rownum * rowspan_total;
            for (k = 0; k < rowspan_total; k++) {
                unc[dst + k] = linebuf[k];
            }
            rownum++;
        }
    }

    STB_GEMRAS_FREE(linebuf);
    return 1;
}

static void stb_gemras__setup_default_pal(int nplanes, int is_color,
    unsigned char *pr, unsigned char *pg, unsigned char *pb)
{
    static const unsigned char pal3_r[8] = {255,0,255,255,0,0,255,0};
    static const unsigned char pal3_g[8] = {255,255,0,255,0,255,0,0};
    static const unsigned char pal3_b[8] = {255,255,255,0,255,0,0,0};
    static const unsigned char pal4_r[16] = {255,255,0,255,0,255,0,174,85,174,0,174,0,174,0,0};
    static const unsigned char pal4_g[16] = {255,0,255,255,0,0,255,174,85,0,174,174,0,0,174,0};
    static const unsigned char pal4_b[16] = {255,0,0,0,255,255,255,174,85,0,0,0,174,174,174,0};
    int i;
    int ncolors;
    ncolors = 1 << nplanes;
    if (ncolors > 256) ncolors = 256;
    if (is_color && nplanes == 3) {
        for (i = 0; i < 8; i++) {
            pr[i] = pal3_r[i];
            pg[i] = pal3_g[i];
            pb[i] = pal3_b[i];
        }
        return;
    }
    if (is_color && nplanes == 4) {
        for (i = 0; i < 16; i++) {
            pr[i] = pal4_r[i];
            pg[i] = pal4_g[i];
            pb[i] = pal4_b[i];
        }
        return;
    }
    if (nplanes == 8) {
        for (i = 0; i < 256; i++) {
            unsigned char x;
            x = stb_gemras__rev8((unsigned char)(255 - i));
            pr[i] = x;
            pg[i] = x;
            pb[i] = x;
        }
        return;
    }
    if (ncolors < 2) ncolors = 2;
    for (i = 0; i < ncolors; i++) {
        unsigned char b;
        b = stb_gemras__scale_n(ncolors - 1, i);
        b = (unsigned char)(255 - b);
        pr[i] = b;
        pg[i] = b;
        pb[i] = b;
    }
    for (i = ncolors; i < 256; i++) {
        pr[i] = 0; pg[i] = 0; pb[i] = 0;
    }
}

/* core decode: returns malloced RGB or gray buffer, sets *x,*y,*comp_native.
   comp_native is 1 or 3. Returns NULL on failure. */
static unsigned char *stb_gemras__decode(const unsigned char *data, int len,
    int *x, int *y, int *comp_native)
{
    struct stb_gemras__hdr h;
    int pdwidth;
    int rowspan_per_plane;
    int rowspan_total;
    long total_unc;
    unsigned char *unc;
    unsigned char *out;
    unsigned char pal_r[256];
    unsigned char pal_g[256];
    unsigned char pal_b[256];
    int is_mono;
    int i;
    int xx, yy;

    if (!stb_gemras__parse_header(data, len, &h)) {
        return NULL;
    }
    if (!stb_gemras__is_supported(&h)) {
        return NULL;
    }

    pdwidth = stb_gemras__pad_width(h.npwidth);
    rowspan_per_plane = pdwidth / 8;
    rowspan_total = rowspan_per_plane * h.nplanes;
    if (rowspan_per_plane <= 0 || rowspan_total <= 0) {
        stb_gemras__err("bad dimensions");
        return NULL;
    }
    if (rowspan_total > 0 && h.height > (200 * 1024 * 1024) / rowspan_total) {
        stb_gemras__err("image too large");
        return NULL;
    }
    total_unc = (long)rowspan_total * (long)h.height;
    if (total_unc <= 0 || total_unc > (200 * 1024 * 1024)) {
        if (total_unc > (200 * 1024 * 1024)) {
            stb_gemras__err("image too large");
            return NULL;
        }
        stb_gemras__err("bad dimensions");
        return NULL;
    }

    unc = (unsigned char *)STB_GEMRAS_MALLOC((size_t)total_unc);
    if (!unc) {
        stb_gemras__err("out of memory");
        return NULL;
    }
    for (i = 0; i < (int)total_unc; i++) unc[i] = 0;

    if (!stb_gemras__decompress(data, len, h.hdr_bytes, h.nplanes, h.patlen,
            pdwidth, h.height, unc)) {
        STB_GEMRAS_FREE(unc);
        return NULL;
    }

    is_mono = 0;
    if (!h.is_ximg && h.hdr_words != 25) {
        int is_color;
        int extv;
        is_color = 0;
        if (h.hdr_words == 9 && (h.nplanes == 3 || h.nplanes == 4)) {
            if (len >= 18) {
                extv = (int)stb_gemras__get16(data + 16);
                if (extv == 0) is_color = 1;
            }
        }
        if (h.nplanes == 1) {
            is_mono = 1;
        } else {
            stb_gemras__setup_default_pal(h.nplanes, is_color, pal_r, pal_g, pal_b);
        }
    } else {
        /* ximg or 25-word path: palette based, never mono except via palette */
        if (h.is_ximg) {
            stb_gemras__read_ximg_pal(data, len, h.hdr_bytes, h.nplanes, pal_r, pal_g, pal_b);
        } else {
            int pal_pos;
            pal_pos = h.hdr_bytes - 32;
            for (i = 0; i < 256; i++) pal_r[i] = pal_g[i] = pal_b[i] = 0;
            if (pal_pos < 0 || pal_pos + 32 > h.hdr_bytes || pal_pos + 32 > len) {
                stb_gemras__err("bad palette");
                STB_GEMRAS_FREE(unc);
                return NULL;
            }
            {
                unsigned char tr[16], tg[16], tb[16];
                int k;
                for (k = 0; k < 16; k++) tr[k] = tg[k] = tb[k] = 0;
                stb_gemras__read_atari_pal(data, len, pal_pos, tr, tg, tb);
                for (k = 0; k < 16; k++) {
                    pal_r[k] = tr[k]; pal_g[k] = tg[k]; pal_b[k] = tb[k];
                }
                for (k = 16; k < 256; k++) pal_r[k] = pal_g[k] = pal_b[k] = 0;
            }
            if (h.nplanes == 1) {
                if (pal_r[0] == pal_r[1] && pal_g[0] == pal_g[1] && pal_b[0] == pal_b[1]) {
                    pal_r[0] = 255; pal_g[0] = 255; pal_b[0] = 255;
                    pal_r[1] = 0; pal_g[1] = 0; pal_b[1] = 0;
                }
            }
        }
    }

    if (is_mono) {
        long outsize;
        if (h.npwidth > 0 && h.height > (200 * 1024 * 1024) / h.npwidth) {
            stb_gemras__err("image too large");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        outsize = (long)h.npwidth * (long)h.height;
        if (outsize <= 0) {
            stb_gemras__err("bad dimensions");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        out = (unsigned char *)STB_GEMRAS_MALLOC((size_t)outsize);
        if (!out) {
            stb_gemras__err("out of memory");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        for (yy = 0; yy < h.height; yy++) {
            for (xx = 0; xx < h.npwidth; xx++) {
                int byteoff;
                int bit;
                unsigned char b;
                unsigned char v;
                byteoff = yy * rowspan_total + xx / 8;
                bit = 7 - (xx % 8);
                b = unc[byteoff];
                v = (unsigned char)((b >> bit) & 1);
                out[yy * h.npwidth + xx] = v ? 0 : 255;
            }
        }
        if (x) *x = h.npwidth;
        if (y) *y = h.height;
        if (comp_native) *comp_native = 1;
    } else {
        long outsize;
        int tmp;
        tmp = h.npwidth * 3;
        if (tmp <= 0) {
            stb_gemras__err("bad dimensions");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        if (h.height > (200 * 1024 * 1024) / tmp) {
            stb_gemras__err("image too large");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        outsize = (long)h.npwidth * (long)h.height * 3L;
        if (outsize <= 0) {
            stb_gemras__err("bad dimensions");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        out = (unsigned char *)STB_GEMRAS_MALLOC((size_t)outsize);
        if (!out) {
            stb_gemras__err("out of memory");
            STB_GEMRAS_FREE(unc);
            return NULL;
        }
        for (yy = 0; yy < h.height; yy++) {
            for (xx = 0; xx < h.npwidth; xx++) {
                int idx;
                int p;
                int byteidx;
                int bit;
                idx = 0;
                byteidx = xx / 8;
                bit = 7 - (xx % 8);
                for (p = 0; p < h.nplanes; p++) {
                    unsigned char b;
                    b = unc[yy * rowspan_total + p * rowspan_per_plane + byteidx];
                    if (b & (1 << bit)) {
                        idx |= (1 << p);
                    }
                }
                if (idx < 0) idx = 0;
                if (idx > 255) idx = 255;
                out[(yy * h.npwidth + xx) * 3 + 0] = pal_r[idx];
                out[(yy * h.npwidth + xx) * 3 + 1] = pal_g[idx];
                out[(yy * h.npwidth + xx) * 3 + 2] = pal_b[idx];
            }
        }
        if (x) *x = h.npwidth;
        if (y) *y = h.height;
        if (comp_native) *comp_native = 3;
    }

    STB_GEMRAS_FREE(unc);
    return out;
}

static unsigned char *stb_gemras__convert(const unsigned char *src,
    int w, int h, int native_comp, int req_comp)
{
    unsigned char *dst;
    int i;
    long npix;
    npix = (long)w * (long)h;
    if (req_comp == 0) req_comp = native_comp;
    if (req_comp == native_comp) {
        long sz;
        sz = npix * req_comp;
        dst = (unsigned char *)STB_GEMRAS_MALLOC((size_t)sz);
        if (!dst) {
            stb_gemras__err("out of memory");
            return NULL;
        }
        for (i = 0; i < (int)sz; i++) dst[i] = src[i];
        return dst;
    }
    {
        long sz;
        sz = npix * req_comp;
        dst = (unsigned char *)STB_GEMRAS_MALLOC((size_t)sz);
        if (!dst) {
            stb_gemras__err("out of memory");
            return NULL;
        }
    }
    if (native_comp == 1) {
        for (i = 0; i < (int)npix; i++) {
            unsigned char g;
            g = src[i];
            if (req_comp == 1) {
                dst[i] = g;
            } else if (req_comp == 2) {
                dst[i * 2 + 0] = g;
                dst[i * 2 + 1] = 255;
            } else if (req_comp == 3) {
                dst[i * 3 + 0] = g;
                dst[i * 3 + 1] = g;
                dst[i * 3 + 2] = g;
            } else if (req_comp == 4) {
                dst[i * 4 + 0] = g;
                dst[i * 4 + 1] = g;
                dst[i * 4 + 2] = g;
                dst[i * 4 + 3] = 255;
            }
        }
    } else if (native_comp == 3) {
        for (i = 0; i < (int)npix; i++) {
            unsigned char r, g, b;
            unsigned char yv;
            r = src[i * 3 + 0];
            g = src[i * 3 + 1];
            b = src[i * 3 + 2];
            yv = (unsigned char)(((int)r * 77 + (int)g * 150 + (int)b * 29) >> 8);
            if (req_comp == 1) {
                dst[i] = yv;
            } else if (req_comp == 2) {
                dst[i * 2 + 0] = yv;
                dst[i * 2 + 1] = 255;
            } else if (req_comp == 3) {
                dst[i * 3 + 0] = r;
                dst[i * 3 + 1] = g;
                dst[i * 3 + 2] = b;
            } else if (req_comp == 4) {
                dst[i * 4 + 0] = r;
                dst[i * 4 + 1] = g;
                dst[i * 4 + 2] = b;
                dst[i * 4 + 3] = 255;
            }
        }
    } else {
        STB_GEMRAS_FREE(dst);
        stb_gemras__err("bad component count");
        return NULL;
    }
    return dst;
}

STBGEMDEF int stb_gemras_is_from_memory(const unsigned char *data, int len)
{
    struct stb_gemras__hdr h;
    const char *save;
    int ok;
    if (!data || len < 16) return 0;
    save = stb_gemras__g_failure;
    ok = stb_gemras__parse_header(data, len, &h);
    stb_gemras__g_failure = save;
    if (!ok) return 0;
    /* quick sanity: header words plausible for planes */
    if (h.is_ximg) return 1;
    if (h.hdr_words == 8 || h.hdr_words == 9 || h.hdr_words == 25) return 1;
    return 0;
}

STBGEMDEF int stb_gemras_info_from_memory(const unsigned char *data, int len, int *x, int *y, int *comp)
{
    struct stb_gemras__hdr h;
    int native;
    if (!data || len < 16) {
        stb_gemras__err("not GEM data");
        return 0;
    }
    if (!stb_gemras__parse_header(data, len, &h)) {
        return 0;
    }
    if (!stb_gemras__is_supported(&h)) {
        return 0;
    }
    /* native comp: 1 for mono GEM, 3 otherwise */
    if (!h.is_ximg && h.hdr_words != 25 && h.nplanes == 1) {
        native = 1;
    } else {
        native = 3;
    }
    if (x) *x = h.npwidth;
    if (y) *y = h.height;
    if (comp) *comp = native;
    return 1;
}

STBGEMDEF unsigned char *stb_gemras_load_from_memory(const unsigned char *data, int len,
    int *x, int *y, int *comp, int req_comp)
{
    unsigned char *raw;
    unsigned char *finalp;
    int w, h;
    int native;
    if (!data || len < 16) {
        stb_gemras__err("not GEM data");
        return NULL;
    }
    if (req_comp < 0 || req_comp > 4) {
        stb_gemras__err("bad req_comp");
        return NULL;
    }
    raw = stb_gemras__decode(data, len, &w, &h, &native);
    if (!raw) {
        return NULL;
    }
    if (req_comp == 0) {
        if (x) *x = w;
        if (y) *y = h;
        if (comp) *comp = native;
        return raw;
    }
    finalp = stb_gemras__convert(raw, w, h, native, req_comp);
    STB_GEMRAS_FREE(raw);
    if (!finalp) {
        return NULL;
    }
    if (x) *x = w;
    if (y) *y = h;
    if (comp) *comp = req_comp;
    return finalp;
}

#ifndef STB_GEMRAS_NO_STDIO
static unsigned char *stb_gemras__read_file(const char *filename, int *out_len)
{
    FILE *f;
    long sz;
    unsigned char *buf;
    size_t got;
    f = fopen(filename, "rb");
    if (!f) {
        stb_gemras__err("cannot open file");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        stb_gemras__err("cannot seek file");
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || sz < 16) {
        fclose(f);
        stb_gemras__err("bad file size");
        return NULL;
    }
    if (sz > (200 * 1024 * 1024)) {
        fclose(f);
        stb_gemras__err("file too large");
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        stb_gemras__err("cannot seek file");
        return NULL;
    }
    buf = (unsigned char *)STB_GEMRAS_MALLOC((size_t)sz);
    if (!buf) {
        fclose(f);
        stb_gemras__err("out of memory");
        return NULL;
    }
    got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if ((long)got != sz) {
        STB_GEMRAS_FREE(buf);
        stb_gemras__err("cannot read file");
        return NULL;
    }
    if (out_len) *out_len = (int)sz;
    return buf;
}

STBGEMDEF int stb_gemras_info(const char *filename, int *x, int *y, int *comp)
{
    unsigned char *buf;
    int len;
    int ok;
    if (!filename) {
        stb_gemras__err("bad filename");
        return 0;
    }
    buf = stb_gemras__read_file(filename, &len);
    if (!buf) return 0;
    ok = stb_gemras_info_from_memory(buf, len, x, y, comp);
    STB_GEMRAS_FREE(buf);
    return ok;
}

STBGEMDEF unsigned char *stb_gemras_load(const char *filename, int *x, int *y, int *comp, int req_comp)
{
    unsigned char *buf;
    int len;
    unsigned char *out;
    if (!filename) {
        stb_gemras__err("bad filename");
        return NULL;
    }
    buf = stb_gemras__read_file(filename, &len);
    if (!buf) return NULL;
    out = stb_gemras_load_from_memory(buf, len, x, y, comp, req_comp);
    STB_GEMRAS_FREE(buf);
    return out;
}

STBGEMDEF int stb_gemras_is_file(const char *filename)
{
    unsigned char *buf;
    int len;
    int ok;
    const char *save;
    struct stb_gemras__hdr h;
    if (!filename) return 0;
    buf = stb_gemras__read_file(filename, &len);
    if (!buf) return 0;
    save = stb_gemras__g_failure;
    ok = stb_gemras__parse_header(buf, len, &h);
    STB_GEMRAS_FREE(buf);
    stb_gemras__g_failure = save;
    if (!ok) return 0;
    if (h.is_ximg) return 1;
    if (h.hdr_words == 8 || h.hdr_words == 9 || h.hdr_words == 25) return 1;
    return 0;
}
#endif /* STB_GEMRAS_NO_STDIO */

#endif /* STB_GEMRAS_H_IMPLEMENTED */
#endif /* STB_GEMRAS_IMPLEMENTATION */

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2026
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
