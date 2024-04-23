/*
 * Copyright 2020, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include <microkit.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include <sddf/util/util.h>
#include <sddf/util/printf.h>

#include "echo.h"

#define UDP_ECHO_PORT 1235
static struct udp_pcb *udp_socket;

static const char *err_strerr[] = {
  "Ok.",                    /* ERR_OK          0  */
  "Out of memory error.",   /* ERR_MEM        -1  */
  "Buffer error.",          /* ERR_BUF        -2  */
  "Timeout.",               /* ERR_TIMEOUT    -3  */
  "Routing problem.",       /* ERR_RTE        -4  */
  "Operation in progress.", /* ERR_INPROGRESS -5  */
  "Illegal value.",         /* ERR_VAL        -6  */
  "Operation would block.", /* ERR_WOULDBLOCK -7  */
  "Address in use.",        /* ERR_USE        -8  */
  "Already connecting.",    /* ERR_ALREADY    -9  */
  "Already connected.",     /* ERR_ISCONN     -10 */
  "Not connected.",         /* ERR_CONN       -11 */
  "Low-level netif error.", /* ERR_IF         -12 */
  "Connection aborted.",    /* ERR_ABRT       -13 */
  "Connection reset.",      /* ERR_RST        -14 */
  "Connection closed.",     /* ERR_CLSD       -15 */
  "Illegal argument.",      /* ERR_ARG        -16 */
  "Unknown error."          /* ERR_UNKN       -17 */
};

#define ERR_UNKN 17

/**
 * Convert an lwip internal error to a string representation.
 *
 * @param err an lwip internal err_t
 * @return a string representation for err
 */
const char * lwip_strerr(err_t err)
{
  if ((err > 0) || (-err >= (err_t)LWIP_ARRAYSIZE(err_strerr))) return err_strerr[-ERR_UNKN];
  return err_strerr[-err];
}

static void lwip_udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    err_t error = udp_sendto(pcb, p, addr, port);
    if (error) printf("Failed to send UDP packet through socket: %s\n", lwip_strerr(error));
    pbuf_free(p);
}

int setup_udp_socket(void)
{
    udp_socket = udp_new_ip_type(IPADDR_TYPE_V4);
    if (udp_socket == NULL) {
        printf("Failed to open a UDP socket\n");
        return -1;
    }

    int error = udp_bind(udp_socket, IP_ANY_TYPE, UDP_ECHO_PORT);
    if (error == ERR_OK) {
        udp_recv(udp_socket, lwip_udp_recv_callback, udp_socket);
    } else {
        printf("Failed to bind the UDP socket\n");
        return -1;
    }

    return 0;
}