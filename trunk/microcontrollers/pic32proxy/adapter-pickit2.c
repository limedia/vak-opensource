/*
 * Interface to PIC32 ICSP port using Microchip PICkit2/PICkit3 USB adapter.
 *
 * To use PICkit3, you would need to upgrade the firmware
 * using free PICkit 3 Scripting Tool from Microchip.
 *
 * Copyright (C) 2011-2012 Serge Vakulenko
 *
 * This file is part of PIC32PROXY project, which is distributed
 * under the terms of the GNU General Public License (GPL).
 * See the accompanying file "COPYING" for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <usb.h>

#include "adapter.h"
#include "hidapi.h"
#include "pickit2.h"
#include "pic32.h"

typedef struct {
    /* Common part */
    adapter_t adapter;
    int is_pk3;

    /* Device handle for libusb. */
    hid_device *hiddev;

    unsigned char reply [64];
    unsigned serial_execution_mode;

} pickit_adapter_t;

/*
 * Identifiers of USB adapter.
 */
#define MICROCHIP_VID           0x04d8
#define PICKIT2_PID             0x0033  /* Microchip PICkit 2 */
#define PICKIT3_PID             0x900a  /* Microchip PICkit 3 */

/*
 * USB endpoints.
 */
#define OUT_EP                  0x01
#define IN_EP                   0x81

#define IFACE                   0
#define TIMO_MSEC               1000

#define WORD_AS_BYTES(w)  (unsigned char) (w), \
                          (unsigned char) ((w) >> 8), \
                          (unsigned char) ((w) >> 16), \
                          (unsigned char) ((w) >> 24)

static void pickit_send_buf (pickit_adapter_t *a, unsigned char *buf, unsigned nbytes)
{
    if (debug_level > 0) {
        int k;
        fprintf (stderr, "---Send");
        for (k=0; k<nbytes; ++k) {
            if (k != 0 && (k & 15) == 0)
                fprintf (stderr, "\n       ");
            fprintf (stderr, " %02x", buf[k]);
        }
        fprintf (stderr, "\n");
    }
    hid_write (a->hiddev, buf, 64);
}

static void pickit_send (pickit_adapter_t *a, unsigned argc, ...)
{
    va_list ap;
    unsigned i;
    unsigned char buf [64];

    memset (buf, CMD_END_OF_BUFFER, 64);
    va_start (ap, argc);
    for (i=0; i<argc; ++i)
        buf[i] = va_arg (ap, int);
    va_end (ap);
    pickit_send_buf (a, buf, i);
}

static void pickit_recv (pickit_adapter_t *a)
{
    if (hid_read (a->hiddev, a->reply, 64) != 64) {
        fprintf (stderr, "pickit: error receiving packet\n");
        exit (-1);
    }
    if (debug_level > 0) {
        int k;
        fprintf (stderr, "--->>>>");
        for (k=0; k<64; ++k) {
            if (k != 0 && (k & 15) == 0)
                fprintf (stderr, "\n       ");
            fprintf (stderr, " %02x", a->reply[k]);
        }
        fprintf (stderr, "\n");
    }
}

static void check_timeout (pickit_adapter_t *a, const char *message)
{
    unsigned status;

    pickit_send (a, 1, CMD_READ_STATUS);
    pickit_recv (a);
    status = a->reply[0] | a->reply[1] << 8;
    if (status & STATUS_ICD_TIMEOUT) {
        fprintf (stderr, "pickit: timed out at %s, status = %04x\n",
            message, status);
        exit (-1);
    }
}

/*
 * Hardware reset.
 */
static void pickit_reset_cpu (adapter_t *adapter)
{
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;

    /* Reset active low. */
    pickit_send (a, 3, CMD_EXECUTE_SCRIPT, 1,
        SCRIPT_MCLR_GND_ON);

    /* Disable reset. */
    pickit_send (a, 3, CMD_EXECUTE_SCRIPT, 1,
        SCRIPT_MCLR_GND_OFF);
}

/*
 * Is the processor stopped?
 */
