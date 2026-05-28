#include <libcart/cart.h>
#include <libdragon.h>

#include <dir.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <system.h>

#include "accessory.h"
#include "pif.h"
#include "tom_thumb_font.h"

#ifdef debug_init_sdfs
#undef debug_init_sdfs
#endif
#ifdef debug_close_sdfs
#undef debug_close_sdfs
#endif
extern bool debug_init_sdfs(const char *prefix, int npart);
extern void debug_close_sdfs(void);
static int shark_errno = 0;
#define errno shark_errno
#define EBUSY 16

#define printf iprintf

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

#define COMP_BLOCK_DEFAULT 0x20000
#define COMP_BLOCK_FINAL 0x8000
#define COMP_BLOCK_MAX 0x100000
#define COMP_HASH_BITS_MAX 15
#define COMP_HASH_BITS_FINAL 13
#define COMP_MIN_MATCH 4
#define COMP_SKIP_STRENGTH 5
#define COMP_MAX_SKIP 64
#define COMP_EMPTY 0
#define COMP_RAW_FLAG 0x80000000u
#define COMP_SPECULATIVE_MIN_REMAINING 0x10000
#define CRC32_POLY 0xEDB88320u
#define CHECKSUM_READ_SIZE 0x100000
#define CONSOLE_FRAMEBUFFER_WIDTH 96
#define CONSOLE_FRAMEBUFFER_HEIGHT 40
#define CONSOLE_H_OFFSET 96
#define CONSOLE_V_OFFSET 32
#define CONSOLE_H_STRETCH_PERCENT 400
#define CONSOLE_V_STRETCH_PERCENT 200
#define CONSOLE_PROGRESS_BAR_WIDTH 12
#define CONSOLE_ORIGIN_WRAP_PIXELS 4
#define PROGRESS_BAR_WIDTH CONSOLE_PROGRESS_BAR_WIDTH
#define ROM_BUFFER_ALIGN (1*1024)
#define ROM_BUFFER_HEAP_RESERVE (1*1024)
#define RAW_TAIL_BUFFER_COUNT 3
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MINI_CONSOLE_COLS (CONSOLE_FRAMEBUFFER_WIDTH / TOM_THUMB_CELL_WIDTH)
#define MINI_CONSOLE_ROWS (CONSOLE_FRAMEBUFFER_HEIGHT / TOM_THUMB_CELL_HEIGHT)
#define MINI_CONSOLE_SIZE (MINI_CONSOLE_COLS * MINI_CONSOLE_ROWS + 1)
#define MINI_CONSOLE_X 0
#define MINI_CONSOLE_Y 0
#define MINI_CONSOLE_FG 0xFFFF
#define MINI_CONSOLE_BG 0x18C6
#define CONSOLE_RIGHT_EXTEND_PIXELS CONSOLE_ORIGIN_WRAP_PIXELS
#define VI_H_VIDEO_REG ((volatile uint32_t *)0xA4400024)
#define VI_ORIGIN_REG ((volatile uint32_t *)0xA4400004)
#define VI_V_VIDEO_REG ((volatile uint32_t *)0xA4400028)
#define VI_X_SCALE_REG ((volatile uint32_t *)0xA4400030)
#define VI_Y_SCALE_REG ((volatile uint32_t *)0xA4400034)
#define VI_CTRL_REG ((volatile uint32_t *)0xA4400000)
#define VI_PIXEL_ADVANCE_MASK (0xF << 12)
#define VI_PIXEL_ADVANCE(value) (((value) & 0xF) << 12)
#define VI_AA_MODE_NONE (0x3 << 8)
#define CONSOLE_VI_BASE_OUTPUT_WIDTH ((CONSOLE_FRAMEBUFFER_WIDTH * CONSOLE_H_STRETCH_PERCENT) / 100)
#define CONSOLE_VI_OUTPUT_WIDTH (((CONSOLE_FRAMEBUFFER_WIDTH + CONSOLE_RIGHT_EXTEND_PIXELS) * CONSOLE_H_STRETCH_PERCENT) / 100)
#define CONSOLE_VI_OUTPUT_HEIGHT ((CONSOLE_FRAMEBUFFER_HEIGHT * CONSOLE_V_STRETCH_PERCENT) / 100)
#define VI_X_SCALE_VALUE ((0x400 * CONSOLE_FRAMEBUFFER_WIDTH + (CONSOLE_VI_BASE_OUTPUT_WIDTH / 2)) / CONSOLE_VI_BASE_OUTPUT_WIDTH)
#define VI_X_SCALE_CONSOLE ((0x200 << 16) | (VI_X_SCALE_VALUE & 0xFFF))
#define VI_Y_SCALE_VALUE ((0x400 * CONSOLE_FRAMEBUFFER_HEIGHT + (CONSOLE_VI_OUTPUT_HEIGHT / 2)) / CONSOLE_VI_OUTPUT_HEIGHT)
#define VI_Y_SCALE_CONSOLE (VI_Y_SCALE_VALUE & 0xFFF)
#define VI_VIDEO_START(value) (((value) >> 16) & 0x3FF)
#define VI_VIDEO_SET(start, end) ((((start) & 0x3FF) << 16) | ((end) & 0x3FF))

#define PI_BSD_DOM1_LAT_REG ((volatile uint32_t *)0xA4600014)
#define PI_BSD_DOM1_PWD_REG ((volatile uint32_t *)0xA4600018)
#define PI_BSD_DOM1_PGS_REG ((volatile uint32_t *)0xA460001C)
#define PI_BSD_DOM1_RLS_REG ((volatile uint32_t *)0xA4600020)
#define PI_BSD_DOM2_LAT_REG ((volatile uint32_t *)0xA4600024)
#define PI_BSD_DOM2_PWD_REG ((volatile uint32_t *)0xA4600028)
#define PI_BSD_DOM2_PGS_REG ((volatile uint32_t *)0xA460002C)
#define PI_BSD_DOM2_RLS_REG ((volatile uint32_t *)0xA4600030)

#define SAVE_SRAM_SIZE 0x8000
#define SAVE_FLASH_SIZE 0x20000
#define SAVE_EEPROM_4K_SIZE 0x200
#define SAVE_EEPROM_16K_SIZE 0x800
#define JOYBUS_COMMAND_ID_STATUS 0x00
#define JOYBUS_COMMAND_ID_EEPROM_READ_BLOCK 0x04
#define JOYBUS_COMMAND_ID_EEPROM_WRITE_BLOCK 0x05
#define JOYBUS_EEPROM_TYPE 0x8000
#define JOYBUS_EEPROM_16K_TYPE 0x4000
#define JOYBUS_EEPROM_BUSY 0x80
#define JOYBUS_EEPROM_STATUS_TIMEOUT_MS 250
#define CART_DOM2_ADDR2_START 0x08000000
#define FLASHRAM_IDENTIFIER 0x11118001
#define FLASHRAM_OFFSET_COMMAND 0x00010000
#define FLASHRAM_PAGE_SIZE 0x80
#define FLASHRAM_SECTOR_SIZE 0x4000
#define FLASHRAM_PAGES_PER_SECTOR (FLASHRAM_SECTOR_SIZE / FLASHRAM_PAGE_SIZE)
#define FLASHRAM_SECTOR_COUNT (SAVE_FLASH_SIZE / FLASHRAM_SECTOR_SIZE)
#define FLASHRAM_COMMAND_CHIP_ERASE_MODE 0x3C000000
#define FLASHRAM_COMMAND_SET_ERASE_OFFSET 0x4B000000
#define FLASHRAM_COMMAND_EXECUTE_ERASE 0x78000000
#define FLASHRAM_COMMAND_PROGRAM_PAGE 0xA5000000
#define FLASHRAM_COMMAND_PAGE_PROGRAM_MODE 0xB4000000
#define FLASHRAM_COMMAND_STATUS_MODE 0xD2000000
#define FLASHRAM_COMMAND_SET_IDENTIFY_MODE 0xE1000000
#define FLASHRAM_COMMAND_SET_READ_MODE 0xF0000000
#define FLASHRAM_STATUS_PROGRAM_BUSY 0x01
#define FLASHRAM_STATUS_ERASE_BUSY 0x02
#define FLASHRAM_STATUS_PROGRAM_OK 0x04
#define FLASHRAM_STATUS_ERASE_OK 0x08
#define FLASHRAM_VERSION_OLD 0
#define FLASHRAM_VERSION_NEW 1
#define FLASHRAM_VENDOR_MX_PROTO_A 0x00C20000u
#define FLASHRAM_VENDOR_MX_A 0x00C20001u
#define FLASHRAM_VENDOR_MX_C 0x00C2001Eu
#define FLASHRAM_VENDOR_MX_B_AND_D 0x00C2001Du
#define FLASHRAM_VENDOR_MEI 0x003200F1u
#define FLASHRAM_PROGRAM_DELAY_MS 20
#define FLASHRAM_ERASE_DELAY_MS 500
#define ROM_DUMP_DIR "sd:/dump"
#define SAVE_DUMP_DIR "sd:/dump/saves"
#define RESTORE_DIR "sd:/dump/saves"
#define MAX_RESTORE_FILES 32
#define MAX_PART_FILES 16
#define RESTORE_NAME_LEN 32
#define RESTORE_PATH_LEN 64
#define RESTORE_MAX_FILE_SIZE 0x80000
#define PART_NAME_LEN 40
#define PART_PATH_LEN 64
#define PART_META_MAGIC 0x53445054u
#define PART_META_VERSION 1u

joypad_inputs_t inputs = {0}, prev_inputs;
#define INPUT(inp) ((inputs.inp) && !(prev_inputs.inp))

static bool pif_hung = false;
static int flashram_driver_version = FLASHRAM_VERSION_NEW;
static uint32_t flashram_driver_type_id = 0;
static uint32_t flashram_driver_vendor_id = 0;
static bool flashram_driver_initialized = false;

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
    char rom_path[64];
    char save_path[64];
    uint8_t region;
    size_t detected_size;
    size_t selected_size;
} cart_info_t;

typedef struct {
    save_kind_t kind;
    const char *name;
    size_t size;
} save_info_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t command;
        uint8_t block;
    } send;
    struct __attribute__((packed)) {
        uint8_t data[EEPROM_BLOCK_SIZE];
        uint8_t status;
    } recv;
} eeprom_read_block_cmd_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t command;
        uint8_t block;
        uint8_t data[EEPROM_BLOCK_SIZE];
    } send;
    struct __attribute__((packed)) {
        uint8_t status;
    } recv;
} eeprom_write_block_cmd_t;

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t command;
    } send;
    struct __attribute__((packed)) {
        uint8_t type_lo;
        uint8_t type_hi;
        uint8_t status;
    } recv;
} eeprom_status_cmd_t;

typedef struct {
    size_t raw_start;
    size_t raw_size;
    size_t comp_size;
    size_t work_offset;
    size_t scratch_size;
    size_t hash_size;
    size_t raw_tail_size[RAW_TAIL_BUFFER_COUNT];
    uint32_t read_kbs;
} compressed_pass_t;

typedef struct {
    uint32_t raw_size;
    uint32_t payload_size;
} comp_block_header_t;

typedef struct {
    size_t block_size;
    uint8_t hash_bits;
    uint8_t skip_strength;
    uint8_t max_skip;
    size_t max_match_len;
} compression_settings_t;

typedef struct {
    compression_settings_t settings;
    uint8_t *scratch_buf;
    size_t scratch_size;
    uint32_t *head;
    size_t hash_size;
    size_t work_offset;
    size_t stream_cap;
} compression_workspace_t;

typedef struct {
    cart_info_t *cart;
    size_t read_addr;
    size_t write_addr;
    size_t total_size;
    uint32_t read_kbs;
} dump_progress_t;

typedef struct {
    char name[RESTORE_NAME_LEN];
    char path[RESTORE_PATH_LEN];
    size_t size;
} restore_file_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    char product[CART_PRODUCT_LENGTH + 1];
    uint8_t revision;
    uint8_t reserved[3];
    uint32_t rom_size;
    uint32_t offset;
    uint32_t cart_crc32;
} dump_part_meta_t;

typedef struct {
    char name[PART_NAME_LEN];
    char path[PART_PATH_LEN];
    dump_part_meta_t meta;
    bool meta_valid;
} part_file_t;

typedef struct {
    const char *generic_error;
    const char *app_header;
    const char *newline;
    const char *revision_suffix_fmt;
    const char *press_a_continue;
    const char *prompt_left_right_confirm;
    const char *prompt_restore_anyway;
    const char *prompt_overwrite_cancel;
    const char *prompt_continue_cancel;
    const char *prompt_resume_dump;
    const char *prompt_retry_reinsert;
    const char *prompt_reset_now;
    const char *prompt_swap;
    const char *prompt_flashcart_retry;
    const char *prompt_sd_retry;
    const char *prompt_mount_retry;
    const char *err_crc_scratch_small;
    const char *err_need_got;
    const char *err_invalid_header;
    const char *checking_cart_crc;
    const char *crc_ok;
    const char *err_header_changed;
    const char *err_crc_failed;
    const char *crc_expected;
    const char *crc_read_as;
    const char *err_different_cart;
    const char *detecting_rom_size;
    const char *err_size_scratch_small;
    const char *detected_rom_size;
    const char *title_label;
    const char *product_label;
    const char *rev_label;
    const char *region_label;
    const char *crc_label;
    const char *rom_file_label;
    const char *rom_size_label;
    const char *detected_suffix;
    const char *dump_mode_label;
    const char *mode_rom_only;
    const char *mode_save_only;
    const char *mode_rom_save;
    const char *flash_restore_unsupported;
    const char *flash_erasing;
    const char *flash_programming;
    const char *flash_wait_timeout;
    const char *flash_status_failed;
    const char *cart_crc32_title;
    const char *cart_crc32_subtitle;
    const char *sd_crc32_title;
    const char *fmt_failed_open;
    const char *fmt_failed_read;
    const char *fmt_failed_seek;
    const char *fmt_failed_write;
    const char *fmt_failed_close;
    const char *restore_name_unrecognized;
    const char *expected_like;
    const char *restore_identity_warning;
    const char *file_label;
    const char *plain_line;
    const char *plain_line_spaced;
    const char *cart_label;
    const char *region_differs;
    const char *revision_differs;
    const char *restore_does_not_match;
    const char *restore_save_title;
    const char *dir_label;
    const char *bytes_line;
    const char *err_alloc_save;
    const char *save_exists;
    const char *progress_title_line;
    const char *progress_size;
    const char *progress_read;
    const char *progress_bar_end;
    const char *mib_progress;
    const char *progress_speed;
    const char *progress_write;
    const char *err_decompress_chunk;
    const char *save_write_cancelled;
    const char *err_alloc_crc;
    const char *err_alloc_compressor;
    const char *err_alloc_rom;
    const char *reading_save;
    const char *err_read_save_data;
    const char *rom_buffer;
    const char *compression_label;
    const char *compression_blocks_short;
    const char *cart_crc32_result;
    const char *chunk_cancelled;
    const char *err_compress_pass;
    const char *buffered_range;
    const char *compressed_summary;
    const char *raw_tail_summary;
    const char *wrote_save;
    const char *wrote_chunk;
    const char *full_rom_crc32;
    const char *checksum_status;
    const char *cart_crc_line;
    const char *sd_crc_line;
    const char *checksum_matches;
    const char *checksum_mismatch;
    const char *success;
    const char *err_no_accessory;
    const char *accessory_found;
    const char *streaming_accessory;
    const char *verifying;
    const char *no_sav_files;
    const char *last_tried;
    const char *err_alloc_verify;
    const char *save_too_small;
    const char *save_too_large;
    const char *file_bytes;
    const char *cart_save_bytes;
    const char *truncate_restore;
    const char *restore_file_line;
    const char *restore_to_title;
    const char *restore_confirm_body;
    const char *writing_save;
    const char *restore_verify_failed;
    const char *restore_success;
    const char *err_ique;
    const char *main_dump_sd;
    const char *main_dump_accessory;
    const char *main_restore;
    const char *main_clear_save;
    const char *clear_title;
    const char *clear_warning;
    const char *clear_second_warning;
    const char *prompt_clear_second;
    const char *clearing_save;
    const char *clear_verify_failed;
    const char *clear_success;
    const char *resume_title;
    const char *resume_offset;
} loc_t;

