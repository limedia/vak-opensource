/*
 * Simulate the peripherals of PIC32 microcontroller.
 *
 * Copyright (C) 2014 Serge Vakulenko, <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#include <stdio.h>
#include <unistd.h>
#include "globals.h"
#include "pic32mx.h"

#define STORAGE(name) case name: *namep = #name;
#define READONLY(name) case name: *namep = #name; goto readonly
#define WRITEOP(name) case name: *namep = #name; goto op_##name;\
                      case name+4: *namep = #name"CLR"; goto op_##name;\
                      case name+8: *namep = #name"SET"; goto op_##name;\
                      case name+12: *namep = #name"INV"; op_##name: \
                      VALUE(name) = write_op (VALUE(name), data, address)
#define WRITEOPX(name,label) \
		      case name: *namep = #name; goto op_##label;\
                      case name+4: *namep = #name"CLR"; goto op_##label;\
                      case name+8: *namep = #name"SET"; goto op_##label;\
                      case name+12: *namep = #name"INV"; goto op_##label
#define WRITEOPR(name,romask) \
                      case name: *namep = #name; goto op_##name;\
                      case name+4: *namep = #name"CLR"; goto op_##name;\
                      case name+8: *namep = #name"SET"; goto op_##name;\
                      case name+12: *namep = #name"INV"; op_##name: \
                      VALUE(name) &= romask; \
                      VALUE(name) |= write_op (VALUE(name), data, address) & ~(romask)

static uint32_t *bootmem;       // image of boot memory

static unsigned syskey_unlock;	// syskey state

/*
 * PIC32MX7 specific table:
 * translate IRQ number to interrupt vector.
 */