static int pickit_cpu_stopped (adapter_t *adapter)
{
#if 0
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;
    unsigned ir;

    pickit_send (a, 1, 1, 4, TAP_DEBUG_ENABLE, 1);
    ir = pickit_recv (a);
    return ir & 4;
#else
    return 0;
#endif
}

/*
 * Stop the processor.
 */
static void pickit_stop_cpu (adapter_t *adapter)
{
#if 0
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;
    unsigned old_ir, i;

    /* Debug request. */
    pickit_send (a, 1, 1, 4, TAP_DEBUG_REQUEST, 0);

    /* Wait while processor enters debug mode. */
    i = 0;
    for (;;) {
        pickit_send (a, 1, 1, 4, TAP_DEBUG_ENABLE, 1);
        old_ir = pickit_recv (a);
        if (old_ir == 5)
            break;
        mdelay (10);
        if (++i >= 50) {
            fprintf (stderr, "Timeout while entering debug mode\n");
            exit (1);
        }
    }
#endif
}

static void pickit_finish (pickit_adapter_t *a, int power_on)
{
    /* Exit programming mode. */
    pickit_send (a, 18, CMD_CLEAR_UPLOAD_BUFFER, CMD_EXECUTE_SCRIPT, 15,
        SCRIPT_JT2_SETMODE, 5, 0x1f,
        SCRIPT_VPP_OFF,
        SCRIPT_MCLR_GND_ON,
        SCRIPT_VPP_PWM_OFF,
        SCRIPT_SET_ICSP_PINS, 6,                // set PGC high, PGD input
        SCRIPT_SET_ICSP_PINS, 2,                // set PGC low, PGD input
        SCRIPT_SET_ICSP_PINS, 3,                // set PGC and PGD as input
        SCRIPT_DELAY_LONG, 10,                  // 50 msec
        SCRIPT_BUSY_LED_OFF);

    if (! power_on) {
        /* Detach power from the board. */
        pickit_send (a, 4, CMD_EXECUTE_SCRIPT, 2,
            SCRIPT_VDD_OFF,
            SCRIPT_VDD_GND_ON);
    }

    /* Disable reset. */
    pickit_send (a, 3, CMD_EXECUTE_SCRIPT, 1,
        SCRIPT_MCLR_GND_OFF);

    /* Read board status. */
    check_timeout (a, "finish");
}

static void pickit_close (adapter_t *adapter, int power_on)
{
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;
    //fprintf (stderr, "pickit: close\n");

    pickit_finish (a, power_on);
    free (a);
}

/*
 * Read the Device Identification code
 */
static unsigned pickit_get_idcode (adapter_t *adapter)
{
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;
    unsigned idcode;

    /* Read device id. */
    pickit_send (a, 12, CMD_CLEAR_UPLOAD_BUFFER, CMD_EXECUTE_SCRIPT, 9,
        SCRIPT_JT2_SENDCMD, TAP_SW_MTAP,
        SCRIPT_JT2_SENDCMD, MTAP_IDCODE,
        SCRIPT_JT2_XFERDATA32_LIT, 0, 0, 0, 0);
    pickit_send (a, 1, CMD_UPLOAD_DATA);
    pickit_recv (a);
    //fprintf (stderr, "pickit: read id, %d bytes: %02x %02x %02x %02x\n",
    //  a->reply[0], a->reply[1], a->reply[2], a->reply[3], a->reply[4]);
    if (a->reply[0] != 4)
        return 0;
    idcode = a->reply[1] | a->reply[2] << 8 | a->reply[3] << 16 | a->reply[4] << 24;
    return idcode;
}

#if 0
/*
 * Put device to serial execution mode.
 */