static const char LOC_GENERIC_ERROR[] = "Err\n";

static const loc_t LOC = {
    .generic_error = LOC_GENERIC_ERROR,
    .app_header = "N64SwapDumper v0.3(Shrk)\n",
    .newline = "\n",
    .revision_suffix_fmt = " (%c)",
    .press_a_continue = "A: OK\n",
    .prompt_left_right_confirm = "Dpad:Change A:OK B:Back\n",
    .prompt_restore_anyway = "A:Restore - B:Back\n",
    .prompt_overwrite_cancel = "A:Overwrite - B:Back\n",
    .prompt_continue_cancel = "A:OK - B:Back\n",
    .prompt_resume_dump = "A:Resume - B:Start Over\n",
    .prompt_retry_reinsert = "Reinsert cart+press A\n",
    .prompt_reset_now = "Press RESET now\n",
    .prompt_swap = "Swap- then press A\n",
    .prompt_flashcart_retry = "Flashcart init failed\nA: retry\n",
    .prompt_sd_retry = "SD init failed\nA: retry\n",
    .prompt_mount_retry = "SD mount failed e%d f%d\nA: retry\n",
    .err_crc_scratch_small = LOC_GENERIC_ERROR,
    .err_need_got = "Need %06X, got %06X\n",
    .err_invalid_header = "Bad header: %02X %02X %02X %02X\n",
    .checking_cart_crc = "Checking CRC...\n",
    .crc_ok = "CRC OK (%s)\n",
    .err_header_changed = LOC_GENERIC_ERROR,
    .err_crc_failed = "CRC err\n",
    .crc_expected = "W:%08"PRIX32"%08"PRIX32"\n",
    .crc_read_as = "G:%08"PRIX32"%08"PRIX32"(%s)\n",
    .err_different_cart = LOC_GENERIC_ERROR,
    .detecting_rom_size = "Detect...\n",
    .err_size_scratch_small = LOC_GENERIC_ERROR,
    .detected_rom_size = "Detected:%uMB\n",
    .title_label = "%s",
    .product_label = "Prod:%s",
    .rev_label = "(%c)",
    .region_label = "\n",
    .crc_label = "CRC:%08" PRIX32 " %08" PRIX32 "\n",
    .rom_file_label = "Save as:%s\n",
    .rom_size_label = "Size:%uMB",
    .detected_suffix = "(auto)",
    .dump_mode_label = "Dump:%s\n",
    .mode_rom_only = "ROM",
    .mode_save_only = "Save",
    .mode_rom_save = "Both",
    .flash_restore_unsupported = LOC_GENERIC_ERROR,
    .flash_erasing = "Erase Flash %u/%u\n",
    .flash_programming = "Write Flash %u/%u\n",
    .flash_wait_timeout = LOC_GENERIC_ERROR,
    .flash_status_failed = LOC_GENERIC_ERROR,
    .cart_crc32_title = "Cart CRC32\n",
    .cart_crc32_subtitle = "\n",
    .sd_crc32_title = "Verify ROM...\n",
    .fmt_failed_open = LOC_GENERIC_ERROR,
    .fmt_failed_read = LOC_GENERIC_ERROR,
    .fmt_failed_seek = LOC_GENERIC_ERROR,
    .fmt_failed_write = LOC_GENERIC_ERROR,
    .fmt_failed_close = LOC_GENERIC_ERROR,
    .restore_name_unrecognized = "Unknown save name\n\n",
    .expected_like = "Expected: %s\n",
    .restore_identity_warning = "Save may differ\n\n",
    .file_label = "File: %s\n",
    .plain_line = "%s\n",
    .plain_line_spaced = "%s\n",
    .cart_label = "Cart: %s\n",
    .region_differs = "Region differs\n",
    .revision_differs = "Revision differs\n",
    .restore_does_not_match = "Wrong save\n\n",
    .restore_save_title = "Restore\n\n",
    .dir_label = "Dir: %s\n",
    .bytes_line = "%u bytes\n\n",
    .err_alloc_save = LOC_GENERIC_ERROR,
    .save_exists = "Overwrite?\n%s\n\n",
    .progress_title_line = "%s",
    .progress_size = "%u MB",
    .progress_read = "R:(",
    .progress_bar_end = ")%u/%uMB\n",
    .mib_progress = "%u/%uMB\n",
    .progress_speed = "",
    .progress_write = "W:(",
    .err_decompress_chunk = LOC_GENERIC_ERROR,
    .save_write_cancelled = "Save cancelled\n",
    .err_alloc_crc = LOC_GENERIC_ERROR,
    .err_alloc_compressor = LOC_GENERIC_ERROR,
    .err_alloc_rom = LOC_GENERIC_ERROR,
    .reading_save = "Read save %s, %u B\n",
    .err_read_save_data = "Save read failed\n",
    .rom_buffer = "",
    .compression_label = "Comp: ",
    .compression_blocks_short = "%u KB blocks\n",
    .cart_crc32_result = "CRC32: %08" PRIX32 "\n",
    .chunk_cancelled = "Chunk cancelled\n\n",
    .err_compress_pass = LOC_GENERIC_ERROR,
    .buffered_range = "",
    .compressed_summary = "%uKB->%uKB",
    .raw_tail_summary = "+%uKB raw",
    .wrote_save = "Wrote %s\n",
    .wrote_chunk = "Wrote %06X-%06X\n",
    .full_rom_crc32 = "Checksum...\n",
    .checksum_status = "%s\n",
    .cart_crc_line = "Cart: %08" PRIX32 "\n",
    .sd_crc_line = "SD:   %08" PRIX32 "\n",
    .checksum_matches = "Verified!\n",
    .checksum_mismatch = "BAD DUMP\n",
    .success = "Done!\n",
    .err_no_accessory = LOC_GENERIC_ERROR,
    .accessory_found = "",
    .streaming_accessory = "",
    .verifying = "Verify...\n",
    .no_sav_files = "No .sav files found\n\n",
    .last_tried = "Last tried: %s\n",
    .err_alloc_verify = LOC_GENERIC_ERROR,
    .save_too_small = "Save too small\n",
    .save_too_large = "Save too large\n",
    .file_bytes = "File: %u bytes\n",
    .cart_save_bytes = "Cart: %s, %u bytes\n",
    .truncate_restore = "Restoring first %u bytes.\n",
    .restore_file_line = "Restore %s\n",
    .restore_to_title = "to %s",
    .restore_confirm_body = "?\nOverwrites cart save.\n",
    .writing_save = "Write save %s\n",
    .restore_verify_failed = "Restore verify failed\n",
    .restore_success = "Save restored\n",
    .err_ique = LOC_GENERIC_ERROR,
    .main_dump_sd =         "A:      Dump to SD\n",
    .main_dump_accessory =  "B:      Dump to Joybus\n",
    .main_restore =         "C-Up:   Restore save\n",
    .main_clear_save =      "Z+Down: Clear save\n",
    .clear_title = "Clear save\n",
    .clear_warning = "Save will be erased.\n",
    .clear_second_warning = "Last chance.\n",
    .prompt_clear_second = "L+Z+Start: erase  B: back\n",
    .clearing_save = "Clear save %s, %u B\n",
    .clear_verify_failed = "Clear verify failed\n",
    .clear_success = "Save cleared\nWhat have you done?\n",
    .resume_title = "Incomplete dump found\n",
    .resume_offset = "Resume at %u/%u MB\n",
};

static dump_progress_t dprog;
static bool dump_abort_requested = false;

static char sdfs_prefix[] = "sd:/";
static bool sdfs_mounted = false;
static int sdfs_last_mount_result = 0;

__attribute__((weak, noreturn))
void debug_assert_func_f(const char *file, int line, const char *func,
                         const char *failedexpr, const char *msg, ...) {
    (void)file;
    (void)line;
    (void)func;
    (void)failedexpr;
    (void)msg;

    while (true) {
        continue;
    }
}

__attribute__((weak, noreturn))
void debug_assert_func(const char *file, int line, const char *func,
                       const char *failedexpr) {
    debug_assert_func_f(file, line, func, failedexpr, NULL);
}

__attribute__((weak))
void __debug_backtrace(FILE *out, bool skip_exception) {
    (void)out;
    (void)skip_exception;
}

__attribute__((weak))
void __inspector_exception(exception_t *ex) {
    (void)ex;
}

__attribute__((weak, noreturn))
void __rsp_crash(const char *file, int line, const char *func, const char *msg, ...) {
    (void)file;
    (void)line;
    (void)func;
    (void)msg;

    while (true) {
        continue;
    }
}

__attribute__((weak))
bool __sprite_upgrade(sprite_t *sprite) {
    (void)sprite;
    return false;
}

__attribute__((weak))
const char *__mips_gpr[34] = {
    "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra",
    "lo", "hi",
};

__attribute__((weak))
const char *__mips_fpreg[32] = {
    "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7",
    "$f8", "$f9", "$f10", "$f11", "$f12", "$f13", "$f14", "$f15",
    "$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23",
    "$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31",
};

static bool sdfs_mount(const char *prefix, int npart) {
    errno = 0;
    sdfs_last_mount_result = 0;

    if (sdfs_mounted) {
        return true;
    }

    if (!debug_init_sdfs(prefix, npart)) {
        errno = EBUSY;
        sdfs_last_mount_result = 3; /* FR_NOT_READY */
        return false;
    }

    strncpy(sdfs_prefix, prefix, sizeof(sdfs_prefix) - 1);
    sdfs_prefix[sizeof(sdfs_prefix) - 1] = '\0';
    sdfs_mounted = true;
    return true;
}

static void sdfs_unmount(void) {
    if (!sdfs_mounted) {
        return;
    }

    debug_close_sdfs();
    sdfs_mounted = false;
}

static char mini_console_buf[MINI_CONSOLE_SIZE];

static void mini_console_scroll(int *pos) {
    memmove(mini_console_buf, mini_console_buf + MINI_CONSOLE_COLS,
            MINI_CONSOLE_SIZE - MINI_CONSOLE_COLS);
    *pos -= MINI_CONSOLE_COLS;
}

static void mini_console_put(int *pos, char ch) {
    if (*pos >= (MINI_CONSOLE_COLS * MINI_CONSOLE_ROWS)) {
        mini_console_scroll(pos);
    }

    mini_console_buf[(*pos)++] = ch;
    mini_console_buf[*pos] = '\0';
}

static int mini_console_write(char *buf, unsigned int len) {
    int pos = strlen(mini_console_buf);

    for (unsigned int i = 0; i < len; i++) {
        switch (buf[i]) {
        case '\r':
        case '\n':
            if ((pos % MINI_CONSOLE_COLS) == 0) {
                mini_console_put(&pos, ' ');
            }
            while ((pos % MINI_CONSOLE_COLS) != 0) {
                mini_console_put(&pos, ' ');
            }
            break;
        case '\t':
            do {
                mini_console_put(&pos, ' ');
            } while ((pos % 4) != 0);
            break;
        default:
            mini_console_put(&pos, buf[i]);
            break;
        }
    }

    return len;
}

static void mini_console_draw_char(surface_t *disp, int x, int y, char ch) {
    if (x < 0 || y < 0 ||
        x + TOM_THUMB_WIDTH > disp->width ||
        y + TOM_THUMB_HEIGHT > disp->height) {
        return;
    }

    const uint8_t *glyph = tom_thumb_get_glyph((uint8_t)ch);
    for (int row = 0; row < TOM_THUMB_HEIGHT; row++) {
        uint16_t *dst = (uint16_t *)((uint8_t *)disp->buffer + (y + row) * disp->stride) + x;
        uint8_t bits = glyph[row];
        for (int col = 0; col < TOM_THUMB_WIDTH; col++) {
            if (bits & (1 << (TOM_THUMB_WIDTH - 1 - col))) {
                dst[col] = MINI_CONSOLE_FG;
            }
        }
    }
}

static const resolution_t CONSOLE_RESOLUTION = {
    .width = CONSOLE_FRAMEBUFFER_WIDTH,
    .height = CONSOLE_FRAMEBUFFER_HEIGHT,
    .interlaced = false,
};

static void console_apply_vi_window(void) {
    const uint32_t h_default = *VI_H_VIDEO_REG;
    const uint32_t v_start = VI_VIDEO_START(*VI_V_VIDEO_REG);
    const uint32_t h_start = VI_VIDEO_START(h_default);

    *VI_X_SCALE_REG = VI_X_SCALE_CONSOLE;
    *VI_Y_SCALE_REG = VI_Y_SCALE_CONSOLE;
    *VI_CTRL_REG = (*VI_CTRL_REG & ~(VI_PIXEL_ADVANCE_MASK | VI_AA_MODE_MASK)) |
                   VI_PIXEL_ADVANCE(0) |
                   VI_AA_MODE_NONE;
    *VI_H_VIDEO_REG = VI_VIDEO_SET(h_start + CONSOLE_H_OFFSET,
                                   h_start + CONSOLE_H_OFFSET + CONSOLE_VI_OUTPUT_WIDTH);
    *VI_V_VIDEO_REG = VI_VIDEO_SET(v_start + CONSOLE_V_OFFSET * 2,
                                   v_start + (CONSOLE_V_OFFSET + CONSOLE_VI_OUTPUT_HEIGHT) * 2);
}

