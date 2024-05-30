/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <microkit.h>
#include <sel4_dma.h>
#include <uboot_drivers.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio_microkit.h>
#include <sel4_timer.h>
#include <plat/plat_support.h>
#include <plat/usb_platform_devices.h>

// DMA state
static ps_dma_man_t dma_manager;
uintptr_t dma_base_1;
uintptr_t dma_cp_paddr_1;
size_t dma_size = 0x100000;

// fdt initialise 
#define STR2(x) #x
#define STR(x) STR2(x)
#define INCBIN_SECTION ".rodata"
#define INCBIN(name, file) \
    __asm__(".section " INCBIN_SECTION "\n" \
            ".global incbin_" STR(name) "_start\n" \
            ".balign 16\n" \
            "incbin_" STR(name) "_start:\n" \
            ".incbin \"" file "\"\n" \
            \
            ".global incbin_" STR(name) "_end\n" \
            ".balign 1\n" \
            "incbin_" STR(name) "_end:\n" \
            ".byte 0\n" \
    ); \
    extern __attribute__((aligned(16))) const char incbin_ ## name ## _start[]; \
    extern                              const char incbin_ ## name ## _end[] 
INCBIN(device_tree, DTB_PATH); 

const char* _end = incbin_device_tree_end;

void handle_keypress(void) {
   
    
}

void
init(void)
{
    // Initalise DMA manager
    microkit_dma_manager(&dma_manager);
    
    // Initialise DMA
    microkit_dma_init(dma_base_1, dma_size,
        4096, 1);
    
    const char *const_dev_paths[] = DEV_PATHS;

    // Initialise uboot library
    initialise_uboot_drivers(
    dma_manager,
    incbin_device_tree_start,
    /* List the device tree paths for the devices */
    const_dev_paths, DEV_PATH_COUNT);

    /* Set USB keyboard as input device */
    int ret = run_uboot_command("setenv stdin usbkbd");
    if (ret < 0) {
        assert(!"Failed to set USB keyboard as the input device");
    }

    /* Start the USB subsystem */
    ret = run_uboot_command("usb start");
    if (ret < 0) {
        assert(!"Failed to start USB driver");
    }

    printf("Reading input from the USB keyboard:\n");
    int count = 0;
    bool enter_pressed = false;

    while(!enter_pressed) {
        while (uboot_stdin_tstc() > 0) {
            char c = uboot_stdin_getc();
            if (c == 13){
                enter_pressed = true;
                break;
            }
            printf("Received character: %c\n", c, stdout);
            microkit_ppcall(5, seL4_MessageInfo_new((uint64_t) c,1,0,0));
            count++;
        }
    }
    microkit_notify(5);

    return 0;
}

void
notified(microkit_channel ch)
{
    printf("Microkit notify crypto to keyreader on %d\n", ch);
    switch (ch) {
        case 5:
            shutdown_uboot_wrapper();
            handle_keypress();
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
}