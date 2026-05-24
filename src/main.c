#include <libcart/cart.h>
#include <libdragon.h>

#include <eeprom.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "accessory.h"
#include "pif.h"

#define CART_ROM_BASE 0x10000000
#define CART_HEADER_SIZE 0x40
#define CART_TITLE_OFFSET 0x20
#define CART_TITLE_LENGTH 20
#define CART_PRODUCT_OFFSET 0x3B
#define CART_PRODUCT_LENGTH 4
#define CART_REVISION_OFFSET (CART_PRODUCT_OFFSET + CART_PRODUCT_LENGTH)
#define CART_REGION_OFFSET 0x3E
#define CART_MIN_SIZE 0x400000
#define CART_MAX_SIZE 0x4000000
#define CART_SIZE_STEP 0x400000
#define CART_SIZE_PROBE_BLOCK 0x1000
#define CART_CRC1_OFFSET 0x10
#define CART_CRC2_OFFSET 0x14
#define CART_CHECKSUM_START 0x1000
#define CART_CHECKSUM_LENGTH 0x100000
#define CART_CHECKSUM_IMAGE_SIZE (CART_CHECKSUM_START + CART_CHECKSUM_LENGTH)
#define CART_CIC6105_TABLE_OFFSET (CART_HEADER_SIZE + 0x0710)
#define CART_SAFE_PI_CONFIG 0x8030FFFF

#define COMP_BLOCK_SIZE 0x10000
#define COMP_HASH_BITS 15
#define COMP_HASH_SIZE (1 << COMP_HASH_BITS)
#define COMP_MIN_MATCH 4
#define COMP_SKIP_STRENGTH 5
#define COMP_RAW_FLAG 0x80000000u
#define CRC32_POLY 0xEDB88320u
#define CHECKSUM_READ_SIZE 0x100000
#define PROGRESS_BAR_WIDTH 40
#define ROM_BUFFER_ALIGN 0x10000

#define PI_BSD_DOM1_LAT_REG ((volatile uint32_t *)0xA4600014)
#define PI_BSD_DOM1_PWD_REG ((volatile uint32_t *)0xA4600018)
#define PI_BSD_DOM1_PGS_REG ((volatile uint32_t *)0xA460001C)
#define PI_BSD_DOM1_RLS_REG ((volatile uint32_t *)0xA4600020)
#define PI_BSD_DOM2_LAT_REG ((volatile uint32_t *)0xA4600024)
#define PI_BSD_DOM2_PWD_REG ((volatile uint32_t *)0xA4600028)
#define PI_BSD_DOM2_PGS_REG ((volatile uint32_t *)0xA460002C)
#define PI_BSD_DOM2_RLS_REG ((volatile uint32_t *)0xA4600030)
#define PI_STATUS_DMA_BUSY 0x01
#define PI_STATUS_IO_BUSY 0x02

#define SAVE_SRAM_SIZE 0x8000
#define SAVE_FLASH_SIZE 0x20000
#define SAVE_EEPROM_4K_SIZE 0x200
#define SAVE_EEPROM_16K_SIZE 0x800
#define CART_DOM2_ADDR2_START 0x08000000
#define FLASHRAM_IDENTIFIER 0x11118001
#define FLASHRAM_OFFSET_COMMAND 0x00010000
#define FLASHRAM_COMMAND_SET_IDENTIFY_MODE 0xE1000000
#define FLASHRAM_COMMAND_SET_READ_MODE 0xF0000000
#define ROM_DUMP_DIR "sd:/dump"
#define SAVE_DUMP_DIR "sd:/saves"

joypad_inputs_t inputs = {0}, prev_inputs;
#define INPUT(inp) ((inputs.inp) && !(prev_inputs.inp))

static bool pif_hung = false;

typedef enum {
    CART_CRC_KIND_6102,
    CART_CRC_KIND_6103,
    CART_CRC_KIND_6105,
    CART_CRC_KIND_6106,
} cart_crc_kind_t;

typedef struct {
    const char *name;
    uint32_t seed;
    cart_crc_kind_t kind;
} cart_crc_variant_t;

typedef struct {
    const char *cic_name;
    uint32_t expected_crc1;
    uint32_t expected_crc2;
    uint32_t calculated_crc1;
    uint32_t calculated_crc2;
} cart_crc_result_t;

typedef enum {
    DUMP_MODE_ROM_ONLY,
    DUMP_MODE_SAVE_ONLY,
    DUMP_MODE_ROM_AND_SAVE,
} dump_mode_t;

typedef enum {
    SAVE_KIND_NONE,
    SAVE_KIND_EEPROM,
    SAVE_KIND_FLASH,
    SAVE_KIND_SRAM,
} save_kind_t;

typedef struct {
    uint8_t header[CART_HEADER_SIZE];
    char title[CART_TITLE_LENGTH + 1];
    char product[CART_PRODUCT_LENGTH + 1];
    uint8_t revision;
    char rom_path[32];
    char save_path[32];
    uint8_t region;
    size_t detected_size;
    size_t selected_size;
} cart_info_t;

typedef struct {
    save_kind_t kind;
    const char *name;
    size_t size;
} save_info_t;

typedef struct {
    size_t raw_start;
    size_t raw_size;
    size_t comp_size;
} compressed_pass_t;

typedef struct {
    uint32_t raw_size;
    uint32_t payload_size;
} comp_block_header_t;

typedef struct {
    cart_info_t *cart;
    size_t read_addr;
    size_t write_addr;
    size_t total_size;
} dump_progress_t;

static dump_progress_t dprog;

static joypad_inputs_t read_controller(void) {
    static const uint64_t si_read_controller_block[8] = {
        0xff010401ffffffff,
        0xff010401ffffffff,
        0xff010401ffffffff,
        0xff010401ffffffff,
        0xfe00000000000000,
        0,
        0,
        1,
    };

    struct controller_data data = {0};
    joybus_exec(si_read_controller_block, &data);

    joypad_inputs_t out = {0};
    if (data.c[0].err == ERROR_NONE) {
        out.btn.a = data.c[0].A;
        out.btn.b = data.c[0].B;
        out.btn.d_left = data.c[0].left;
        out.btn.d_right = data.c[0].right;
    }
    return out;
}

static void poll_controller(void) {
    prev_inputs = inputs;
    inputs = read_controller();
}

static void wait_for_a_press(void) {
    do {
        poll_controller();
        wait_ms(16);
    } while (inputs.btn.a);

    do {
        poll_controller();
        wait_ms(16);
    } while (!INPUT(btn.a));
}

static void wait_pi_idle(void) {
    while (*PI_STATUS & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY)) {
        continue;
    }
}

