// steg.c - LSB steganography with low-contrast region selection.

#include "steg.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper: compute luminance for a pixel given its RGB values.
static double compute_luminance(unsigned char r,
                                unsigned char g,
                                unsigned char b)
{
    // Standard ITU-R BT.601 luma transform.
    return 0.299 * (double)r + 0.587 * (double)g + 0.114 * (double)b;
}

// Helper: safely allocate an array of doubles for luminance.
static int compute_luminance_map(const BmpImage *img, double **lum_out)
{
    assert(img != NULL);
    assert(lum_out != NULL);

    *lum_out = NULL;

    if (img->data == NULL || img->width <= 0 || img->height == 0) {
        fprintf(stderr, "compute_luminance_map: invalid image\n");
        return 1;
    }

    int32_t width = img->width;
    int32_t abs_height = img->height > 0 ? img->height : -img->height;
    size_t count = (size_t)width * (size_t)abs_height;

    double *lum = (double *)malloc(count * sizeof(double));
    if (!lum) {
        perror("compute_luminance_map: malloc");
        return 1;
    }

    // We treat rows in the order stored in data, which is bottom-up for positive height.
    // Since encode and decode both use the same convention, consistency is all we need.
    for (int32_t row = 0; row < abs_height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            size_t pixel_index = (size_t)row * (size_t)width + (size_t)col;
            size_t offset = (size_t)row * (size_t)img->stride + (size_t)col * 3u;

            unsigned char b = img->data[offset + 0];
            unsigned char g = img->data[offset + 1];
            unsigned char r = img->data[offset + 2];

            // Ignore the least significant bit of each channel so that
            // our low-contrast selection is stable under LSB embedding.
            // This way, encode and decode see the same blocks.
            unsigned char b_hi = (unsigned char)(b & 0xFEu);
            unsigned char g_hi = (unsigned char)(g & 0xFEu);
            unsigned char r_hi = (unsigned char)(r & 0xFEu);

            lum[pixel_index] = compute_luminance(r_hi, g_hi, b_hi);
        }
    }

    *lum_out = lum;
    return 0;
}

int find_low_contrast_positions(const BmpImage *img,
                                int block_size,
                                double contrast_threshold,
                                EmbedPosition **positions_out,
                                size_t *count_out)
{
    assert(positions_out != NULL);
    assert(count_out != NULL);

    *positions_out = NULL;
    *count_out = 0;

    if (img == NULL || img->data == NULL) {
        fprintf(stderr, "find_low_contrast_positions: invalid image\n");
        return 1;
    }

    if (block_size <= 0) {
        fprintf(stderr, "find_low_contrast_positions: block_size must be > 0\n");
        return 1;
    }

    int32_t width = img->width;
    int32_t height = img->height;
    int32_t abs_height = height > 0 ? height : -height;

    if (width <= 0 || abs_height <= 0) {
        fprintf(stderr, "find_low_contrast_positions: invalid dimensions\n");
        return 1;
    }

    double *lum = NULL;
    if (compute_luminance_map(img, &lum) != 0) {
        return 1;
    }

    EmbedPosition *positions = NULL;
    size_t capacity = 0;
    size_t count = 0;

    int32_t max_row = abs_height - block_size + 1;
    int32_t max_col = width - block_size + 1;

    if (max_row < 0 || max_col < 0) {
        free(lum);
        return 0; // no blocks available, but not a hard error
    }

    for (int32_t br = 0; br < max_row; ++br) {
        for (int32_t bc = 0; bc < max_col; ++bc) {
            // Compute mean luminance in the block
            double sum = 0.0;
            int n = 0;
            for (int r = 0; r < block_size; ++r) {
                int32_t row = br + r;
                for (int c = 0; c < block_size; ++c) {
                    int32_t col = bc + c;
                    size_t idx = (size_t)row * (size_t)width + (size_t)col;
                    sum += lum[idx];
                    ++n;
                }
            }

            if (n == 0) {
                continue;
            }

            double mean = sum / (double)n;

            // Compute standard deviation
            double sq_sum = 0.0;
            for (int r = 0; r < block_size; ++r) {
                int32_t row = br + r;
                for (int c = 0; c < block_size; ++c) {
                    int32_t col = bc + c;
                    size_t idx = (size_t)row * (size_t)width + (size_t)col;
                    double d = lum[idx] - mean;
                    sq_sum += d * d;
                }
            }

            double variance = sq_sum / (double)n;
            double stddev = sqrt(variance);

            if (stddev < contrast_threshold) {
                // Block is "low contrast": record all its pixels.
                for (int r = 0; r < block_size; ++r) {
                    int32_t row = br + r;
                    for (int c = 0; c < block_size; ++c) {
                        int32_t col = bc + c;
                        int pixel_index = row * width + col;

                        if (count == capacity) {
                            size_t new_cap = capacity == 0 ? 128 : capacity * 2;
                            EmbedPosition *tmp = (EmbedPosition *)realloc(
                                positions, new_cap * sizeof(EmbedPosition));
                            if (!tmp) {
                                perror("find_low_contrast_positions: realloc");
                                free(positions);
                                free(lum);
                                return 1;
                            }
                            positions = tmp;
                            capacity = new_cap;
                        }

                        positions[count].pixel_index = pixel_index;
                        ++count;
                    }
                }
            }
        }
    }

    free(lum);

    *positions_out = positions;
    *count_out = count;
    return 0;
}

