/*
 * arch/arm/plat-omap/include/mach/uncompress.h
 *
 * Serial port stubs for kernel decompress status messages
 *
 * Initially based on:
 * linux-2.4.15-rmk1-dsplinux1.6/arch/arm/plat-omap/include/mach1510/uncompress.h
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Rewritten by:
 * Author: <source@mvista.com>
 * 2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/serial_reg.h>

#include <asm/memory.h>
#include <asm/mach-types.h>

//#include <mach/serial.h>

#define OMAP_UART_INFO_OFS 0x3ffc

#define MDR1_MODE_MASK			0x07

volatile u8 *uart_base;
int uart_shift;

/*
 * Store the DEBUG_LL uart number into memory.
 * See also debug-macro.S, and serial.c for related code.
 */
static void set_omap_uart_info(unsigned char port)
{
	/*
	 * Get address of some.bss variable and round it down
	 * a la CONFIG_AUTO_ZRELADDR.
	 */
	u32 ram_start = (u32)&uart_shift & 0xf8000000;
	u32 *uart_info = (u32 *)(ram_start + OMAP_UART_INFO_OFS);
	*uart_info = port;
}

static inline void putc(int c)
{
	if (!uart_base)
		return;

	/* Check for UART 16x mode */
	if ((uart_base[UART_OMAP_MDR1 << uart_shift] & MDR1_MODE_MASK) != 0)
		return;

	while (!(uart_base[UART_LSR << uart_shift] & UART_LSR_THRE))
		barrier();
	uart_base[UART_TX << uart_shift] = c;
}

static inline void flush(void)
{
}

/*
 * Macros to configure UART1 and debug UART
 */
#define _DEBUG_LL_ENTRY(mach, dbg_uart, dbg_shft, dbg_id)		\
	if (1) {					\
		uart_base = (volatile u8 *)(dbg_uart);			\
		uart_shift = (dbg_shft);				\
		port = (dbg_id);					\
		set_omap_uart_info(port);				\
		break;							\
	}

#define DEBUG_LL_OMAP7XX(p, mach)					\
	_DEBUG_LL_ENTRY(mach, OMAP1_UART##p##_BASE, OMAP7XX_PORT_SHIFT,	\
		OMAP1UART##p)

#define DEBUG_LL_OMAP1(p, mach)						\
	_DEBUG_LL_ENTRY(mach, OMAP1_UART##p##_BASE, OMAP_PORT_SHIFT,	\
		OMAP1UART##p)

#define DEBUG_LL_OMAP3(p, mach)                                         \
	_DEBUG_LL_ENTRY(mach, OMAP3_UART##p##_BASE, OMAP_PORT_SHIFT,    \
			OMAP3UART##p)

#define OMAP3_UART3_BASE 0x49020000
#define OMAP_PORT_SHIFT 2
#define OMAP3UART3 33

static inline void arch_decomp_setup(void)
{
	int port = 0;

	/*
	 * Initialize the port based on the machine ID from the bootloader.
	 * Note that we're using macros here instead of switch statement
	 * as machine_is functions are optimized out for the boards that
	 * are not selected.
	 */
	do {
		DEBUG_LL_OMAP3(3, nokia_rm680);
	} while (0);
}
