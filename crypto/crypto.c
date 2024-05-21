/*
 * Copyright 2022, Capgemini Engineering
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <microkit.h>
// #include <sel4_dma.h>
#include <uboot_drivers.h>
#include <string.h>
#include <stdio_microkit.h>
// #include <sel4_timer.h>
#include <plat_support.h>
#include <shared_ringbuffer.h>

/* It should be noted that the Crypto component is not an active component. This means
 * it does not have a main function and does not have an active thread of control.
 * All functionality within the Crypto component is triggered through remote
 * procedure calls:
 *  - circular_buffer_lock_lock and circular_buffer_lock_unlock called by the
 *    Transmitter component.
 *  - clear_text_handle_character called by the KeyReader component.
 * Instead, all functionality within the Crypto component is triggered through remote
 * procedure calls.
 *
 * See documentation on the 'control' keyword within the CAmkES manual for details
 * (https://docs.sel4.systems/projects/camkes/manual.html).
 */

/* ASCII codes for characters referenced in the rot_13 routine */
#define UC_A 65
#define UC_Z 90
#define LC_a 97
#define LC_z 122

uintptr_t data_packet;
uintptr_t ring_base;
uintptr_t ring_cp_paddr;
uintptr_t rx_cookies;
uintptr_t tx_cookies;
uintptr_t kbd_ring_buffer_vaddr;
uintptr_t kbd_ring_buffer_paddr;


/* A buffer of encrypted characters to log to the SD/MMC card */
#define MMC_TX_BUF_LEN 4096
uintptr_t data_packet;
char* mmc_pending_tx_buf = (void *)(uintptr_t)0x5011000;
// NEED TO CHANGE TO PROPER LENGTH
uint mmc_pending_length = 7;

#define MAX_KEYS 256
#define TX_COUNT 256

#define PACKET_BUFFER_SIZE  sizeof(uint16_t) * MAX_KEYS

// /* Make the minimum frame buffer 2k. This is a bit of a waste of memory, but ensures alignment */
// #define PACKET_BUFFER_SIZE  2048
// #define MAX_PACKET_SIZE     1536

#define RXD_EMPTY       (1UL << 15)
#define WRAP            (1UL << 13)

char* kbd_mem_write;
uintptr_t ta_limit;
uint64_t keyboard_size = 0x10000;
int ta_blocks = 256;
int ta_thresh = 16;
int ta_align = 64;

struct descriptor {
    uint16_t len;
    uint16_t stat;
    uint32_t addr;
};

typedef struct {
    unsigned int cnt;
    unsigned int remain;
    unsigned int tail;
    unsigned int head;
    volatile struct descriptor *descr;
    uintptr_t phys;
    void **cookies;
} ring_ctx_t;

ring_ctx_t kbd_ring;

/* Pointers to shared_ringbuffers */
ring_handle_t *kbd_buffer_ring;

static void
ring_setup()
{
    /* set up descriptor rings */
    kbd_ring.cnt = MAX_KEYS;
    kbd_ring.remain = kbd_ring.cnt - 2;
    kbd_ring.tail = 0;
    kbd_ring.head = 0;
    kbd_ring.phys = ring_cp_paddr;
    kbd_ring.cookies = (void **)rx_cookies;
    kbd_ring.descr = (volatile struct descriptor *)kbd_ring_buffer_vaddr;
}

/* Encryption routine. For the purposes of the demo we use "rot 13" */
char rot_13(char src)
{
    char result;

    // Only handle alphabet characters
    if((src >= LC_a && src <= LC_z) || (src >= UC_A && src <= UC_Z)) {
        if(src >= LC_a + 13 || (src >= UC_A + 13 && src <= UC_Z)) {
            // Characters that wrap around to the start of the alphabet
            result = src - 13;
        } else {
            // Characters that can be incremented
            result = src + 13;
        }
    } else {
        // Not an alphabet character, leave it unchanged
        result = src;
    }

    return result;
}

void handle_character(char c){
    char encrypted_char = rot_13(c);

    printf("Encrypted char %c\n", encrypted_char);

}




void init()
{
    for (size_t i = 0; i < mmc_pending_length; i++) {
        mmc_pending_tx_buf[i] = rot_13(mmc_pending_tx_buf[i]);
    }

    kbd_buffer_ring = malloc(sizeof(*kbd_buffer_ring));
    ring_setup();
    
}

void
notified(microkit_channel ch)
{
}

seL4_MessageInfo_t
protected(microkit_channel ch, microkit_msginfo msginfo)
{
    printf("entering ppcall from keyreader to crypto %d\n", ch);
    char c;
    switch (ch) {
        case 5:
            // return addr of root_intr_methods
            c = (char) microkit_msginfo_get_label(msginfo);
            handle_character(c);
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
    return seL4_MessageInfo_new(0,0,0,0);
}