static void console_apply_vi_origin_offset(void) {
    const uint32_t offset = CONSOLE_ORIGIN_WRAP_PIXELS * sizeof(uint16_t);
    *VI_ORIGIN_REG = (*VI_ORIGIN_REG - offset) & 0x00FFFFFF;
}

void shark_console_clear(void);
void shark_console_render(void);

void shark_console_init(void) {
    static stdio_t console_calls = {0, mini_console_write, 0};

    register_VI_handler(console_apply_vi_origin_offset);
    display_init(CONSOLE_RESOLUTION, DEPTH_16_BPP, 1, GAMMA_NONE, FILTERS_RESAMPLE);
    console_apply_vi_window();
    hook_stdio_calls(&console_calls);
    shark_console_clear();
}

void shark_console_set_render_mode(int mode) {
    (void)mode;
}

void shark_console_clear(void) {
    mini_console_buf[0] = '\0';
}

void shark_console_render(void) {
    surface_t *disp = display_get();
    if (disp == NULL) {
        return;
    }

    for (int y = 0; y < disp->height; y++) {
        uint16_t *dst = (uint16_t *)((uint8_t *)disp->buffer + y * disp->stride);
        for (int x = 0; x < disp->width; x++) {
            dst[x] = MINI_CONSOLE_BG;
        }
    }

    for (int y = 0; y < MINI_CONSOLE_ROWS; y++) {
        for (int x = 0; x < MINI_CONSOLE_COLS; x++) {
            char ch = mini_console_buf[y * MINI_CONSOLE_COLS + x];
            if (ch == '\0') {
                display_show(disp);
                return;
            }
            mini_console_draw_char(disp,
                                   MINI_CONSOLE_X + x * TOM_THUMB_CELL_WIDTH,
                                   MINI_CONSOLE_Y + y * TOM_THUMB_CELL_HEIGHT,
                                   ch);
        }
    }
    display_show(disp);
}

static void print_loc(const char *text) {
    printf("%s", text);
}

static void print_header(void);
static void print_hex_bytes(const char *label, const uint8_t *buf, size_t len);
static bool flashram_clear_data(void);
static bool eeprom_read_data_raw(uint8_t *save_buf, size_t len);
static bool read_save_data(uint8_t *save_buf, const save_info_t *save_info);
static eeprom_type_t eeprom_present_raw(void);
static bool read_part_meta(const char *path, dump_part_meta_t *meta);
static bool flashram_os_read_array(uint8_t *dst, size_t page_num, size_t page_count);
static void swap(const char *to, const char *action);
static void remount_sd(void);
static uint32_t crc32_buffer(const uint8_t *buf, size_t len);
static bool write_sd_file_to_path(const char *path, const uint8_t *buf, size_t len,
                                  bool append, size_t offset, bool quiet);

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
        out.btn.z = data.c[0].Z;
        out.btn.l = data.c[0].L;
        out.btn.r = data.c[0].R;
        out.btn.start = data.c[0].start;
        out.btn.c_up = data.c[0].C_up;
        out.btn.d_down = data.c[0].down;
        out.btn.d_left = data.c[0].left;
        out.btn.d_right = data.c[0].right;
    }
    return out;
}

static joypad_inputs_t read_controller_combined(void) {
    joypad_inputs_t out = read_controller();

    joypad_poll();
    const joypad_buttons_t btn = joypad_get_buttons(JOYPAD_PORT_1);
    out.btn.a |= btn.a;
    out.btn.b |= btn.b;
    out.btn.z |= btn.z;
    out.btn.l |= btn.l;
    out.btn.r |= btn.r;
    out.btn.start |= btn.start;
    out.btn.c_up |= btn.c_up;
    out.btn.d_down |= btn.d_down;
    out.btn.d_left |= btn.d_left;
    out.btn.d_right |= btn.d_right;
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

static void set_dom2_flash_config(void) {
    wait_pi_idle();
    *PI_BSD_DOM2_LAT_REG = 0x05;
    *PI_BSD_DOM2_PWD_REG = 0x0C;
    *PI_BSD_DOM2_PGS_REG = 0x0F;
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
        printf(LOC.revision_suffix_fmt, suffix);
    }
}

static void append_path_char(char *out, size_t out_size, size_t *pos, char ch) {
    if (out_size == 0) {
        return;
    }

    if (*pos + 1 < out_size) {
        out[*pos] = ch;
        (*pos)++;
    }
    out[*pos] = '\0';
}

static void append_path_text(char *out, size_t out_size, size_t *pos, const char *text) {
    while (*text != '\0') {
        append_path_char(out, out_size, pos, *text);
        text++;
    }
}

