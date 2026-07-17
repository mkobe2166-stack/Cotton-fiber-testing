#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INF_DIST 1000000000

typedef struct { int w, h; unsigned char *gray; } GrayImage;
typedef struct { int x, y; } Point;
typedef struct { long count; double sum, sum2, minv, maxv; } Stats;

static uint16_t rd16(FILE *fp) {
    unsigned char b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t rd32(FILE *fp) {
    unsigned char b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

static int32_t rdi32(FILE *fp) { return (int32_t)rd32(fp); }

static void wr16(FILE *fp, uint16_t v) {
    fputc(v & 255, fp);
    fputc((v >> 8) & 255, fp);
}

static void wr32(FILE *fp, uint32_t v) {
    fputc(v & 255, fp);
    fputc((v >> 8) & 255, fp);
    fputc((v >> 16) & 255, fp);
    fputc((v >> 24) & 255, fp);
}

static void free_image(GrayImage *img) {
    free(img->gray);
    img->gray = NULL;
    img->w = img->h = 0;
}

static int load_bmp_gray(const char *path, GrayImage *img) {
    FILE *fp = fopen(path, "rb");
    uint16_t sig, planes, bpp;
    uint32_t data_offset, dib_size, compression, colors_used;
    int32_t width, height_signed;
    int height, top_down, row_stride;
    unsigned char *row = NULL, *palette = NULL;
    int ok = 0;

    memset(img, 0, sizeof(*img));
    if (!fp) {
        fprintf(stderr, "Cannot open input BMP: %s\n", path);
        return 0;
    }

    sig = rd16(fp);
    if (sig != 0x4D42) {
        fprintf(stderr, "Input is not a BMP file.\n");
        goto done;
    }
    (void)rd32(fp);
    (void)rd16(fp);
    (void)rd16(fp);
    data_offset = rd32(fp);
    dib_size = rd32(fp);
    if (dib_size < 40) goto done;

    width = rdi32(fp);
    height_signed = rdi32(fp);
    planes = rd16(fp);
    bpp = rd16(fp);
    compression = rd32(fp);
    (void)rd32(fp);
    (void)rdi32(fp);
    (void)rdi32(fp);
    colors_used = rd32(fp);
    (void)rd32(fp);

    if (planes != 1 || compression != 0 || width <= 0 || height_signed == 0) {
        fprintf(stderr, "Only uncompressed BMP files are supported.\n");
        goto done;
    }
    if (!(bpp == 8 || bpp == 24 || bpp == 32)) {
        fprintf(stderr, "Only 8-bit, 24-bit, and 32-bit BMP files are supported.\n");
        goto done;
    }

    top_down = height_signed < 0;
    height = top_down ? -height_signed : height_signed;
    img->w = width;
    img->h = height;
    img->gray = (unsigned char *)malloc((size_t)width * height);
    if (!img->gray) goto done;

    if (bpp == 8) {
        uint32_t ncolors = colors_used ? colors_used : 256;
        long palette_pos = 14L + (long)dib_size;
        palette = (unsigned char *)malloc((size_t)ncolors * 4);
        if (!palette) goto done;
        fseek(fp, palette_pos, SEEK_SET);
        if (fread(palette, 4, ncolors, fp) != ncolors) goto done;
    }

    row_stride = (int)(((int64_t)width * bpp + 31) / 32 * 4);
    row = (unsigned char *)malloc((size_t)row_stride);
    if (!row) goto done;

    fseek(fp, (long)data_offset, SEEK_SET);
    for (int fy = 0; fy < height; ++fy) {
        int y = top_down ? fy : height - 1 - fy;
        if (fread(row, 1, row_stride, fp) != (size_t)row_stride) goto done;
        for (int x = 0; x < width; ++x) {
            unsigned char g;
            if (bpp == 8) {
                unsigned char idx = row[x];
                if (palette) {
                    unsigned char b = palette[idx * 4 + 0];
                    unsigned char gr = palette[idx * 4 + 1];
                    unsigned char r = palette[idx * 4 + 2];
                    g = (unsigned char)((77 * r + 150 * gr + 29 * b) >> 8);
                } else {
                    g = idx;
                }
            } else {
                int p = x * (bpp / 8);
                unsigned char b = row[p + 0];
                unsigned char gr = row[p + 1];
                unsigned char r = row[p + 2];
                g = (unsigned char)((77 * r + 150 * gr + 29 * b) >> 8);
            }
            img->gray[(size_t)y * width + x] = g;
        }
    }
    ok = 1;

done:
    free(row);
    free(palette);
    fclose(fp);
    if (!ok) free_image(img);
    return ok;
}

static int write_bmp_rgb(const char *path, const unsigned char *rgb, int w, int h) {
    FILE *fp = fopen(path, "wb");
    int stride = ((w * 3 + 3) / 4) * 4;
    uint32_t image_size = (uint32_t)stride * h;
    unsigned char *row = (unsigned char *)calloc((size_t)stride, 1);
    if (!fp || !row) {
        if (fp) fclose(fp);
        free(row);
        return 0;
    }

    wr16(fp, 0x4D42);
    wr32(fp, 14 + 40 + image_size);
    wr16(fp, 0);
    wr16(fp, 0);
    wr32(fp, 54);
    wr32(fp, 40);
    wr32(fp, (uint32_t)w);
    wr32(fp, (uint32_t)h);
    wr16(fp, 1);
    wr16(fp, 24);
    wr32(fp, 0);
    wr32(fp, image_size);
    wr32(fp, 2835);
    wr32(fp, 2835);
    wr32(fp, 0);
    wr32(fp, 0);

    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = rgb + ((size_t)y * w + x) * 3;
            row[x * 3 + 0] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        fwrite(row, 1, (size_t)stride, fp);
    }
    free(row);
    fclose(fp);
    return 1;
}

static int write_bmp_gray(const char *path, const unsigned char *gray, int w, int h) {
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
    int ok;
    if (!rgb) return 0;
    for (int i = 0; i < w * h; ++i) {
        rgb[i * 3 + 0] = gray[i];
        rgb[i * 3 + 1] = gray[i];
        rgb[i * 3 + 2] = gray[i];
    }
    ok = write_bmp_rgb(path, rgb, w, h);
    free(rgb);
    return ok;
}

static void write_dtm_bmp(const char *path, const int *dist, int w, int h) {
    unsigned char *gray = (unsigned char *)malloc((size_t)w * h);
    int max_d = 0;
    if (!gray) return;
    
    for (int i = 0; i < w * h; ++i) {
        if (dist[i] < INF_DIST && dist[i] > max_d) {
            max_d = dist[i];
        }
    }
    if (max_d == 0) max_d = 1; 
    
    for (int i = 0; i < w * h; ++i) {
        if (dist[i] >= INF_DIST || dist[i] == 0) {
            gray[i] = 0; 
        } else {
            int val = (dist[i] * 255) / max_d;
            gray[i] = (unsigned char)(val > 255 ? 255 : val);
        }
    }
    write_bmp_gray(path, gray, w, h);
    free(gray);
}

static int otsu_threshold(const unsigned char *gray, int n) {
    long hist[256] = {0};
    double total = 0.0, sum0 = 0.0, best = -1.0;
    long n0 = 0;
    int th = 0;

    for (int i = 0; i < n; ++i) {
        hist[gray[i]]++;
        total += gray[i];
    }
    for (int t = 0; t < 256; ++t) {
        long n1;
        double m0, m1, score;
        n0 += hist[t];
        if (!n0) continue;
        n1 = n - n0;
        if (!n1) break;
        sum0 += (double)t * hist[t];
        m0 = sum0 / n0;
        m1 = (total - sum0) / n1;
        score = (double)n0 * n1 * (m0 - m1) * (m0 - m1);
        if (score > best) {
            best = score;
            th = t;
        }
    }
    return th;
}

static void threshold_dark(const unsigned char *gray, unsigned char *bin, int n, int th) {
    for (int i = 0; i < n; ++i) bin[i] = gray[i] < th ? 1 : 0;
}

static void bin_to_gray(const unsigned char *bin, unsigned char *gray, int n) {
    for (int i = 0; i < n; ++i) gray[i] = bin[i] ? 0 : 255;
}

static int push_if(unsigned char *seen, Point *q, int *tail, int w, int h, int x, int y, int want, const unsigned char *img) {
    int idx;
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    idx = y * w + x;
    if (seen[idx] || img[idx] != want) return 0;
    seen[idx] = 1;
    q[(*tail)++] = (Point){x, y};
    return 1;
}

static long denoise_components(unsigned char *bin, int w, int h, int min_area, int min_span) {
    int n = w * h;
    unsigned char *seen = (unsigned char *)calloc((size_t)n, 1);
    Point *q = (Point *)malloc((size_t)n * sizeof(Point));
    long removed = 0;

    if (!seen || !q) {
        free(seen);
        free(q);
        return 0;
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int head = 0, tail = 0, minx = x, maxx = x, miny = y, maxy = y;
            int area, span, remove;
            if (!bin[idx] || seen[idx]) continue;
            push_if(seen, q, &tail, w, h, x, y, 1, bin);
            while (head < tail) {
                Point p = q[head++];
                if (p.x < minx) minx = p.x;
                if (p.x > maxx) maxx = p.x;
                if (p.y < miny) miny = p.y;
                if (p.y > maxy) maxy = p.y;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx || dy) push_if(seen, q, &tail, w, h, p.x + dx, p.y + dy, 1, bin);
                    }
                }
            }
            area = tail;
            span = (maxx - minx + 1 > maxy - miny + 1) ? maxx - minx + 1 : maxy - miny + 1;
            remove = (area < min_area) || (span < min_span && area < min_area * 8);
            if (remove) {
                for (int i = 0; i < tail; ++i) bin[q[i].y * w + q[i].x] = 0;
                removed += tail;
            }
        }
    }

    free(seen);
    free(q);
    return removed;
}

