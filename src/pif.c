#include <libdragon.h>

#include <stdbool.h>

#include "pif.h"

extern char __nasty_label_hack;

static exception_handler_t old_exception_handler;

static uint32_t sr;

static void exception_handler(exception_t *ex) {
    if (ex->type != EXCEPTION_TYPE_CRITICAL) {
        return old_exception_handler(ex);
    }

    if (ex->code != EXCEPTION_CODE_WATCH) {
        return old_exception_handler(ex);
    }

    ex->regs->sr = sr;
    ex->regs->epc = (uint32_t)&__nasty_label_hack;
    ex->regs->t1 = 0xA5A5A5A55A5A5A5A;
}

__attribute__((noinline)) void nasty_hack(void) {
    __asm__ volatile(".set push\n"
                     ".set noreorder\n"
                     "1:\n"
                     "j 1b\n"
                     "nop\n"
                     "__nasty_label_hack:\n"
                     ".set pop\n"
                     :
                     :
                     : "t1", "cc", "memory");
}

void hang_pif(void (*reset_callback)(), void (*setup_callback)(void)) {
    sr = C0_STATUS();

    MEMORY_BARRIER();

    old_exception_handler = register_exception_handler(exception_handler);

    bool reset_registered = reset_callback != NULL;
    if (reset_callback != NULL) {
        register_RESET_handler(reset_callback);
    }
    set_RESET_interrupt(true);

    MEMORY_BARRIER();

    C0_WRITE_WATCHLO(PhysicalAddr(SP_STATUS) | (1 << 1));

    if (setup_callback != NULL) {
        setup_callback();
    }

    MEMORY_BARRIER();

    nasty_hack();

    MEMORY_BARRIER();

    C0_WRITE_WATCHLO(0);

    set_RESET_interrupt(false);
    if (reset_registered) {
        unregister_RESET_handler(reset_callback);
    }

    register_exception_handler(old_exception_handler);
}
