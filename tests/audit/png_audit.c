#include <png.h>
#include <setjmp.h>
#include <spng.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_LIMIT (4u * 1000u * 1000u)

enum audit_mode
{
    MODE_BUF_RGBA8,
    MODE_BUF_RGBA16,
    MODE_STREAM_RGBA8,
    MODE_STREAM_PROGRESSIVE_RGBA8,
};

struct buffer_state
{
    const unsigned char *data;
    size_t bytes_left;
    size_t total;
};

struct chunk_info
{
    char type[5];
    size_t offset;
    uint32_t length;
    size_t payload;
    int truncated;
};

struct chunk_table
{
    struct chunk_info *items;
    size_t count;
    size_t file_size;
    int has_signature;
};

static char png_error_message[256];

static const char *mode_name(int mode)
{
    switch(mode)
    {
        case MODE_BUF_RGBA8: return "buffer-rgba8";
        case MODE_BUF_RGBA16: return "buffer-rgba16";
        case MODE_STREAM_RGBA8: return "stream-rgba8";
        case MODE_STREAM_PROGRESSIVE_RGBA8: return "stream-progressive-rgba8";
        default: return "?";
    }
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f;
    long len;
    unsigned char *buf;

    f = fopen(path, "rb");
    if(f == NULL) return NULL;
    if(fseek(f, 0, SEEK_END)) goto err;
    len = ftell(f);
    if(len < 0) goto err;
    if(fseek(f, 0, SEEK_SET)) goto err;

    buf = malloc((size_t)len ? (size_t)len : 1);
    if(buf == NULL) goto err;
    if(len && fread(buf, (size_t)len, 1, f) != 1)
    {
        free(buf);
        goto err;
    }
    fclose(f);
    *size = (size_t)len;
    return buf;

err:
    fclose(f);
    return NULL;
}

static uint32_t read_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void free_chunk_table(struct chunk_table *table)
{
    free(table->items);
    memset(table, 0, sizeof(*table));
}

static int parse_chunk_table(const unsigned char *data, size_t size,
                             struct chunk_table *table)
{
    size_t pos;
    size_t capacity = 0;

    memset(table, 0, sizeof(*table));
    table->file_size = size;
    table->has_signature = size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8);
    if(!table->has_signature) return 0;

    pos = 8;
    while(pos + 8 <= size)
    {
        uint32_t length = read_u32(data + pos);
        size_t payload = pos + 8;
        struct chunk_info *item;

        if(table->count == capacity)
        {
            size_t new_capacity = capacity ? capacity * 2 : 32;
            struct chunk_info *new_items = realloc(table->items,
                                                  new_capacity * sizeof(*new_items));
            if(new_items == NULL) return -1;
            table->items = new_items;
            capacity = new_capacity;
        }

        item = &table->items[table->count++];
        memset(item, 0, sizeof(*item));
        memcpy(item->type, data + pos + 4, 4);
        item->type[4] = '\0';
        item->offset = pos;
        item->length = length;
        item->payload = payload;

        if(payload > size || length > size - payload ||
           4 > size - payload - (size_t)length)
        {
            item->truncated = 1;
            break;
        }
        pos = payload + length + 4;
    }
    return 0;
}

static struct chunk_info *find_type(struct chunk_table *table, const char *type)
{
    size_t i;
    for(i = 0; i < table->count; i++)
    {
        if(!strcmp(table->items[i].type, type)) return &table->items[i];
    }
    return NULL;
}

static void set_locus(char type[5], size_t *offset, const char *name,
                      size_t off)
{
    snprintf(type, 5, "%s", name);
    type[4] = '\0';
    *offset = off;
}