static const int irq_to_vector[] = {
    PIC32_VECT_CT,      /* 0  - Core Timer Interrupt */
    PIC32_VECT_CS0,     /* 1  - Core Software Interrupt 0 */
    PIC32_VECT_CS1,     /* 2  - Core Software Interrupt 1 */
    PIC32_VECT_INT0,    /* 3  - External Interrupt 0 */
    PIC32_VECT_T1,      /* 4  - Timer1 */
    PIC32_VECT_IC1,     /* 5  - Input Capture 1 */
    PIC32_VECT_OC1,     /* 6  - Output Compare 1 */
    PIC32_VECT_INT1,    /* 7  - External Interrupt 1 */
    PIC32_VECT_T2,      /* 8  - Timer2 */
    PIC32_VECT_IC2,     /* 9  - Input Capture 2 */
    PIC32_VECT_OC2,     /* 10 - Output Compare 2 */
    PIC32_VECT_INT2,    /* 11 - External Interrupt 2 */
    PIC32_VECT_T3,      /* 12 - Timer3 */
    PIC32_VECT_IC3,     /* 13 - Input Capture 3 */
    PIC32_VECT_OC3,     /* 14 - Output Compare 3 */
    PIC32_VECT_INT3,    /* 15 - External Interrupt 3 */
    PIC32_VECT_T4,      /* 16 - Timer4 */
    PIC32_VECT_IC4,     /* 17 - Input Capture 4 */
    PIC32_VECT_OC4,     /* 18 - Output Compare 4 */
    PIC32_VECT_INT4,    /* 19 - External Interrupt 4 */
    PIC32_VECT_T5,      /* 20 - Timer5 */
    PIC32_VECT_IC5,     /* 21 - Input Capture 5 */
    PIC32_VECT_OC5,     /* 22 - Output Compare 5 */
    PIC32_VECT_SPI1,    /* 23 - SPI1 Fault */
    PIC32_VECT_SPI1,    /* 24 - SPI1 Transfer Done */
    PIC32_VECT_SPI1,    /* 25 - SPI1 Receive Done */

    PIC32_VECT_U1     | /* 26 - UART1 Error */
    PIC32_VECT_SPI3   | /* 26 - SPI3 Fault */
    PIC32_VECT_I2C3,    /* 26 - I2C3 Bus Collision Event */

    PIC32_VECT_U1     | /* 27 - UART1 Receiver */
    PIC32_VECT_SPI3   | /* 27 - SPI3 Transfer Done */
    PIC32_VECT_I2C3,    /* 27 - I2C3 Slave Event */

    PIC32_VECT_U1     | /* 28 - UART1 Transmitter */
    PIC32_VECT_SPI3   | /* 28 - SPI3 Receive Done */
    PIC32_VECT_I2C3,    /* 28 - I2C3 Master Event */

    PIC32_VECT_I2C1,    /* 29 - I2C1 Bus Collision Event */
    PIC32_VECT_I2C1,    /* 30 - I2C1 Slave Event */
    PIC32_VECT_I2C1,    /* 31 - I2C1 Master Event */
    PIC32_VECT_CN,      /* 32 - Input Change Interrupt */
    PIC32_VECT_AD1,     /* 33 - ADC1 Convert Done */
    PIC32_VECT_PMP,     /* 34 - Parallel Master Port */
    PIC32_VECT_CMP1,    /* 35 - Comparator Interrupt */
    PIC32_VECT_CMP2,    /* 36 - Comparator Interrupt */

    PIC32_VECT_U3     | /* 37 - UART3 Error */
    PIC32_VECT_SPI2   | /* 37 - SPI2 Fault */
    PIC32_VECT_I2C4,    /* 37 - I2C4 Bus Collision Event */

    PIC32_VECT_U3     | /* 38 - UART3 Receiver */
    PIC32_VECT_SPI2   | /* 38 - SPI2 Transfer Done */
    PIC32_VECT_I2C4,    /* 38 - I2C4 Slave Event */

    PIC32_VECT_U3     | /* 39 - UART3 Transmitter */
    PIC32_VECT_SPI2   | /* 39 - SPI2 Receive Done */
    PIC32_VECT_I2C4,    /* 39 - I2C4 Master Event */

    PIC32_VECT_U2     | /* 40 - UART2 Error */
    PIC32_VECT_SPI4   | /* 40 - SPI4 Fault */
    PIC32_VECT_I2C5,    /* 40 - I2C5 Bus Collision Event */

    PIC32_VECT_U2     | /* 41 - UART2 Receiver */
    PIC32_VECT_SPI4   | /* 41 - SPI4 Transfer Done */
    PIC32_VECT_I2C5,    /* 41 - I2C5 Slave Event */

    PIC32_VECT_U2     | /* 42 - UART2 Transmitter */
    PIC32_VECT_SPI4   | /* 42 - SPI4 Receive Done */
    PIC32_VECT_I2C5,    /* 42 - I2C5 Master Event */

    PIC32_VECT_I2C2,    /* 43 - I2C2 Bus Collision Event */
    PIC32_VECT_I2C2,    /* 44 - I2C2 Slave Event */
    PIC32_VECT_I2C2,    /* 45 - I2C2 Master Event */
    PIC32_VECT_FSCM,    /* 46 - Fail-Safe Clock Monitor */
    PIC32_VECT_RTCC,    /* 47 - Real-Time Clock and Calendar */
    PIC32_VECT_DMA0,    /* 48 - DMA Channel 0 */
    PIC32_VECT_DMA1,    /* 49 - DMA Channel 1 */
    PIC32_VECT_DMA2,    /* 50 - DMA Channel 2 */
    PIC32_VECT_DMA3,    /* 51 - DMA Channel 3 */
    PIC32_VECT_DMA4,    /* 52 - DMA Channel 4 */
    PIC32_VECT_DMA5,    /* 53 - DMA Channel 5 */
    PIC32_VECT_DMA6,    /* 54 - DMA Channel 6 */
    PIC32_VECT_DMA7,    /* 55 - DMA Channel 7 */
    PIC32_VECT_FCE,     /* 56 - Flash Control Event */
    PIC32_VECT_USB,     /* 57 - USB */
    PIC32_VECT_CAN1,    /* 58 - Control Area Network 1 */
    PIC32_VECT_CAN2,    /* 59 - Control Area Network 2 */
    PIC32_VECT_ETH,     /* 60 - Ethernet Interrupt */
    PIC32_VECT_IC1,     /* 61 - Input Capture 1 Error */
    PIC32_VECT_IC2,     /* 62 - Input Capture 2 Error */
    PIC32_VECT_IC3,     /* 63 - Input Capture 3 Error */
    PIC32_VECT_IC4,     /* 64 - Input Capture 4 Error */
    PIC32_VECT_IC5,     /* 65 - Input Capture 5 Error */
    PIC32_VECT_PMP,     /* 66 - Parallel Master Port Error */
    PIC32_VECT_U4,      /* 67 - UART4 Error */
    PIC32_VECT_U4,      /* 68 - UART4 Receiver */
    PIC32_VECT_U4,      /* 69 - UART4 Transmitter */
    PIC32_VECT_U6,      /* 70 - UART6 Error */
    PIC32_VECT_U6,      /* 71 - UART6 Receiver */
    PIC32_VECT_U6,      /* 72 - UART6 Transmitter */
    PIC32_VECT_U5,      /* 73 - UART5 Error */
    PIC32_VECT_U5,      /* 74 - UART5 Receiver */
    PIC32_VECT_U5,      /* 75 - UART5 Transmitter */
};

static void update_irq_status()
{
    /* Assume no interrupts pending. */
    int cause_ripl = 0;
    int vector = 0;
    VALUE(INTSTAT) = 0;

    if ((VALUE(IFS0) & VALUE(IEC0)) ||
        (VALUE(IFS1) & VALUE(IEC1)) ||
        (VALUE(IFS2) & VALUE(IEC2)))
    {
        /* Find the most prioritive pending interrupt,
         * it's vector and level. */
        int irq;
        for (irq=0; irq<sizeof(irq_to_vector)/sizeof(int); irq++) {
            int n = irq >> 5;

            if (((VALUE(IFS(n)) & VALUE(IEC(n))) >> (irq & 31)) & 1) {
                /* Interrupt is pending. */
                int v = irq_to_vector [irq];
                if (v < 0)
                    continue;

                int level = VALUE(IPC(v >> 2));
                level >>= 2 + (v & 3) * 8;
                level &= 7;
                if (level > cause_ripl) {
                    vector = v;
                    cause_ripl = level;
                }
            }
        }
        VALUE(INTSTAT) = vector | (cause_ripl << 8);
//printf ("-- vector = %d, level = %d\n", vector, level);
    }
//else printf ("-- no irq pending\n");

    eic_level_vector (cause_ripl, vector);
}

/*
 * Set interrupt flag status
 */
