#ifndef BMP_H
#define BMP_H

// Basic BMP loading/saving helpers for 24-bit uncompressed BMP files.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char header[54];  // copy BMP header as-is
    int32_t width;
    int32_t height;
    int32_t stride;   // bytes per row (including padding)
    int32_t size;     // total pixel data size in bytes
    unsigned char *data; // dynamically allocated pixel array (B, G, R, B, G, R, ...)
} BmpImage;

// Load a 24-bit uncompressed BMP from disk.
// Returns 0 on success, non-zero on failure.
int bmp_load(const char *filename, BmpImage *img);

// Save a 24-bit BMP to disk, using the header/data from img.
// Returns 0 on success, non-zero on failure.
int bmp_save(const char *filename, const BmpImage *img);

// Free dynamic memory associated with img.
void bmp_free(BmpImage *img);

#ifdef __cplusplus
}
#endif

#endif