static void locate_failure(int ret, struct chunk_table *table,
                           const char *mutation_type, size_t mutation_offset,
                           char type[5], size_t *offset)
{
    size_t i;
    struct chunk_info *idat;

    if(ret == SPNG_IO_EOF || ret == SPNG_IO_ERROR || ret == SPNG_EOF)
    {
        if(table->file_size < 8)
        {
            set_locus(type, offset, "SIG", 0);
            return;
        }
        for(i = 0; i < table->count; i++)
        {
            if(table->items[i].truncated)
            {
                set_locus(type, offset, table->items[i].type,
                          table->items[i].offset);
                return;
            }
        }
        if(find_type(table, "IEND") == NULL)
        {
            set_locus(type, offset, "EOF", table->file_size);
            return;
        }
        set_locus(type, offset, "IEND", find_type(table, "IEND")->offset);
        return;
    }

    switch(ret)
    {
        case SPNG_ESIGNATURE:
        case SPNG_EWIDTH:
        case SPNG_EHEIGHT:
        case SPNG_EUSER_WIDTH:
        case SPNG_EUSER_HEIGHT:
        case SPNG_EBIT_DEPTH:
        case SPNG_ECOLOR_TYPE:
        case SPNG_ECOMPRESSION_METHOD:
        case SPNG_EFILTER_METHOD:
        case SPNG_EINTERLACE_METHOD:
        case SPNG_EIHDR_SIZE:
        case SPNG_ENOIHDR:
            set_locus(type, offset, "IHDR", 8);
            return;
        default:
            break;
    }

    if(ret == SPNG_EIDAT_TOO_SHORT || ret == SPNG_EIDAT_STREAM ||
       ret == SPNG_EZLIB || ret == SPNG_EFILTER || ret == SPNG_EBUFSIZ)
    {
        if(!strcmp(mutation_type, "IDAT"))
        {
            set_locus(type, offset, "IDAT", mutation_offset);
            return;
        }
        idat = find_type(table, "IDAT");
        if(idat != NULL)
        {
            set_locus(type, offset, "IDAT", idat->offset);
            return;
        }
    }

    if(mutation_type[0] != '\0' && strcmp(mutation_type, "-"))
    {
        set_locus(type, offset, mutation_type, mutation_offset);
        return;
    }

    if(table->count)
    {
        struct chunk_info *last = &table->items[table->count - 1];
        set_locus(type, offset, last->type, last->offset);
    }
    else set_locus(type, offset, "EOF", 0);
}

static int stream_read(spng_ctx *ctx, void *user, void *dest, size_t length)
{
    struct buffer_state *state = user;
    (void)ctx;

    if(length > state->bytes_left) return SPNG_IO_EOF;
    memcpy(dest, state->data, length);
    state->bytes_left -= length;
    state->data += length;
    return 0;
}

static int decode_spng(const unsigned char *data, size_t size, int mode,
                       unsigned char *out, size_t out_capacity,
                       size_t *out_size, struct spng_ihdr *ihdr_out)
{
    spng_ctx *ctx = NULL;
    struct buffer_state state;
    struct spng_ihdr ihdr;
    int ret;
    int fmt;
    int progressive;
    size_t calculated_size;

    fmt = (mode == MODE_BUF_RGBA16) ? SPNG_FMT_RGBA16 : SPNG_FMT_RGBA8;
    progressive = (mode == MODE_STREAM_PROGRESSIVE_RGBA8);

    ctx = spng_ctx_new(0);
    if(ctx == NULL) return SPNG_EINTERNAL;

    if(mode == MODE_BUF_RGBA8 || mode == MODE_BUF_RGBA16)
    {
        ret = spng_set_png_buffer(ctx, data, size);
    }
    else
    {
        state.data = data;
        state.bytes_left = size;
        state.total = size;
        ret = spng_set_png_stream(ctx, stream_read, &state);
    }
    if(ret) goto out;

    ret = spng_set_image_limits(ctx, 16000, 16000);
    if(ret) goto out;
    ret = spng_set_chunk_limits(ctx, 8u * 1000u * 1000u, 16u * 1000u * 1000u);
    if(ret) goto out;
    ret = spng_set_crc_action(ctx, SPNG_CRC_ERROR, SPNG_CRC_ERROR);
    if(ret) goto out;
    ret = spng_set_option(ctx, SPNG_KEEP_UNKNOWN_CHUNKS, 1);
    if(ret) goto out;

    ret = spng_decoded_image_size(ctx, fmt, &calculated_size);
    if(ret) goto out;
    *out_size = calculated_size;
    if(calculated_size > out_capacity || calculated_size > OUT_LIMIT)
    {
        ret = SPNG_EBUFSIZ;
        goto out;
    }

    ret = spng_get_ihdr(ctx, &ihdr);
    if(ret) goto out;
    if(ihdr_out != NULL) *ihdr_out = ihdr;

    if(progressive)
    {
        struct spng_row_info row_info;
        size_t row_size;
        if(ihdr.height == 0)
        {
            ret = SPNG_EHEIGHT;
            goto out;
        }
        row_size = calculated_size / ihdr.height;

        ret = spng_decode_image(ctx, NULL, 0, fmt,
                                SPNG_DECODE_TRNS | SPNG_DECODE_PROGRESSIVE);
        if(ret) goto out;

        for(;;)
        {
            ret = spng_get_row_info(ctx, &row_info);
            if(ret) break;
            if(row_info.row_num >= ihdr.height)
            {
                ret = SPNG_EINTERNAL;
                break;
            }
            ret = spng_decode_row(ctx, out + row_info.row_num * row_size,
                                  row_size);
            if(ret) break;
            if(ret == SPNG_EOI) break;
        }
        if(ret != SPNG_EOI) goto out;
        ret = 0;
    }
    else
    {
        ret = spng_decode_image(ctx, out, calculated_size, fmt,
                                SPNG_DECODE_TRNS);
        if(ret) goto out;
    }

    ret = spng_decode_chunks(ctx);

out:
    spng_ctx_free(ctx);
    return ret;
}