// ==========================================
// 边缘修复组件：加强版膨胀与腐蚀 (支持边界保护)
// ==========================================
static void morph_dilate(const unsigned char *src, unsigned char *dst, int w, int h, int r) {
    int n = w * h;
    memcpy(dst, src, n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (src[y * w + x]) continue; 
            int fill = 0;
            for (int dy = -r; dy <= r && !fill; ++dy) {
                for (int dx = -r; dx <= r && !fill; ++dx) {
                    if (dx * dx + dy * dy <= r * r) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            if (src[ny * w + nx]) fill = 1;
                        }
                    }
                }
            }
            dst[y * w + x] = fill;
        }
    }
}

static void morph_erode(const unsigned char *src, unsigned char *dst, int w, int h, int r) {
    int n = w * h;
    memcpy(dst, src, n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!src[y * w + x]) continue; 
            int keep = 1;
            for (int dy = -r; dy <= r && keep; ++dy) {
                for (int dx = -r; dx <= r && keep; ++dx) {
                    if (dx * dx + dy * dy <= r * r) {
                        int nx = x + dx, ny = y + dy;
                        // 边界保护：越界视作前景，避免边缘纤维被腐蚀变细
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            if (!src[ny * w + nx]) keep = 0;
                        }
                    }
                }
            }
            dst[y * w + x] = keep;
        }
    }
}

