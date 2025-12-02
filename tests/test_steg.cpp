// test_steg.cpp - GoogleTest unit tests for steganography library.

#include <gtest/gtest.h>

extern "C" {
#include "bmp.h"
#include "steg.h"
}

#include <cstring>

// Helper to create a synthetic BMP image in memory with solid color.
static void create_test_image(int32_t width,
                              int32_t height,
                              unsigned char r,
                              unsigned char g,
                              unsigned char b,
                              BmpImage *img)
{
    img->width = width;
    img->height = height;
    img->stride = ((width * 3 + 3) / 4) * 4;
    int32_t abs_height = height > 0 ? height : -height;
    img->size = img->stride * abs_height;

    std::memset(img->header, 0, sizeof(img->header)); // not used for tests

    img->data = (unsigned char *)std::malloc((size_t)img->size);
    ASSERT_NE(img->data, nullptr);

    for (int32_t row = 0; row < abs_height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            size_t base = (size_t)row * (size_t)img->stride + (size_t)col * 3u;
            img->data[base + 0] = b;
            img->data[base + 1] = g;
            img->data[base + 2] = r;
        }
    }
}

// 1) Test bit-packing/unpacking logic (simulate basic LSB operations).
TEST(StegBitTest, LsbPackingSimulation)
{
    uint8_t value = 0b10101010;
    // Clear LSB
    value &= (uint8_t)~1u;
    EXPECT_EQ(value & 1u, 0u);

    // Set LSB to 1
    value |= 1u;
    EXPECT_EQ(value & 1u, 1u);

    // Simulate embedding bitstream [1,0,1,1] into 4 bytes
    uint8_t bytes[4] = {0b00000000, 0b00000000, 0b00000000, 0b00000000};
    uint8_t bits[4] = {1, 0, 1, 1};

    for (int i = 0; i < 4; ++i) {
        bytes[i] &= (uint8_t)~1u;
        bytes[i] |= bits[i] & 1u;
    }

    EXPECT_EQ(bytes[0] & 1u, 1u);
    EXPECT_EQ(bytes[1] & 1u, 0u);
    EXPECT_EQ(bytes[2] & 1u, 1u);
    EXPECT_EQ(bytes[3] & 1u, 1u);
}

// 2) Encode and decode a short message in a small synthetic image.
TEST(StegIntegrationTest, EncodeDecodeRoundTrip)
{
    BmpImage img;
    create_test_image(16, 16, 100, 100, 100, &img); // uniform image

    const char *msg = "Hello, world!";
    size_t msg_len = std::strlen(msg);

    int block_size = 4;
    double contrast_threshold = 1.0; // uniform -> very low contrast

    int rc = steg_encode_message(&img,
                                 (const uint8_t *)msg,
                                 msg_len,
                                 block_size,
                                 contrast_threshold);
    EXPECT_EQ(rc, 0);

    uint8_t *decoded = nullptr;
    size_t decoded_len = 0;

    rc = steg_decode_message(&img, &decoded, &decoded_len,
                             block_size, contrast_threshold);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decoded_len, msg_len);
    EXPECT_EQ(std::memcmp(decoded, msg, msg_len), 0);

    std::free(decoded);
    bmp_free(&img);
}

// 3) Verify error when message is too large for capacity.
TEST(StegIntegrationTest, TooLargeMessageFails)
{
    BmpImage img;
    create_test_image(4, 4, 50, 50, 50, &img); // small image

    // Max bits = width * height * 3 * (due to low contrast selection).
    // For a 4x4 image, that's 4*4*3 = 48 bits = 6 bytes. Header uses 4 bytes,
    // so at most 2 bytes of message are possible. Here we use a lot more.
    size_t msg_len = 100;
    uint8_t *msg = (uint8_t *)std::malloc(msg_len);
    ASSERT_NE(msg, nullptr);
    std::memset(msg, 'A', msg_len);

    int block_size = 2;
    double contrast_threshold = 1.0;

    int rc = steg_encode_message(&img, msg, msg_len, block_size, contrast_threshold);
    EXPECT_EQ(rc, -1); // capacity insufficient

    std::free(msg);
    bmp_free(&img);
}