static void png_error_handler(png_structp png_ptr, png_const_charp message)
{
    (void)png_ptr;
    snprintf(png_error_message, sizeof(png_error_message), "%s", message);
    longjmp(png_jmpbuf(png_ptr), 1);
}

static void png_warning_handler(png_structp png_ptr, png_const_charp message)
{
    (void)png_ptr;
    (void)message;
}

static void png_read_from_memory(png_structp png_ptr, png_bytep dest,
                                 png_size_t length)
{
    struct buffer_state *state = png_get_io_ptr(png_ptr);
    if(length > state->bytes_left) png_error(png_ptr, "read past end");
    memcpy(dest, state->data, length);
    state->data += length;
    state->bytes_left -= length;
}

static int decode_libpng(const unsigned char *data, size_t size, int mode,
                         unsigned char *out, size_t out_capacity,
                         size_t *out_size)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep *volatile rows = NULL;
    unsigned char *volatile image = NULL;
    struct buffer_state state;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace, compression, filter;
    size_t rowbytes, total;
    png_uint_32 y;
    volatile int ret = 0;

    png_error_message[0] = '\0';
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
                                      png_error_handler, png_warning_handler);
    if(png_ptr == NULL) return -1;
    info_ptr = png_create_info_struct(png_ptr);
    if(info_ptr == NULL)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return -1;
    }

    if(setjmp(png_jmpbuf(png_ptr)))
    {
        ret = -1;
        goto out;
    }

    state.data = data;
    state.bytes_left = size;
    state.total = size;
    png_set_read_fn(png_ptr, &state, png_read_from_memory);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                 &interlace, &compression, &filter);

    if(mode == MODE_BUF_RGBA8 || mode == MODE_STREAM_RGBA8 ||
       mode == MODE_STREAM_PROGRESSIVE_RGBA8)
    {
        png_set_gray_to_rgb(png_ptr);
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
        png_set_expand(png_ptr);
        png_set_strip_16(png_ptr);
    }
    else
    {
        png_set_gray_to_rgb(png_ptr);
        png_set_filler(png_ptr, 0xFFFF, PNG_FILLER_AFTER);
        png_set_expand_16(png_ptr);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        png_set_swap(png_ptr);
#endif
    }

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);
    rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    total = rowbytes * height;
    if(total > out_capacity || total > OUT_LIMIT)
    {
        ret = -2;
        goto out;
    }

    image = calloc(1, total ? total : 1);
    rows = malloc(height ? height * sizeof(png_bytep) : sizeof(png_bytep));
    if(image == NULL || rows == NULL)
    {
        ret = -1;
        goto out;
    }
    for(y = 0; y < height; y++) rows[y] = image + y * rowbytes;

    png_read_image(png_ptr, (png_bytep*)rows);
    png_read_end(png_ptr, NULL);
    memcpy(out, image, total);
    *out_size = total;

out:
    free(rows);
    free(image);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return ret;
}

static const char *error_detail(int ret)
{
    return spng_strerror(ret);
}