static void set_dom1_pi_config(uint32_t config) {
    wait_pi_idle();
    *PI_BSD_DOM1_LAT_REG = config >> 0;
    *PI_BSD_DOM1_PWD_REG = config >> 8;
    *PI_BSD_DOM1_PGS_REG = config >> 16;
    *PI_BSD_DOM1_RLS_REG = config >> 20;
    MEMORY_BARRIER();
}

static void set_dom2_save_config(void) {
    wait_pi_idle();
    *PI_BSD_DOM2_LAT_REG = 0x05;
    *PI_BSD_DOM2_PWD_REG = 0x0C;
    *PI_BSD_DOM2_PGS_REG = 0x0D;
    *PI_BSD_DOM2_RLS_REG = 0x02;
    MEMORY_BARRIER();
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           ((uint32_t)data[3] << 0);
}

static uint32_t pi_config_from_header(const uint8_t *header) {
    return read_be32(header);
}

static bool cart_header_valid(const uint8_t *header) {
    return header[0] == 0x80 &&
           header[1] == 0x37 &&
           header[2] == 0x12 &&
           header[3] == 0x40;
}

static char printable_header_char(uint8_t ch) {
    return (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '_';
}

static char revision_suffix(uint8_t revision) {
    if (revision == 0 || revision > 26) {
        return '\0';
    }

    return (char)('A' + revision - 1);
}

static void print_revision_suffix(uint8_t revision) {
    const char suffix = revision_suffix(revision);
    if (suffix != '\0') {
        printf(" (%c)", suffix);
    }
}

static void format_cart_file_path(char *out, size_t out_size, const cart_info_t *info,
                                  const char *dir, const char *ext) {
    const char suffix = revision_suffix(info->revision);
    if (suffix != '\0') {
        snprintf(out, out_size, "%s/%s%c.%s", dir, info->product, suffix, ext);
    } else {
        snprintf(out, out_size, "%s/%s.%s", dir, info->product, ext);
    }
}

static void set_cart_paths(cart_info_t *info) {
    format_cart_file_path(info->rom_path, sizeof(info->rom_path), info, "sd:", "z64");
    format_cart_file_path(info->save_path, sizeof(info->save_path), info, "sd:", "sav");
}

static void parse_cart_info(cart_info_t *info, const uint8_t *header) {
    memcpy(info->header, header, CART_HEADER_SIZE);

    for (size_t i = 0; i < CART_TITLE_LENGTH; i++) {
        info->title[i] = printable_header_char(header[CART_TITLE_OFFSET + i]);
    }
    info->title[CART_TITLE_LENGTH] = '\0';

    for (int i = CART_TITLE_LENGTH - 1; i >= 0; i--) {
        if (info->title[i] != ' ' && info->title[i] != '_') {
            break;
        }
        info->title[i] = '\0';
    }

    for (size_t i = 0; i < CART_PRODUCT_LENGTH; i++) {
        info->product[i] = printable_header_char(header[CART_PRODUCT_OFFSET + i]);
    }
    info->product[CART_PRODUCT_LENGTH] = '\0';
    info->revision = header[CART_REVISION_OFFSET];
    info->region = header[CART_REGION_OFFSET];

    set_cart_paths(info);
}

static void cart_dma_read(void *ram, size_t cart_offset, size_t len) {
    data_cache_hit_writeback_invalidate(ram, len);
    dma_read(ram, CART_ROM_BASE + cart_offset, len);
}

static void cart_dom2_read(void *ram, size_t cart_offset, size_t len) {
    set_dom2_save_config();
    data_cache_hit_writeback_invalidate(ram, len);
    dma_read_raw_async(ram, CART_DOM2_ADDR2_START + cart_offset, len);
    dma_wait();
}

static uint32_t read_u32_unaligned(const uint8_t *data) {
    return ((uint32_t)data[0] << 0) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t comp_hash4(const uint8_t *data) {
    return (read_u32_unaligned(data) * 2654435761u) >> (32 - COMP_HASH_BITS);
}

static size_t comp_match_len(const uint8_t *a, const uint8_t *b,
                             size_t max_len) {
    size_t len = 0;

    while ((len + 4) <= max_len &&
           read_u32_unaligned(a + len) == read_u32_unaligned(b + len)) {
        len += 4;
    }

    while (len < max_len && a[len] == b[len]) {
        len++;
    }

    return len;
}

static size_t comp_find_and_insert_match(const uint8_t *src, size_t src_len, size_t pos,
                                         int32_t *head, size_t *match_offset) {
    const uint32_t hash = comp_hash4(src + pos);
    const int32_t candidate = head[hash];
    head[hash] = (int32_t)pos;

    if (candidate < 0) {
        return 0;
    }

    const size_t cand = (size_t)candidate;
    const size_t offset = pos - cand;

    if (offset == 0 || offset > UINT16_MAX ||
        read_u32_unaligned(src + cand) != read_u32_unaligned(src + pos)) {
        return 0;
    }

    *match_offset = offset;
    return comp_match_len(src + cand, src + pos, src_len - pos);
}

static bool comp_write_len(uint8_t *dst, size_t dst_cap, size_t *out_pos, size_t len) {
    while (len >= 255) {
        if (*out_pos >= dst_cap) {
            return false;
        }
        dst[(*out_pos)++] = 255;
        len -= 255;
    }

    if (*out_pos >= dst_cap) {
        return false;
    }
    dst[(*out_pos)++] = (uint8_t)len;
    return true;
}

static size_t comp_lz4_block(const uint8_t *src, size_t src_len, uint8_t *dst,
                             size_t dst_cap, int32_t *head) {
    memset(head, 0xFF, COMP_HASH_SIZE * sizeof(head[0]));

    size_t in_pos = 0;
    size_t anchor = 0;
    size_t out_pos = 0;
    size_t misses = 0;

    while ((in_pos + COMP_MIN_MATCH) <= src_len) {
        size_t match_offset = 0;
        size_t match_len = comp_find_and_insert_match(src, src_len, in_pos,
                                                      head, &match_offset);

        if (match_len < COMP_MIN_MATCH) {
            size_t step = 1 + (misses >> COMP_SKIP_STRENGTH);
            if (step > 16) {
                step = 16;
            }
            in_pos += step;
            misses++;
            continue;
        }

        misses = 0;
        const size_t token_pos = out_pos++;
        if (token_pos >= dst_cap) {
            return 0;
        }

        const size_t lit_len = in_pos - anchor;
        uint8_t token = (uint8_t)((lit_len < 15 ? lit_len : 15) << 4);

        if (lit_len >= 15 && !comp_write_len(dst, dst_cap, &out_pos, lit_len - 15)) {
            return 0;
        }

        if ((out_pos + lit_len + 2) > dst_cap) {
            return 0;
        }
        memcpy(dst + out_pos, src + anchor, lit_len);
        out_pos += lit_len;

        dst[out_pos++] = (uint8_t)(match_offset & 0xFF);
        dst[out_pos++] = (uint8_t)(match_offset >> 8);

        const size_t enc_match_len = match_len - COMP_MIN_MATCH;
        token |= (uint8_t)(enc_match_len < 15 ? enc_match_len : 15);
        if (enc_match_len >= 15 &&
            !comp_write_len(dst, dst_cap, &out_pos, enc_match_len - 15)) {
            return 0;
        }
        dst[token_pos] = token;

        in_pos += match_len;
        anchor = in_pos;
    }

    const size_t lit_len = src_len - anchor;
    const size_t token_pos = out_pos++;
    if (token_pos >= dst_cap) {
        return 0;
    }

    uint8_t token = (uint8_t)((lit_len < 15 ? lit_len : 15) << 4);
    if (lit_len >= 15 && !comp_write_len(dst, dst_cap, &out_pos, lit_len - 15)) {
        return 0;
    }

    if ((out_pos + lit_len) > dst_cap) {
        return 0;
    }
    memcpy(dst + out_pos, src + anchor, lit_len);
    out_pos += lit_len;
    dst[token_pos] = token;

    return out_pos;
}

static bool comp_lz4_decode_block(const uint8_t *src, size_t src_len, uint8_t *dst,
                                  size_t raw_len) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < src_len) {
        const uint8_t token = src[in_pos++];

        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t ext;
            do {
                if (in_pos >= src_len) {
                    return false;
                }
                ext = src[in_pos++];
                lit_len += ext;
            } while (ext == 255);
        }

        if ((in_pos + lit_len) > src_len || (out_pos + lit_len) > raw_len) {
            return false;
        }
        memcpy(dst + out_pos, src + in_pos, lit_len);
        in_pos += lit_len;
        out_pos += lit_len;

        if (in_pos == src_len) {
            break;
        }

        if ((in_pos + 2) > src_len) {
            return false;
        }
        const size_t offset = (size_t)src[in_pos] | ((size_t)src[in_pos + 1] << 8);
        in_pos += 2;

        if (offset == 0 || offset > out_pos) {
            return false;
        }

        size_t match_len = (token & 0x0F) + COMP_MIN_MATCH;
        if ((token & 0x0F) == 15) {
            uint8_t ext;
            do {
                if (in_pos >= src_len) {
                    return false;
                }
                ext = src[in_pos++];
                match_len += ext;
            } while (ext == 255);
        }

        if ((out_pos + match_len) > raw_len) {
            return false;
        }

        for (size_t i = 0; i < match_len; i++) {
            dst[out_pos] = dst[out_pos - offset];
            out_pos++;
        }
    }

    return out_pos == raw_len;
}