void irq_raise (int irq)
{
    if (VALUE(IFS(irq >> 5)) & (1 << (irq & 31)))
        return;
//printf ("-- %s() irq = %d\n", __func__, irq);
    VALUE(IFS(irq >> 5)) |= 1 << (irq & 31);
    update_irq_status();
}

/*
 * Clear interrupt flag status
 */
void irq_clear (int irq)
{
    if (! (VALUE(IFS(irq >> 5)) & (1 << (irq & 31))))
        return;
//printf ("-- %s() irq = %d\n", __func__, irq);
    VALUE(IFS(irq >> 5)) &= ~(1 << (irq & 31));
    update_irq_status();
}

static void gpio_write (int gpio_port, unsigned lat_value)
{
    /* Control SD card 0 */
    if (gpio_port == sdcard_gpio_port0 && sdcard_gpio_cs0) {
        sdcard_select (0, ! (lat_value & sdcard_gpio_cs0));
    }
    /* Control SD card 1 */
    if (gpio_port == sdcard_gpio_port1 && sdcard_gpio_cs1) {
        sdcard_select (1, ! (lat_value & sdcard_gpio_cs1));
    }
}

/*
 * Perform an assign/clear/set/invert operation.
 */
static inline unsigned write_op (a, b, op)
{
    switch (op & 0xc) {
    case 0x0: a = b;   break;   // Assign
    case 0x4: a &= ~b; break;   // Clear
    case 0x8: a |= b;  break;   // Set
    case 0xc: a ^= b;  break;   // Invert
    }
    return a;
}