static void format_cart_file_path(char *out, size_t out_size, const cart_info_t *info,
                                  const char *dir, const char *ext) {
    const char suffix = revision_suffix(info->revision);
    size_t pos = 0;
    if (out_size != 0) {
        out[0] = '\0';
    }

    append_path_text(out, out_size, &pos, dir);
    append_path_char(out, out_size, &pos, '/');
    append_path_text(out, out_size, &pos, info->product);
    if (suffix != '\0') {
        append_path_char(out, out_size, &pos, suffix);
    }
    append_path_char(out, out_size, &pos, '.');
    append_path_text(out, out_size, &pos, ext);
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

static void cart_dom2_write(const void *ram, size_t cart_offset, size_t len) {
    set_dom2_save_config();
    data_cache_hit_writeback_invalidate((void *)ram, len);
    dma_write_raw_async(ram, CART_DOM2_ADDR2_START + cart_offset, len);
    dma_wait();
}

static void flashram_dom2_read(void *ram, size_t cart_offset, size_t len) {
    set_dom2_flash_config();
    data_cache_hit_writeback_invalidate(ram, len);
    dma_read_raw_async(ram, CART_DOM2_ADDR2_START + cart_offset, len);
    dma_wait();
}

static void flashram_dom2_write(const void *ram, size_t cart_offset, size_t len) {
    set_dom2_flash_config();
    data_cache_hit_writeback_invalidate((void *)ram, len);
    dma_write_raw_async(ram, CART_DOM2_ADDR2_START + cart_offset, len);
    dma_wait();
}

static void flashram_command(uint32_t command) {
    set_dom2_flash_config();
    wait_pi_idle();
    io_write(CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND, command);
    MEMORY_BARRIER();
}

static uint32_t flashram_read_io_base(void) {
    set_dom2_flash_config();
    wait_pi_idle();
    return io_read(CART_DOM2_ADDR2_START);
}

static void flashram_write_io_base(uint32_t value) {
    set_dom2_flash_config();
    wait_pi_idle();
    io_write(CART_DOM2_ADDR2_START, value);
    MEMORY_BARRIER();
}

static void flashram_set_read_mode(void) {
    flashram_command(FLASHRAM_COMMAND_SET_READ_MODE);
    (void)flashram_read_io_base(); /* libultra-style dummy read to prime array reads */
}

static void flashram_read_status(uint8_t *out_status) {
    uint32_t status;

    flashram_command(FLASHRAM_COMMAND_STATUS_MODE);
    status = flashram_read_io_base();
    flashram_command(FLASHRAM_COMMAND_STATUS_MODE);
    status = flashram_read_io_base();
    *out_status = (uint8_t)(status & 0xFF);
}

static uint32_t __attribute__((unused)) flashram_status_word(void) {
    flashram_command(FLASHRAM_COMMAND_STATUS_MODE);
    return flashram_read_io_base();
}

static uint8_t __attribute__((unused)) flashram_status_byte(void) {
    uint8_t status = 0;
    flashram_read_status(&status);
    return status;
}

static void flashram_clear_status(void) {
    /* osFlashClearStatus: status mode, then write zero to the base address. */
    flashram_command(FLASHRAM_COMMAND_STATUS_MODE);
    flashram_write_io_base(0);
}

static void flashram_read_id(uint32_t *type_id, uint32_t *vendor_id) {
    uint32_t ids[2] __attribute__((aligned(16))) = {0};
    uint8_t ignored_status = 0;

    flashram_read_status(&ignored_status);
    flashram_command(FLASHRAM_COMMAND_SET_IDENTIFY_MODE);
    flashram_dom2_read(ids, 0, sizeof(ids));

    *type_id = ids[0];
    *vendor_id = ids[1];
}

static void flashram_init_driver(void) {
    uint32_t type_id = 0;
    uint32_t vendor_id = 0;

    if (flashram_driver_initialized) {
        return;
    }

    flashram_read_id(&type_id, &vendor_id);
    flashram_driver_type_id = type_id;
    flashram_driver_vendor_id = vendor_id;

    if (vendor_id == FLASHRAM_VENDOR_MX_PROTO_A ||
        vendor_id == FLASHRAM_VENDOR_MX_A ||
        vendor_id == FLASHRAM_VENDOR_MX_C) {
        flashram_driver_version = FLASHRAM_VERSION_OLD;
    } else {
        flashram_driver_version = FLASHRAM_VERSION_NEW;
    }

    flashram_clear_status();
    flashram_set_read_mode();
    flashram_driver_initialized = true;
}

static size_t flashram_get_addr(size_t page) {
    flashram_init_driver();
    if (flashram_driver_version == FLASHRAM_VERSION_OLD) {
        return page * (FLASHRAM_PAGE_SIZE / 2);
    }
    return page * FLASHRAM_PAGE_SIZE;
}

static void __attribute__((unused)) flashram_wait_operation_delay(uint32_t delay_ms) {
    wait_ms(delay_ms);
}

static bool flashram_wait_erase_end(size_t sector, uint32_t timeout_ms, uint8_t *last_status_out) {
    const uint64_t start_ms = get_ticks_ms();
    uint8_t status = 0xFF;

    do {
        wait_ms(13);
        status = (uint8_t)(flashram_read_io_base() & 0xFF);
        if ((status & FLASHRAM_STATUS_ERASE_BUSY) == 0) {
            status = (uint8_t)(flashram_read_io_base() & 0xFF);
            flashram_clear_status();
            if (last_status_out != NULL) {
                *last_status_out = status;
            }
            return ((status & FLASHRAM_STATUS_ERASE_OK) == FLASHRAM_STATUS_ERASE_OK) ||
                   status == 0x48;
        }
    } while ((get_ticks_ms() - start_ms) < timeout_ms);

    if (last_status_out != NULL) {
        *last_status_out = status;
    }
    flashram_clear_status();
    return false;
}

static bool flashram_wait_program_end(size_t page, uint32_t timeout_ms, uint8_t *last_status_out) {
    const uint64_t start_ms = get_ticks_ms();
    uint8_t status = 0xFF;

    do {
        wait_ms(1);
        status = (uint8_t)(flashram_read_io_base() & 0xFF);
        if ((status & FLASHRAM_STATUS_PROGRAM_BUSY) == 0) {
            status = (uint8_t)(flashram_read_io_base() & 0xFF);
            flashram_clear_status();
            if (last_status_out != NULL) {
                *last_status_out = status;
            }
            return ((status & FLASHRAM_STATUS_PROGRAM_OK) == FLASHRAM_STATUS_PROGRAM_OK) ||
                   status == 0x44;
        }
    } while ((get_ticks_ms() - start_ms) < timeout_ms);

    if (last_status_out != NULL) {
        *last_status_out = status;
    }
    flashram_clear_status();
    return false;
}

static bool __attribute__((unused)) flashram_wait_status_bit0_clear(const char *phase, size_t index,
                                            uint32_t timeout_ms) {
    uint8_t status = 0;
    const bool ok = flashram_wait_program_end(index, timeout_ms, &status);
    return ok;
}

static bool flashram_os_read_array(uint8_t *dst, size_t page_num, size_t page_count) {
    flashram_init_driver();

    if (page_count == 0) {
        return true;
    }

    flashram_command(FLASHRAM_COMMAND_SET_READ_MODE);
    (void)flashram_read_io_base();

    while (page_count > 0) {
        size_t pages = page_count;
        const size_t pages_until_bank_end = 256 - (page_num & 0xFF);
        if (pages > pages_until_bank_end) {
            pages = pages_until_bank_end;
        }

        flashram_dom2_read(dst, flashram_get_addr(page_num), pages * FLASHRAM_PAGE_SIZE);

        dst += pages * FLASHRAM_PAGE_SIZE;
        page_num += pages;
        page_count -= pages;
    }

    return true;
}

static void __attribute__((unused)) flashram_read_page(size_t page, uint8_t *data) {
    flashram_os_read_array(data, page, 1);
}

static bool __attribute__((unused)) flashram_page_is_erased(const uint8_t *page) {
    for (size_t i = 0; i < FLASHRAM_PAGE_SIZE; i++) {
        if (page[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

static bool flashram_erase_sector(size_t sector) {
    const uint32_t page = (uint32_t)(sector * FLASHRAM_PAGES_PER_SECTOR);
    uint8_t status = 0xFF;
    bool ok;

    flashram_init_driver();

    flashram_command(FLASHRAM_COMMAND_SET_ERASE_OFFSET | page);
    flashram_command(FLASHRAM_COMMAND_EXECUTE_ERASE);
    ok = flashram_wait_erase_end(sector, 5000, &status);
    flashram_set_read_mode();
    return ok;
}

static bool flashram_program_page(size_t page, const uint8_t *data) {
    uint8_t status = 0xFF;
    bool ok;

    flashram_init_driver();

    /* osFlashWriteBuffer: select load-page mode, then DMA 128 bytes to devAddr 0. */
    flashram_command(FLASHRAM_COMMAND_PAGE_PROGRAM_MODE);
    flashram_dom2_write(data, 0, FLASHRAM_PAGE_SIZE);

    /* osFlashWriteArray: new chips repeat PAGE_PROGRAM before PROGRAM_PAGE. */
    if (flashram_driver_version == FLASHRAM_VERSION_NEW) {
        flashram_command(FLASHRAM_COMMAND_PAGE_PROGRAM_MODE);
    }
    flashram_command(FLASHRAM_COMMAND_PROGRAM_PAGE | (uint32_t)page);

    ok = flashram_wait_program_end(page, 1000, &status);
    if (!ok) {
    }
    flashram_set_read_mode();
    return ok;
}

static size_t __attribute__((unused)) flashram_count_data_diffs(const uint8_t *want, const uint8_t *got) {
    size_t diffs = 0;

    for (size_t i = 0; i < FLASHRAM_PAGE_SIZE; i++) {
        if (want[i] != got[i]) {
            diffs++;
        }
    }

    return diffs;
}

static bool flashram_write_data(const uint8_t *save_buf, size_t len) {
    uint8_t page_buf[FLASHRAM_PAGE_SIZE] __attribute__((aligned(16)));

    if (len != SAVE_FLASH_SIZE) {
        return false;
    }

    for (size_t sector = 0; sector < FLASHRAM_SECTOR_COUNT; sector++) {
        print_header();
        printf(LOC.flash_erasing,
               (unsigned)(sector + 1), (unsigned)FLASHRAM_SECTOR_COUNT);
        shark_console_render();

        if (!flashram_erase_sector(sector)) {
            printf("Erase failed sector %u\n", (unsigned)sector);
            shark_console_render();
            flashram_set_read_mode();
            return false;
        }
    }

    for (size_t page = 0; page < (SAVE_FLASH_SIZE / FLASHRAM_PAGE_SIZE); page++) {
        if ((page % 32) == 0) {
            print_header();
            printf(LOC.flash_programming,
                   (unsigned)(page + 1),
                   (unsigned)(SAVE_FLASH_SIZE / FLASHRAM_PAGE_SIZE));
            shark_console_render();
        }

        memcpy(page_buf, save_buf + page * FLASHRAM_PAGE_SIZE, FLASHRAM_PAGE_SIZE);
        if (!flashram_program_page(page, page_buf)) {
            printf("Program failed page %u\n", (unsigned)page);
            shark_console_render();
            flashram_set_read_mode();
            return false;
        }
    }

    flashram_set_read_mode();
    return true;
}

static bool flashram_clear_data(void) {
    for (size_t sector = 0; sector < FLASHRAM_SECTOR_COUNT; sector++) {
        print_header();
        printf(LOC.flash_erasing,
               (unsigned)(sector + 1), (unsigned)FLASHRAM_SECTOR_COUNT);
        shark_console_render();

        if (!flashram_erase_sector(sector)) {
            printf("Erase failed sector %u\n", (unsigned)sector);
            shark_console_render();
            flashram_set_read_mode();
            return false;
        }
    }

    flashram_set_read_mode();
    return true;
}

static uint32_t read_u32_unaligned(const uint8_t *data) {
    return ((uint32_t)data[0] << 0) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t comp_hash4(const uint8_t *data, uint8_t hash_bits) {
    return (read_u32_unaligned(data) * 2654435761u) >> (32 - hash_bits);
}

static size_t comp_match_len(const uint8_t *a, const uint8_t *b,
                             size_t max_len, size_t max_match_len) {
    size_t len = 0;

    if (max_match_len != 0 && max_len > max_match_len) {
        max_len = max_match_len;
    }

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
                                         uint32_t *head, const compression_settings_t *settings,
                                         size_t *match_offset) {
    const uint32_t hash = comp_hash4(src + pos, settings->hash_bits);
    const uint32_t candidate = head[hash];
    head[hash] = (uint32_t)(pos + 1);

    if (candidate == COMP_EMPTY) {
        return 0;
    }

    const size_t cand = (size_t)candidate - 1;
    const size_t offset = pos - cand;

    if (offset == 0 || offset > UINT16_MAX ||
        read_u32_unaligned(src + cand) != read_u32_unaligned(src + pos)) {
        return 0;
    }

    *match_offset = offset;
    return comp_match_len(src + cand, src + pos, src_len - pos,
                          settings->max_match_len);
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
                             size_t dst_cap, uint32_t *head,
                             const compression_settings_t *settings) {
    memset(head, COMP_EMPTY, ((size_t)1 << settings->hash_bits) * sizeof(head[0]));

    size_t in_pos = 0;
    size_t anchor = 0;
    size_t out_pos = 0;
    size_t misses = 0;

    while ((in_pos + COMP_MIN_MATCH) <= src_len) {
        size_t match_offset = 0;
        size_t match_len = comp_find_and_insert_match(src, src_len, in_pos,
                                                      head, settings, &match_offset);

        if (match_len < COMP_MIN_MATCH) {
            size_t step = 1 + (misses >> settings->skip_strength);
            if (step > settings->max_skip) {
                step = settings->max_skip;
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

static bool cart_window_mirrors_after_size(size_t size, size_t window_size,
                                           uint8_t *scratch) {
    static const size_t sample_offsets[] = {
        0x000000, 0x001000, 0x020000, 0x05F000,
        0x123000, 0x2A5000, 0x3C0000, 0x555000,
        0x7F0000, 0xA5A000, 0xF00000,
    };

    const size_t window_start = size - window_size;
    size_t samples = 0;

    if (window_size == 0 || window_size > size) {
        return false;
    }

    for (size_t i = 0; i < ARRAY_COUNT(sample_offsets); i++) {
        const size_t sample = sample_offsets[i] % window_size;
        const size_t source = window_start + sample;
        const size_t mirror = size + sample;

        if ((source + CART_SIZE_PROBE_BLOCK) > size ||
            (mirror + CART_SIZE_PROBE_BLOCK) > CART_MAX_SIZE) {
            continue;
        }

        samples++;
        if (!cart_probe_blocks_match(scratch, source, mirror)) {
            return false;
        }
    }

    return samples > 0;
}

static size_t detect_game_cart_windowed_mirror_size(uint8_t *scratch) {
    static const size_t tail_windows[] = {
        32 * 0x100000, 20 * 0x100000, 16 * 0x100000,
        12 * 0x100000, 8 * 0x100000, 4 * 0x100000,
    };

    for (size_t size = CART_MIN_SIZE; size < CART_MAX_SIZE; size += CART_SIZE_STEP) {
        for (size_t i = 0; i < ARRAY_COUNT(tail_windows); i++) {
            if (tail_windows[i] >= size) {
                continue;
            }

            if (cart_window_mirrors_after_size(size, tail_windows[i], scratch)) {
                return size;
            }
        }
    }

    return 0;
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

    const size_t windowed_mirror_size = detect_game_cart_windowed_mirror_size(scratch);
    if (windowed_mirror_size != 0) {
        return windowed_mirror_size;
    }

    return CART_MAX_SIZE;
}

static bool wait_for_valid_game_cart(uint8_t *scratch, size_t scratch_size, uint8_t *out_header) {
    uint8_t header[CART_HEADER_SIZE] __attribute__((aligned(8))) = {0};
    cart_crc_result_t crc = {0};

    if (scratch_size < CART_CHECKSUM_IMAGE_SIZE) {
        print_loc(LOC.err_crc_scratch_small);
        printf(LOC.err_need_got,
               (unsigned)CART_CHECKSUM_IMAGE_SIZE, (unsigned)scratch_size);
        shark_console_render();
        return false;
    }

    while (true) {
        if (!probe_game_cart(header)) {
            printf(LOC.err_invalid_header,
                   header[0], header[1], header[2], header[3]);
        } else {
            print_loc(LOC.checking_cart_crc);
            shark_console_render();

            if (check_game_cart_crc(header, scratch, scratch_size, &crc)) {
                printf(LOC.crc_ok, crc.cic_name);
                shark_console_render();
                memcpy(out_header, header, CART_HEADER_SIZE);
                return true;
            }

            if (!cart_header_valid(scratch) ||
                memcmp(header, scratch, CART_HEADER_SIZE) != 0) {
                print_loc(LOC.err_header_changed);
            } else {
                print_loc(LOC.err_crc_failed);
                printf(LOC.crc_expected,
                       crc.expected_crc1, crc.expected_crc2);
                printf(LOC.crc_read_as,
                       crc.calculated_crc1, crc.calculated_crc2, crc.cic_name);
            }
        }

        print_loc(LOC.prompt_retry_reinsert);
        shark_console_render();
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

            print_loc(LOC.err_different_cart);
            print_loc(LOC.prompt_retry_reinsert);
            shark_console_render();
            wait_for_a_press();
            continue;
        }

        print_loc(LOC.detecting_rom_size);
        shark_console_render();
        const size_t detected_size = detect_game_cart_size(scratch, scratch_size);
        if (detected_size == 0) {
            print_loc(LOC.err_size_scratch_small);
            shark_console_render();
            return false;
        }

        parse_cart_info(info, header);
        info->detected_size = detected_size;
        info->selected_size = detected_size;
        printf(LOC.detected_rom_size,
               (unsigned)(detected_size / 0x100000));
        shark_console_render();
        return true;
    }
}

static void print_header(void) {
    shark_console_clear();
    print_loc(LOC.app_header);
}

static void pause_after_error(void) {
    print_loc(LOC.press_a_continue);
    shark_console_render();
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

static bool wait_for_clear_combo_or_b(void) {
    do {
        poll_controller();
        wait_ms(16);
    } while (inputs.btn.b || inputs.btn.l || inputs.btn.z || inputs.btn.start);

    int combo_frames = 0;
    while (true) {
        inputs = read_controller_combined();

        print_header();
        print_loc(LOC.clear_title);
        print_loc(LOC.clear_second_warning);
        print_loc(LOC.prompt_clear_second);
        shark_console_render();

        if (inputs.btn.b) {
            return false;
        }
        if (inputs.btn.l && inputs.btn.z && inputs.btn.start) {
            combo_frames++;
            if (combo_frames >= 3) {
                prev_inputs = inputs;
                return true;
            }
        } else {
            combo_frames = 0;
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
        printf(LOC.title_label, info->title);
        print_revision_suffix(info->revision);
        print_loc(LOC.newline);
        printf(LOC.product_label, info->product);
        if (revision_suffix(info->revision) != '\0') {
            printf(LOC.rev_label, revision_suffix(info->revision));
        }
        printf(LOC.region_label, printable_header_char(info->region));
        printf(LOC.crc_label,
               read_be32(info->header + CART_CRC1_OFFSET),
               read_be32(info->header + CART_CRC2_OFFSET));
        printf(LOC.rom_file_label, info->rom_path);
        printf(LOC.rom_size_label, (unsigned)size_options[selected]);
        if ((size_options[selected] * 0x100000) == info->detected_size) {
            print_loc(LOC.detected_suffix);
        }
        print_loc(LOC.newline);
        print_loc(LOC.prompt_left_right_confirm);
        shark_console_render();

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
        case DUMP_MODE_ROM_ONLY: return LOC.mode_rom_only;
        case DUMP_MODE_SAVE_ONLY: return LOC.mode_save_only;
        case DUMP_MODE_ROM_AND_SAVE: return LOC.mode_rom_save;
    }

    return LOC.mode_rom_only;
}

static bool select_dump_mode(dump_mode_t *mode) {
    int selected = DUMP_MODE_ROM_AND_SAVE;

    while (true) {
        print_header();
        printf(LOC.dump_mode_label, dump_mode_name((dump_mode_t)selected));
        print_loc(LOC.prompt_left_right_confirm);
        shark_console_render();

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

static bool cart_uses_4k_eeprom(const cart_info_t *cart) {
    if (cart == NULL) {
        return false;
    }

    return cart->product[0] == 'N' &&
           cart->product[1] == 'K' &&
           cart->product[2] == '4';
}

static save_info_t detect_save_info(const cart_info_t *cart) {
    const eeprom_type_t eeprom = eeprom_present_raw();
    if (eeprom == EEPROM_4K || eeprom == EEPROM_16K) {
        const bool force_4k = cart_uses_4k_eeprom(cart);
        save_info_t info = {
            .kind = SAVE_KIND_EEPROM,
            .name = (eeprom == EEPROM_4K || force_4k) ? "EEPROM 4Kbit" : "EEPROM 16Kbit",
            .size = (eeprom == EEPROM_4K || force_4k) ? SAVE_EEPROM_4K_SIZE : SAVE_EEPROM_16K_SIZE,
        };
        return info;
    }

    uint32_t flash_id[2] __attribute__((aligned(16))) = {0};
    set_dom2_save_config();
    io_write(CART_DOM2_ADDR2_START | FLASHRAM_OFFSET_COMMAND, FLASHRAM_COMMAND_SET_IDENTIFY_MODE);
    cart_dom2_read(flash_id, 0, sizeof(flash_id));
    flashram_set_read_mode();

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
        return eeprom_read_data_raw(save_buf, save_info->size);
    }

    if (save_info->kind == SAVE_KIND_FLASH) {
        return flashram_os_read_array(save_buf, 0, save_info->size / FLASHRAM_PAGE_SIZE);
    }

    cart_dom2_read(save_buf, 0, save_info->size);
    return true;
}

static bool eeprom_exec_packet(const void *send, size_t send_len,
                               void *recv, size_t recv_len) {
    /*
     * Libultra packs EEPROM commands onto SI channel 4 after four NOP bytes,
     * then performs a write/read PIF transaction.  joybus_exec() wraps that
     * two-phase SI exchange for libdragon, so this helper keeps the packet
     * layout libultra-compatible while avoiding duplicated boilerplate.
     */
    static const uint8_t eeprom_port = 4;
    uint8_t input[0x40] = {0};
    uint8_t output[0x40] = {0};
    size_t pos = eeprom_port;

    if ((pos + 2 + send_len + recv_len + 1) > sizeof(input)) {
        return false;
    }

    input[pos++] = (uint8_t)send_len;
    input[pos++] = (uint8_t)recv_len;
    memcpy(&input[pos], send, send_len);
    pos += send_len + recv_len;
    input[pos] = 0xFE;
    input[sizeof(input) - 1] = 0x01;

    joybus_exec(input, output);
    memcpy(recv, &output[pos - recv_len], recv_len);
    return true;
}

static uint8_t __attribute__((unused)) eeprom_channel_error(uint8_t status) {
    return status & 0xC0;
}

static bool eeprom_status_raw(uint16_t *out_type, uint8_t *out_status) {
    eeprom_status_cmd_t cmd = { .send = {
        .command = JOYBUS_COMMAND_ID_STATUS,
    } };

    if (!eeprom_exec_packet(&cmd.send, sizeof(cmd.send),
                            &cmd.recv, sizeof(cmd.recv))) {
        return false;
    }

    if (out_type != NULL) {
        *out_type = ((uint16_t)cmd.recv.type_hi << 8) | cmd.recv.type_lo;
    }
    if (out_status != NULL) {
        *out_status = cmd.recv.status;
    }
    return true;
}

static eeprom_type_t eeprom_present_raw(void) {
    uint16_t type = 0;
    uint8_t status = 0;

    if (!eeprom_status_raw(&type, &status)) {
        return EEPROM_NONE;
    }
    if ((type & JOYBUS_EEPROM_TYPE) == 0) {
        return EEPROM_NONE;
    }

    return (type & JOYBUS_EEPROM_16K_TYPE) ? EEPROM_16K : EEPROM_4K;
}

static bool eeprom_wait_ready(void) {
    const uint64_t start_ms = get_ticks_ms();

    while (true) {
        uint16_t type = 0;
        uint8_t status = 0;

        if (!eeprom_status_raw(&type, &status)) {
            return false;
        }
        if ((type & JOYBUS_EEPROM_TYPE) == 0) {
            return false;
        }
        if ((status & JOYBUS_EEPROM_BUSY) == 0) {
            return true;
        }
        if ((get_ticks_ms() - start_ms) > JOYBUS_EEPROM_STATUS_TIMEOUT_MS) {
            return false;
        }

        wait_ms(1);
    }
}

static bool eeprom_read_block_raw(uint8_t block, uint8_t *dst) {
    eeprom_read_block_cmd_t cmd = { .send = {
        .command = JOYBUS_COMMAND_ID_EEPROM_READ_BLOCK,
        .block = block,
    } };

    if (!eeprom_wait_ready()) {
        return false;
    }

    if (!eeprom_exec_packet(&cmd.send, sizeof(cmd.send),
                            &cmd.recv, sizeof(cmd.recv))) {
        return false;
    }
    if (cmd.recv.status != 0) {
        return false;
    }

    memcpy(dst, cmd.recv.data, EEPROM_BLOCK_SIZE);
    return true;
}

static bool eeprom_write_block_raw(uint8_t block, const uint8_t *src) {
    eeprom_write_block_cmd_t cmd = { .send = {
        .command = JOYBUS_COMMAND_ID_EEPROM_WRITE_BLOCK,
        .block = block,
    } };

    if (!eeprom_wait_ready()) {
        return false;
    }

    memcpy(cmd.send.data, src, EEPROM_BLOCK_SIZE);
    if (!eeprom_exec_packet(&cmd.send, sizeof(cmd.send),
                            &cmd.recv, sizeof(cmd.recv))) {
        return false;
    }
    if (cmd.recv.status != 0) {
        return false;
    }

    return eeprom_wait_ready();
}

static bool eeprom_read_data_raw(uint8_t *save_buf, size_t len) {
    if ((len % EEPROM_BLOCK_SIZE) != 0) {
        return false;
    }

    for (size_t offset = 0; offset < len; offset += EEPROM_BLOCK_SIZE) {
        const uint8_t block = (uint8_t)(offset / EEPROM_BLOCK_SIZE);
        if (!eeprom_read_block_raw(block, save_buf + offset)) {
            return false;
        }
    }

    return true;
}

static bool eeprom_write_data_verified(const uint8_t *save_buf, size_t len) {
    uint8_t verify[EEPROM_BLOCK_SIZE];

    if ((len % EEPROM_BLOCK_SIZE) != 0) {
        return false;
    }

    for (size_t offset = 0; offset < len; offset += EEPROM_BLOCK_SIZE) {
        const uint8_t block = (uint8_t)(offset / EEPROM_BLOCK_SIZE);
        bool ok = false;

        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            if (!eeprom_write_block_raw(block, save_buf + offset)) {
                wait_ms(2);
                continue;
            }

            ok = eeprom_read_block_raw(block, verify) &&
                 memcmp(save_buf + offset, verify, EEPROM_BLOCK_SIZE) == 0;
            if (!ok) {
                wait_ms(2);
            }
        }

        if (!ok) {
            return false;
        }
    }

    return true;
}

static bool write_save_data_to_cart(const uint8_t *save_buf, const save_info_t *save_info) {
    if (save_info->kind == SAVE_KIND_EEPROM) {
        return eeprom_write_data_verified(save_buf, save_info->size);
    }

    if (save_info->kind == SAVE_KIND_FLASH) {
        return flashram_write_data(save_buf, save_info->size);
    }

    cart_dom2_write(save_buf, 0, save_info->size);
    return true;
}

static uint8_t clear_save_fill_byte(const save_info_t *save_info) {
    (void)save_info;
    return 0xFF;
}

static bool clear_save_data_on_cart(const uint8_t *save_buf, const save_info_t *save_info) {
    if (save_info->kind == SAVE_KIND_FLASH) {
        return flashram_clear_data();
    }

    if (save_info->kind == SAVE_KIND_EEPROM) {
        uint8_t clear_block[EEPROM_BLOCK_SIZE];
        memset(clear_block, save_buf[0], sizeof(clear_block));

        if (!eeprom_write_data_verified(save_buf, save_info->size)) {
            return false;
        }

        for (size_t offset = save_info->size; offset < SAVE_EEPROM_16K_SIZE;
             offset += EEPROM_BLOCK_SIZE) {
            const uint8_t block = (uint8_t)(offset / EEPROM_BLOCK_SIZE);
            if (!eeprom_write_block_raw(block, clear_block)) {
                break;
            }
            wait_ms(8);
        }

        return true;
    }

    return write_save_data_to_cart(save_buf, save_info);
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

static char ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }

    return ch;
}

static bool ascii_streq_casefold(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (ascii_lower(*a++) != ascii_lower(*b++)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

static bool ascii_ends_with_casefold(const char *str, const char *suffix) {
    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return false;
    }

    return ascii_streq_casefold(str + str_len - suffix_len, suffix);
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

static uint32_t crc32_buffer(const uint8_t *buf, size_t len) {
    return crc32_update(0xFFFFFFFFu, buf, len) ^ 0xFFFFFFFFu;
}

static size_t first_diff_offset(const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return i;
        }
    }

    return len;
}

static void print_hex_bytes(const char *label, const uint8_t *buf, size_t len) {
    printf("%s", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X", buf[i]);
    }
    print_loc(LOC.newline);
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
    print_loc(LOC.cart_crc32_title);
    print_loc(LOC.cart_crc32_subtitle);
    printf(LOC.mib_progress, (unsigned)0, (unsigned)(rom_size / 0x100000));
    shark_console_render();

    while (offset < rom_size) {
        const size_t len = checksum_read_len(buf_size, rom_size - offset);

        cart_dma_read(buf, offset, len);
        crc = crc32_update(crc, buf, len);
        offset += len;

        if (offset >= next_report) {
            print_header();
            print_loc(LOC.cart_crc32_title);
            print_loc(LOC.cart_crc32_subtitle);
            printf(LOC.mib_progress,
                   (unsigned)(offset / 0x100000),
                   (unsigned)(rom_size / 0x100000));
            shark_console_render();
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
        printf(LOC.fmt_failed_open, path, errno);
        shark_console_render();
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
            printf(LOC.fmt_failed_read, path, errno);
            shark_console_render();
            close(fd);
            return false;
        }

        crc = crc32_update(crc, buf, len);
        offset += len;

        if (offset >= next_report) {
            print_header();
            print_loc(LOC.sd_crc32_title);
            printf(LOC.plain_line_spaced, path);
            printf(LOC.mib_progress,
                   (unsigned)(offset / 0x100000),
                   (unsigned)(size / 0x100000));
            shark_console_render();
            next_report = offset + 0x100000;
        }
    }

    if (close(fd) == -1) {
        printf(LOC.fmt_failed_close, path, errno);
        shark_console_render();
        return false;
    }

    *out_crc = crc ^ 0xFFFFFFFFu;
    return true;
}

static bool file_exists(const char *path) {
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return false;
    }

    close(fd);
    return true;
}

static const char *path_basename(const char *path) {
    const char *base = path;

    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }

    return base;
}

static bool copy_cstr_checked(char *dst, size_t dst_size, const char *src) {
    const size_t len = strlen(src);

    if (len >= dst_size) {
        return false;
    }

    memcpy(dst, src, len + 1);
    return true;
}

static bool format_restore_path(char *out, size_t out_size, const char *dir, const char *name) {
    const size_t dir_len = strlen(dir);
    const size_t name_len = strlen(name);
    const bool has_slash = dir_len > 0 && dir[dir_len - 1] == '/';
    const size_t slash_len = has_slash ? 0 : 1;

    if ((dir_len + slash_len + name_len) >= out_size) {
        return false;
    }

    memcpy(out, dir, dir_len);
    if (!has_slash) {
        out[dir_len] = '/';
    }
    memcpy(out + dir_len + slash_len, name, name_len + 1);
    return true;
}

static bool append_part_suffix(char *out, size_t out_size, const char *path) {
    static const char suffix[] = ".part";
    const size_t path_len = strlen(path);
    const size_t suffix_len = sizeof(suffix) - 1;

    if ((path_len + suffix_len) >= out_size) {
        return false;
    }

    memcpy(out, path, path_len);
    memcpy(out + path_len, suffix, suffix_len + 1);
    return true;
}

static void cart_save_filename(char *out, size_t out_size, const cart_info_t *cart) {
    char path[RESTORE_PATH_LEN];

    format_cart_file_path(path, sizeof(path), cart, "sd:", "sav");
    copy_cstr_checked(out, out_size, path_basename(path));
}

static bool parse_save_filename(const char *name, char *product,
                                uint8_t *revision, char *region) {
    const size_t len = strlen(name);
    if (!ascii_ends_with_casefold(name, ".sav") ||
        (len != (CART_PRODUCT_LENGTH + 4) && len != (CART_PRODUCT_LENGTH + 5))) {
        return false;
    }

    for (size_t i = 0; i < CART_PRODUCT_LENGTH; i++) {
        product[i] = name[i];
    }
    product[CART_PRODUCT_LENGTH] = '\0';

    *revision = 0;
    if (len == (CART_PRODUCT_LENGTH + 5)) {
        const char suffix = name[CART_PRODUCT_LENGTH];
        const char lower = ascii_lower(suffix);
        if (lower < 'a' || lower > 'z') {
            return false;
        }
        *revision = (uint8_t)(lower - 'a' + 1);
    }

    *region = product[CART_PRODUCT_LENGTH - 1];
    return true;
}

static bool confirm_restore_identity_warning(const restore_file_t *file,
                                             const cart_info_t *cart,
                                             const char *expected_name) {
    char file_product[CART_PRODUCT_LENGTH + 1];
    uint8_t file_revision = 0;
    char file_region = '\0';

    if (!parse_save_filename(file->name, file_product, &file_revision, &file_region)) {
        print_header();
        print_loc(LOC.restore_name_unrecognized);
        printf(LOC.plain_line_spaced, file->name);
        printf(LOC.expected_like, expected_name);
        print_loc(LOC.prompt_restore_anyway);
        shark_console_render();
        return wait_for_a_or_b();
    }

    const bool same_game = memcmp(file_product, cart->product, CART_PRODUCT_LENGTH - 1) == 0;
    const bool same_region = ascii_lower(file_region) ==
                             ascii_lower((char)cart->product[CART_PRODUCT_LENGTH - 1]);
    const bool same_revision = file_revision == cart->revision;

    if (same_game) {
        if (same_region && same_revision) {
            return true;
        }

        print_header();
        print_loc(LOC.restore_identity_warning);
        printf(LOC.file_label, file->name);
        printf(LOC.cart_label, expected_name);
        if (!same_region) {
            print_loc(LOC.region_differs);
        }
        if (!same_revision) {
            print_loc(LOC.revision_differs);
        }
        print_loc(LOC.newline);
        print_loc(LOC.prompt_restore_anyway);
        shark_console_render();
        return wait_for_a_or_b();
    }

    print_header();
    print_loc(LOC.restore_does_not_match);
    printf(LOC.file_label, file->name);
    printf(LOC.cart_label, expected_name);
    shark_console_render();
    return false;
}

static bool restore_file_size(int64_t dir_size, size_t *out_size) {
    if (dir_size > 0 && dir_size <= RESTORE_MAX_FILE_SIZE) {
        *out_size = (size_t)dir_size;
        return true;
    }

    return false;
}

static bool restore_file_size_from_path(const char *path, int64_t dir_size, size_t *out_size) {
    struct stat st;

    if (restore_file_size(dir_size, out_size)) {
        return true;
    }

    if (stat(path, &st) == 0) {
        return restore_file_size(st.st_size, out_size);
    }

    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return false;
    }

    const off_t end = lseek(fd, 0, SEEK_END);
    close(fd);

    return restore_file_size(end, out_size);
}

static int load_restore_files_from_dir(restore_file_t *files, int max_files, const char *dir_path) {
    dir_t dir;
    int count = 0;
    int res = dir_findfirst(dir_path, &dir);

    while (res == 0 && count < max_files) {
        size_t file_size = 0;
        char path[RESTORE_PATH_LEN];
        if (dir.d_type != DT_DIR &&
            ascii_ends_with_casefold(dir.d_name, ".sav") &&
            copy_cstr_checked(files[count].name, sizeof(files[count].name), dir.d_name) &&
            format_restore_path(path, sizeof(path), dir_path, dir.d_name) &&
            restore_file_size_from_path(path, dir.d_size, &file_size) &&
            copy_cstr_checked(files[count].path, sizeof(files[count].path), path)) {
            files[count].size = file_size;
            count++;
        }

        res = dir_findnext(dir_path, &dir);
    }

    return count;
}

static int load_restore_files(restore_file_t *files, int max_files,
                              const char **used_dir, const char **last_dir) {
    static const char *dir_paths[] = {
        RESTORE_DIR
    };

    *used_dir = NULL;
    *last_dir = dir_paths[0];
    for (size_t i = 0; i < sizeof(dir_paths) / sizeof(dir_paths[0]); i++) {
        *last_dir = dir_paths[i];
        const int count = load_restore_files_from_dir(files, max_files, dir_paths[i]);
        if (count > 0) {
            *used_dir = dir_paths[i];
            return count;
        }
    }

    return 0;
}

static int load_part_files_from_dir(part_file_t *files, int max_files, const char *dir_path) {
    dir_t dir;
    int count = 0;
    int res = dir_findfirst(dir_path, &dir);

    while (res == 0 && count < max_files) {
        char path[PART_PATH_LEN];
        dump_part_meta_t meta;

        if (dir.d_type != DT_DIR &&
            ascii_ends_with_casefold(dir.d_name, ".part") &&
            copy_cstr_checked(files[count].name, sizeof(files[count].name), dir.d_name) &&
            format_restore_path(path, sizeof(path), dir_path, dir.d_name) &&
            read_part_meta(path, &meta) &&
            copy_cstr_checked(files[count].path, sizeof(files[count].path), path)) {
            files[count].meta = meta;
            files[count].meta_valid = true;
            count++;
        }

        res = dir_findnext(dir_path, &dir);
    }

    return count;
}

static int load_part_files(part_file_t *files, int max_files) {
    static const char *const dir_paths[] = {
        ROM_DUMP_DIR,
        "sd:",
    };
    int count = 0;

    for (size_t i = 0; i < ARRAY_COUNT(dir_paths) && count < max_files; i++) {
        count += load_part_files_from_dir(files + count, max_files - count, dir_paths[i]);
    }

    return count;
}

static bool read_part_meta(const char *path, dump_part_meta_t *meta) {
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return false;
    }

    const bool ok = read_all(fd, (uint8_t *)meta, sizeof(*meta)) &&
                    meta->magic == PART_META_MAGIC &&
                    meta->version == PART_META_VERSION;
    close(fd);
    return ok;
}

static bool part_matches_cart(const part_file_t *file, const cart_info_t *cart,
                              dump_part_meta_t *meta) {
    char expected_rom[sizeof(cart->rom_path)];
    char expected_part[PART_NAME_LEN];

    format_cart_file_path(expected_rom, sizeof(expected_rom), cart, "sd:", "z64");
    if (!append_part_suffix(expected_part, sizeof(expected_part), path_basename(expected_rom))) {
        return false;
    }

    if (!file->meta_valid || !ascii_streq_casefold(file->name, expected_part)) {
        return false;
    }

    *meta = file->meta;
    return ascii_streq_casefold(meta->product, cart->product) &&
           meta->revision == cart->revision &&
           meta->rom_size != 0 &&
           meta->offset < meta->rom_size;
}

static bool find_resume_part(const part_file_t *files, int count, const cart_info_t *cart,
                             dump_part_meta_t *meta, char *rom_path, size_t rom_path_size) {
    for (int i = 0; i < count; i++) {
        if (!part_matches_cart(&files[i], cart, meta)) {
            continue;
        }

        const size_t path_len = strlen(files[i].path);
        const size_t suffix_len = strlen(".part");
        if (path_len <= suffix_len || (path_len - suffix_len) >= rom_path_size) {
            continue;
        }

        memcpy(rom_path, files[i].path, path_len - suffix_len);
        rom_path[path_len - suffix_len] = '\0';
        return true;
    }

    return false;
}

static bool find_resume_part_direct(const cart_info_t *cart, dump_part_meta_t *meta,
                                    char *rom_path, size_t rom_path_size) {
    static const char *const dirs[] = {
        ROM_DUMP_DIR,
        "sd:",
    };
    char expected_rom[PART_PATH_LEN];
    char expected_part[PART_PATH_LEN];

    for (size_t i = 0; i < ARRAY_COUNT(dirs); i++) {
        format_cart_file_path(expected_rom, sizeof(expected_rom), cart, dirs[i], "z64");
        if (!append_part_suffix(expected_part, sizeof(expected_part), expected_rom)) {
            continue;
        }

        if (!read_part_meta(expected_part, meta) ||
            !ascii_streq_casefold(meta->product, cart->product) ||
            meta->revision != cart->revision ||
            meta->rom_size == 0 ||
            meta->offset >= meta->rom_size ||
            !copy_cstr_checked(rom_path, rom_path_size, expected_rom)) {
            continue;
        }

        return true;
    }

    return false;
}

static bool confirm_resume_dump(const cart_info_t *cart, const dump_part_meta_t *meta) {
    print_header();
    print_loc(LOC.resume_title);
    printf(LOC.title_label, cart->title);
    print_revision_suffix(cart->revision);
    print_loc(LOC.newline);
    printf(LOC.resume_offset,
           (unsigned)(meta->offset / 0x100000),
           (unsigned)(meta->rom_size / 0x100000));
    print_loc(LOC.prompt_resume_dump);
    shark_console_render();
    return wait_for_a_or_b();
}

static bool write_part_meta(const cart_info_t *cart, size_t offset, uint32_t cart_crc32) {
    char part_path[PART_PATH_LEN];
    dump_part_meta_t meta = {
        .magic = PART_META_MAGIC,
        .version = PART_META_VERSION,
        .revision = cart->revision,
        .rom_size = (uint32_t)cart->selected_size,
        .offset = (uint32_t)offset,
        .cart_crc32 = cart_crc32,
    };

    memcpy(meta.product, cart->product, sizeof(meta.product));
    if (!append_part_suffix(part_path, sizeof(part_path), cart->rom_path)) {
        return false;
    }

    return write_sd_file_to_path(part_path, (const uint8_t *)&meta, sizeof(meta), false, 0, true);
}

static void delete_part_meta(const cart_info_t *cart) {
    char part_path[PART_PATH_LEN];

    if (append_part_suffix(part_path, sizeof(part_path), cart->rom_path)) {
        unlink(part_path);
    }
}

static bool select_restore_file(restore_file_t *files, int count,
                                const char *restore_dir, int *selected) {
    int index = 0;

    while (true) {
        print_header();
        print_loc(LOC.restore_save_title);
        printf(LOC.dir_label, restore_dir);
        printf(LOC.plain_line, files[index].name);
        printf(LOC.bytes_line, (unsigned)files[index].size);
        print_loc(LOC.prompt_left_right_confirm);
        shark_console_render();

        do {
            poll_controller();
            wait_ms(16);
        } while (inputs.btn.a || inputs.btn.b || inputs.btn.d_left || inputs.btn.d_right);

        while (true) {
            poll_controller();

            if (INPUT(btn.d_left)) {
                index = (index + count - 1) % count;
                break;
            }
            if (INPUT(btn.d_right)) {
                index = (index + 1) % count;
                break;
            }
            if (INPUT(btn.a)) {
                *selected = index;
                return true;
            }
            if (INPUT(btn.b)) {
                return false;
            }

            wait_ms(16);
        }
    }
}

static uint8_t *load_restore_save_file(const restore_file_t *file) {
    uint8_t *buf = malloc(file->size);
    if (buf == NULL) {
        printf(LOC.err_alloc_save, errno);
        shark_console_render();
        return NULL;
    }

    const int fd = open(file->path, O_RDONLY);
    if (fd == -1) {
        printf(LOC.fmt_failed_open, file->path, errno);
        shark_console_render();
        free(buf);
        return NULL;
    }

    if (!read_all(fd, buf, file->size)) {
        printf(LOC.fmt_failed_read, file->path, errno);
        shark_console_render();
        close(fd);
        free(buf);
        return NULL;
    }

    close(fd);
    return buf;
}

static bool confirm_save_overwrite(const char *path) {
    if (!file_exists(path)) {
        return true;
    }

    printf(LOC.save_exists, path);
    print_loc(LOC.prompt_overwrite_cancel);
    shark_console_render();
    return wait_for_a_or_b();
}

static int open_sd_output_file(const char *path, bool append, bool quiet) {
    int flags = O_WRONLY | O_CREAT;
    if (!append) {
        flags |= O_TRUNC;
    }

    const int fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1 && !quiet) {
        printf(LOC.fmt_failed_open, path, errno);
        shark_console_render();
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
        printf(LOC.fmt_failed_seek, path, errno);
        shark_console_render();
        close(fd);
        return false;
    }

    if (!write_all(fd, buf, len)) {
        printf(LOC.fmt_failed_write, path, errno);
        shark_console_render();
        close(fd);
        return false;
    }

    if (close(fd) == -1) {
        printf(LOC.fmt_failed_close, path, errno);
        shark_console_render();
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
    shark_console_clear();
    print_loc(LOC.app_header);

    if (dprog.cart) {
        printf(LOC.progress_title_line, dprog.cart->title);
        print_revision_suffix(dprog.cart->revision);
        print_loc(LOC.newline);
        printf(LOC.progress_size,
               (unsigned)(dprog.total_size / 0x100000));
        if (dprog.read_kbs != 0) {
            printf(" - %u KB/s", (unsigned)dprog.read_kbs);
        }
        print_loc(LOC.newline);
    }

    {
        int filled = dprog.total_size > 0
            ? (int)((dprog.read_addr * PROGRESS_BAR_WIDTH) / dprog.total_size)
            : 0;
        if (filled > PROGRESS_BAR_WIDTH) {
            filled = PROGRESS_BAR_WIDTH;
        }
        print_loc(LOC.progress_read);
        for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
            putchar(i < filled ? '*' : '-');
        }
        printf(LOC.progress_bar_end,
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
        print_loc(LOC.progress_write);
        for (int i = 0; i < PROGRESS_BAR_WIDTH; i++) {
            putchar(i < filled ? '*' : '-');
        }
        printf(LOC.progress_bar_end,
               (unsigned)(dprog.write_addr / 0x100000),
               (unsigned)(dprog.total_size / 0x100000));
    }

    shark_console_render();
}

static bool abort_combo_pressed(void) {
    poll_controller();
    return inputs.btn.z && inputs.btn.start;
}

static void comp_buf_tail_region(uint8_t *comp_buf, size_t comp_cap, size_t comp_size,
                                 uint8_t **tail_buf, size_t *tail_size) {
    const uintptr_t start = (uintptr_t)(comp_buf + comp_size);
    const uintptr_t aligned_start = (start + 15u) & ~(uintptr_t)15u;
    const uintptr_t end = (uintptr_t)(comp_buf + comp_cap);

    *tail_buf = NULL;
    *tail_size = 0;
    if (aligned_start >= end) {
        return;
    }

    *tail_buf = (uint8_t *)aligned_start;
    *tail_size = (size_t)(end - aligned_start);
}

static bool comp_workspace_setup(compression_workspace_t *work, uint8_t *comp_buf,
                                 size_t comp_cap,
                                 const compression_settings_t *settings,
                                 size_t scratch_size) {
    const size_t hash_size = ((size_t)1 << settings->hash_bits) * sizeof(work->head[0]);
    const size_t work_size = scratch_size + hash_size;

    if (comp_cap <= work_size + sizeof(comp_block_header_t)) {
        return false;
    }

    work->settings = *settings;
    work->scratch_size = scratch_size;
    work->hash_size = hash_size;
    work->work_offset = comp_cap - work_size;
    work->stream_cap = work->work_offset;
    work->scratch_buf = comp_buf + work->work_offset;
    work->head = (uint32_t *)(void *)(work->scratch_buf + scratch_size);
    return true;
}

static bool comp_workspace_use_final(compression_workspace_t *work, uint8_t *comp_buf,
                                     size_t comp_cap,
                                     const compression_settings_t *base_settings,
                                     size_t scratch_size, size_t out_pos) {
    compression_settings_t final_settings = *base_settings;
    final_settings.block_size = COMP_BLOCK_FINAL;
    final_settings.hash_bits = COMP_HASH_BITS_FINAL;

    compression_workspace_t final_work;
    if (!comp_workspace_setup(&final_work, comp_buf, comp_cap,
                              &final_settings, scratch_size) ||
        out_pos > final_work.stream_cap) {
        return false;
    }

    *work = final_work;
    return true;
}

static bool compress_cart_pass(uint8_t *comp_buf, size_t comp_cap,
                               const compression_settings_t *settings, size_t scratch_size,
                               size_t rom_offset,
                               size_t rom_size, compressed_pass_t *pass) {
    size_t out_pos = 0;
    size_t raw_pos = rom_offset;
    const bool compress = settings->block_size != 0;
    const uint64_t start_ms = get_ticks_ms();
    compression_workspace_t work = {0};
    bool using_final_work = false;

    dump_abort_requested = false;
    pass->raw_start = rom_offset;
    pass->raw_size = 0;
    pass->comp_size = 0;
    pass->work_offset = comp_cap;
    pass->scratch_size = 0;
    pass->hash_size = 0;
    pass->read_kbs = 0;
    for (size_t i = 0; i < RAW_TAIL_BUFFER_COUNT; i++) {
        pass->raw_tail_size[i] = 0;
    }

    if (comp_cap <= sizeof(comp_block_header_t)) {
        return false;
    }

    if (compress && !comp_workspace_setup(&work, comp_buf, comp_cap,
                                          settings, scratch_size)) {
        return false;
    }

    while (raw_pos < rom_size) {
        const compression_settings_t *active_settings = compress ? &work.settings : settings;
        const size_t io_block_size = compress ? active_settings->block_size : COMP_BLOCK_MAX;
        const size_t stream_cap = compress ? work.stream_cap : comp_cap;
        size_t block_len = rom_size - raw_pos;
        if (block_len > io_block_size) {
            block_len = io_block_size;
        }

        if ((out_pos + sizeof(comp_block_header_t)) > stream_cap) {
            if (compress && !using_final_work &&
                comp_workspace_use_final(&work, comp_buf, comp_cap,
                                         settings, scratch_size, out_pos)) {
                using_final_work = true;
                continue;
            }
            break;
        }

        const size_t header_pos = out_pos;
        const size_t payload_cap = stream_cap - (out_pos + sizeof(comp_block_header_t));
        const bool raw_block_fits = block_len <= payload_cap;
        if (!compress && !raw_block_fits) {
            break;
        }
        if (compress && !raw_block_fits && payload_cap <= (block_len / 2)) {
            if (!using_final_work &&
                comp_workspace_use_final(&work, comp_buf, comp_cap,
                                         settings, scratch_size, out_pos)) {
                using_final_work = true;
                continue;
            }
            break;
        }

        out_pos += sizeof(comp_block_header_t);

        uint8_t *const payload_buf = comp_buf + out_pos;
        uint8_t *const raw_buf = compress ? work.scratch_buf : payload_buf;

        cart_dma_read(raw_buf, raw_pos, block_len);

        size_t payload_size = 0;
        uint32_t flags = 0;

        if (compress) {
            const size_t compress_cap = raw_block_fits ? block_len : payload_cap;
            payload_size = comp_lz4_block(raw_buf, block_len, payload_buf,
                                          compress_cap, work.head, active_settings);
        }

        if (!compress || payload_size == 0 || payload_size >= block_len) {
            payload_size = block_len;
            flags = COMP_RAW_FLAG;

            if (!raw_block_fits) {
                out_pos = header_pos;
                if (compress && !using_final_work &&
                    comp_workspace_use_final(&work, comp_buf, comp_cap,
                                             settings, scratch_size, out_pos)) {
                    using_final_work = true;
                    continue;
                }
                break;
            }

            if (compress) {
                memcpy(payload_buf, raw_buf, payload_size);
            }
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
        const uint64_t elapsed_ms = get_ticks_ms() - start_ms;
        if (elapsed_ms > 0) {
            dprog.read_kbs = (uint32_t)((pass->raw_size * 1000ULL) /
                                        (elapsed_ms * 1024ULL));
            pass->read_kbs = dprog.read_kbs;
        }
        draw_dump_progress();

        if (abort_combo_pressed()) {
            dump_abort_requested = true;
            break;
        }
    }

    pass->comp_size = out_pos;
    if (compress) {
        pass->work_offset = work.work_offset;
        pass->scratch_size = work.scratch_size;
        pass->hash_size = work.hash_size;
    }

    uint8_t *comp_tail_buf = NULL;
    size_t comp_tail_size = 0;
    comp_buf_tail_region(comp_buf, pass->work_offset, pass->comp_size,
                         &comp_tail_buf, &comp_tail_size);

    uint8_t *const tail_bufs[RAW_TAIL_BUFFER_COUNT] = {
        compress ? comp_buf + pass->work_offset : NULL,
        compress ? comp_buf + pass->work_offset + pass->scratch_size : NULL,
        comp_tail_buf,
    };
    const size_t tail_caps[RAW_TAIL_BUFFER_COUNT] = {
        pass->scratch_size,
        pass->hash_size,
        comp_tail_size,
    };

    for (size_t i = 0; !dump_abort_requested && compress &&
         pass->raw_size > 0 && raw_pos < rom_size && i < RAW_TAIL_BUFFER_COUNT; i++) {
        size_t tail_len = rom_size - raw_pos;
        if (tail_len > tail_caps[i]) {
            tail_len = tail_caps[i];
        }
        if (tail_len == 0 || tail_bufs[i] == NULL) {
            continue;
        }

        cart_dma_read(tail_bufs[i], raw_pos, tail_len);
        raw_pos += tail_len;
        pass->raw_size += tail_len;
        pass->raw_tail_size[i] = tail_len;
        dprog.read_addr = raw_pos;

        const uint64_t elapsed_ms = get_ticks_ms() - start_ms;
        if (elapsed_ms > 0) {
            dprog.read_kbs = (uint32_t)((pass->raw_size * 1000ULL) /
                                        (elapsed_ms * 1024ULL));
            pass->read_kbs = dprog.read_kbs;
        }
        draw_dump_progress();
    }

    return !dump_abort_requested && pass->raw_size > 0;
}

static bool write_compressed_pass_to_sd(const char *path, uint8_t *comp_buf,
                                        const compressed_pass_t *pass,
                                        size_t block_size,
                                        uint8_t *scratch_buf, size_t scratch_size,
                                        const uint8_t *const tail_bufs[RAW_TAIL_BUFFER_COUNT],
                                        const size_t tail_caps[RAW_TAIL_BUFFER_COUNT],
                                        bool append,
                                        bool quiet_open, bool *opened) {
    if (opened != NULL) {
        *opened = false;
    }

    const int fd = open_sd_output_file(path, append, quiet_open);
    if (fd == -1) {
        return false;
    }
    if (opened != NULL) {
        *opened = true;
    }

    if (lseek(fd, pass->raw_start, SEEK_SET) == -1) {
        printf(LOC.fmt_failed_seek, path, errno);
        shark_console_render();
        close(fd);
        return false;
    }

    size_t in_pos = 0;
    size_t write_offset = pass->raw_start;
    size_t raw_tail_total = 0;

    for (size_t i = 0; i < RAW_TAIL_BUFFER_COUNT; i++) {
        if (pass->raw_tail_size[i] == 0) {
            continue;
        }

        if (tail_bufs == NULL || tail_caps == NULL || tail_bufs[i] == NULL ||
            pass->raw_tail_size[i] > tail_caps[i] ||
            pass->raw_tail_size[i] > (pass->raw_size - raw_tail_total)) {
            close(fd);
            return false;
        }

        raw_tail_total += pass->raw_tail_size[i];
    }

    const size_t stream_raw_size = pass->raw_size - raw_tail_total;
    size_t tail_offset = pass->raw_start + stream_raw_size;

    for (size_t i = 0; i < RAW_TAIL_BUFFER_COUNT; i++) {
        if (pass->raw_tail_size[i] == 0) {
            continue;
        }

        if (lseek(fd, tail_offset, SEEK_SET) == -1) {
            printf(LOC.fmt_failed_seek, path, errno);
            shark_console_render();
            close(fd);
            return false;
        }

        if (!write_all(fd, tail_bufs[i], pass->raw_tail_size[i])) {
            printf(LOC.fmt_failed_write, path, errno);
            shark_console_render();
            close(fd);
            return false;
        }

        tail_offset += pass->raw_tail_size[i];
    }

    if (raw_tail_total != 0 && lseek(fd, pass->raw_start, SEEK_SET) == -1) {
        printf(LOC.fmt_failed_seek, path, errno);
        shark_console_render();
        close(fd);
        return false;
    }

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

        if (raw_size > block_size || (in_pos + payload_size) > pass->comp_size ||
            (!raw && (scratch_buf == NULL || raw_size > scratch_size))) {
            close(fd);
            return false;
        }

        if (raw) {
            if (!write_all(fd, comp_buf + in_pos, payload_size)) {
                printf(LOC.fmt_failed_write, path, errno);
                shark_console_render();
                close(fd);
                return false;
            }
        } else {
            if (!comp_lz4_decode_block(comp_buf + in_pos, payload_size, scratch_buf, raw_size)) {
                print_loc(LOC.err_decompress_chunk);
                shark_console_render();
                close(fd);
                return false;
            }

            if (!write_all(fd, scratch_buf, raw_size)) {
                printf(LOC.fmt_failed_write, path, errno);
                shark_console_render();
                close(fd);
                return false;
            }
        }

        in_pos += payload_size;
        write_offset += raw_size;
        dprog.write_addr = write_offset;
        draw_dump_progress();
    }

    if (raw_tail_total != 0) {
        dprog.write_addr = pass->raw_start + pass->raw_size;
        draw_dump_progress();
    }

    if (close(fd) == -1) {
        printf(LOC.fmt_failed_close, path, errno);
        shark_console_render();
        return false;
    }

    return true;
}

static bool write_rom_pass_to_sd(cart_info_t *cart, uint8_t *comp_buf,
                                 const compressed_pass_t *pass,
                                 size_t block_size,
                                 uint8_t *scratch_buf, size_t scratch_size,
                                 const uint8_t *const tail_bufs[RAW_TAIL_BUFFER_COUNT],
                                 const size_t tail_caps[RAW_TAIL_BUFFER_COUNT],
                                 bool append) {
    char path[sizeof(cart->rom_path)];

    if (append) {
        return write_compressed_pass_to_sd(cart->rom_path, comp_buf, pass,
                                           block_size, scratch_buf, scratch_size,
                                           tail_bufs, tail_caps,
                                           true, false, NULL);
    }

    format_cart_file_path(path, sizeof(path), cart, ROM_DUMP_DIR, "z64");
    bool opened = false;
    if (write_compressed_pass_to_sd(path, comp_buf, pass,
                                    block_size, scratch_buf, scratch_size,
                                    tail_bufs, tail_caps,
                                    false, true, &opened)) {
        strcpy(cart->rom_path, path);
        return true;
    }
    if (opened) {
        return false;
    }

    format_cart_file_path(path, sizeof(path), cart, "sd:", "z64");
    strcpy(cart->rom_path, path);
    return write_compressed_pass_to_sd(cart->rom_path, comp_buf, pass,
                                       block_size, scratch_buf, scratch_size,
                                       tail_bufs, tail_caps,
                                       false, false, NULL);
}

static bool write_save_file_to_sd(cart_info_t *cart, const uint8_t *save_buf,
                                  const save_info_t *save_info) {
    char path[sizeof(cart->save_path)];

    format_cart_file_path(path, sizeof(path), cart, SAVE_DUMP_DIR, "sav");
    if (file_exists(path) && !confirm_save_overwrite(path)) {
        print_loc(LOC.save_write_cancelled);
        shark_console_render();
        return false;
    }

    if (write_sd_file_to_path(path, save_buf, save_info->size, false, 0, true)) {
        strcpy(cart->save_path, path);
        return true;
    }

    format_cart_file_path(path, sizeof(path), cart, "sd:", "sav");
    if (!confirm_save_overwrite(path)) {
        print_loc(LOC.save_write_cancelled);
        shark_console_render();
        return false;
    }

    strcpy(cart->save_path, path);
    return write_sd_file(cart->save_path, save_buf, save_info->size, false, 0);
}

static size_t align_down_size(size_t value, size_t align) {
    return value & ~(align - 1);
}

static uint8_t *malloc_largest_rom_buffer(size_t *out_size, size_t mem_size) {
    if (mem_size <= (CART_CHECKSUM_IMAGE_SIZE + ROM_BUFFER_HEAP_RESERVE)) {
        *out_size = 0;
        return NULL;
    }

    size_t try_size = align_down_size(mem_size - ROM_BUFFER_HEAP_RESERVE, ROM_BUFFER_ALIGN);

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

static bool realloc_largest_rom_buffer(uint8_t **buf, size_t *size, size_t mem_size) {
    const size_t old_size = *size;

    free(*buf);
    *buf = malloc_largest_rom_buffer(size, mem_size);
    if (*buf != NULL) {
        return true;
    }

    *buf = malloc(old_size);
    if (*buf != NULL) {
        *size = old_size;
        return true;
    }

    *size = 0;
    return false;
}

static void swap_reset_callback(void) {
}

static void swap_setup_callback(void) {
    print_loc(LOC.prompt_reset_now);
    shark_console_render();
}

static void ensure_pif_hung(void) {
    if (pif_hung) {
        return;
    }

    hang_pif(swap_reset_callback, swap_setup_callback);
    pif_hung = true;
}

static void swap(const char *to, const char *action) {
    (void)to;
    (void)action;
    ensure_pif_hung();

    print_loc(LOC.prompt_swap);
    shark_console_render();
}

static void prepare_flashcart_removal(void) {
    sdfs_unmount();
    cart_exit();
}

static void remount_sd(void) {
    sdfs_unmount();

    while (cart_init() < 0) {
        print_loc(LOC.prompt_flashcart_retry);
        shark_console_render();
        wait_for_a_press();
    }

    while (!sdfs_mount("sd:/", -1)) {
        printf(LOC.prompt_mount_retry, errno, sdfs_last_mount_result);
        shark_console_render();
        wait_for_a_press();
    }

}

void dump_rom(void) {
    print_header();

    const size_t mem_size = get_memory_size();
    cart_info_t cart = {0};
    dump_mode_t dump_mode = DUMP_MODE_ROM_AND_SAVE;
    save_info_t save_info = {0};
    part_file_t *part_files = NULL;
    int part_count = 0;
    dump_part_meta_t resume_meta = {0};
    uint8_t *crc_buf = NULL;
    uint8_t *comp_buf = NULL;
    uint8_t *save_buf = NULL;
    bool save_buf_owned = false;
    size_t save_buf_reserve = 0;
    size_t chunk = 0;
    size_t offset = 0;
    compression_settings_t comp_settings = {
        .block_size = COMP_BLOCK_DEFAULT,
        .hash_bits = COMP_HASH_BITS_MAX,
        .skip_strength = COMP_SKIP_STRENGTH,
        .max_skip = COMP_MAX_SKIP,
        .max_match_len = 0,
    };
    size_t io_block_size = COMP_BLOCK_DEFAULT;
    uint32_t cart_full_crc = 0;
    uint32_t sd_full_crc = 0;
    bool have_cart_full_crc = false;
    bool first_pass = true;
    bool ok = true;

    crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf(LOC.err_alloc_crc, errno);
        shark_console_render();
        return;
    }

    while (!sdfs_mount("sd:/", -1)) {
        printf(LOC.prompt_mount_retry, errno, sdfs_last_mount_result);
        shark_console_render();
        wait_for_a_press();
        print_header();
    }

    part_files = malloc(sizeof(part_file_t) * MAX_PART_FILES);
    if (part_files != NULL) {
        part_count = load_part_files(part_files, MAX_PART_FILES);
    } else {
        part_count = 0;
    }

    prepare_flashcart_removal();
    swap("cartridge", "read");
    wait_for_a_press();
    if (!wait_for_ready_game_cart(crc_buf, CART_CHECKSUM_IMAGE_SIZE, &cart, true)) {
        ok = false;
        pause_after_error();
    }

    bool resume_dump = false;
    if (ok) {
        char resume_rom_path[sizeof(cart.rom_path)];
        const bool found_resume =
            (part_files != NULL &&
             find_resume_part(part_files, part_count, &cart,
                              &resume_meta, resume_rom_path, sizeof(resume_rom_path))) ||
            find_resume_part_direct(&cart, &resume_meta,
                                    resume_rom_path, sizeof(resume_rom_path));
        if (found_resume &&
            confirm_resume_dump(&cart, &resume_meta)) {
            copy_cstr_checked(cart.rom_path, sizeof(cart.rom_path), resume_rom_path);
            cart.selected_size = resume_meta.rom_size;
            cart.detected_size = resume_meta.rom_size;
            offset = resume_meta.offset;
            cart_full_crc = resume_meta.cart_crc32;
            have_cart_full_crc = cart_full_crc != 0;
            dump_mode = DUMP_MODE_ROM_ONLY;
            resume_dump = true;
        }
    }

    if (part_files != NULL) {
        free(part_files);
        part_files = NULL;
        part_count = 0;
    }

    if (ok && !resume_dump &&
        (!select_rom_size(&cart) || !select_dump_mode(&dump_mode))) {
        ok = false;
    }

    if (ok && dump_mode != DUMP_MODE_ROM_ONLY) {
        save_info = detect_save_info(&cart);
        if (dump_mode == DUMP_MODE_SAVE_ONLY) {
            save_buf = malloc(save_info.size);
            save_buf_owned = save_buf != NULL;
            if (save_buf == NULL) {
                printf(LOC.err_alloc_save, errno);
                shark_console_render();
                ok = false;
                pause_after_error();
            }
        }
    }

    free(crc_buf);
    crc_buf = NULL;

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY) {
        io_block_size = comp_settings.block_size;
        comp_buf = malloc_largest_rom_buffer(&chunk, mem_size);
        if (comp_buf == NULL) {
            printf(LOC.err_alloc_rom, errno);
            shark_console_render();
            ok = false;
            pause_after_error();
        } else {
            if (dump_mode == DUMP_MODE_ROM_AND_SAVE) {
                save_buf_reserve = (save_info.size + 15u) & ~(size_t)15u;
                if (chunk <= save_buf_reserve + COMP_BLOCK_DEFAULT) {
                    printf(LOC.err_alloc_save, errno);
                    shark_console_render();
                    ok = false;
                    pause_after_error();
                } else {
                    save_buf = comp_buf + chunk - save_buf_reserve;
                    chunk -= save_buf_reserve;
                }
            }

            print_loc(LOC.compression_label);
            printf(LOC.compression_blocks_short, (unsigned)(comp_settings.block_size / 1024));
            shark_console_render();
        }
    }

    if (ok && dump_mode != DUMP_MODE_ROM_ONLY) {
        printf(LOC.reading_save,
               save_info.name, (unsigned)save_info.size);
        shark_console_render();

        if (!read_save_data(save_buf, &save_info)) {
            print_loc(LOC.err_read_save_data);
            shark_console_render();
            ok = false;
            pause_after_error();
        }
    }

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY && !have_cart_full_crc) {
        if (!checksum_cart_rom(comp_buf, chunk, cart.selected_size, &cart_full_crc)) {
            ok = false;
            pause_after_error();
        } else {
            have_cart_full_crc = true;
            printf(LOC.cart_crc32_result, cart_full_crc);
            shark_console_render();
        }
    }

    dprog.cart = &cart;
    dprog.total_size = cart.selected_size;
    dprog.read_addr = offset;
    dprog.write_addr = offset;
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
        bool save_freed_this_pass = false;

        if (!compress_cart_pass(comp_buf, chunk, &comp_settings, io_block_size,
                                offset, cart.selected_size, &pass)) {
            if (dump_abort_requested) {
                print_header();
                print_loc(LOC.chunk_cancelled);
                shark_console_render();
                ok = false;
                pause_after_error();
                break;
            }

            print_loc(LOC.err_compress_pass);
            shark_console_render();
            ok = false;
            pause_after_error();
            break;
        }

        printf(LOC.buffered_range,
               (unsigned)pass.raw_start,
               (unsigned)(pass.raw_start + pass.raw_size));
        size_t raw_tail_total = 0;
        for (size_t i = 0; i < RAW_TAIL_BUFFER_COUNT; i++) {
            raw_tail_total += pass.raw_tail_size[i];
        }
        printf(LOC.compressed_summary,
               (unsigned)(pass.raw_size / 1024),
               (unsigned)((pass.comp_size + raw_tail_total) / 1024));
        print_loc(LOC.newline);
        shark_console_render();

        swap("flashcart", "write");
        wait_for_a_press();
        remount_sd();

        if (first_pass && dump_mode != DUMP_MODE_ROM_ONLY) {
            if (!write_save_file_to_sd(&cart, save_buf, &save_info)) {
                ok = false;
                pause_after_error();
                break;
            }

            printf(LOC.wrote_save, cart.save_path);
            shark_console_render();
            if (save_buf_owned) {
                free(save_buf);
            }
            save_buf = NULL;
            save_buf_owned = false;
            save_freed_this_pass = true;
        }

        uint8_t *comp_tail_buf = NULL;
        size_t comp_tail_size = 0;
        comp_buf_tail_region(comp_buf, pass.work_offset, pass.comp_size,
                             &comp_tail_buf, &comp_tail_size);
        uint8_t *const scratch_buf = pass.scratch_size != 0
            ? comp_buf + pass.work_offset
            : NULL;

        const uint8_t *const tail_bufs[RAW_TAIL_BUFFER_COUNT] = {
            scratch_buf,
            pass.hash_size != 0 ? scratch_buf + pass.scratch_size : NULL,
            comp_tail_buf,
        };
        const size_t tail_caps[RAW_TAIL_BUFFER_COUNT] = {
            pass.scratch_size,
            pass.hash_size,
            comp_tail_size,
        };
        if (!write_rom_pass_to_sd(&cart, comp_buf, &pass,
                                  io_block_size, scratch_buf, pass.scratch_size,
                                  tail_bufs, tail_caps,
                                  offset != 0)) {
            ok = false;
            pause_after_error();
            break;
        }

        printf(LOC.wrote_chunk,
               (unsigned)pass.raw_start,
               (unsigned)(pass.raw_start + pass.raw_size));
        shark_console_render();
        offset += pass.raw_size;
        if (offset < cart.selected_size) {
            write_part_meta(&cart, offset, cart_full_crc);
        }

        if (save_freed_this_pass && save_buf_reserve != 0) {
            chunk += save_buf_reserve;
            save_buf_reserve = 0;
            shark_console_render();
        } else if (save_freed_this_pass && offset < cart.selected_size) {
            const size_t old_chunk = chunk;
            if (!realloc_largest_rom_buffer(&comp_buf, &chunk, mem_size)) {
                printf(LOC.err_alloc_rom, errno);
                shark_console_render();
                ok = false;
                pause_after_error();
                break;
            }
            if (chunk > old_chunk) {
                shark_console_render();
            }
        }

        first_pass = false;
    }

    if (ok && dump_mode != DUMP_MODE_SAVE_ONLY && have_cart_full_crc) {
        if (!checksum_sd_file(cart.rom_path, comp_buf, chunk, cart.selected_size, &sd_full_crc)) {
            ok = false;
            pause_after_error();
        } else {
            print_header();
            print_loc(LOC.full_rom_crc32);
            printf(LOC.cart_crc_line, cart_full_crc);
            printf(LOC.sd_crc_line, sd_full_crc);
            printf(LOC.checksum_status, (sd_full_crc == cart_full_crc) ? LOC.checksum_matches : LOC.checksum_mismatch);
            print_loc(LOC.press_a_continue);
            shark_console_render();
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
            printf(LOC.wrote_save, cart.save_path);
            shark_console_render();
        }
    }

    if (ok) {
        if (dump_mode != DUMP_MODE_SAVE_ONLY) {
            delete_part_meta(&cart);
        }
        print_loc(LOC.success);
        shark_console_render();
    }

    if (save_buf_owned) {
        free(save_buf);
    }
    free(comp_buf);
    free(crc_buf);
}

void dump_to_accessory(void) {
    print_header();

    joypad_port_t port;
    if (!accessory_find(&port)) {
        print_loc(LOC.err_no_accessory);
        shark_console_render();

        return;
    }

    printf(LOC.accessory_found, port + 1);
    shark_console_render();

    uint8_t *const crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf(LOC.err_alloc_crc, errno);
        shark_console_render();

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

    printf(LOC.streaming_accessory, port + 1);
    shark_console_render();

    accessory_dump_stream(port, cart.selected_size);

    print_loc(LOC.verifying);
    shark_console_render();

    accessory_verify(port, cart.selected_size);
}

void restore_save(void) {
    print_header();

    while (!sdfs_mount("sd:/", -1)) {
        printf(LOC.prompt_mount_retry, errno, sdfs_last_mount_result);
        shark_console_render();
        wait_for_a_press();
        print_header();
    }

    restore_file_t files[MAX_RESTORE_FILES];
    int selected = 0;
    const char *restore_dir = NULL;
    const char *last_restore_dir = NULL;
    const int count = load_restore_files(files, MAX_RESTORE_FILES,
                                         &restore_dir, &last_restore_dir);
    if (count <= 0) {
        print_loc(LOC.no_sav_files);
        printf(LOC.last_tried, last_restore_dir ? last_restore_dir : RESTORE_DIR);
        print_loc(LOC.press_a_continue);
        shark_console_render();
        wait_for_a_press();
        return;
    }

    if (!select_restore_file(files, count, restore_dir, &selected)) {
        return;
    }

    const restore_file_t *file = &files[selected];
    uint8_t *save_buf = load_restore_save_file(file);
    if (save_buf == NULL) {
        pause_after_error();
        return;
    }

    uint8_t *verify_buf = malloc(SAVE_FLASH_SIZE);
    if (verify_buf == NULL) {
        printf(LOC.err_alloc_verify, errno);
        shark_console_render();
        free(save_buf);
        pause_after_error();
        return;
    }

    uint8_t *crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf(LOC.err_alloc_crc, errno);
        shark_console_render();
        free(verify_buf);
        free(save_buf);
        pause_after_error();
        return;
    }

    prepare_flashcart_removal();
    swap("cartridge", "restore save");
    wait_for_a_press();

    cart_info_t cart = {0};
    bool ok = true;
    if (!wait_for_ready_game_cart(crc_buf, CART_CHECKSUM_IMAGE_SIZE, &cart, true)) {
        ok = false;
        pause_after_error();
    }

    save_info_t save_info = {0};
    if (ok) {
        char expected_name[RESTORE_NAME_LEN];
        cart_save_filename(expected_name, sizeof(expected_name), &cart);

        if (!confirm_restore_identity_warning(file, &cart, expected_name)) {
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        save_info = detect_save_info(&cart);
        if (file->size < save_info.size) {
            print_header();
            print_loc(LOC.save_too_small);
            printf(LOC.file_bytes, (unsigned)file->size);
            printf(LOC.cart_save_bytes, save_info.name, (unsigned)save_info.size);
            ok = false;
            pause_after_error();
        } else if (file->size > save_info.size) {
            print_header();
            print_loc(LOC.save_too_large);
            printf(LOC.file_bytes, (unsigned)file->size);
            printf(LOC.cart_save_bytes, save_info.name, (unsigned)save_info.size);
            printf(LOC.truncate_restore,
                   (unsigned)save_info.size);
            print_loc(LOC.prompt_continue_cancel);
            shark_console_render();
            if (!wait_for_a_or_b()) {
                ok = false;
            }
        }
    }

    if (ok) {
        print_header();
        printf(LOC.restore_file_line, file->name);
        printf(LOC.restore_to_title, cart.title);
        print_revision_suffix(cart.revision);
        print_loc(LOC.restore_confirm_body);
        print_loc(LOC.prompt_restore_anyway);
        shark_console_render();

        if (!wait_for_a_or_b()) {
            ok = false;
        }
    }

    if (ok) {
        print_header();
        printf(LOC.writing_save, save_info.name);
        shark_console_render();

        if (!write_save_data_to_cart(save_buf, &save_info)) {
            ok = false;
            pause_after_error();
        }
    }


    if (ok) {
        print_loc(LOC.verifying);
        shark_console_render();

        if (!read_save_data(verify_buf, &save_info) ||
            memcmp(save_buf, verify_buf, save_info.size) != 0) {
            const uint32_t want_crc = crc32_buffer(save_buf, save_info.size);
            const uint32_t got_crc = crc32_buffer(verify_buf, save_info.size);
            const size_t diff = first_diff_offset(save_buf, verify_buf, save_info.size);
            print_header();
            print_loc(LOC.restore_verify_failed);
            printf("Want: %08" PRIX32 "\n", want_crc);
            printf("Got:  %08" PRIX32 "\n", got_crc);
            printf("Diff: %06X\n", (unsigned)diff);
            print_hex_bytes("W0:", save_buf, 8);
            print_hex_bytes("G0:", verify_buf, 8);
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        print_header();
        print_loc(LOC.restore_success);
        print_loc(LOC.press_a_continue);
        shark_console_render();
        wait_for_a_press();
    }

    free(crc_buf);
    free(verify_buf);
    free(save_buf);
}

void clear_cart_save(void) {
    print_header();

    uint8_t *crc_buf = malloc(CART_CHECKSUM_IMAGE_SIZE);
    if (crc_buf == NULL) {
        printf(LOC.err_alloc_crc, errno);
        shark_console_render();
        pause_after_error();
        return;
    }

    prepare_flashcart_removal();
    swap("cartridge", "clear save");
    wait_for_a_press();

    cart_info_t cart = {0};
    bool ok = true;
    if (!wait_for_ready_game_cart(crc_buf, CART_CHECKSUM_IMAGE_SIZE, &cart, true)) {
        ok = false;
        pause_after_error();
    }

    save_info_t save_info = {0};
    uint8_t *save_buf = NULL;
    uint8_t *verify_buf = NULL;
    uint32_t before_crc = 0;
    uint32_t expected_crc = 0;
    uint32_t after_crc = 0;

    if (ok) {
        save_info = detect_save_info(&cart);

        print_header();
        print_loc(LOC.clear_title);
        printf(LOC.title_label, cart.title);
        print_revision_suffix(cart.revision);
        print_loc(LOC.newline);
        printf(LOC.product_label, cart.product);
        printf(LOC.region_label, printable_header_char(cart.region));
        printf(LOC.cart_save_bytes, save_info.name, (unsigned)save_info.size);
        print_loc(LOC.clear_warning);
        print_loc(LOC.prompt_continue_cancel);
        shark_console_render();

        if (!wait_for_a_or_b()) {
            ok = false;
        }
    }

    if (ok) {
        print_header();
        print_loc(LOC.clear_title);
        print_loc(LOC.clear_second_warning);
        print_loc(LOC.prompt_clear_second);
        shark_console_render();

        if (!wait_for_clear_combo_or_b()) {
            ok = false;
        }
    }

    if (ok) {
        save_buf = malloc(save_info.size);
        verify_buf = malloc(save_info.size);
        if (save_buf == NULL || verify_buf == NULL) {
            printf(LOC.err_alloc_save, errno);
            shark_console_render();
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        if (!read_save_data(verify_buf, &save_info)) {
            print_loc(LOC.err_read_save_data);
            shark_console_render();
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        before_crc = crc32_buffer(verify_buf, save_info.size);
        memset(save_buf, clear_save_fill_byte(&save_info), save_info.size);
        expected_crc = crc32_buffer(save_buf, save_info.size);

        print_header();
        printf(LOC.clearing_save, save_info.name, (unsigned)save_info.size);
        printf("Before:%08" PRIX32 "\n", before_crc);
        printf("Want:  %08" PRIX32 "\n", expected_crc);
        shark_console_render();

        if (!clear_save_data_on_cart(save_buf, &save_info)) {
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        print_loc(LOC.verifying);
        shark_console_render();

        const bool read_ok = read_save_data(verify_buf, &save_info);
        after_crc = read_ok ? crc32_buffer(verify_buf, save_info.size) : 0;
        if (!read_ok || memcmp(save_buf, verify_buf, save_info.size) != 0) {
            const size_t diff = read_ok ? first_diff_offset(save_buf, verify_buf, save_info.size) : 0;
            print_header();
            print_loc(LOC.clear_verify_failed);
            printf("Before:%08" PRIX32 "\n", before_crc);
            printf("Want:  %08" PRIX32 "\n", expected_crc);
            printf("After: %08" PRIX32 "\n", after_crc);
            if (read_ok) {
                printf("Diff:  %06X\n", (unsigned)diff);
            }
            ok = false;
            pause_after_error();
        }
    }

    if (ok) {
        print_header();
        print_loc(LOC.clear_success);
        printf(LOC.cart_save_bytes, save_info.name, (unsigned)save_info.size);
        printf("Before:%08" PRIX32 "\n", before_crc);
        printf("After: %08" PRIX32 "\n", after_crc);
        print_loc(LOC.press_a_continue);
        shark_console_render();
        wait_for_a_press();
    }

    free(verify_buf);
    free(save_buf);
    free(crc_buf);
}

int main(void) {
    shark_console_init();
    shark_console_set_render_mode(RENDER_MANUAL);
    timer_init();

    if (sys_bbplayer()) {
        print_loc(LOC.err_ique);
        shark_console_render();

        while (true) {
            continue;
        }
    }

    joypad_init();

    while (true) {
        print_header();
        print_loc(LOC.main_dump_sd);
        print_loc(LOC.main_dump_accessory);
        print_loc(LOC.main_restore);
        print_loc(LOC.main_clear_save);
        shark_console_render();

        poll_controller();

        if (inputs.btn.z && inputs.btn.d_down) {
            clear_cart_save();
        } else if (INPUT(btn.a)) {
            dump_rom();
        } else if (INPUT(btn.b)) {
            dump_to_accessory();
        } else if (INPUT(btn.c_up)) {
            restore_save();
        }
    }
}