static void morph_close(unsigned char *bin, int w, int h, int r) {
    unsigned char *tmp = (unsigned char *)malloc((size_t)w * h);
    if (!tmp) return;
    morph_dilate(bin, tmp, w, h, r);
    morph_erode(tmp, bin, w, h, r);
    free(tmp);
}

// ==========================================
// 深度内部孔洞填充 (不考虑亮度，只看形状)
// ==========================================
static long fill_closed_voids(unsigned char *bin, int w, int h, int max_area, int max_span) {
    int n = w * h;
    unsigned char *seen = (unsigned char *)calloc((size_t)n, 1);
    Point *q = (Point *)malloc((size_t)n * sizeof(Point));
    long filled = 0;

    if (!seen || !q) {
        free(seen);
        free(q);
        return 0;
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int head = 0, tail = 0, minx = x, maxx = x, miny = y, maxy = y, border = 0;
            int area, span, fill = 0;
            
            if (bin[idx] || seen[idx]) continue;

            push_if(seen, q, &tail, w, h, x, y, 0, bin);
            while (head < tail) {
                Point p = q[head++];
                // 触碰到图像边缘说明是外部空间
                if (p.x == 0 || p.y == 0 || p.x == w - 1 || p.y == h - 1) border = 1;
                
                if (p.x < minx) minx = p.x;
                if (p.x > maxx) maxx = p.x;
                if (p.y < miny) miny = p.y;
                if (p.y > maxy) maxy = p.y;
                
                push_if(seen, q, &tail, w, h, p.x + 1, p.y, 0, bin);
                push_if(seen, q, &tail, w, h, p.x - 1, p.y, 0, bin);
                push_if(seen, q, &tail, w, h, p.x, p.y + 1, 0, bin);
                push_if(seen, q, &tail, w, h, p.x, p.y - 1, 0, bin);
            }
            area = tail;
            span = (maxx - minx + 1 > maxy - miny + 1) ? maxx - minx + 1 : maxy - miny + 1;
            
            // 只要被包围的洞口尺寸在合理范围内，强行填充
            if (!border && area <= max_area && span <= max_span) {
                fill = 1;
            }
            
            if (fill) {
                for (int i = 0; i < tail; ++i) bin[q[i].y * w + q[i].x] = 1;
                filled += tail;
            }
        }
    }

    free(seen);
    free(q);
    return filled;
}

static int px(const unsigned char *img, int w, int h, int x, int y) {
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    return img[y * w + x] ? 1 : 0;
}

static int neighbor_count(const unsigned char *img, int w, int h, int x, int y) {
    int c = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) c += px(img, w, h, x + dx, y + dy);
    return c;
}

static int transitions(const unsigned char *img, int w, int h, int x, int y) {
    int p[9], a = 0;
    p[0] = px(img, w, h, x, y - 1);
    p[1] = px(img, w, h, x + 1, y - 1);
    p[2] = px(img, w, h, x + 1, y);
    p[3] = px(img, w, h, x + 1, y + 1);
    p[4] = px(img, w, h, x, y + 1);
    p[5] = px(img, w, h, x - 1, y + 1);
    p[6] = px(img, w, h, x - 1, y);
    p[7] = px(img, w, h, x - 1, y - 1);
    p[8] = p[0];
    for (int i = 0; i < 8; ++i) if (p[i] == 0 && p[i + 1] == 1) a++;
    return a;
}