unsigned io_read32 (unsigned address, unsigned *bufp, const char **namep)
{
    switch (address) {
    /*-------------------------------------------------------------------------
     * Bus matrix control registers.
     */
    STORAGE (BMXCON); break;    // Bus Mmatrix Control
    STORAGE (BMXDKPBA); break;  // Data RAM kernel program base address
    STORAGE (BMXDUDBA); break;  // Data RAM user data base address
    STORAGE (BMXDUPBA); break;  // Data RAM user program base address
    STORAGE (BMXPUPBA); break;  // Program Flash user program base address
    STORAGE (BMXDRMSZ); break;  // Data RAM memory size
    STORAGE (BMXPFMSZ); break;  // Program Flash memory size
    STORAGE (BMXBOOTSZ); break; // Boot Flash size

    /*-------------------------------------------------------------------------
     * Interrupt controller registers.
     */
    STORAGE (INTCON); break;	// Interrupt Control
    STORAGE (INTSTAT); break;   // Interrupt Status
    STORAGE (IFS0); break;	// IFS(0..2) - Interrupt Flag Status
    STORAGE (IFS1); break;
    STORAGE (IFS2); break;
    STORAGE (IEC0); break;	// IEC(0..2) - Interrupt Enable Control
    STORAGE (IEC1); break;
    STORAGE (IEC2); break;
    STORAGE (IPC0); break;	// IPC(0..11) - Interrupt Priority Control
    STORAGE (IPC1); break;
    STORAGE (IPC2); break;
    STORAGE (IPC3); break;
    STORAGE (IPC4); break;
    STORAGE (IPC5); break;
    STORAGE (IPC6); break;
    STORAGE (IPC7); break;
    STORAGE (IPC8); break;
    STORAGE (IPC9); break;
    STORAGE (IPC10); break;
    STORAGE (IPC11); break;
    STORAGE (IPC12); break;

    /*-------------------------------------------------------------------------
     * Prefetch controller.
     */
    STORAGE (CHECON); break;	// Prefetch Control

    /*-------------------------------------------------------------------------
     * System controller.
     */
    STORAGE (OSCCON); break;	// Oscillator Control
    STORAGE (OSCTUN); break;	// Oscillator Tuning
    STORAGE (DDPCON); break;	// Debug Data Port Control
    STORAGE (DEVID); break;	// Device Identifier
    STORAGE (SYSKEY); break;	// System Key
    STORAGE (RCON); break;	// Reset Control
    STORAGE (RSWRST); break;	// Software Reset

    /*-------------------------------------------------------------------------
     * Analog to digital converter.
     */
    STORAGE (AD1CON1); break;	// Control register 1
    STORAGE (AD1CON2); break;	// Control register 2
    STORAGE (AD1CON3); break;	// Control register 3
    STORAGE (AD1CHS); break;    // Channel select
    STORAGE (AD1CSSL); break;   // Input scan selection
    STORAGE (AD1PCFG); break;   // Port configuration
    STORAGE (ADC1BUF0); break;  // Result words
    STORAGE (ADC1BUF1); break;
    STORAGE (ADC1BUF2); break;
    STORAGE (ADC1BUF3); break;
    STORAGE (ADC1BUF4); break;
    STORAGE (ADC1BUF5); break;
    STORAGE (ADC1BUF6); break;
    STORAGE (ADC1BUF7); break;
    STORAGE (ADC1BUF8); break;
    STORAGE (ADC1BUF9); break;
    STORAGE (ADC1BUFA); break;
    STORAGE (ADC1BUFB); break;
    STORAGE (ADC1BUFC); break;
    STORAGE (ADC1BUFD); break;
    STORAGE (ADC1BUFE); break;
    STORAGE (ADC1BUFF); break;

    /*-------------------------------------------------------------------------
     * General purpose IO signals.
     */
    STORAGE (TRISA); break;     // Port A: mask of inputs
    STORAGE (PORTA); break;     // Port A: read inputs
    STORAGE (LATA); break;      // Port A: read outputs
    STORAGE (ODCA); break;      // Port A: open drain configuration
    STORAGE (TRISB); break;     // Port B: mask of inputs
    STORAGE (PORTB); break;     // Port B: read inputs
    STORAGE (LATB); break;      // Port B: read outputs
    STORAGE (ODCB); break;      // Port B: open drain configuration
    STORAGE (TRISC); break;     // Port C: mask of inputs
    STORAGE (PORTC); break;     // Port C: read inputs
    STORAGE (LATC); break;      // Port C: read outputs
    STORAGE (ODCC); break;      // Port C: open drain configuration
    STORAGE (TRISD); break;     // Port D: mask of inputs
    STORAGE (PORTD); break;	// Port D: read inputs
    STORAGE (LATD); break;      // Port D: read outputs
    STORAGE (ODCD); break;      // Port D: open drain configuration
    STORAGE (TRISE); break;     // Port E: mask of inputs
    STORAGE (PORTE); break;	// Port E: read inputs
    STORAGE (LATE); break;      // Port E: read outputs
    STORAGE (ODCE); break;      // Port E: open drain configuration
    STORAGE (TRISF); break;     // Port F: mask of inputs
    STORAGE (PORTF); break;     // Port F: read inputs
    STORAGE (LATF); break;      // Port F: read outputs
    STORAGE (ODCF); break;      // Port F: open drain configuration
    STORAGE (TRISG); break;     // Port G: mask of inputs
    STORAGE (PORTG); break;     // Port G: read inputs
    STORAGE (LATG); break;      // Port G: read outputs
    STORAGE (ODCG); break;      // Port G: open drain configuration
    STORAGE (CNCON); break;     // Interrupt-on-change control
    STORAGE (CNEN); break;      // Input change interrupt enable
    STORAGE (CNPUE); break;     // Input pin pull-up enable

    /*-------------------------------------------------------------------------
     * UART 1.
     */
    STORAGE (U1RXREG);                          // Receive data
        *bufp = uart_get_char(0);
        break;
    STORAGE (U1BRG); break;                     // Baud rate
    STORAGE (U1MODE); break;                    // Mode
    STORAGE (U1STA);                            // Status and control
        uart_poll_status(0);
        break;
    STORAGE (U1TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U1MODECLR); *bufp = 0; break;
    STORAGE (U1MODESET); *bufp = 0; break;
    STORAGE (U1MODEINV); *bufp = 0; break;
    STORAGE (U1STACLR);  *bufp = 0; break;
    STORAGE (U1STASET);  *bufp = 0; break;
    STORAGE (U1STAINV);  *bufp = 0; break;
    STORAGE (U1BRGCLR);  *bufp = 0; break;
    STORAGE (U1BRGSET);  *bufp = 0; break;
    STORAGE (U1BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 2.
     */
    STORAGE (U2RXREG);                          // Receive data
        *bufp = uart_get_char(1);
        break;
    STORAGE (U2BRG); break;                     // Baud rate
    STORAGE (U2MODE); break;                    // Mode
    STORAGE (U2STA);                            // Status and control
        uart_poll_status(1);
        break;
    STORAGE (U2TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U2MODECLR); *bufp = 0; break;
    STORAGE (U2MODESET); *bufp = 0; break;
    STORAGE (U2MODEINV); *bufp = 0; break;
    STORAGE (U2STACLR);  *bufp = 0; break;
    STORAGE (U2STASET);  *bufp = 0; break;
    STORAGE (U2STAINV);  *bufp = 0; break;
    STORAGE (U2BRGCLR);  *bufp = 0; break;
    STORAGE (U2BRGSET);  *bufp = 0; break;
    STORAGE (U2BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 3.
     */
    STORAGE (U3RXREG);                          // Receive data
        *bufp = uart_get_char(2);
        break;
    STORAGE (U3BRG); break;                     // Baud rate
    STORAGE (U3MODE); break;                    // Mode
    STORAGE (U3STA);                            // Status and control
        uart_poll_status(2);
        break;
    STORAGE (U3TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U3MODECLR); *bufp = 0; break;
    STORAGE (U3MODESET); *bufp = 0; break;
    STORAGE (U3MODEINV); *bufp = 0; break;
    STORAGE (U3STACLR);  *bufp = 0; break;
    STORAGE (U3STASET);  *bufp = 0; break;
    STORAGE (U3STAINV);  *bufp = 0; break;
    STORAGE (U3BRGCLR);  *bufp = 0; break;
    STORAGE (U3BRGSET);  *bufp = 0; break;
    STORAGE (U3BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 4.
     */
    STORAGE (U4RXREG);                          // Receive data
        *bufp = uart_get_char(3);
        break;
    STORAGE (U4BRG); break;                     // Baud rate
    STORAGE (U4MODE); break;                    // Mode
    STORAGE (U4STA);                            // Status and control
        uart_poll_status(3);
        break;
    STORAGE (U4TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U4MODECLR); *bufp = 0; break;
    STORAGE (U4MODESET); *bufp = 0; break;
    STORAGE (U4MODEINV); *bufp = 0; break;
    STORAGE (U4STACLR);  *bufp = 0; break;
    STORAGE (U4STASET);  *bufp = 0; break;
    STORAGE (U4STAINV);  *bufp = 0; break;
    STORAGE (U4BRGCLR);  *bufp = 0; break;
    STORAGE (U4BRGSET);  *bufp = 0; break;
    STORAGE (U4BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 5.
     */
    STORAGE (U5RXREG);                          // Receive data
        *bufp = uart_get_char(4);
        break;
    STORAGE (U5BRG); break;                     // Baud rate
    STORAGE (U5MODE); break;                    // Mode
    STORAGE (U5STA);                            // Status and control
        uart_poll_status(4);
        break;
    STORAGE (U5TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U5MODECLR); *bufp = 0; break;
    STORAGE (U5MODESET); *bufp = 0; break;
    STORAGE (U5MODEINV); *bufp = 0; break;
    STORAGE (U5STACLR);  *bufp = 0; break;
    STORAGE (U5STASET);  *bufp = 0; break;
    STORAGE (U5STAINV);  *bufp = 0; break;
    STORAGE (U5BRGCLR);  *bufp = 0; break;
    STORAGE (U5BRGSET);  *bufp = 0; break;
    STORAGE (U5BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 6.
     */
    STORAGE (U6RXREG);                          // Receive data
        *bufp = uart_get_char(5);
        break;
    STORAGE (U6BRG); break;                     // Baud rate
    STORAGE (U6MODE); break;                    // Mode
    STORAGE (U6STA);                            // Status and control
        uart_poll_status(5);
        break;
    STORAGE (U6TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U6MODECLR); *bufp = 0; break;
    STORAGE (U6MODESET); *bufp = 0; break;
    STORAGE (U6MODEINV); *bufp = 0; break;
    STORAGE (U6STACLR);  *bufp = 0; break;
    STORAGE (U6STASET);  *bufp = 0; break;
    STORAGE (U6STAINV);  *bufp = 0; break;
    STORAGE (U6BRGCLR);  *bufp = 0; break;
    STORAGE (U6BRGSET);  *bufp = 0; break;
    STORAGE (U6BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 1.
     */
    STORAGE (SPI1CON); break;                   // Control
    STORAGE (SPI1CONCLR); *bufp = 0; break;
    STORAGE (SPI1CONSET); *bufp = 0; break;
    STORAGE (SPI1CONINV); *bufp = 0; break;
    STORAGE (SPI1STAT); break;                  // Status
    STORAGE (SPI1STATCLR); *bufp = 0; break;
    STORAGE (SPI1STATSET); *bufp = 0; break;
    STORAGE (SPI1STATINV); *bufp = 0; break;
    STORAGE (SPI1BUF);                          // Buffer
        *bufp = spi_readbuf (0);
        break;
    STORAGE (SPI1BRG); break;                   // Baud rate
    STORAGE (SPI1BRGCLR); *bufp = 0; break;
    STORAGE (SPI1BRGSET); *bufp = 0; break;
    STORAGE (SPI1BRGINV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 2.
     */
    STORAGE (SPI2CON); break;                   // Control
    STORAGE (SPI2CONCLR); *bufp = 0; break;
    STORAGE (SPI2CONSET); *bufp = 0; break;
    STORAGE (SPI2CONINV); *bufp = 0; break;
    STORAGE (SPI2STAT); break;                  // Status
    STORAGE (SPI2STATCLR); *bufp = 0; break;
    STORAGE (SPI2STATSET); *bufp = 0; break;
    STORAGE (SPI2STATINV); *bufp = 0; break;
    STORAGE (SPI2BUF);                          // Buffer
        *bufp = spi_readbuf (1);
        break;
    STORAGE (SPI2BRG); break;                   // Baud rate
    STORAGE (SPI2BRGCLR); *bufp = 0; break;
    STORAGE (SPI2BRGSET); *bufp = 0; break;
    STORAGE (SPI2BRGINV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 3.
     */
    STORAGE (SPI3CON); break;                   // Control
    STORAGE (SPI3CONCLR); *bufp = 0; break;
    STORAGE (SPI3CONSET); *bufp = 0; break;
    STORAGE (SPI3CONINV); *bufp = 0; break;
    STORAGE (SPI3STAT); break;                  // Status
    STORAGE (SPI3STATCLR); *bufp = 0; break;
    STORAGE (SPI3STATSET); *bufp = 0; break;
    STORAGE (SPI3STATINV); *bufp = 0; break;
    STORAGE (SPI3BUF);                          // SPIx Buffer
        *bufp = spi_readbuf (2);
        break;
    STORAGE (SPI3BRG); break;                   // Baud rate
    STORAGE (SPI3BRGCLR); *bufp = 0; break;
    STORAGE (SPI3BRGSET); *bufp = 0; break;
    STORAGE (SPI3BRGINV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 4.
     */
    STORAGE (SPI4CON); break;                   // Control
    STORAGE (SPI4CONCLR); *bufp = 0; break;
    STORAGE (SPI4CONSET); *bufp = 0; break;
    STORAGE (SPI4CONINV); *bufp = 0; break;
    STORAGE (SPI4STAT); break;                  // Status
    STORAGE (SPI4STATCLR); *bufp = 0; break;
    STORAGE (SPI4STATSET); *bufp = 0; break;
    STORAGE (SPI4STATINV); *bufp = 0; break;
    STORAGE (SPI4BUF);                          // Buffer
        *bufp = spi_readbuf (3);
        break;
    STORAGE (SPI4BRG); break;                   // Baud rate
    STORAGE (SPI4BRGCLR); *bufp = 0; break;
    STORAGE (SPI4BRGSET); *bufp = 0; break;
    STORAGE (SPI4BRGINV); *bufp = 0; break;

    default:
        fprintf (stderr, "--- Read %08x: peripheral register not supported\n",
            address);
        exit (1);
    }
    return *bufp;
}

void io_write32 (unsigned address, unsigned *bufp, unsigned data, const char **namep)
{
    switch (address) {
    /*-------------------------------------------------------------------------
     * Bus matrix control registers.
     */
    WRITEOP (BMXCON); return;   // Bus Matrix Control
    STORAGE (BMXDKPBA); break;  // Data RAM kernel program base address
    STORAGE (BMXDUDBA); break;  // Data RAM user data base address
    STORAGE (BMXDUPBA); break;  // Data RAM user program base address
    STORAGE (BMXPUPBA); break;  // Program Flash user program base address
    READONLY(BMXDRMSZ);         // Data RAM memory size
    READONLY(BMXPFMSZ);         // Program Flash memory size
    READONLY(BMXBOOTSZ);        // Boot Flash size

    /*-------------------------------------------------------------------------
     * Interrupt controller registers.
     */
    WRITEOP (INTCON); return;   // Interrupt Control
    READONLY(INTSTAT);          // Interrupt Status
    WRITEOP (IPTMR);  return;   // Temporal Proximity Timer
    WRITEOP (IFS0); goto irq;	// IFS(0..2) - Interrupt Flag Status
    WRITEOP (IFS1); goto irq;
    WRITEOP (IFS2); goto irq;
    WRITEOP (IEC0); goto irq;	// IEC(0..2) - Interrupt Enable Control
    WRITEOP (IEC1); goto irq;
    WRITEOP (IEC2); goto irq;
    WRITEOP (IPC0); goto irq;	// IPC(0..11) - Interrupt Priority Control
    WRITEOP (IPC1); goto irq;
    WRITEOP (IPC2); goto irq;
    WRITEOP (IPC3); goto irq;
    WRITEOP (IPC4); goto irq;
    WRITEOP (IPC5); goto irq;
    WRITEOP (IPC6); goto irq;
    WRITEOP (IPC7); goto irq;
    WRITEOP (IPC8); goto irq;
    WRITEOP (IPC9); goto irq;
    WRITEOP (IPC10); goto irq;
    WRITEOP (IPC11); goto irq;
    WRITEOP (IPC12);
irq:    update_irq_status();
        return;

    /*-------------------------------------------------------------------------
     * Prefetch controller.
     */
    WRITEOP (CHECON); return;   // Prefetch Control

    /*-------------------------------------------------------------------------
     * System controller.
     */
    STORAGE (OSCCON); break;	// Oscillator Control
    STORAGE (OSCTUN); break;	// Oscillator Tuning
    STORAGE (DDPCON); break;	// Debug Data Port Control
    READONLY(DEVID);		// Device Identifier
    STORAGE (SYSKEY);		// System Key
	/* Unlock state machine. */
	if (syskey_unlock == 0 && VALUE(SYSKEY) == 0xaa996655)
	    syskey_unlock = 1;
	if (syskey_unlock == 1 && VALUE(SYSKEY) == 0x556699aa)
	    syskey_unlock = 2;
	else
	    syskey_unlock = 0;
	break;
    STORAGE (RCON); break;	// Reset Control
    STORAGE (RSWRST);		// Software Reset
	if (syskey_unlock == 2 && (VALUE(RSWRST) & 1)) {
            /* Reset CPU. */
            soft_reset();

            /* Reset all devices */
            io_reset();
            sdcard_reset();
        }
	break;

    /*-------------------------------------------------------------------------
     * Analog to digital converter.
     */
    WRITEOP (AD1CON1); return;	// Control register 1
    WRITEOP (AD1CON2); return;	// Control register 2
    WRITEOP (AD1CON3); return;	// Control register 3
    WRITEOP (AD1CHS); return;   // Channel select
    WRITEOP (AD1CSSL); return;  // Input scan selection
    WRITEOP (AD1PCFG); return;  // Port configuration
    READONLY(ADC1BUF0);         // Result words
    READONLY(ADC1BUF1);
    READONLY(ADC1BUF2);
    READONLY(ADC1BUF3);
    READONLY(ADC1BUF4);
    READONLY(ADC1BUF5);
    READONLY(ADC1BUF6);
    READONLY(ADC1BUF7);
    READONLY(ADC1BUF8);
    READONLY(ADC1BUF9);
    READONLY(ADC1BUFA);
    READONLY(ADC1BUFB);
    READONLY(ADC1BUFC);
    READONLY(ADC1BUFD);
    READONLY(ADC1BUFE);
    READONLY(ADC1BUFF);

    /*-------------------------------------------------------------------------
     * General purpose IO signals.
     */
    WRITEOP (TRISA); return;	    // Port A: mask of inputs
    WRITEOPX(PORTA, LATA);          // Port A: write outputs
    WRITEOP (LATA);                 // Port A: write outputs
        gpio_write (0, VALUE(LATA));
	return;
    WRITEOP (ODCA); return;	    // Port A: open drain configuration
    WRITEOP (TRISB); return;	    // Port B: mask of inputs
    WRITEOPX(PORTB, LATB);          // Port B: write outputs
    WRITEOP (LATB);		    // Port B: write outputs
        gpio_write (1, VALUE(LATB));
	return;
    WRITEOP (ODCB); return;	    // Port B: open drain configuration
    WRITEOP (TRISC); return;	    // Port C: mask of inputs
    WRITEOPX(PORTC, LATC);          // Port C: write outputs
    WRITEOP (LATC);                 // Port C: write outputs
        gpio_write (2, VALUE(LATC));
	return;
    WRITEOP (ODCC); return;	    // Port C: open drain configuration
    WRITEOP (TRISD); return;	    // Port D: mask of inputs
    WRITEOPX(PORTD, LATD);          // Port D: write outputs
    WRITEOP (LATD);		    // Port D: write outputs
        gpio_write (3, VALUE(LATD));
	return;
    WRITEOP (ODCD); return;	    // Port D: open drain configuration
    WRITEOP (TRISE); return;	    // Port E: mask of inputs
    WRITEOPX(PORTE, LATE);          // Port E: write outputs
    WRITEOP (LATE);		    // Port E: write outputs
        gpio_write (4, VALUE(LATE));
	return;
    WRITEOP (ODCE); return;	    // Port E: open drain configuration
    WRITEOP (TRISF); return;	    // Port F: mask of inputs
    WRITEOPX(PORTF, LATF);          // Port F: write outputs
    WRITEOP (LATF);		    // Port F: write outputs
        gpio_write (5, VALUE(LATF));
	return;
    WRITEOP (ODCF); return;	    // Port F: open drain configuration
    WRITEOP (TRISG); return;	    // Port G: mask of inputs
    WRITEOPX(PORTG, LATG);          // Port G: write outputs
    WRITEOP (LATG);		    // Port G: write outputs
        gpio_write (6, VALUE(LATG));
	return;
    WRITEOP (ODCG); return;	    // Port G: open drain configuration
    WRITEOP (CNCON); return;	    // Interrupt-on-change control
    WRITEOP (CNEN); return;	    // Input change interrupt enable
    WRITEOP (CNPUE); return;	    // Input pin pull-up enable

    /*-------------------------------------------------------------------------
     * UART 1.
     */
    STORAGE (U1TXREG);                              // Transmit
        uart_put_char (0, data);
        break;
    WRITEOP (U1MODE);                               // Mode
        uart_update_mode (0);
        return;
    WRITEOPR (U1STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (0);
        return;
    WRITEOP (U1BRG); return;                        // Baud rate
    READONLY (U1RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 2.
     */
    STORAGE (U2TXREG);                              // Transmit
        uart_put_char (1, data);
        break;
    WRITEOP (U2MODE);                               // Mode
        uart_update_mode (1);
        return;
    WRITEOPR (U2STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (1);
        return;
    WRITEOP (U2BRG); return;                        // Baud rate
    READONLY (U2RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 3.
     */
    STORAGE (U3TXREG);                              // Transmit
        uart_put_char (2, data);
        break;
    WRITEOP (U3MODE);                               // Mode
        uart_update_mode (2);
        return;
    WRITEOPR (U3STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (2);
        return;
    WRITEOP (U3BRG); return;                        // Baud rate
    READONLY (U3RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 4.
     */
    STORAGE (U4TXREG);                              // Transmit
        uart_put_char (3, data);
        break;
    WRITEOP (U4MODE);                               // Mode
        uart_update_mode (3);
        return;
    WRITEOPR (U4STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (3);
        return;
    WRITEOP (U4BRG); return;                        // Baud rate
    READONLY (U4RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 5.
     */
    STORAGE (U5TXREG);                              // Transmit
        uart_put_char (4, data);
        break;
    WRITEOP (U5MODE);                               // Mode
        uart_update_mode (4);
        return;
    WRITEOPR (U5STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (4);
        return;
    WRITEOP (U5BRG); return;                        // Baud rate
    READONLY (U5RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 6.
     */
    STORAGE (U6TXREG);                              // Transmit
        uart_put_char (5, data);
        break;
    WRITEOP (U6MODE);                               // Mode
        uart_update_mode (5);
        return;
    WRITEOPR (U6STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (5);
        return;
    WRITEOP (U6BRG); return;                        // Baud rate
    READONLY (U6RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * SPI.
     */
    WRITEOP (SPI1CON);                              // Control
	spi_control (0);
        return;
    WRITEOPR (SPI1STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI1BUF);                              // Buffer
        spi_writebuf (0, data);
        return;
    WRITEOP (SPI1BRG); return;                      // Baud rate
    WRITEOP (SPI2CON);                              // Control
	spi_control (1);
        return;
    WRITEOPR (SPI2STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI2BUF);                              // Buffer
        spi_writebuf (1, data);
        return;
    WRITEOP (SPI2BRG); return;                      // Baud rate
    WRITEOP (SPI3CON);                              // Control
	spi_control (2);
        return;
    WRITEOPR (SPI3STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI3BUF);                              // Buffer
        spi_writebuf (2, data);
        return;
    WRITEOP (SPI3BRG); return;                      // Baud rate
    WRITEOP (SPI4CON);                              // Control
	spi_control (3);
        return;
    WRITEOPR (SPI4STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI4BUF);                              // Buffer
        spi_writebuf (3, data);
        return;
    WRITEOP (SPI4BRG); return;      // Baud rate

    default:
        fprintf (stderr, "--- Write %08x to %08x: peripheral register not supported\n",
            data, address);
        exit (1);
readonly:
        fprintf (stderr, "--- Write %08x to %s: readonly register\n",
            data, *namep);
        *namep = 0;
        return;
    }
    *bufp = data;
}

void io_reset()
{
    /*
     * Bus matrix control registers.
     */
    VALUE(BMXCON)    = 0x001f0041;      // Bus Matrix Control
    VALUE(BMXDKPBA)  = 0;               // Data RAM kernel program base address
    VALUE(BMXDUDBA)  = 0;               // Data RAM user data base address
    VALUE(BMXDUPBA)  = 0;               // Data RAM user program base address
    VALUE(BMXPUPBA)  = 0;               // Program Flash user program base address
    VALUE(BMXDRMSZ)  = 128 * 1024;      // Data RAM memory size
    VALUE(BMXPFMSZ)  = 512 * 1024;      // Program Flash memory size
    VALUE(BMXBOOTSZ) = 12 * 1024;       // Boot Flash size

    /*
     * Prefetch controller.
     */
    VALUE(CHECON) = 0x00000007;

    /*
     * System controller.
     */
    VALUE(OSCCON) = 0x01453320;         // from ubw32 board
    VALUE(OSCTUN) = 0;
    VALUE(DDPCON) = 0;
    VALUE(DEVID)  = 0x04307053;         // 795F512L
    VALUE(SYSKEY) = 0;
    VALUE(RCON)   = 0;
    VALUE(RSWRST) = 0;
    syskey_unlock  = 0;

    /*
     * Analog to digital converter.
     */
    VALUE(AD1CON1) = 0;                 // Control register 1
    VALUE(AD1CON2) = 0;                 // Control register 2
    VALUE(AD1CON3) = 0;                 // Control register 3
    VALUE(AD1CHS)  = 0;                 // Channel select
    VALUE(AD1CSSL) = 0;                 // Input scan selection
    VALUE(AD1PCFG) = 0;                 // Port configuration

    /*
     * General purpose IO signals.
     * All pins are inputs, high, open drains and pullups disabled.
     * No interrupts on change.
     */
    VALUE(TRISA) = 0xFFFF;		// Port A: mask of inputs
    VALUE(PORTA) = 0xFFFF;		// Port A: read inputs, write outputs
    VALUE(LATA)  = 0xFFFF;		// Port A: read/write outputs
    VALUE(ODCA)  = 0;			// Port A: open drain configuration
    VALUE(TRISB) = 0xFFFF;		// Port B: mask of inputs
    VALUE(PORTB) = 0xFFFF;		// Port B: read inputs, write outputs
    VALUE(LATB)  = 0xFFFF;		// Port B: read/write outputs
    VALUE(ODCB)  = 0;			// Port B: open drain configuration
    VALUE(TRISC) = 0xFFFF;		// Port C: mask of inputs
    VALUE(PORTC) = 0xFFFF;		// Port C: read inputs, write outputs
    VALUE(LATC)  = 0xFFFF;		// Port C: read/write outputs
    VALUE(ODCC)  = 0;			// Port C: open drain configuration
    VALUE(TRISD) = 0xFFFF;		// Port D: mask of inputs
    VALUE(PORTD) = 0xFFFF;		// Port D: read inputs, write outputs
    VALUE(LATD)  = 0xFFFF;		// Port D: read/write outputs
    VALUE(ODCD)  = 0;			// Port D: open drain configuration
    VALUE(TRISE) = 0xFFFF;		// Port E: mask of inputs
    VALUE(PORTE) = 0xFFFF;		// Port D: read inputs, write outputs
    VALUE(LATE)  = 0xFFFF;		// Port E: read/write outputs
    VALUE(ODCE)  = 0;			// Port E: open drain configuration
    VALUE(TRISF) = 0xFFFF;		// Port F: mask of inputs
    VALUE(PORTF) = 0xFFFF;		// Port F: read inputs, write outputs
    VALUE(LATF)  = 0xFFFF;		// Port F: read/write outputs
    VALUE(ODCF)  = 0;			// Port F: open drain configuration
    VALUE(TRISG) = 0xFFFF;		// Port G: mask of inputs
    VALUE(PORTG) = 0xFFFF;		// Port G: read inputs, write outputs
    VALUE(LATG)  = 0xFFFF;		// Port G: read/write outputs
    VALUE(ODCG)  = 0;			// Port G: open drain configuration
    VALUE(CNCON) = 0;			// Interrupt-on-change control
    VALUE(CNEN)  = 0;			// Input change interrupt enable
    VALUE(CNPUE) = 0;			// Input pin pull-up enable

    uart_reset();
    spi_reset();
}

void io_init (void *bootp)
{
    bootmem = bootp;

    // Preset DEVCFG data, from Max32 bootloader.
    BOOTMEM(DEVCFG3) = 0xffff0722;
    BOOTMEM(DEVCFG2) = 0xd979f8f9;
    BOOTMEM(DEVCFG1) = 0x5bfd6aff;
    BOOTMEM(DEVCFG0) = 0xffffff7f;

    io_reset();
    sdcard_reset();
}