static uint32_t rol32(uint32_t value, uint32_t amount) {
    amount &= 31;
    if (amount == 0) {
        return value;
    }

    return (value << amount) | (value >> (32 - amount));
}

static void calc_cart_checksum(const uint8_t *rom, const cart_crc_variant_t *variant,
                               uint32_t *crc1, uint32_t *crc2) {
    uint32_t t1 = variant->seed;
    uint32_t t2 = variant->seed;
    uint32_t t3 = variant->seed;
    uint32_t t4 = variant->seed;
    uint32_t t5 = variant->seed;
    uint32_t t6 = variant->seed;

    for (size_t offset = 0; offset < CART_CHECKSUM_LENGTH; offset += 4) {
        const uint32_t d = read_be32(rom + CART_CHECKSUM_START + offset);
        const uint32_t old_t6 = t6;

        t6 += d;
        if (t6 < old_t6) {
            t4++;
        }

        t3 ^= d;

        const uint32_t r = rol32(d, d & 0x1F);
        t5 += r;

        if (t2 < d) {
            t2 ^= t6 ^ d;
        } else {
            t2 ^= r;
        }

        if (variant->kind == CART_CRC_KIND_6105) {
            t1 += read_be32(rom + CART_CIC6105_TABLE_OFFSET + (offset & 0xFF)) ^ d;
        } else {
            t1 += t5 ^ d;
        }
    }

    if (variant->kind == CART_CRC_KIND_6103) {
        *crc1 = (t6 ^ t4) + t3;
        *crc2 = (t5 ^ t2) + t1;
    } else if (variant->kind == CART_CRC_KIND_6106) {
        *crc1 = (t6 * t4) + t3;
        *crc2 = (t5 * t2) + t1;
    } else {
        *crc1 = t6 ^ t4 ^ t3;
        *crc2 = t5 ^ t2 ^ t1;
    }
}

static bool probe_game_cart(uint8_t *header) {
    uint8_t first[CART_HEADER_SIZE] __attribute__((aligned(8))) = {0};
    uint8_t second[CART_HEADER_SIZE] __attribute__((aligned(8))) = {0};

    set_dom1_pi_config(CART_SAFE_PI_CONFIG);
    wait_ms(20);
    cart_dma_read(first, 0, sizeof(first));
    memcpy(header, first, CART_HEADER_SIZE);

    if (!cart_header_valid(first)) {
        return false;
    }

    set_dom1_pi_config(pi_config_from_header(first));
    wait_ms(2);
    cart_dma_read(second, 0, sizeof(second));
    memcpy(header, second, CART_HEADER_SIZE);

    return cart_header_valid(second) &&
           memcmp(first, second, CART_HEADER_SIZE) == 0;
}

static bool check_game_cart_crc(const uint8_t *header, uint8_t *scratch,
                                size_t scratch_size, cart_crc_result_t *result) {
    static const cart_crc_variant_t variants[] = {
        { "6101/6102/7101/7102", 0xF8CA4DDC, CART_CRC_KIND_6102 },
        { "6103/7103", 0xA3886759, CART_CRC_KIND_6103 },
        { "6105/7105", 0xDF26F436, CART_CRC_KIND_6105 },
        { "6106/7106", 0x1FEA617A, CART_CRC_KIND_6106 },
    };

    memset(result, 0, sizeof(*result));

    if (scratch_size < CART_CHECKSUM_IMAGE_SIZE) {
        return false;
    }

    cart_dma_read(scratch, 0, CART_CHECKSUM_IMAGE_SIZE);

    result->expected_crc1 = read_be32(header + CART_CRC1_OFFSET);
    result->expected_crc2 = read_be32(header + CART_CRC2_OFFSET);

    if (!cart_header_valid(scratch) ||
        memcmp(header, scratch, CART_HEADER_SIZE) != 0) {
        return false;
    }

    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        uint32_t crc1;
        uint32_t crc2;

        calc_cart_checksum(scratch, &variants[i], &crc1, &crc2);

        if (i == 0) {
            result->calculated_crc1 = crc1;
            result->calculated_crc2 = crc2;
            result->cic_name = variants[i].name;
        }

        if (crc1 == result->expected_crc1 && crc2 == result->expected_crc2) {
            result->calculated_crc1 = crc1;
            result->calculated_crc2 = crc2;
            result->cic_name = variants[i].name;
            return true;
        }
    }

    return false;
}

