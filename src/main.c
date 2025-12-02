// main.c - Simple CLI for BMP LSB steganography with low-contrast selection.
//
// Usage:
//   Encode: steg_cli encode <input_bmp> <input_txt> <output_bmp>
//   Decode: steg_cli decode <input_bmp> <output_txt>

#include "bmp.h"
#include "steg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file_to_buffer(const char *path, unsigned char **buf_out, size_t *len_out)
{
    *buf_out = NULL;
    *len_out = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("read_file_to_buffer: fopen");
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("read_file_to_buffer: fseek");
        fclose(f);
        return 1;
    }

    long size = ftell(f);
    if (size < 0) {
        perror("read_file_to_buffer: ftell");
        fclose(f);
        return 1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        perror("read_file_to_buffer: fseek");
        fclose(f);
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        perror("read_file_to_buffer: malloc");
        fclose(f);
        return 1;
    }

    size_t read_count = fread(buf, 1, (size_t)size, f);
    if (read_count != (size_t)size) {
        fprintf(stderr, "read_file_to_buffer: short read\n");
        free(buf);
        fclose(f);
        return 1;
    }

    fclose(f);

    *buf_out = buf;
    *len_out = (size_t)size;
    return 0;
}

static int write_buffer_to_file(const char *path, const unsigned char *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("write_buffer_to_file: fopen");
        return 1;
    }

    size_t written = fwrite(buf, 1, len, f);
    if (written != len) {
        fprintf(stderr, "write_buffer_to_file: short write\n");
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s encode <input_bmp> <input_txt> <output_bmp>\n"
            "  %s decode <input_bmp> <output_txt>\n",
            prog, prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *mode = argv[1];

    // Reasonable defaults
    const int block_size = 8;
    const double contrast_threshold = 5.0;

    if (strcmp(mode, "encode") == 0) {
        if (argc != 5) {
            print_usage(argv[0]);
            return 1;
        }

        const char *input_bmp = argv[2];
        const char *input_txt = argv[3];
        const char *output_bmp = argv[4];

        BmpImage img;
        if (bmp_load(input_bmp, &img) != 0) {
            fprintf(stderr, "Failed to load input BMP '%s'\n", input_bmp);
            return 1;
        }

        unsigned char *message = NULL;
        size_t message_len = 0;

        if (read_file_to_buffer(input_txt, &message, &message_len) != 0) {
            fprintf(stderr, "Failed to read input text '%s'\n", input_txt);
            bmp_free(&img);
            return 1;
        }

        int rc = steg_encode_message(&img, message, message_len,
                                     block_size, contrast_threshold);
        if (rc != 0) {
            if (rc == -1) {
                fprintf(stderr, "Error: message too large for cover image\n");
            } else {
                fprintf(stderr, "Error: steg_encode_message failed (code %d)\n", rc);
            }
            free(message);
            bmp_free(&img);
            return 1;
        }

        if (bmp_save(output_bmp, &img) != 0) {
            fprintf(stderr, "Failed to save output BMP '%s'\n", output_bmp);
            free(message);
            bmp_free(&img);
            return 1;
        }

        free(message);
        bmp_free(&img);
        return 0;

    } else if (strcmp(mode, "decode") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }

        const char *input_bmp = argv[2];
        const char *output_txt = argv[3];

        BmpImage img;
        if (bmp_load(input_bmp, &img) != 0) {
            fprintf(stderr, "Failed to load input BMP '%s'\n", input_bmp);
            return 1;
        }

        uint8_t *message = NULL;
        size_t message_len = 0;

        int rc = steg_decode_message(&img, &message, &message_len,
                                     block_size, contrast_threshold);
        if (rc != 0) {
            fprintf(stderr, "Error: steg_decode_message failed (code %d)\n", rc);
            bmp_free(&img);
            return 1;
        }

        if (write_buffer_to_file(output_txt, message, message_len) != 0) {
            fprintf(stderr, "Failed to write output text '%s'\n", output_txt);
            free(message);
            bmp_free(&img);
            return 1;
        }

        free(message);
        bmp_free(&img);
        return 0;

    } else {
        print_usage(argv[0]);
        return 1;
    }
}