static void hilditch(unsigned char *bin, int w, int h) {
    int n = w * h, changed, iter = 0;
    unsigned char *mark = (unsigned char *)calloc((size_t)n, 1);
    if (!mark) return;

    do {
        changed = 0;
        memset(mark, 0, (size_t)n);
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                int idx = y * w + x;
                int b, a, p2, p4, p6, p8;
                if (!bin[idx]) continue;
                b = neighbor_count(bin, w, h, x, y);
                a = transitions(bin, w, h, x, y);
                p2 = px(bin, w, h, x, y - 1);
                p4 = px(bin, w, h, x + 1, y);
                p6 = px(bin, w, h, x, y + 1);
                p8 = px(bin, w, h, x - 1, y);
                if (b >= 2 && b <= 6 && a == 1 &&
                    ((p2 * p4 * p8) == 0 || transitions(bin, w, h, x, y - 1) != 1) &&
                    ((p2 * p4 * p6) == 0 || transitions(bin, w, h, x + 1, y) != 1)) {
                    mark[idx] = 1;
                    changed = 1;
                }
            }
        }
        for (int i = 0; i < n; ++i) if (mark[i]) bin[i] = 0;
        iter++;
        if (iter > w + h) break;
    } while (changed);
    free(mark);
}

static void clear_border(unsigned char *img, int w, int h) {
    for (int x = 0; x < w; ++x) {
        img[x] = 0;
        img[(h - 1) * w + x] = 0;
    }
    for (int y = 0; y < h; ++y) {
        img[y * w] = 0;
        img[y * w + w - 1] = 0;
    }
}

static void prune(unsigned char *skel, int w, int h, int iterations) {
    int n = w * h;
    unsigned char *mark = (unsigned char *)calloc((size_t)n, 1);
    if (!mark) return;
    for (int it = 0; it < iterations; ++it) {
        int removed = 0;
        memset(mark, 0, (size_t)n);
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                int idx = y * w + x;
                if (skel[idx] && neighbor_count(skel, w, h, x, y) == 1) {
                    mark[idx] = 1;
                    removed++;
                }
            }
        }
        for (int i = 0; i < n; ++i) if (mark[i]) skel[i] = 0;
        if (!removed) break;
    }
    free(mark);
}

static int *distance_transform(const unsigned char *obj, int w, int h) {
    int n = w * h;
    int *d = (int *)malloc((size_t)n * sizeof(int));
    if (!d) return NULL;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            int border = (x == 0 || y == 0 || x == w - 1 || y == h - 1);
            d[idx] = (obj[idx] && !border) ? INF_DIST : 0;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x, v = d[idx];
            if (!v) continue;
            if (x > 0 && d[idx - 1] + 3 < v) v = d[idx - 1] + 3;
            if (y > 0 && d[idx - w] + 3 < v) v = d[idx - w] + 3;
            if (x > 0 && y > 0 && d[idx - w - 1] + 4 < v) v = d[idx - w - 1] + 4;
            if (x + 1 < w && y > 0 && d[idx - w + 1] + 4 < v) v = d[idx - w + 1] + 4;
            d[idx] = v;
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            int idx = y * w + x, v = d[idx];
            if (!v) continue;
            if (x + 1 < w && d[idx + 1] + 3 < v) v = d[idx + 1] + 3;
            if (y + 1 < h && d[idx + w] + 3 < v) v = d[idx + w] + 3;
            if (x + 1 < w && y + 1 < h && d[idx + w + 1] + 4 < v) v = d[idx + w + 1] + 4;
            if (x > 0 && y + 1 < h && d[idx + w - 1] + 4 < v) v = d[idx + w - 1] + 4;
            d[idx] = v;
        }
    }
    return d;
}

static void stats_init(Stats *s) {
    s->count = 0; s->sum = 0.0; s->sum2 = 0.0; s->minv = DBL_MAX; s->maxv = -DBL_MAX;
}
static void stats_add(Stats *s, double v) {
    s->count++; s->sum += v; s->sum2 += v * v;
    if (v < s->minv) s->minv = v;
    if (v > s->maxv) s->maxv = v;
}
static double stats_mean(const Stats *s) { return s->count ? s->sum / s->count : 0.0; }
static double stats_std(const Stats *s) {
    double m, v;
    if (s->count < 2) return 0.0;
    m = stats_mean(s);
    v = (s->sum2 - s->count * m * m) / (s->count - 1);
    return v > 0.0 ? sqrt(v) : 0.0;
}

static void put_px(unsigned char *rgb, int w, int h, int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    size_t idx;
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    idx = ((size_t)y * w + x) * 3;
    rgb[idx + 0] = r; rgb[idx + 1] = g; rgb[idx + 2] = b;
}