static bool cart_probe_blocks_match(uint8_t *scratch, size_t offset_a, size_t offset_b) {
    uint8_t *const a = scratch;
    uint8_t *const b = scratch + CART_SIZE_PROBE_BLOCK;

    cart_dma_read(a, offset_a, CART_SIZE_PROBE_BLOCK);
    cart_dma_read(b, offset_b, CART_SIZE_PROBE_BLOCK);

    return memcmp(a, b, CART_SIZE_PROBE_BLOCK) == 0;
}

static bool cart_size_mirrors_at(size_t size, uint8_t *scratch) {
    static const size_t sample_offsets[] = {
        0x000000, 0x001000, 0x020000, 0x05F000,
        0x123000, 0x2A5000, 0x3C0000, 0x555000,
        0x7F0000, 0xA5A000, 0xF00000,
    };

    size_t samples = 0;

    for (size_t i = 0; i < sizeof(sample_offsets) / sizeof(sample_offsets[0]); i++) {
        const size_t sample = sample_offsets[i];

        if ((sample + CART_SIZE_PROBE_BLOCK) > size ||
            (size + sample + CART_SIZE_PROBE_BLOCK) > CART_MAX_SIZE) {
            continue;
        }

        samples++;
        if (!cart_probe_blocks_match(scratch, sample, size + sample)) {
            return false;
        }
    }

    return samples > 0;
}

static size_t detect_game_cart_size(uint8_t *scratch, size_t scratch_size) {
    if (scratch_size < (CART_SIZE_PROBE_BLOCK * 2)) {
        return 0;
    }

    for (size_t size = CART_MIN_SIZE; size < CART_MAX_SIZE; size += CART_SIZE_STEP) {
        if (cart_size_mirrors_at(size, scratch)) {
            return size;
        }
    }

    return CART_MAX_SIZE;
}

static bool wait_for_valid_game_cart(uint8_t *scratch, size_t scratch_size, uint8_t *out_header) {
    uint8_t header[CART_HEADER_SIZE] __attribute__((aligned(8))) = {0};
    cart_crc_result_t crc = {0};

    if (scratch_size < CART_CHECKSUM_IMAGE_SIZE) {
        printf("CRC scratch buffer too small\n");
        printf("Need 0x%06X bytes, got 0x%06X\n",
               (unsigned)CART_CHECKSUM_IMAGE_SIZE, (unsigned)scratch_size);
        console_render();
        return false;
    }

    while (true) {
        if (!probe_game_cart(header)) {
            printf("Invalid cartridge header: %02X %02X %02X %02X\n",
                   header[0], header[1], header[2], header[3]);
        } else {
            printf("Checking cartridge CRC...\n");
            console_render();

            if (check_game_cart_crc(header, scratch, scratch_size, &crc)) {
                printf("CRC OK (%s)\n", crc.cic_name);
                console_render();
                memcpy(out_header, header, CART_HEADER_SIZE);
                return true;
            }

            if (!cart_header_valid(scratch) ||
                memcmp(header, scratch, CART_HEADER_SIZE) != 0) {
                printf("Cartridge header changed during CRC read\n");
            } else {
                printf("CRC check failed\n");
                printf("Expected: %08" PRIX32 " %08" PRIX32 "\n",
                       crc.expected_crc1, crc.expected_crc2);
                printf("Read as:  %08" PRIX32 " %08" PRIX32 " (%s)\n",
                       crc.calculated_crc1, crc.calculated_crc2, crc.cic_name);
            }
        }

        printf("Remove and reinsert the cartridge,\nthen press A to retry\n");
        console_render();
        wait_for_a_press();
    }
}

static bool wait_for_ready_game_cart(uint8_t *scratch, size_t scratch_size,
                                     cart_info_t *info, bool first_insert) {
    uint8_t header[CART_HEADER_SIZE] = {0};

    while (true) {
        if (!wait_for_valid_game_cart(scratch, scratch_size, header)) {
            return false;
        }

        if (!first_insert) {
            if (memcmp(header + CART_PRODUCT_OFFSET, info->header + CART_PRODUCT_OFFSET,
                       CART_PRODUCT_LENGTH) == 0 &&
                header[CART_REVISION_OFFSET] == info->header[CART_REVISION_OFFSET]) {
                return true;
            }

            printf("Different cartridge inserted\n");
            printf("Remove and reinsert the cartridge,\nthen press A to retry\n");
            console_render();
            wait_for_a_press();
            continue;
        }

        printf("Detecting ROM size...\n");
        console_render();
        const size_t detected_size = detect_game_cart_size(scratch, scratch_size);
        if (detected_size == 0) {
            printf("ROM size scratch buffer too small\n");
            console_render();
            return false;
        }

        parse_cart_info(info, header);
        info->detected_size = detected_size;
        info->selected_size = detected_size;
        printf("Detected ROM size: %u MiB\n",
               (unsigned)(detected_size / 0x100000));
        console_render();
        return true;
    }
}

static void print_header(void) {
    console_clear();
    printf("N64 SwapDumper v0.2 (Shark Week)\n\n");
}

static void pause_after_error(void) {
    printf("Press A to continue\n");
    console_render();
    wait_for_a_press();
}

static bool wait_for_a_or_b(void) {
    do {
        poll_controller();
        wait_ms(16);
    } while (inputs.btn.a || inputs.btn.b);

    while (true) {
        poll_controller();

        if (INPUT(btn.a)) {
            return true;
        }
        if (INPUT(btn.b)) {
            return false;
        }

        wait_ms(16);
    }
}

static int find_size_option(size_t size) {
    static const size_t size_options[] = {
        4, 8, 12, 16, 20, 32, 40, 64,
    };

    for (int i = 0; i < (int)(sizeof(size_options) / sizeof(size_options[0])); i++) {
        if (size_options[i] == (size / 0x100000)) {
            return i;
        }
    }

    return 1;
}

