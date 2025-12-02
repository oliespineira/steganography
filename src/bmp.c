// bmp.c - Simple 24-bit BMP loading/saving implementation.

#include "bmp.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bmp_load(const char *filename, BmpImage *img)
{
    assert(img != NULL);

    img->data = NULL;
    img->width = 0;
    img->height = 0;
    img->stride = 0;
    img->size = 0;

    if (filename == NULL) {
        fprintf(stderr, "bmp_load: filename is NULL\n");
        return 1;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("bmp_load: fopen");
        return 1;
    }

    size_t read_count = fread(img->header, 1, 54, f);
    if (read_count != 54) {
        fprintf(stderr, "bmp_load: failed to read BMP header\n");
        fclose(f);
        return 1;
    }

    // Basic header checks
    if (img->header[0] != 'B' || img->header[1] != 'M') {
        fprintf(stderr, "bmp_load: not a BMP file\n");
        fclose(f);
        return 1;
    }

    // Extract width, height, bits-per-pixel, compression from header (little endian)
    int32_t width = 0;
    int32_t height = 0;
    memcpy(&width, img->header + 18, sizeof(int32_t));
    memcpy(&height, img->header + 22, sizeof(int32_t));

    uint16_t bpp = 0;
    memcpy(&bpp, img->header + 28, sizeof(uint16_t));

    uint32_t compression = 0;
    memcpy(&compression, img->header + 30, sizeof(uint32_t));

    if (bpp != 24) {
        fprintf(stderr, "bmp_load: only 24-bit BMP supported (got %u bpp)\n",
                (unsigned)bpp);
        fclose(f);
        return 1;
    }

    if (compression != 0) {
        fprintf(stderr, "bmp_load: compressed BMP not supported (compression=%u)\n",
                (unsigned)compression);
        fclose(f);
        return 1;
    }

    if (width <= 0 || height == 0) {
        fprintf(stderr, "bmp_load: invalid BMP dimensions\n");
        fclose(f);
        return 1;
    }

    img->width = width;
    img->height = height;

    // BMP rows are padded to multiples of 4 bytes
    int32_t abs_height = height > 0 ? height : -height;
    int32_t stride = ((width * 3 + 3) / 4) * 4;
    int32_t size = stride * abs_height;

    img->stride = stride;
    img->size = size;

    img->data = (unsigned char *)malloc((size_t)size);
    if (!img->data) {
        perror("bmp_load: malloc");
        fclose(f);
        return 1;
    }

    size_t data_read = fread(img->data, 1, (size_t)size, f);
    if (data_read != (size_t)size) {
        fprintf(stderr, "bmp_load: failed to read pixel data\n");
        free(img->data);
        img->data = NULL;
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

int bmp_save(const char *filename, const BmpImage *img)
{
    assert(img != NULL);

    if (filename == NULL) {
        fprintf(stderr, "bmp_save: filename is NULL\n");
        return 1;
    }

    if (img->data == NULL || img->size <= 0) {
        fprintf(stderr, "bmp_save: invalid image data\n");
        return 1;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("bmp_save: fopen");
        return 1;
    }

    size_t written = fwrite(img->header, 1, 54, f);
    if (written != 54) {
        fprintf(stderr, "bmp_save: failed to write header\n");
        fclose(f);
        return 1;
    }

    written = fwrite(img->data, 1, (size_t)img->size, f);
    if (written != (size_t)img->size) {
        fprintf(stderr, "bmp_save: failed to write pixel data\n");
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

void bmp_free(BmpImage *img)
{
    if (img == NULL) {
        return;
    }

    if (img->data) {
        free(img->data);
        img->data = NULL;
    }

    img->width = 0;
    img->height = 0;
    img->stride = 0;
    img->size = 0;
}