static void report_malformed(const char *category, const char *path,
                             int mode, int ret, const char *mutation_type,
                             size_t mutation_offset,
                             struct chunk_table *table, size_t decoded_size)
{
    char type[5];
    size_t offset;

    if(ret)
    {
        locate_failure(ret, table, mutation_type, mutation_offset,
                       type, &offset);
        printf("malformed\t%s\t%s\t%s\trejected\t%d\t%s@%zu\t%zu\t"
               "%s@%zu\t%s\n",
               category, path, mode_name(mode), ret, type, offset, offset,
               mutation_type, mutation_offset, error_detail(ret));
    }
    else
    {
        struct chunk_info *iend = find_type(table, "IEND");
        if(iend != NULL)
        {
            snprintf(type, sizeof(type), "%s", iend->type);
            offset = iend->offset;
        }
        else
        {
            snprintf(type, sizeof(type), "EOF");
            offset = table->file_size;
        }
        printf("malformed\t%s\t%s\t%s\taccepted\t0\t%s@%zu\t%zu\t"
               "%s@%zu\t%zu\tdecoded=%zu\n",
               category, path, mode_name(mode), type, offset, offset,
               mutation_type, mutation_offset, mutation_offset, decoded_size);
    }
    fflush(stdout);
}

static void first_difference(const unsigned char *a, const unsigned char *b,
                             size_t size, size_t *index)
{
    size_t i;
    *index = size;
    for(i = 0; i < size; i++)
    {
        if(a[i] != b[i])
        {
            *index = i;
            return;
        }
    }
}

static void report_valid(const char *path, int mode, int spng_ret,
                         const unsigned char *spng_image, size_t spng_size,
                         struct spng_ihdr *ihdr,
                         const unsigned char *png_image, size_t png_size)
{
    if(spng_ret)
    {
        printf("valid\tvalid\t%s\t%s\tspng_error\t%d\t-\t-\t%s\t%s\n",
               path, mode_name(mode), spng_ret, "-", error_detail(spng_ret));
        fflush(stdout);
        return;
    }

    if(spng_size != png_size)
    {
        printf("valid\tvalid\t%s\t%s\tmismatch\t0\t%zu\t%zu\t%s\t%s\n",
               path, mode_name(mode), spng_size, png_size, "-",
               "decoded size differs");
        fflush(stdout);
        return;
    }

    {
        size_t index;
        int bpp = (mode == MODE_BUF_RGBA16) ? 8 : 4;
        int bytes_per_channel = (mode == MODE_BUF_RGBA16) ? 2 : 1;
        first_difference(spng_image, png_image, spng_size, &index);
        if(index != spng_size)
        {
            size_t pixel = index / (size_t)bpp;
            size_t channel_byte = index % (size_t)bpp;
            printf("valid\tvalid\t%s\t%s\tmismatch\t0\t%zu\t%zu\t"
                   "first=%zu,spng=%u,libpng=%u,x=%zu,y=%zu,channel=%zu\t%s\n",
                   path, mode_name(mode), spng_size, png_size, index,
                   spng_image[index], png_image[index],
                   pixel % ihdr->width, pixel / ihdr->width,
                   channel_byte / (size_t)bytes_per_channel, "-");
            fflush(stdout);
            return;
        }
    }

    printf("valid\tvalid\t%s\t%s\tmatch\t0\t%zu\t%zu\t%s\t%s\n",
           path, mode_name(mode), spng_size, png_size, "-",
           "pixel-exact");
    fflush(stdout);
}