static bool select_rom_size(cart_info_t *info) {
    static const size_t size_options[] = {
        4, 8, 12, 16, 20, 32, 40, 64,
    };

    int selected = find_size_option(info->detected_size);

    while (true) {
        print_header();
        printf("Title: %s", info->title);
        print_revision_suffix(info->revision);
        printf("\n");
        printf("Product: %s", info->product);
        if (revision_suffix(info->revision) != '\0') {
            printf("  Rev: %c", revision_suffix(info->revision));
        }
        printf("  Region: %c\n", printable_header_char(info->region));
        printf("CRC: %08" PRIX32 " %08" PRIX32 "\n",
               read_be32(info->header + CART_CRC1_OFFSET),
               read_be32(info->header + CART_CRC2_OFFSET));
        printf("ROM file: %s\n\n", info->rom_path);
        printf("ROM size: %u MiB", (unsigned)size_options[selected]);
        if ((size_options[selected] * 0x100000) == info->detected_size) {
            printf(" (detected)");
        }
        printf("\n\nLeft/Right: change size\nA: confirm  B: cancel\n");
        console_render();

        do {
            poll_controller();
            wait_ms(16);
        } while (inputs.btn.a || inputs.btn.b || inputs.btn.d_left || inputs.btn.d_right);

        while (true) {
            poll_controller();

            if (INPUT(btn.d_left)) {
                selected = (selected + (int)(sizeof(size_options) / sizeof(size_options[0])) - 1) %
                           (int)(sizeof(size_options) / sizeof(size_options[0]));
                break;
            }
            if (INPUT(btn.d_right)) {
                selected = (selected + 1) % (int)(sizeof(size_options) / sizeof(size_options[0]));
                break;
            }
            if (INPUT(btn.a)) {
                info->selected_size = size_options[selected] * 0x100000;
                return true;
            }
            if (INPUT(btn.b)) {
                return false;
            }

            wait_ms(16);
        }
    }
}

static const char *dump_mode_name(dump_mode_t mode) {
    switch (mode) {
        case DUMP_MODE_ROM_ONLY: return "ROM only";
        case DUMP_MODE_SAVE_ONLY: return "Save only";
        case DUMP_MODE_ROM_AND_SAVE: return "ROM + save";
    }

    return "ROM only";
}

static bool select_dump_mode(dump_mode_t *mode) {
    int selected = DUMP_MODE_ROM_AND_SAVE;

    while (true) {
        print_header();
        printf("Dump mode: %s\n\n", dump_mode_name((dump_mode_t)selected));
        printf("Left/Right: change mode\nA: confirm  B: cancel\n");
        console_render();

        do {
            poll_controller();
            wait_ms(16);
        } while (inputs.btn.a || inputs.btn.b || inputs.btn.d_left || inputs.btn.d_right);

        while (true) {
            poll_controller();

            if (INPUT(btn.d_left)) {
                selected = (selected + 2) % 3;
                break;
            }
            if (INPUT(btn.d_right)) {
                selected = (selected + 1) % 3;
                break;
            }
            if (INPUT(btn.a)) {
                *mode = (dump_mode_t)selected;
                return true;
            }
            if (INPUT(btn.b)) {
                return false;
            }

            wait_ms(16);
        }
    }
}

static save_info_t detect_save_info(void) {
    const eeprom_type_t eeprom = eeprom_present();
    if (eeprom == EEPROM_4K || eeprom == EEPROM_16K) {
        save_info_t info = {
            .kind = SAVE_KIND_EEPROM,
            .name = (eeprom == EEPROM_4K) ? "EEPROM 4Kbit" : "EEPROM 16Kbit",
            .size = (eeprom == EEPROM_4K) ? SAVE_EEPROM_4K_SIZE : SAVE_EEPROM_16K_SIZE,
        };
        return info;
    }

    uint32_t flash_id[2] __attribute__((aligned(16))) = {0};
    set_dom2_save_config();
    io_write(CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND, FLASHRAM_COMMAND_SET_IDENTIFY_MODE);
    cart_dom2_read(flash_id, 0, sizeof(flash_id));
    io_write(CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND, FLASHRAM_COMMAND_SET_READ_MODE);

    if (flash_id[0] == FLASHRAM_IDENTIFIER) {
        save_info_t info = {
            .kind = SAVE_KIND_FLASH,
            .name = "FlashRAM 1Mbit",
            .size = SAVE_FLASH_SIZE,
        };
        return info;
    }

    save_info_t info = {
        .kind = SAVE_KIND_SRAM,
        .name = "SRAM 256Kbit",
        .size = SAVE_SRAM_SIZE,
    };
    return info;
}

static bool read_save_data(uint8_t *save_buf, const save_info_t *save_info) {
    if (save_info->kind == SAVE_KIND_EEPROM) {
        eeprom_read_bytes(save_buf, 0, save_info->size);
        return true;
    }

    if (save_info->kind == SAVE_KIND_FLASH) {
        set_dom2_save_config();
        io_write(CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND, FLASHRAM_COMMAND_SET_READ_MODE);
        cart_dom2_read(save_buf, 0, save_info->size);
        return true;
    }

    cart_dom2_read(save_buf, 0, save_info->size);
    return true;
}

static bool write_all(int fd, const uint8_t *buf, size_t len) {
    size_t done = 0;

    while (done < len) {
        const size_t left = len - done;
        const ssize_t res = write(fd, buf + done, left);
        if ((res == 0) || (res == -1)) {
            return false;
        }

        done += (size_t)res;
    }

    return true;
}

static bool read_all(int fd, uint8_t *buf, size_t len) {
    size_t done = 0;

    while (done < len) {
        const size_t left = len - done;
        const ssize_t res = read(fd, buf + done, left);
        if ((res == 0) || (res == -1)) {
            return false;
        }

        done += (size_t)res;
    }

    return true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    static uint32_t table[256];
    static bool table_ready = false;

    if (!table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; bit++) {
                if ((value & 1) != 0) {
                    value = (value >> 1) ^ CRC32_POLY;
                } else {
                    value >>= 1;
                }
            }
            table[i] = value;
        }
        table_ready = true;
    }

    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc;
}

static size_t checksum_read_len(size_t buffer_size, size_t remaining) {
    size_t len = buffer_size;

    if (len > CHECKSUM_READ_SIZE) {
        len = CHECKSUM_READ_SIZE;
    }
    if (len > remaining) {
        len = remaining;
    }

    return len;
}

static bool checksum_cart_rom(uint8_t *buf, size_t buf_size, size_t rom_size,
                              uint32_t *out_crc) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t offset = 0;
    size_t next_report = 0;

    if (buf_size == 0) {
        return false;
    }

    print_header();
    printf("Calculating cartridge CRC32...\n");
    printf("This covers the full selected ROM.\n\n");
    printf("0/%u MiB\n", (unsigned)(rom_size / 0x100000));
    console_render();

    while (offset < rom_size) {
        const size_t len = checksum_read_len(buf_size, rom_size - offset);

        cart_dma_read(buf, offset, len);
        crc = crc32_update(crc, buf, len);
        offset += len;

        if (offset >= next_report) {
            print_header();
            printf("Calculating cartridge CRC32...\n");
            printf("This covers the full selected ROM.\n\n");
            printf("%u/%u MiB\n",
                   (unsigned)(offset / 0x100000),
                   (unsigned)(rom_size / 0x100000));
            console_render();
            next_report = offset + 0x100000;
        }
    }

    *out_crc = crc ^ 0xFFFFFFFFu;
    return true;
}

