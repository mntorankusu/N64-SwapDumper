#include <libdragon.h>

#include <inttypes.h>
#include <string.h>

#include "accessory.h"

#define MAGIC_UPPER11 0x3FF
#define MAGIC_LO      0x44
#define MAGIC_HI      0x53

#define WRITE_RETRIES 3

static uint8_t accessory_crc5(uint16_t data11) {
    uint8_t crc = 0;

    for (int i = 10; i >= 0; i--) {
        uint8_t bit = (data11 >> i) & 1;
        uint8_t msb = (crc >> 4) & 1;

        crc = ((crc << 1) | bit) & 0x1F;
        if (msb) {
            crc ^= 0x05;
        }
    }

    return crc;
}

static uint16_t accessory_counter_addr(uint8_t counter) {
    uint16_t upper11 = ((uint16_t)counter) << 3;

    return (upper11 << 5) | accessory_crc5(upper11);
}

static uint16_t accessory_magic_addr(void) {
    return (MAGIC_UPPER11 << 5) | accessory_crc5(MAGIC_UPPER11);
}

bool accessory_detect(joypad_port_t port) {
    uint8_t buf[32];
    uint16_t addr = accessory_magic_addr();

    int ret = joybus_accessory_read(port, addr, buf);
    if (ret < 0) {
        return false;
    }

    return buf[0] == MAGIC_LO && buf[1] == MAGIC_HI;
}

int accessory_write(joypad_port_t port, uint16_t addr, const uint8_t *data) {
    int retries = 0;
    int ret;

    do {
        ret = joybus_accessory_write(port, addr, data);
    } while (ret == -3 && ++retries < WRITE_RETRIES);

    return ret;
}

int accessory_read_block(joypad_port_t port, uint16_t addr, uint8_t *data) {
    int retries = 0;
    int ret;

    do {
        ret = joybus_accessory_read(port, addr, data);
    } while (ret == -3 && ++retries < WRITE_RETRIES);

    return ret;
}

bool accessory_find(joypad_port_t *out_port) {
    for (int i = 0; i < 4; i++) {
        joypad_port_t p = (joypad_port_t)i;

        if (!joypad_is_connected(p)) {
            continue;
        }

        if (accessory_detect(p)) {
            *out_port = p;
            return true;
        }
    }

    return false;
}

void accessory_dump_stream(joypad_port_t port, size_t cart_size) {
    uint8_t block[32];
    uint8_t counter = 0;
    size_t last_report = 0;

    for (size_t offset = 0; offset < cart_size; offset += 32) {
        data_cache_hit_writeback_invalidate(block, sizeof(block));
        dma_read(block, 0x10000000 + offset, 32);

        uint16_t addr = accessory_counter_addr(counter);

        int ret = accessory_write(port, addr, block);
        if (ret < 0) {
            printf("Write failed at offset 0x%06" PRIXMAX " (err %d)\n",
                   (uintmax_t)offset, ret);
            console_render();

            return;
        }

        counter++;

        if (offset - last_report >= 0x80000) {
            printf("Progress: %u%%\n", (unsigned)((offset * 100) / cart_size));
            console_render();
            last_report = offset;
        }
    }

    printf("Stream complete!\n");
    console_render();
}

bool accessory_verify(joypad_port_t port, size_t cart_size) {
    uint8_t cart_buf[32];
    uint8_t acc_buf[32];
    bool ok = true;

    accessory_detect(port);

    uint8_t counter = 0;

    for (size_t offset = 0; ok && offset < cart_size; offset += 32) {
        data_cache_hit_writeback_invalidate(cart_buf, sizeof(cart_buf));
        dma_read(cart_buf, 0x10000000 + offset, 32);

        uint16_t addr = accessory_counter_addr(counter);

        int ret = accessory_read_block(port, addr, acc_buf);
        if (ret < 0) {
            printf("Verify read failed at 0x%06" PRIXMAX "\n", (uintmax_t)offset);
            console_render();
            ok = false;
            break;
        }

        if (memcmp(acc_buf, cart_buf, 32) != 0) {
            printf("Mismatch at 0x%06" PRIXMAX "\n", (uintmax_t)offset);
            console_render();
            ok = false;
        }

        counter++;
    }

    if (ok) {
        printf("Verify OK!\n");
    } else {
        printf("Verify FAILED\n");
    }
    console_render();

    return ok;
}