static int run_malformed(const char *root, const char *category,
                         const char *path, const char *mutation_type,
                         size_t mutation_offset)
{
    char full_path[4096];
    unsigned char *data;
    size_t size;
    int modes[] = {
        MODE_BUF_RGBA8, MODE_BUF_RGBA16,
        MODE_STREAM_RGBA8, MODE_STREAM_PROGRESSIVE_RGBA8
    };
    size_t m;

    snprintf(full_path, sizeof(full_path), "%s/%s", root, path);
    data = read_file(full_path, &size);
    if(data == NULL)
    {
        fprintf(stderr, "read failed: %s\n", full_path);
        return 1;
    }

    for(m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
    {
        struct chunk_table table;
        struct spng_ihdr ihdr;
        unsigned char *out;
        size_t out_size = 0;
        int ret;

        if(parse_chunk_table(data, size, &table))
        {
            free(data);
            return 1;
        }
        out = malloc(OUT_LIMIT);
        if(out == NULL)
        {
            free_chunk_table(&table);
            free(data);
            return 1;
        }
        memset(&ihdr, 0, sizeof(ihdr));
        ret = decode_spng(data, size, modes[m], out, OUT_LIMIT,
                          &out_size, &ihdr);
        report_malformed(category, path, modes[m], ret, mutation_type,
                         mutation_offset, &table, out_size);
        free(out);
        free_chunk_table(&table);
    }
    free(data);
    return 0;
}

static int run_valid(const char *root, const char *path)
{
    char full_path[4096];
    unsigned char *data;
    size_t size;
    int modes[] = {
        MODE_BUF_RGBA8, MODE_BUF_RGBA16,
        MODE_STREAM_PROGRESSIVE_RGBA8
    };
    size_t m;

    snprintf(full_path, sizeof(full_path), "%s/%s", root, path);
    data = read_file(full_path, &size);
    if(data == NULL)
    {
        fprintf(stderr, "read failed: %s\n", full_path);
        return 1;
    }

    for(m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
    {
        struct spng_ihdr ihdr;
        unsigned char *spng_image = NULL;
        unsigned char *png_image = NULL;
        size_t spng_size = 0, png_size = 0;
        int ret;
        int ref_ret;

        memset(&ihdr, 0, sizeof(ihdr));
        spng_image = malloc(OUT_LIMIT);
        png_image = malloc(OUT_LIMIT);
        if(spng_image == NULL || png_image == NULL)
        {
            free(spng_image);
            free(png_image);
            free(data);
            return 1;
        }

        ret = decode_spng(data, size, modes[m], spng_image, OUT_LIMIT,
                          &spng_size, &ihdr);
        if(ret)
        {
            report_valid(path, modes[m], ret, NULL, 0, &ihdr, NULL, 0);
            free(spng_image);
            free(png_image);
            continue;
        }

        ref_ret = decode_libpng(data, size, modes[m], png_image, OUT_LIMIT,
                                &png_size);
        if(ref_ret)
        {
            printf("valid\tvalid\t%s\t%s\treference_error\t%d\t-\t-\t%s\t%s\n",
                   path, mode_name(modes[m]), ref_ret, "-",
                   png_error_message[0] ? png_error_message : "libpng failed");
            fflush(stdout);
        }
        else report_valid(path, modes[m], 0, spng_image, spng_size, &ihdr,
                          png_image, png_size);

        free(spng_image);
        free(png_image);
    }
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *manifest;
    char line[8192];
    int malformed_runs = 0, valid_runs = 0;

    if(argc != 3)
    {
        fprintf(stderr, "usage: %s CORPUS_ROOT MANIFEST.tsv\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    manifest = fopen(argv[2], "r");
    if(manifest == NULL)
    {
        perror("manifest");
        return 2;
    }

    while(fgets(line, sizeof(line), manifest))
    {
        char *fields[6];
        char *p = line;
        int i;
        size_t len = strlen(line);

        while(len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if(!len) continue;

        fields[0] = p;
        for(i = 1; i < 6; i++)
        {
            p = strchr(p, '\t');
            if(p == NULL)
            {
                fprintf(stderr, "bad manifest line: %s\n", line);
                fclose(manifest);
                return 2;
            }
            *p++ = '\0';
            fields[i] = p;
        }

        if(!strcmp(fields[0], "malformed"))
        {
            if(run_malformed(argv[1], fields[1], fields[4], fields[2],
                             (size_t)strtoull(fields[3], NULL, 10)))
            {
                fclose(manifest);
                return 1;
            }
            malformed_runs++;
        }
        else if(!strcmp(fields[0], "valid"))
        {
            if(run_valid(argv[1], fields[4]))
            {
                fclose(manifest);
                return 1;
            }
            valid_runs++;
        }
        else
        {
            fprintf(stderr, "unknown manifest kind: %s\n", fields[0]);
            fclose(manifest);
            return 2;
        }
    }

    fclose(manifest);
    fprintf(stderr, "processed %d malformed images and %d valid images\n",
            malformed_runs, valid_runs);
    return 0;
}
