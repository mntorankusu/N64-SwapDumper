#pragma once

#include <libdragon.h>

bool accessory_detect(joypad_port_t port);

int accessory_write(joypad_port_t port, uint16_t addr, const uint8_t *data);

int accessory_read_block(joypad_port_t port, uint16_t addr, uint8_t *data);

void accessory_dump_stream(joypad_port_t port, size_t cart_size);

bool accessory_verify(joypad_port_t port, size_t cart_size);

bool accessory_find(joypad_port_t *out_port);