static void serial_execution (pickit_adapter_t *a)
{
    if (a->serial_execution_mode)
        return;
    a->serial_execution_mode = 1;

    // Enter serial execution.
    if (debug_level > 0)
        fprintf (stderr, "pickit: enter serial execution\n");
    pickit_send (a, 29, CMD_EXECUTE_SCRIPT, 27,
        SCRIPT_JT2_SENDCMD, TAP_SW_MTAP,
        SCRIPT_JT2_SENDCMD, MTAP_COMMAND,
        SCRIPT_JT2_XFERDATA8_LIT, MCHP_STATUS,
        SCRIPT_JT2_SENDCMD, TAP_SW_MTAP,
        SCRIPT_JT2_SENDCMD, MTAP_COMMAND,
        SCRIPT_JT2_XFERDATA8_LIT, MCHP_ASSERT_RST,
        SCRIPT_JT2_SENDCMD, TAP_SW_ETAP,
        SCRIPT_JT2_SETMODE, 6, 0x1F,
        SCRIPT_JT2_SENDCMD, ETAP_EJTAGBOOT,
        SCRIPT_JT2_SENDCMD, TAP_SW_MTAP,
        SCRIPT_JT2_SENDCMD, MTAP_COMMAND,
        SCRIPT_JT2_XFERDATA8_LIT, MCHP_DEASSERT_RST,
        SCRIPT_JT2_XFERDATA8_LIT, MCHP_FLASH_ENABLE);
}

/*
 * Read a word from memory (without PE).
 */
static unsigned pickit_read_word (adapter_t *adapter, unsigned addr)
{
    pickit_adapter_t *a = (pickit_adapter_t*) adapter;
    serial_execution (a);

    unsigned addr_lo = addr & 0xFFFF;
    unsigned addr_hi = (addr >> 16) & 0xFFFF;
    pickit_send (a, 64, CMD_CLEAR_DOWNLOAD_BUFFER,
        CMD_CLEAR_UPLOAD_BUFFER,
        CMD_DOWNLOAD_DATA, 28,
            WORD_AS_BYTES (0x3c04bf80),             // lui s3, 0xFF20
            WORD_AS_BYTES (0x3c080000 | addr_hi),   // lui t0, addr_hi
            WORD_AS_BYTES (0x35080000 | addr_lo),   // ori t0, addr_lo
            WORD_AS_BYTES (0x8d090000),             // lw t1, 0(t0)
            WORD_AS_BYTES (0xae690000),             // sw t1, 0(s3)
            WORD_AS_BYTES (0x00094842),             // srl t1, 1
            WORD_AS_BYTES (0xae690004),             // sw t1, 4(s3)
        CMD_EXECUTE_SCRIPT, 29,
            SCRIPT_JT2_SENDCMD, TAP_SW_ETAP,
            SCRIPT_JT2_SETMODE, 6, 0x1F,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_SENDCMD, ETAP_FASTDATA,      // read FastData
            SCRIPT_JT2_XFERDATA32_LIT, 0, 0, 0, 0,
            SCRIPT_JT2_SETMODE, 6, 0x1F,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_XFERINST_BUF,
            SCRIPT_JT2_SENDCMD, ETAP_FASTDATA,      // read FastData
            SCRIPT_JT2_XFERDATA32_LIT, 0, 0, 0, 0,
        CMD_UPLOAD_DATA);
    pickit_recv (a);
    if (a->reply[0] != 8) {
        fprintf (stderr, "pickit: read word %08x: bad reply length=%u\n",
            addr, a->reply[0]);
        exit (-1);
    }
    unsigned value = a->reply[1] | (a->reply[2] << 8) |
           (a->reply[3] << 16) | (a->reply[4] << 24);
    unsigned value2 = a->reply[5] | (a->reply[6] << 8) |
           (a->reply[7] << 16) | (a->reply[8] << 24);
//fprintf (stderr, "    %08x -> %08x %08x\n", addr, value, value2);
    value >>= 1;
    value |= value2 & 0x80000000;
    return value;
}

/*
 * Read a block of memory, multiple of 1 kbyte.
 */
static void pickit_read_data (adapter_t *adapter,
    unsigned addr, unsigned nwords, unsigned *data)
{
    //pickit_adapter_t *a = (pickit_adapter_t*) adapter;

    //fprintf (stderr, "pickit: read %d bytes from %08x\n", nwords*4, addr);
    for (; nwords > 0; nwords--) {
        *data++ = pickit_read_word (adapter, addr);
        addr += 4;
    }
}
#endif