// Helper: write bits from a buffer into image pixels at given positions.
// Bits are taken from bitstream[0..total_bits-1] (0 or 1).
// Channels order per pixel: R, G, B (note: data is BGR).
static int embed_bits(BmpImage *img,
                      const EmbedPosition *positions,
                      size_t positions_count,
                      const uint8_t *bitstream,
                      size_t total_bits)
{
    assert(img != NULL);
    assert(positions != NULL);
    assert(bitstream != NULL);

    int32_t width = img->width;
    int32_t abs_height = img->height > 0 ? img->height : -img->height;
    (void)abs_height; // not strictly needed, but kept for invariants

    size_t bit_index = 0;
    for (size_t i = 0; i < positions_count && bit_index < total_bits; ++i) {
        int pixel_index = positions[i].pixel_index;
        assert(pixel_index >= 0);
        int row = pixel_index / width;
        int col = pixel_index % width;

        size_t base = (size_t)row * (size_t)img->stride + (size_t)col * 3u;

        // Order: R, G, B => indices 2, 1, 0 in BGR layout
        for (int channel = 2; channel >= 0 && bit_index < total_bits; --channel) {
            assert(bit_index < total_bits);
            size_t offset = base + (size_t)channel;
            unsigned char value = img->data[offset];
            uint8_t bit = bitstream[bit_index];

            value &= (unsigned char)~1u;       // clear LSB
            value |= (unsigned char)(bit & 1u); // set LSB
            img->data[offset] = value;

            ++bit_index;
        }
    }

    // At this point we assume capacity check was done beforehand, so we should
    // always embed all bits.
    assert(bit_index == total_bits);
    return 0;
}

// Helper: read bits from image according to positions into bitstream.
static int extract_bits(const BmpImage *img,
                        const EmbedPosition *positions,
                        size_t positions_count,
                        uint8_t *bitstream,
                        size_t total_bits)
{
    assert(img != NULL);
    assert(positions != NULL);
    assert(bitstream != NULL);

    int32_t width = img->width;
    int32_t abs_height = img->height > 0 ? img->height : -img->height;
    (void)abs_height;

    size_t bit_index = 0;
    for (size_t i = 0; i < positions_count && bit_index < total_bits; ++i) {
        int pixel_index = positions[i].pixel_index;
        assert(pixel_index >= 0);
        int row = pixel_index / width;
        int col = pixel_index % width;

        size_t base = (size_t)row * (size_t)img->stride + (size_t)col * 3u;

        // Same order as embed: R, G, B
        for (int channel = 2; channel >= 0 && bit_index < total_bits; --channel) {
            size_t offset = base + (size_t)channel;
            unsigned char value = img->data[offset];
            uint8_t bit = (uint8_t)(value & 1u);
            bitstream[bit_index] = bit;
            ++bit_index;
        }
    }

    if (bit_index < total_bits) {
        fprintf(stderr, "extract_bits: not enough bits available\n");
        return 1;
    }

    return 0;
}