static void line(unsigned char *rgb, int w, int h, int x0, int y0, int x1, int y1,
                 unsigned char r, unsigned char g, unsigned char b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        put_px(rgb, w, h, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void rect_fill(unsigned char *rgb, int w, int h, int x0, int y0, int x1, int y1,
                      unsigned char r, unsigned char g, unsigned char b) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1; if (y1 >= h) y1 = h - 1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            put_px(rgb, w, h, x, y, r, g, b);
}

static const char *glyph(char c) {
    switch (c) {
        case 'A': return "01110""10001""10001""11111""10001""10001""10001";
        case 'C': return "01111""10000""10000""10000""10000""10000""01111";
        case 'D': return "11110""10001""10001""10001""10001""10001""11110";
        case 'F': return "11111""10000""10000""11110""10000""10000""10000";
        case 'M': return "10001""11011""10101""10101""10001""10001""10001";
        case 'P': return "11110""10001""10001""11110""10000""10000""10000";
        case 'S': return "01111""10000""10000""01110""00001""00001""11110";
        case 'W': return "10001""10001""10001""10101""10101""11011""10001";
        case 'a': return "00000""00000""01110""00001""01111""10001""01111";
        case 'c': return "00000""00000""01111""10000""10000""10000""01111";
        case 'd': return "00001""00001""01111""10001""10001""10001""01111";
        case 'e': return "00000""00000""01110""10001""11111""10000""01110";
        case 'h': return "10000""10000""11110""10001""10001""10001""10001";
        case 'i': return "00100""00000""01100""00100""00100""00100""01110";
        case 'l': return "01100""00100""00100""00100""00100""00100""01110";
        case 'n': return "00000""00000""11110""10001""10001""10001""10001";
        case 'o': return "00000""00000""01110""10001""10001""10001""01110";
        case 'q': return "00000""00000""01111""10001""10001""01111""00001";
        case 'r': return "00000""00000""10110""11001""10000""10000""10000";
        case 's': return "00000""00000""01111""10000""01110""00001""11110";
        case 't': return "00100""00100""11111""00100""00100""00100""00011";
        case 'u': return "00000""00000""10001""10001""10001""10011""01101";
        case 'x': return "00000""00000""10001""01010""00100""01010""10001";
        case 'y': return "00000""00000""10001""10001""01111""00001""01110";
        case '0': return "01110""10001""10011""10101""11001""10001""01110";
        case '1': return "00100""01100""00100""00100""00100""00100""01110";
        case '2': return "01110""10001""00001""00010""00100""01000""11111";
        case '3': return "11110""00001""00001""01110""00001""00001""11110";
        case '4': return "00010""00110""01010""10010""11111""00010""00010";
        case '5': return "11111""10000""10000""11110""00001""00001""11110";
        case '6': return "00110""01000""10000""11110""10001""10001""01110";
        case '7': return "11111""00001""00010""00100""01000""01000""01000";
        case '8': return "01110""10001""10001""01110""10001""10001""01110";
        case '9': return "01110""10001""10001""01111""00001""00010""11100";
        case '.': return "00000""00000""00000""00000""00000""01100""01100";
        case '=': return "00000""00000""11111""00000""11111""00000""00000";
        case '(': return "00010""00100""01000""01000""01000""00100""00010";
        case ')': return "01000""00100""00010""00010""00010""00100""01000";
        case '/': return "00001""00010""00010""00100""01000""01000""10000";
        case '-': return "00000""00000""00000""11111""00000""00000""00000";
        case ' ': return "00000""00000""00000""00000""00000""00000""00000";
        default:  return "00000""00000""00000""00000""00000""00000""00000";
    }
}

static void draw_char(unsigned char *rgb, int w, int h, int x, int y, char c, int scale,
                      unsigned char r, unsigned char g, unsigned char b) {
    const char *pat = glyph(c);
    for (int yy = 0; yy < 7; ++yy) {
        for (int xx = 0; xx < 5; ++xx) {
            if (pat[yy * 5 + xx] == '1') {
                rect_fill(rgb, w, h, x + xx * scale, y + yy * scale,
                          x + (xx + 1) * scale - 1, y + (yy + 1) * scale - 1, r, g, b);
            }
        }
    }
}

static void text(unsigned char *rgb, int w, int h, int x, int y, const char *s, int scale,
                 unsigned char r, unsigned char g, unsigned char b) {
    for (; *s; ++s) {
        draw_char(rgb, w, h, x, y, *s, scale, r, g, b);
        x += 6 * scale;
    }
}

static void text_rot90(unsigned char *rgb, int w, int h, int x, int y, const char *s, int scale,
                       unsigned char r, unsigned char g, unsigned char b) {
    for (; *s; ++s) {
        const char *pat = glyph(*s);
        for (int yy = 0; yy < 7; ++yy) {
            for (int xx = 0; xx < 5; ++xx) {
                if (pat[yy * 5 + xx] == '1') {
                    int rx = x - yy * scale;
                    int ry = y + xx * scale;
                    rect_fill(rgb, w, h, rx - scale + 1, ry, rx, ry + scale - 1, r, g, b);
                }
            }
        }
        y += 6 * scale;
    }
}

static void overlay(const char *path, const unsigned char *bg_gray, const unsigned char *skel, int w, int h) {
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
    if (!rgb) return;
    for (int i = 0; i < w * h; ++i) {
        rgb[i * 3 + 0] = bg_gray[i];
        rgb[i * 3 + 1] = bg_gray[i];
        rgb[i * 3 + 2] = bg_gray[i];
        if (skel[i]) {
            rgb[i * 3 + 0] = 255; 
            rgb[i * 3 + 1] = 0;   
            rgb[i * 3 + 2] = 0;   
        }
    }
    write_bmp_rgb(path, rgb, w, h);
    free(rgb);
}

static double nice_ymax(double v) {
    int step = 50;
    int m = (int)ceil(v / step) * step;
    return m < step ? step : m;
}

static void write_histogram_bmp(const char *path, const long *bins, int nbins, double binw,
                                const Stats *width_px) {
    int w = 1050, h = 760;
    int left = 150, right = 90, top = 95, bottom = 120;
    int pw = w - left - right, ph = h - top - bottom;
    long maxc = 0;
    double ymax;
    unsigned char *rgb = (unsigned char *)malloc((size_t)w * h * 3);
    char buf[96];

    if (!rgb) return;
    for (int i = 0; i < w * h; ++i) {
        rgb[i * 3 + 0] = 255;
        rgb[i * 3 + 1] = 255;
        rgb[i * 3 + 2] = 255;
    }
    for (int i = 0; i < nbins; ++i) if (bins[i] > maxc) maxc = bins[i];
    ymax = nice_ymax((double)maxc);

    text(rgb, w, h, 50, 25, "Statistical Analysis", 5, 50, 50, 50);

    for (int i = 0; i <= 5; ++i) {
        int y = top + ph - (ph * i) / 5;
        int val = (int)(ymax * i / 5.0 + 0.5);
        line(rgb, w, h, left, y, left + pw, y, 225, 225, 225);
        line(rgb, w, h, left - 8, y, left, y, 0, 0, 0);
        snprintf(buf, sizeof(buf), "%d", val);
        text(rgb, w, h, left - 70, y - 10, buf, 3, 30, 30, 30);
    }
    for (int i = 0; i <= 9; ++i) {
        int x = left + (pw * i) / 9;
        int val = (int)(i * 5);
        line(rgb, w, h, x, top, x, top + 8, 0, 0, 0);
        line(rgb, w, h, x, top + ph, x, top + ph + 8, 0, 0, 0);
        snprintf(buf, sizeof(buf), "%d", val);
        text(rgb, w, h, x - 13, top + ph + 22, buf, 3, 30, 30, 30);
    }

    line(rgb, w, h, left, top, left + pw, top, 0, 0, 0);
    line(rgb, w, h, left, top + ph, left + pw, top + ph, 0, 0, 0);
    line(rgb, w, h, left, top, left, top + ph, 0, 0, 0);
    line(rgb, w, h, left + pw, top, left + pw, top + ph, 0, 0, 0);

    for (int i = 0; i < nbins; ++i) {
        int x0 = left + (pw * i) / nbins;
        int x1 = left + (pw * (i + 1)) / nbins - 2;
        int bh = (int)(bins[i] * ph / ymax + 0.5);
        rect_fill(rgb, w, h, x0, top + ph - bh, x1, top + ph - 1, 245, 25, 35);
    }

    text(rgb, w, h, left + pw / 2 - 95, h - 58, "Width (Pixel)", 4, 40, 40, 40);
    text_rot90(rgb, w, h, 78, top + 150, "Frequency", 4, 40, 40, 40);

    snprintf(buf, sizeof(buf), "Max = %.1f", width_px->count ? width_px->maxv : 0.0);
    text(rgb, w, h, left + 35, top + 32, buf, 3, 40, 40, 40);
    snprintf(buf, sizeof(buf), "Min = %.1f", width_px->count ? width_px->minv : 0.0);
    text(rgb, w, h, left + 35, top + 62, buf, 3, 40, 40, 40);
    snprintf(buf, sizeof(buf), "Mean = %.1f", stats_mean(width_px));
    text(rgb, w, h, left + 35, top + 92, buf, 3, 40, 40, 40);
    snprintf(buf, sizeof(buf), "S.D. = %.1f", stats_std(width_px));
    text(rgb, w, h, left + 35, top + 122, buf, 3, 40, 40, 40);
    snprintf(buf, sizeof(buf), "Count = %ld", width_px->count);
    text(rgb, w, h, left + 35, top + 152, buf, 3, 40, 40, 40);

    (void)binw;
    write_bmp_rgb(path, rgb, w, h);
    free(rgb);
}

static int analyze(const char *input, const char *prefix, double um_per_px) {
    GrayImage img;
    int n, th, min_area, min_span, max_hole_area, max_hole_span;
    int prune_iter = 8, margin = 6;
    unsigned char *binary = NULL, *denoised = NULL, *filled = NULL, *skel_raw = NULL, *skel = NULL, *tmp = NULL;
    int *dist = NULL;
    long removed_noise, closed_fill;
    Stats rpx, rum, wpx, wum;
    int nbins = 45;
    double binw = 1.0;
    long *hist = NULL;
    FILE *csv = NULL, *hcsv = NULL, *sum = NULL;
    char path[1024];
    long area_before = 0, area_after = 0;

    if (!load_bmp_gray(input, &img)) return 0;
    n = img.w * img.h;
    binary = (unsigned char *)malloc((size_t)n);
    denoised = (unsigned char *)malloc((size_t)n);
    filled = (unsigned char *)malloc((size_t)n);
    skel_raw = (unsigned char *)malloc((size_t)n);
    skel = (unsigned char *)malloc((size_t)n);
    tmp = (unsigned char *)malloc((size_t)n);
    hist = (long *)calloc((size_t)nbins, sizeof(long));
    if (!binary || !denoised || !filled || !skel_raw || !skel || !tmp || !hist) goto fail;

    th = otsu_threshold(img.gray, n);
    threshold_dark(img.gray, binary, n, th);

    min_area = n / 9000;
    if (min_area < 120) min_area = 120;
    min_span = 55;
    memcpy(denoised, binary, (size_t)n);
    removed_noise = denoise_components(denoised, img.w, img.h, min_area, min_span);

    memcpy(filled, denoised, (size_t)n);
    
    // =======================================================
    // 缝合与补洞算法终极进化
    // =======================================================
    
    // 1. 估算缺口大小进行形态学闭运算 (抹灰缝合)
    // 根据您的反馈，交叉点的 Gap 很大(>20px)，而纤维内部边缘缺口较窄(<16px)。
    // 这里采用半径 r=8 的结构元素：它能完美跨过最多 16 像素的缺口(将其封闭)，
    // 而绝不会把宽度远超 20 像素的交叉 Gap 封死。
    int morph_radius = 8;
    morph_close(filled, img.w, img.h, morph_radius);
    
    // 2. 深度闭孔填充 (无脑填坑)
    // 经过上一步，那些缺口已经被“桥接”变成了独立的内部洞。
    // 我们将允许填洞的最大面积放宽到 5000 像素。只要不是巨大的外部空间，一律填满。
    max_hole_area = 5000; 
    max_hole_span = 120;   
    closed_fill = fill_closed_voids(filled, img.w, img.h, max_hole_area, max_hole_span);

    for (int i = 0; i < n; ++i) {
        if (denoised[i]) area_before++;
        if (filled[i]) area_after++;
    }

    memcpy(skel_raw, filled, (size_t)n);
    hilditch(skel_raw, img.w, img.h);
    clear_border(skel_raw, img.w, img.h);
    memcpy(skel, skel_raw, (size_t)n);
    prune(skel, img.w, img.h, prune_iter);
    dist = distance_transform(filled, img.w, img.h);
    if (!dist) goto fail;

    snprintf(path, sizeof(path), "%s_radius_points.csv", prefix);
    csv = fopen(path, "w");
    if (!csv) goto fail;
    fprintf(csv, "x,y,radius_px,radius_um,width_px,width_um\n");

    stats_init(&rpx); stats_init(&rum); stats_init(&wpx); stats_init(&wum);
    for (int y = margin; y < img.h - margin; ++y) {
        for (int x = margin; x < img.w - margin; ++x) {
            int idx = y * img.w + x;
            if (skel[idx] && dist[idx] > 0 && dist[idx] < INF_DIST) {
                double rp = dist[idx] / 3.0;
                double ru = rp * um_per_px;
                double wp = 2.0 * rp;
                double wu = 2.0 * ru;
                int b = (int)(wp / binw);
                stats_add(&rpx, rp); stats_add(&rum, ru); stats_add(&wpx, wp); stats_add(&wum, wu);
                if (b >= 0 && b < nbins) hist[b]++;
                fprintf(csv, "%d,%d,%.6f,%.6f,%.6f,%.6f\n", x, y, rp, ru, wp, wu);
            }
        }
    }
    fclose(csv); csv = NULL;

    snprintf(path, sizeof(path), "%s_width_histogram.csv", prefix);
    hcsv = fopen(path, "w");
    if (!hcsv) goto fail;
    fprintf(hcsv, "width_px_start,width_px_end,frequency\n");
    for (int i = 0; i < nbins; ++i) fprintf(hcsv, "%.3f,%.3f,%ld\n", i * binw, (i + 1) * binw, hist[i]);
    fclose(hcsv); hcsv = NULL;

    // 输出图像
    snprintf(path, sizeof(path), "%s_01_grayscale.bmp", prefix);
    write_bmp_gray(path, img.gray, img.w, img.h);
    
    bin_to_gray(binary, tmp, n);
    snprintf(path, sizeof(path), "%s_02_binary.bmp", prefix);
    write_bmp_gray(path, tmp, img.w, img.h);
    
    bin_to_gray(denoised, tmp, n);
    snprintf(path, sizeof(path), "%s_03_denoised.bmp", prefix);
    write_bmp_gray(path, tmp, img.w, img.h);
    
    bin_to_gray(filled, tmp, n);
    snprintf(path, sizeof(path), "%s_04_filled_voids.bmp", prefix);
    write_bmp_gray(path, tmp, img.w, img.h);
    
    bin_to_gray(skel_raw, tmp, n);
    snprintf(path, sizeof(path), "%s_05_skeleton_raw_no_pruning.bmp", prefix);
    write_bmp_gray(path, tmp, img.w, img.h);
    
    bin_to_gray(skel, tmp, n);
    snprintf(path, sizeof(path), "%s_06_skeleton_pruned.bmp", prefix);
    write_bmp_gray(path, tmp, img.w, img.h);
    
    snprintf(path, sizeof(path), "%s_07_distance_transform.bmp", prefix);
    write_dtm_bmp(path, dist, img.w, img.h);
    
    bin_to_gray(filled, tmp, n);
    snprintf(path, sizeof(path), "%s_08_skeleton_overlay.bmp", prefix);
    overlay(path, tmp, skel, img.w, img.h);
    
    snprintf(path, sizeof(path), "%s_09_width_frequency_histogram.bmp", prefix);
    write_histogram_bmp(path, hist, nbins, binw, &wpx);

    // 统计数据
    snprintf(path, sizeof(path), "%s_summary.txt", prefix);
    sum = fopen(path, "w");
    if (!sum) goto fail;
    fprintf(sum, "Input image: %s\n", input);
    fprintf(sum, "Image size: %d x %d pixels\n", img.w, img.h);
    fprintf(sum, "Scale: %.6f um/pixel\n", um_per_px);
    fprintf(sum, "Otsu threshold: %d\n", th);
    fprintf(sum, "Denoise: min_area=%d, min_span=%d, removed_noise_pixels=%ld\n", min_area, min_span, removed_noise);
    fprintf(sum, "Morphological Closing (R=%d) + Deep Fill: filled_pixels=%ld\n", morph_radius, closed_fill);
    fprintf(sum, "Fiber area before filling: %ld px\n", area_before);
    fprintf(sum, "Fiber area after filling: %ld px\n", area_after);
    fprintf(sum, "Void ratio Dr=(after-before)/before: %.6f\n", area_before ? (double)(area_after - area_before) / area_before : 0.0);
    fprintf(sum, "Pruning iterations: %d\n", prune_iter);
    fprintf(sum, "Border sampling margin: %d px\n", margin);
    fprintf(sum, "Radius samples: %ld\n", rpx.count);
    fprintf(sum, "Radius mean: %.6f px, %.6f um\n", stats_mean(&rpx), stats_mean(&rum));
    fprintf(sum, "Radius stddev: %.6f px, %.6f um\n", stats_std(&rpx), stats_std(&rum));
    fprintf(sum, "Radius min: %.6f px, %.6f um\n", rpx.count ? rpx.minv : 0.0, rum.count ? rum.minv : 0.0);
    fprintf(sum, "Radius max: %.6f px, %.6f um\n", rpx.count ? rpx.maxv : 0.0, rum.count ? rpx.maxv : 0.0);
    fprintf(sum, "Width mean: %.6f px, %.6f um\n", stats_mean(&wpx), stats_mean(&wum));
    fprintf(sum, "Width stddev: %.6f px, %.6f um\n", stats_std(&wpx), stats_std(&wum));
    fprintf(sum, "Width min/max: %.6f / %.6f px\n", wpx.count ? wpx.minv : 0.0, wpx.count ? wpx.maxv : 0.0);
    fprintf(sum, "Width coefficient of variation Wcv: %.6f\n", stats_mean(&wum) ? stats_std(&wum) / stats_mean(&wum) : 0.0);
    fclose(sum); sum = NULL;

    printf("Done.\n");
    printf("Output prefix: %s\n", prefix);
    printf("Samples: %ld\n", rpx.count);
    printf("Mean radius: %.6f px, %.6f um\n", stats_mean(&rpx), stats_mean(&rum));
    printf("Mean width: %.6f px, %.6f um\n", stats_mean(&wpx), stats_mean(&wum));

    free_image(&img); free(binary); free(denoised); free(filled); free(skel_raw); free(skel); free(tmp); free(dist); free(hist);
    return 1;

fail:
    if (csv) fclose(csv);
    if (hcsv) fclose(hcsv);
    if (sum) fclose(sum);
    free_image(&img); free(binary); free(denoised); free(filled); free(skel_raw); free(skel); free(tmp); free(dist); free(hist);
    return 0;
}

int main() {
    // 您的硬编码输入输出路径保持不变
    const char *input_path = "C:\\Users\\ASUS\\Desktop\\作业任务\\项目1 棉花纤维成熟度检测\\棉花纤维显微图像.bmp";
    const char *output_prefix = "C:\\Users\\ASUS\\Desktop\\作业任务\\项目1 棉花纤维成熟度检测\\处理结果";
    
    // 默认的像素分辨率转换比例
    double um_per_px = 0.85; 

    printf("=========================================\n");
    printf("正在启动棉花纤维成熟度检测算法(智能阈值版)...\n");
    printf("读取路径: %s\n", input_path);
    printf("输出目录: %s\n", output_prefix);
    printf("=========================================\n\n");

    if (analyze(input_path, output_prefix, um_per_px)) {
        printf("\n?? 所有图像处理和数据分析均已完成！\n");
        printf("智能阈值桥接已开启，请查看新生成的 04_filled_voids.bmp。\n");
    } else {
        printf("\n? 处理失败。请检查：\n");
        printf("1. 原图像 %s 是否存在且未损坏。\n", input_path);
        printf("2. 桌面文件夹是否具有写入权限。\n");
    }

    printf("\n");
    system("pause");
    return 0;
}