/*
 * Initialize adapter PICkit2/PICkit3.
 * Return a pointer to a data structure, allocated dynamically.
 * When adapter not found, return 0.
 */
adapter_t *adapter_open_pickit (void)
{
    pickit_adapter_t *a;
    hid_device *hiddev;
    int is_pk3 = 0;

    hiddev = hid_open (MICROCHIP_VID, PICKIT2_PID, 0);
    if (! hiddev) {
        hiddev = hid_open (MICROCHIP_VID, PICKIT3_PID, 0);
        if (! hiddev) {
            /*fprintf (stderr, "HID bootloader not found: vid=%04x, pid=%04x\n",
                MICROCHIP_VID, BOOTLOADER_PID);*/
            return 0;
        }
        is_pk3 = 1;
    }
    a = calloc (1, sizeof (*a));
    if (! a) {
        fprintf (stderr, "Out of memory\n");
        return 0;
    }
    a->hiddev = hiddev;
    a->is_pk3 = is_pk3;
    a->adapter.name = is_pk3 ? "PICkit3" : "PICkit2";

    /* Read version of adapter. */
    unsigned vers_major, vers_minor, vers_rev;
    if (a->is_pk3) {
        pickit_send (a, 2, CMD_GETVERSIONS_MPLAB, 0);
        pickit_recv (a);
        if (a->reply[30] != 'P' ||
            a->reply[31] != 'k' ||
            a->reply[32] != '3')
        {
            free (a);
            fprintf (stderr, "Incompatible PICkit3 firmware detected.\n");
            fprintf (stderr, "Please, upgrade the firmware using PICkit 3 Scripting Tool.\n");
            return 0;
        }
        vers_major = a->reply[33];
        vers_minor = a->reply[34];
        vers_rev = a->reply[35];
    } else {
        pickit_send (a, 2, CMD_CLEAR_UPLOAD_BUFFER, CMD_GET_VERSION);
        pickit_recv (a);
        vers_major = a->reply[0];
        vers_minor = a->reply[1];
        vers_rev = a->reply[2];
    }
    printf ("Adapter: %s Version %d.%d.%d\n",
        a->adapter.name, vers_major, vers_minor, vers_rev);

    /* Detach power from the board. */
    pickit_send (a, 4, CMD_EXECUTE_SCRIPT, 2,
        SCRIPT_VDD_OFF,
        SCRIPT_VDD_GND_ON);

    /* Setup power voltage 3.3V, fault limit 2.81V. */
    unsigned vdd = (unsigned) (3.3 * 32 + 10.5) << 6;
    unsigned vdd_limit = (unsigned) ((2.81 / 5) * 255);
    pickit_send (a, 4, CMD_SET_VDD, vdd, vdd >> 8, vdd_limit);

    /* Setup reset voltage 3.28V, fault limit 2.26V. */
    unsigned vpp = (unsigned) (3.28 * 18.61);
    unsigned vpp_limit = (unsigned) (2.26 * 18.61);
    pickit_send (a, 4, CMD_SET_VPP, 0x40, vpp, vpp_limit);

    /* Setup serial speed. */
    unsigned divisor = 10;
    pickit_send (a, 4, CMD_EXECUTE_SCRIPT, 2,
        SCRIPT_SET_ICSP_SPEED, divisor);

    /* Reset active low. */
    pickit_send (a, 3, CMD_EXECUTE_SCRIPT, 1,
        SCRIPT_MCLR_GND_ON);

    /* Read board status. */
    pickit_send (a, 2, CMD_CLEAR_UPLOAD_BUFFER, CMD_READ_STATUS);
    pickit_recv (a);
    unsigned status = a->reply[0] | a->reply[1] << 8;
    if (debug_level > 0)
        fprintf (stderr, "pickit: status %04x\n", status);

    switch (status & ~(STATUS_RESET | STATUS_BUTTON_PRESSED)) {
    case STATUS_VPP_GND_ON:
    case STATUS_VPP_GND_ON | STATUS_VPP_ON:
        /* Explorer 16 board: no need to enable power. */
        break;

    case STATUS_VDD_GND_ON | STATUS_VPP_GND_ON:
        /* Enable power to the board. */
        pickit_send (a, 4, CMD_EXECUTE_SCRIPT, 2,
            SCRIPT_VDD_GND_OFF,
            SCRIPT_VDD_ON);

        /* Read board status. */
        pickit_send (a, 2, CMD_CLEAR_UPLOAD_BUFFER, CMD_READ_STATUS);
        pickit_recv (a);
        status = a->reply[0] | a->reply[1] << 8;
        if (debug_level > 0)
            fprintf (stderr, "pickit: status %04x\n", status);
        if (status != (STATUS_VDD_ON | STATUS_VPP_GND_ON)) {
            fprintf (stderr, "pickit: invalid status = %04x.\n", status);
            return 0;
        }
        /* Wait for power to stabilize. */
        mdelay (500);
        break;

    default:
        fprintf (stderr, "pickit: invalid status = %04x\n", status);
        return 0;
    }

    /* Enter programming mode. */
    pickit_send (a, 42, CMD_CLEAR_UPLOAD_BUFFER, CMD_EXECUTE_SCRIPT, 39,
        SCRIPT_VPP_OFF,
        SCRIPT_MCLR_GND_ON,
        SCRIPT_VPP_PWM_ON,
        SCRIPT_BUSY_LED_ON,
        SCRIPT_SET_ICSP_PINS, 0,                // set PGC and PGD output low
        SCRIPT_DELAY_LONG, 20,                  // 100 msec
        SCRIPT_MCLR_GND_OFF,
        SCRIPT_VPP_ON,
        SCRIPT_DELAY_SHORT, 23,                 // 1 msec
        SCRIPT_VPP_OFF,
        SCRIPT_MCLR_GND_ON,
        SCRIPT_DELAY_SHORT, 47,                 // 2 msec
        SCRIPT_WRITE_BYTE_LITERAL, 0xb2,        // magic word
        SCRIPT_WRITE_BYTE_LITERAL, 0xc2,
        SCRIPT_WRITE_BYTE_LITERAL, 0x12,
        SCRIPT_WRITE_BYTE_LITERAL, 0x0a,
        SCRIPT_MCLR_GND_OFF,
        SCRIPT_VPP_ON,
        SCRIPT_DELAY_LONG, 2,                   // 10 msec
        SCRIPT_SET_ICSP_PINS, 2,                // set PGC low, PGD input
        SCRIPT_JT2_SETMODE, 6, 0x1f,
        SCRIPT_JT2_SENDCMD, TAP_SW_MTAP,
        SCRIPT_JT2_SENDCMD, MTAP_COMMAND,
        SCRIPT_JT2_XFERDATA8_LIT, MCHP_STATUS);
    pickit_send (a, 1, CMD_UPLOAD_DATA);
    pickit_recv (a);
    if (debug_level > 1)
        fprintf (stderr, "pickit: got %02x-%02x\n", a->reply[0], a->reply[1]);
    if (a->reply[0] != 1) {
        fprintf (stderr, "pickit: cannot get MCHP STATUS\n");
        pickit_finish (a, 0);
        return 0;
    }
    if (! (a->reply[1] & MCHP_STATUS_CFGRDY)) {
        fprintf (stderr, "No device attached.\n");
        pickit_finish (a, 0);
        return 0;
    }
    if (! (a->reply[1] & MCHP_STATUS_CPS)) {
        fprintf (stderr, "Device is code protected and must be erased first.\n");
        pickit_finish (a, 0);
        return 0;
    }

    /* User functions. */
    a->adapter.close = pickit_close;
    a->adapter.get_idcode = pickit_get_idcode;
    a->adapter.cpu_stopped = pickit_cpu_stopped;
    a->adapter.stop_cpu = pickit_stop_cpu;
    a->adapter.reset_cpu = pickit_reset_cpu;
    //a->adapter.read_word = pickit_read_word;
    //a->adapter.read_data = pickit_read_data;
    return &a->adapter;
}