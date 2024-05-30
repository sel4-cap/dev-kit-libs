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
#include <plat/mmc_platform_devices.h>
#include <circular_buffer.h>

#define LOG_FILE_DEVICE "mmc 0:1"  // Partition 1 on mmc device 0
#define LOG_FILENAME  "transmitter_log.txt"
#define LOG_FILE_WRITE_PERIOD_US 30000000  // Time between log file writes (30 seconds)


/* A buffer of encrypted characters to log to the SD/MMC card */
#define MMC_TX_BUF_LEN 4096
char mmc_pending_tx_buf[MMC_TX_BUF_LEN];
uint mmc_pending_length = 0;

uintptr_t data_buffer;
uintptr_t circular_buffer;

// DMA state
static ps_dma_man_t dma_manager;
uintptr_t dma_base_2;
uintptr_t dma_cp_paddr_2;
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

void write_pending_mmc_log()
{
    printf("mmc buffer %c\n", mmc_pending_tx_buf[0]);
    printf("mmc buffer %c\n", mmc_pending_tx_buf[1]);

    /* Track the total number of bytes written to the log file*/
    static uint total_bytes_written = 0;
    printf("mmc_pending_tx_buf %x", &mmc_pending_tx_buf);

    /* Write all keypresses stored in the 'mmc_pending_tx_buf' buffer to the log file */
    char uboot_cmd[64];
    sprintf(uboot_cmd, "fatwrite %s 0x%x %s %x %x",
        LOG_FILE_DEVICE,        // The U-Boot partition designation
        &mmc_pending_tx_buf,    // Address of the buffer to write
        LOG_FILENAME,           // Filename to log to
        mmc_pending_length,     // The number of bytes to write
        total_bytes_written);   // The offset in the file to start writing from
    int ret = run_uboot_command(uboot_cmd);

    // Test string to read the file into
    char read_string[mmc_pending_length];

    // Read then output contents of the file
    sprintf(uboot_cmd, "fatload %s 0x%x %s %x %x",
        LOG_FILE_DEVICE,      // The U-Boot partition designation
        &read_string,         // Address to read the data into
        LOG_FILENAME,         // Filename to read from
        mmc_pending_length,   // Max number of bytes to read (0 = to end of file)
        total_bytes_written); // The offset in the file to start read from
    run_uboot_command(uboot_cmd);
    printf("String read from file %s: %s\n", LOG_FILENAME, read_string);

    /* Clear the buffer if writing to the file was successful */
    /* Clear the buffer if writing to the file was successful */
    if (ret >= 0) {
        total_bytes_written += mmc_pending_length;

        /* All pending characters have now been sent. Clear the buffer */
        memset(mmc_pending_tx_buf, 0, mmc_pending_length);
        mmc_pending_length = 0;
    }

    printf("End of write_mmc_log\n");
}

void recieve_data_from_cypto(){
    // printf("recieve_data_from_cypto\n");
    circular_buffer_t* cb = (circular_buffer_t*)circular_buffer;
    while(!circular_buffer_empty(cb)){
        char encrypted_char = circular_buffer_get(cb);
        // printf("encrypted char %c\n", encrypted_char);
        /* Store the read character in the buffer of pending data to log to SD/MMC. */
        /* If the buffer is full then discard the character */
        if (mmc_pending_length < MMC_TX_BUF_LEN) {
            mmc_pending_tx_buf[mmc_pending_length] = encrypted_char;
            mmc_pending_length += 1;
        }
    }
}

void
init_post(void)
{   
    printf("init_post of transmitter\n");

    const char *const_dev_paths[] = DEV_PATHS;

    // Initalise DMA manager
    microkit_dma_manager(&dma_manager);

    // Initialise DMA
    microkit_dma_init(dma_base_2, dma_size,
        4096, 1);

    // Initialise uboot library
    initialise_uboot_drivers(
    dma_manager,
    incbin_device_tree_start,
    /* List the device tree paths for the devices */
    const_dev_paths, DEV_PATH_COUNT);


     /* Delete any existing log file to ensure we start with an empty file */
    char uboot_cmd[64];
    sprintf(uboot_cmd, "fatrm %s %s", LOG_FILE_DEVICE, LOG_FILENAME);
    run_uboot_command(uboot_cmd);

    recieve_data_from_cypto();
    write_pending_mmc_log();

    return 0;
}

void
init(void)
{
}

void
notified(microkit_channel ch)
{
    printf("Microkit notify crypto to transmitter on %d\n", ch);
    switch (ch) {
        case 6:
            init_post();
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
}

seL4_MessageInfo_t
protected(microkit_channel ch, microkit_msginfo msginfo)
{
    printf("Microkit notify crypto to transmitter on %d\n", ch);
    switch (ch) {
        case 6:
            init_post();
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
    return seL4_MessageInfo_new(0,0,0,0);
}