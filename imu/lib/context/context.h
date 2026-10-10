#ifndef CONTEXT_H
#define CONTEXT_H

#define CONTEXT_SWITCHER_OFFSET 0x100u
#define MNIST_APP_OFFSET 0x100u
#define OCR_APP_OFFSET 0x140000u

#ifdef __cplusplus
extern "C" {
#endif
#include "pico/stdlib.h"
#include "hardware/structs/scb.h"   // scb_hw
#include "hardware/regs/xip.h"      // XIP_BASE
#include <stdint.h>

typedef void (*app_entry_t)(void);

void jump_to_image(uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif // CONTEXT_H