static bool checksum_sd_file(const char *path, uint8_t *buf, size_t buf_size,
                             size_t size, uint32_t *out_crc) {
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        printf("Failed to open %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        return false;
    }

    uint32_t crc = 0xFFFFFFFFu;
    size_t offset = 0;
    size_t next_report = 0;

    if (buf_size == 0) {
        close(fd);
        return false;
    }

    while (offset < size) {
        const size_t len = checksum_read_len(buf_size, size - offset);

        if (!read_all(fd, buf, len)) {
            printf("Failed to read %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
            console_render();
            close(fd);
            return false;
        }

        crc = crc32_update(crc, buf, len);
        offset += len;

        if (offset >= next_report) {
            print_header();
            printf("Verifying SD file CRC32...\n");
            printf("%s\n\n", path);
            printf("%u/%u MiB\n",
                   (unsigned)(offset / 0x100000),
                   (unsigned)(size / 0x100000));
            console_render();
            next_report = offset + 0x100000;
        }
    }

    if (close(fd) == -1) {
        printf("Failed to close %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        return false;
    }

    *out_crc = crc ^ 0xFFFFFFFFu;
    return true;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool confirm_save_overwrite(const char *path) {
    if (!file_exists(path)) {
        return true;
    }

    printf("Save file already exists:\n%s\n\n", path);
    printf("A: overwrite  B: cancel\n");
    console_render();
    return wait_for_a_or_b();
}

static int open_sd_output_file(const char *path, bool append, bool quiet) {
    int flags = O_WRONLY | O_CREAT;
    if (!append) {
        flags |= O_TRUNC;
    }

    const int fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1 && !quiet) {
        printf("Failed to open %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
    }

    return fd;
}

static bool write_sd_file_to_path(const char *path, const uint8_t *buf, size_t len,
                                  bool append, size_t offset, bool quiet_open) {
    const int fd = open_sd_output_file(path, append, quiet_open);
    if (fd == -1) {
        return false;
    }

    if (lseek(fd, offset, SEEK_SET) == -1) {
        printf("Failed to seek %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        close(fd);
        return false;
    }

    if (!write_all(fd, buf, len)) {
        printf("Failed to write %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        close(fd);
        return false;
    }

    if (close(fd) == -1) {
        printf("Failed to close %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        return false;
    }

    return true;
}

static bool write_sd_file(const char *path, const uint8_t *buf, size_t len, bool append, size_t offset) {
    return write_sd_file_to_path(path, buf, len, append, offset, false);
}

static void comp_write_header(uint8_t *dst, const comp_block_header_t *header) {
    memcpy(dst, header, sizeof(*header));
}

static comp_block_header_t comp_read_header(const uint8_t *src) {
    comp_block_header_t header;
    memcpy(&header, src, sizeof(header));
    return header;
}

static void draw_dump_progress(void) {
    console_clear();
    printf("N64 SwapDumper v0.2 (Shark Week)\n\n");

    if (dprog.cart) {
        printf("Title:   %s", dprog.cart->title);
        print_revision_suffix(dprog.cart->revision);
        printf("\n");
        printf("Size:    %u MiB\n\n",
               (unsigned)(dprog.total_size / 0x100000));
    }

    {
        int filled = dprog.total_size > 0
            ? (int)((dprog.read_addr * PROGRESS_BAR_WIDTH) / dprog.total_size)
            : 0;
        if (filled > PROGRESS_BAR_WIDTH) {
            filled = PROGRESS_BAR_WIDTH;
        }
        printf("Read:  [");
        for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
            printf("%c", i < filled ? '*' : '-');
        }
        printf("] %u/%u MiB\n",
               (unsigned)(dprog.read_addr / 0x100000),
               (unsigned)(dprog.total_size / 0x100000));
    }

    {
        int filled = dprog.total_size > 0
            ? (int)((dprog.write_addr * PROGRESS_BAR_WIDTH) / dprog.total_size)
            : 0;
        if (filled > PROGRESS_BAR_WIDTH) {
            filled = PROGRESS_BAR_WIDTH;
        }
        printf("Write: [");
        for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
            printf("%c", i < filled ? '*' : '-');
        }
        printf("] %u/%u MiB\n",
               (unsigned)(dprog.write_addr / 0x100000),
               (unsigned)(dprog.total_size / 0x100000));
    }

    console_render();
}

static bool compress_cart_pass(uint8_t *comp_buf, size_t comp_cap, uint8_t *block_buf,
                                int32_t *head, size_t rom_offset,
                                size_t rom_size, compressed_pass_t *pass) {
    size_t out_pos = 0;
    size_t raw_pos = rom_offset;

    pass->raw_start = rom_offset;
    pass->raw_size = 0;
    pass->comp_size = 0;

    while (raw_pos < rom_size) {
        size_t block_len = rom_size - raw_pos;
        if (block_len > COMP_BLOCK_SIZE) {
            block_len = COMP_BLOCK_SIZE;
        }

        cart_dma_read(block_buf, raw_pos, block_len);

        if ((out_pos + sizeof(comp_block_header_t)) > comp_cap) {
            break;
        }

        const size_t header_pos = out_pos;
        out_pos += sizeof(comp_block_header_t);

        size_t payload_size = comp_lz4_block(block_buf, block_len, comp_buf + out_pos,
                                             comp_cap - out_pos, head);
        uint32_t flags = 0;

        if (payload_size == 0 || payload_size >= block_len) {
            payload_size = block_len;
            flags = COMP_RAW_FLAG;

            if ((out_pos + payload_size) > comp_cap) {
                out_pos = header_pos;
                break;
            }

            memcpy(comp_buf + out_pos, block_buf, payload_size);
        }

        const comp_block_header_t header = {
            .raw_size = (uint32_t)block_len,
            .payload_size = (uint32_t)payload_size | flags,
        };
        comp_write_header(comp_buf + header_pos, &header);

        out_pos += payload_size;
        raw_pos += block_len;
        pass->raw_size += block_len;
        dprog.read_addr = raw_pos;
        draw_dump_progress();

        if ((comp_cap - out_pos) < (sizeof(comp_block_header_t) + 4096)) {
            break;
        }
    }

    pass->comp_size = out_pos;
    return pass->raw_size > 0;
}

static bool write_compressed_pass_to_sd(const char *path, const uint8_t *comp_buf,
                                        const compressed_pass_t *pass,
                                        uint8_t *block_buf, bool append,
                                        bool quiet_open) {
    const int fd = open_sd_output_file(path, append, quiet_open);
    if (fd == -1) {
        return false;
    }

    if (lseek(fd, pass->raw_start, SEEK_SET) == -1) {
        printf("Failed to seek %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        close(fd);
        return false;
    }

    size_t in_pos = 0;
    size_t write_offset = pass->raw_start;
    while (in_pos < pass->comp_size) {
        if ((in_pos + sizeof(comp_block_header_t)) > pass->comp_size) {
            close(fd);
            return false;
        }

        const comp_block_header_t header = comp_read_header(comp_buf + in_pos);
        in_pos += sizeof(header);

        const bool raw = (header.payload_size & COMP_RAW_FLAG) != 0;
        const size_t payload_size = header.payload_size & ~COMP_RAW_FLAG;
        const size_t raw_size = header.raw_size;

        if (raw_size > COMP_BLOCK_SIZE || (in_pos + payload_size) > pass->comp_size) {
            close(fd);
            return false;
        }

        if (raw) {
            if (!write_all(fd, comp_buf + in_pos, payload_size)) {
                printf("Failed to write %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
                console_render();
                close(fd);
                return false;
            }
        } else {
            if (!comp_lz4_decode_block(comp_buf + in_pos, payload_size, block_buf, raw_size)) {
                printf("Failed to decompress ROM chunk\n");
                console_render();
                close(fd);
                return false;
            }

            if (!write_all(fd, block_buf, raw_size)) {
                printf("Failed to write %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
                console_render();
                close(fd);
                return false;
            }
        }

        in_pos += payload_size;
        write_offset += raw_size;
        dprog.write_addr = write_offset;
        draw_dump_progress();
    }

    if (close(fd) == -1) {
        printf("Failed to close %s\nErrno: %d (%s)\n", path, errno, strerror(errno));
        console_render();
        return false;
    }

    return true;
}

static bool write_rom_pass_to_sd(cart_info_t *cart, const uint8_t *comp_buf,
                                 const compressed_pass_t *pass,
                                 uint8_t *block_buf, bool append) {
    char path[sizeof(cart->rom_path)];

    if (append) {
        return write_compressed_pass_to_sd(cart->rom_path, comp_buf, pass,
                                           block_buf, true, false);
    }

    format_cart_file_path(path, sizeof(path), cart, ROM_DUMP_DIR, "z64");
    if (write_compressed_pass_to_sd(path, comp_buf, pass, block_buf, false, true)) {
        strcpy(cart->rom_path, path);
        return true;
    }

    format_cart_file_path(path, sizeof(path), cart, "sd:", "z64");
    strcpy(cart->rom_path, path);
    return write_compressed_pass_to_sd(cart->rom_path, comp_buf, pass,
                                       block_buf, false, false);
}

static bool write_save_file_to_sd(cart_info_t *cart, const uint8_t *save_buf,
                                  const save_info_t *save_info) {
    char path[sizeof(cart->save_path)];

    format_cart_file_path(path, sizeof(path), cart, SAVE_DUMP_DIR, "sav");
    if (file_exists(path) && !confirm_save_overwrite(path)) {
        printf("Save write cancelled\n");
        console_render();
        return false;
    }

    if (write_sd_file_to_path(path, save_buf, save_info->size, false, 0, true)) {
        strcpy(cart->save_path, path);
        return true;
    }

    format_cart_file_path(path, sizeof(path), cart, "sd:", "sav");
    if (!confirm_save_overwrite(path)) {
        printf("Save write cancelled\n");
        console_render();
        return false;
    }

    strcpy(cart->save_path, path);
    return write_sd_file(cart->save_path, save_buf, save_info->size, false, 0);
}

static size_t align_down_size(size_t value, size_t align) {
    return value & ~(align - 1);
}

static uint8_t *malloc_largest_rom_buffer(size_t *out_size, size_t mem_size) {
    if (mem_size <= (CART_CHECKSUM_IMAGE_SIZE + 0x40000)) {
        *out_size = 0;
        return NULL;
    }

    size_t try_size = align_down_size(mem_size - 0x40000, ROM_BUFFER_ALIGN);

    while (try_size >= CART_CHECKSUM_IMAGE_SIZE) {
        uint8_t *buf = malloc(try_size);
        if (buf != NULL) {
            *out_size = try_size;
            return buf;
        }

        try_size -= ROM_BUFFER_ALIGN;
    }

    *out_size = 0;
    return NULL;
}

static void swap_reset_callback(void) {
}

static void swap_setup_callback(void) {
    printf("Press the reset button now\n");
    console_render();
}

static void ensure_pif_hung(void) {
    if (pif_hung) {
        return;
    }

    hang_pif(swap_reset_callback, swap_setup_callback);
    pif_hung = true;
}

static void swap(const char *to, const char *action) {
    ensure_pif_hung();

    printf("Swap to the %s now, then press the A button to %s!\n", to, action);
    console_render();
}

static void prepare_flashcart_removal(void) {
    debug_close_sdfs();
    cart_exit();
}

static void remount_sd(void) {
    debug_close_sdfs();

    while (cart_init() < 0) {
        printf("Failed to initialise flashcart\nPress A to retry\n");
        console_render();
        wait_for_a_press();
    }

    while (cart_card_init() < 0) {
        printf("Failed to initialise SD card\nPress A to retry\n");
        console_render();
        wait_for_a_press();
    }

    while (!debug_init_sdfs("sd:/", -1)) {
        printf("Failed to mount SD filesystem\nErrno: %d (%s)\nPress A to retry\n",
               errno, strerror(errno));
        console_render();
        wait_for_a_press();
    }

}

void dump_rom(void) {
    print_header();

    const size_t mem_size = get_memory_size();
    cart_info_t cart = {0};
    dump_mode_t dump_mode = DUMP_MODE_ROM_AND_SAVE;
    save_info_t save_info = {0};
    uint8_t *crc_buf = NULL;
    uint8_t *comp_buf = NULL;
    uint8_t *block_buf = NULL;
    int32_t *comp_head = NULL;
    uint8_t *save_buf = NULL;
    size_t chunk = 0;
    size_t offset = 0;
    uint32_t cart_full_crc = 0;
    uint32_t sd_full_crc = 0;
    bool have_cart_full_crc = false;
    bool first_pass = true;
    bool ok = true;

    crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf("Failed to allocate CRC buffer\nErrno: %d (%s)\n", errno, strerror(errno));
        console_render();
        return;
    }

    prepare_flashcart_removal();
    swap("cartridge", "read");
    wait_for_a_press();
    if (!wait_for_ready_game_cart(crc_buf, CART_CHECKSUM_IMAGE_SIZE, &cart, true)) {
        ok = false;
        pause_after_error();
    }

    if (ok && (!select_rom_size(&cart) || !select_dump_mode(&dump_mode))) {
        ok = false;
    }

    if (ok && dump_mode != DUMP_MODE_ROM_ONLY) {
        save_info = detect_save_info();
        save_buf = malloc(save_info.size);
        if (save_buf == NULL) {
            printf("Failed to allocate save buffer\nErrno: %d (%s)\n",
                   errno, strerror(errno));
            console_render();
            ok = false;
            pause_after_error();
        } else {
            printf("Reading save (%s, %u bytes)...\n",
                   save_info.name, (unsigned)save_info.size);
            console_render();

            if (!read_save_data(save_buf, &save_info)) {
                printf("Failed to read save data\n");
                console_render();
                ok = false;
                pause_after_error();
            }
        }
    }

    free(crc_buf);
    crc_buf = NULL;

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY) {
        block_buf = malloc(COMP_BLOCK_SIZE);
        comp_head = malloc(COMP_HASH_SIZE * sizeof(comp_head[0]));
        if (block_buf == NULL || comp_head == NULL) {
            printf("Failed to allocate compressor buffers\nErrno: %d (%s)\n",
                   errno, strerror(errno));
            console_render();
            ok = false;
            pause_after_error();
        } else {
            comp_buf = malloc_largest_rom_buffer(&chunk, mem_size);
            if (comp_buf == NULL) {
                printf("Failed to allocate ROM buffer\nErrno: %d (%s)\n",
                       errno, strerror(errno));
                console_render();
                ok = false;
                pause_after_error();
            } else {
                printf("ROM buffer: %u KiB\n", (unsigned)(chunk / 1024));
                console_render();
            }
        }
    }

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY) {
        if (!checksum_cart_rom(comp_buf, chunk, cart.selected_size, &cart_full_crc)) {
            ok = false;
            pause_after_error();
        } else {
            have_cart_full_crc = true;
            printf("Cartridge CRC32: %08" PRIX32 "\n", cart_full_crc);
            console_render();
        }
    }

    dprog.cart = &cart;
    dprog.total_size = cart.selected_size;
    dprog.read_addr = 0;
    dprog.write_addr = 0;
    draw_dump_progress();

    while (ok && dump_mode != DUMP_MODE_SAVE_ONLY && offset < cart.selected_size) {
        if (!first_pass) {
            prepare_flashcart_removal();
            swap("cartridge", "read");
            wait_for_a_press();
            if (!wait_for_ready_game_cart(comp_buf, chunk, &cart, false)) {
                ok = false;
                pause_after_error();
                break;
            }
        }

        compressed_pass_t pass = {0};

        if (!compress_cart_pass(comp_buf, chunk, block_buf, comp_head,
                                offset, cart.selected_size, &pass)) {
            printf("Failed to compress ROM pass\n");
            console_render();
            ok = false;
            pause_after_error();
            break;
        }

        printf("Buffered 0x%06X - 0x%06X\n",
               (unsigned)pass.raw_start,
               (unsigned)(pass.raw_start + pass.raw_size));
        printf("Compressed: %u KiB -> %u KiB\n",
               (unsigned)(pass.raw_size / 1024),
               (unsigned)(pass.comp_size / 1024));
        console_render();

        swap("flashcart", "write");
        wait_for_a_press();
        remount_sd();

        if (first_pass && dump_mode != DUMP_MODE_ROM_ONLY) {
            if (!write_save_file_to_sd(&cart, save_buf, &save_info)) {
                ok = false;
                pause_after_error();
                break;
            }

            printf("Wrote save to %s\n", cart.save_path);
            console_render();
            free(save_buf);
            save_buf = NULL;
        }

        if (!write_rom_pass_to_sd(&cart, comp_buf, &pass,
                                  block_buf, offset != 0)) {
            ok = false;
            pause_after_error();
            break;
        }

        printf("Wrote chunk 0x%06X - 0x%06X\n",
               (unsigned)pass.raw_start,
               (unsigned)(pass.raw_start + pass.raw_size));
        console_render();
        offset += pass.raw_size;
        first_pass = false;
    }

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY && have_cart_full_crc) {
        if (!checksum_sd_file(cart.rom_path, comp_buf, chunk, cart.selected_size, &sd_full_crc)) {
            ok = false;
            pause_after_error();
        } else {
            print_header();
            printf("Full ROM CRC32\n\n");
            printf("Cart: %08" PRIX32 "\n", cart_full_crc);
            printf("SD:   %08" PRIX32 "\n\n", sd_full_crc);
            printf("%s\n\n", (sd_full_crc == cart_full_crc) ? "Checksum matches" : "CHECKSUM MISMATCH");
            printf("Press A to continue\n");
            console_render();
            wait_for_a_press();
            if (sd_full_crc != cart_full_crc) {
                ok = false;
            }
        }
    }

    if (ok && dump_mode == DUMP_MODE_SAVE_ONLY) {
        swap("flashcart", "write");
        wait_for_a_press();
        remount_sd();

        if (!write_save_file_to_sd(&cart, save_buf, &save_info)) {
            ok = false;
            pause_after_error();
        } else {
            printf("Wrote save to %s\n", cart.save_path);
            console_render();
        }
    }

    if (ok) {
        printf("Success!\n");
        console_render();
    }

    free(save_buf);
    free(comp_head);
    free(block_buf);
    free(comp_buf);
    free(crc_buf);
}

void dump_to_accessory(void) {
    print_header();

    joypad_port_t port;
    if (!accessory_find(&port)) {
        printf("No N64 SwapDumper accessory found on any port\n");
        console_render();

        return;
    }

    printf("Accessory found on port %d\n", port + 1);
    console_render();

    uint8_t *const crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf("Failed to allocate CRC buffer\nErrno: %d (%s)\n", errno, strerror(errno));
        console_render();

        return;
    }

    prepare_flashcart_removal();
    swap("cartridge", "dump");
    wait_for_a_press();
    cart_info_t cart = {0};
    if (!wait_for_ready_game_cart(crc_buf, CART_CHECKSUM_IMAGE_SIZE, &cart, true)) {
        free(crc_buf);
        pause_after_error();
        return;
    }

    free(crc_buf);

    printf("Streaming dump to controller port %d...\n", port + 1);
    console_render();

    accessory_dump_stream(port, cart.selected_size);

    printf("Verifying...\n");
    console_render();

    accessory_verify(port, cart.selected_size);
}

int main(void) {
    console_init();
    console_set_render_mode(RENDER_MANUAL);

    if (sys_bbplayer()) {
        printf("This program cannot be used on an iQue Player!\n");
        console_render();

        while (true) {
            continue;
        }
    }

    joypad_init();
    dfs_init(DFS_DEFAULT_LOCATION);
    debug_init_sdfs("sd:/", -1);

    while (true) {
        print_header();
        printf("A: Dump cartridge to SD card\n");
        printf("B: Dump cartridge to controller accessory\n");
        console_render();

        poll_controller();

        if (INPUT(btn.a)) {
            dump_rom();
        } else if (INPUT(btn.b)) {
            dump_to_accessory();
        }
    }
}
