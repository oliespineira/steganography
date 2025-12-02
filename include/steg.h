#ifndef STEG_H
#define STEG_H

// Steganography interface: low-contrast region selection and LSB embedding.

#include <stddef.h>
#include <stdint.h>
#include "bmp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pixel_index;  // index of the pixel in row-major order
} EmbedPosition;

// Compute the candidate positions within the image using a low-contrast metric.
// Returns 0 on success, non-zero on failure. On success, *positions_out must
// be freed by the caller with free().
int find_low_contrast_positions(const BmpImage *img,
                                int block_size,
                                double contrast_threshold,
                                EmbedPosition **positions_out,
                                size_t *count_out);

// Encode a message into the BMP image in memory.
// message_len is in bytes. Function modifies img->data in-place.
// Returns 0 on success, -1 if capacity is insufficient, non-zero on other errors.
int steg_encode_message(BmpImage *img,
                        const uint8_t *message,
                        size_t message_len,
                        int block_size,
                        double contrast_threshold);

// Decode a message from the BMP image in memory.
// The function allocates a buffer for the message and sets *message_out and
// *message_len_out. Caller must free(*message_out).
// Returns 0 on success, non-zero on failure.
int steg_decode_message(const BmpImage *img,
                        uint8_t **message_out,
                        size_t *message_len_out,
                        int block_size,
                        double contrast_threshold);

#ifdef __cplusplus
}
#endif

#endif