int steg_encode_message(BmpImage *img,
                        const uint8_t *message,
                        size_t message_len,
                        int block_size,
                        double contrast_threshold)
{
    assert(img != NULL);

    if (img->data == NULL) {
        fprintf(stderr, "steg_encode_message: invalid image data\n");
        return 1;
    }

    if (message == NULL && message_len > 0) {
        fprintf(stderr, "steg_encode_message: message is NULL but length > 0\n");
        return 1;
    }

    EmbedPosition *positions = NULL;
    size_t positions_count = 0;

    if (find_low_contrast_positions(img, block_size, contrast_threshold,
                                    &positions, &positions_count) != 0) {
        return 1;
    }

    size_t capacity_bits = positions_count * 3u;
    size_t required_bits = (4u + message_len) * 8u; // 4-byte length header + message

    // If capacity is insufficient, return -1 and do not modify image.
    if (capacity_bits < required_bits) {
        free(positions);
        fprintf(stderr, "steg_encode_message: capacity insufficient "
                        "(have %zu bits, need %zu bits)\n",
                capacity_bits, required_bits);
        return -1;
    }

    // Build bitstream: [length(4 bytes, little-endian)] [message bytes]
    size_t total_bytes = 4u + message_len;
    size_t total_bits = total_bytes * 8u;

    uint8_t *bitstream = (uint8_t *)malloc(total_bits * sizeof(uint8_t));
    if (!bitstream) {
        perror("steg_encode_message: malloc");
        free(positions);
        return 1;
    }

    // Pack length as 32-bit unsigned integer in little-endian order.
    uint32_t len32 = (uint32_t)message_len;
    uint8_t header[4];
    header[0] = (uint8_t)(len32 & 0xFFu);
    header[1] = (uint8_t)((len32 >> 8) & 0xFFu);
    header[2] = (uint8_t)((len32 >> 16) & 0xFFu);
    header[3] = (uint8_t)((len32 >> 24) & 0xFFu);

    // Convert bytes to bit stream (MSB-first within each byte).
    size_t bit_index = 0;
    for (size_t i = 0; i < 4; ++i) {
        uint8_t b = header[i];
        for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {
            uint8_t bit = (uint8_t)((b >> bit_pos) & 1u);
            bitstream[bit_index++] = bit;
        }
    }
    for (size_t i = 0; i < message_len; ++i) {
        uint8_t b = message[i];
        for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {
            uint8_t bit = (uint8_t)((b >> bit_pos) & 1u);
            bitstream[bit_index++] = bit;
        }
    }

    assert(bit_index == total_bits);

    int rc = embed_bits(img, positions, positions_count, bitstream, total_bits);

    free(bitstream);
    free(positions);

    return rc;
}

int steg_decode_message(const BmpImage *img,
                        uint8_t **message_out,
                        size_t *message_len_out,
                        int block_size,
                        double contrast_threshold)
{
    assert(img != NULL);
    assert(message_out != NULL);
    assert(message_len_out != NULL);

    *message_out = NULL;
    *message_len_out = 0;

    if (img->data == NULL) {
        fprintf(stderr, "steg_decode_message: invalid image data\n");
        return 1;
    }

    EmbedPosition *positions = NULL;
    size_t positions_count = 0;

    if (find_low_contrast_positions(img, block_size, contrast_threshold,
                                    &positions, &positions_count) != 0) {
        return 1;
    }

    size_t capacity_bits = positions_count * 3u;
    if (capacity_bits < 32u) {
        fprintf(stderr, "steg_decode_message: not enough bits even for header\n");
        free(positions);
        return 1;
    }

    // First read 32 bits (4 bytes) for length header.
    uint8_t header_bits[32];
    if (extract_bits(img, positions, positions_count, header_bits, 32u) != 0) {
        free(positions);
        return 1;
    }

    // Convert header bits (MSB-first per byte) back to 4-byte little-endian integer.
    uint8_t header_bytes[4] = {0, 0, 0, 0};
    size_t bit_index = 0;
    for (size_t i = 0; i < 4; ++i) {
        uint8_t b = 0;
        for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {
            uint8_t bit = header_bits[bit_index++];
            b |= (uint8_t)(bit << bit_pos);
        }
        header_bytes[i] = b;
    }

    uint32_t len32 = 0;
    len32 |= (uint32_t)header_bytes[0];
    len32 |= (uint32_t)header_bytes[1] << 8;
    len32 |= (uint32_t)header_bytes[2] << 16;
    len32 |= (uint32_t)header_bytes[3] << 24;

    size_t message_len = (size_t)len32;

    size_t required_bits = (4u + message_len) * 8u;
    if (capacity_bits < required_bits) {
        fprintf(stderr, "steg_decode_message: capacity insufficient for stored length\n");
        free(positions);
        return 1;
    }

    // Now read all bits (header + message).
    uint8_t *all_bits = (uint8_t *)malloc(required_bits * sizeof(uint8_t));
    if (!all_bits) {
        perror("steg_decode_message: malloc");
        free(positions);
        return 1;
    }

    if (extract_bits(img, positions, positions_count, all_bits, required_bits) != 0) {
        free(all_bits);
        free(positions);
        return 1;
    }

    // Skip first 32 bits, decode remaining into message bytes.
    uint8_t *message = (uint8_t *)malloc(message_len * sizeof(uint8_t));
    if (!message) {
        perror("steg_decode_message: malloc message");
        free(all_bits);
        free(positions);
        return 1;
    }

    size_t bit_offset = 32u;
    for (size_t i = 0; i < message_len; ++i) {
        uint8_t b = 0;
        for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {
            uint8_t bit = all_bits[bit_offset++];
            b |= (uint8_t)(bit << bit_pos);
        }
        message[i] = b;
    }

    free(all_bits);
    free(positions);

    *message_out = message;
    *message_len_out = message_len;

    return 0;
}


