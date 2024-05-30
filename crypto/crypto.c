/*
 * Copyright 2022, Capgemini Engineering
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <microkit.h>
#include <uboot_drivers.h>
#include <string.h>
#include <stdio_microkit.h>
#include <plat/plat_support.h>
#include <circular_buffer.h>

/* ASCII codes for characters referenced in the rot_13 routine */
#define UC_A 65
#define UC_Z 90
#define LC_a 97
#define LC_z 122

/* A buffer of encrypted characters to log to the SD/MMC card */
#define MMC_TX_BUF_LEN 4096
uintptr_t data_buffer;
uintptr_t circular_buffer;
int mmc_pending_length = 0;

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

void write_buffer(uintptr_t memory_region, char encrypted_char){
    circular_buffer_t* cb = (circular_buffer_t*)circular_buffer;
    cb->lock = true;
    // MIGHT NEED DELAY HERE TO STOP WRITING AND READING AT SAME TIME

    printf("Encrypted char %c\n", encrypted_char);

    circular_buffer_put(circular_buffer, encrypted_char);

    cb->lock = false;
}

void handle_character(char c){
    printf("In handle character\n");
    printf("Char is %c\n", c);
    char encrypted_char = rot_13(c);

    write_buffer(data_buffer, encrypted_char);
}


void init()
{ 
    size_t buffer_size = 100;
    circular_buffer = circular_buffer_init(circular_buffer, buffer_size, data_buffer);
}

void
notified(microkit_channel ch)
{
    switch (ch){
        case 5:
        printf("In crypto notified\n");
        microkit_notify(6);
    }
}

seL4_MessageInfo_t
protected(microkit_channel ch, microkit_msginfo msginfo)
{
    char c;
    switch (ch) {
        case 5:
            c = (char) microkit_msginfo_get_label(msginfo);
            handle_character(c);
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
    return seL4_MessageInfo_new(0,0,0,0);
}