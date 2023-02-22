#include "mode7_demo.h"

// Our video output programs for the PIO
#include "mode7.pio.h"

/* ------------------------------------------------------------------------
 PAL info:

 Lines are always 64us, there are no half-lines: VSYNC may come in
 the middle of a line, but after VSYNC the lines of the new field
 are still multiples of 64us after the lines of the previous field.
 625 lines total * 64us = 40000us = 40ms = 25Hz.

 HSYNC lasts 4.7us, equalising pulses are 2.35us
 Front porch (end of active video to start of HSYNC next line): 1.65us
 Back porch (end of HSYNC to start of active video): 5.7us
 (so leading edge of HSYNC to active video is 10.4 inc HSYNC)
 BBC has 4.1us extra delay to first pixel, so 14.5us HSYNC edge -> 1st pixel

 Equalising pulses occur twice per line, one aligned with where HSYNC
 would have been, one in the middle of the line.
 There are always 5 equalising pulses before VSYNC - at the end of the
 first field the first equalising pulse replaces the HSYNC of line 311,
 at the end of the 2nd field the first eq pulse is in the middle of line 623.
 VSYNC always lasts 2.5 lines (160us) and has wide eq pulses (positive-going)
 during it.  Falling edge of VSYNC goes where the falling edge of an
 eq pulse would have been.  On first field (line 1), the falling edge of
 VSYNC is aligned with the HSYNC pulses of active lines; on the 2nd field
 it's half way through line 313

 However, Electron doesn't do any of this stuff with EQ pulses, and just
 has one long pulse for VSYNC (160us).

 Based on scope shots in:
 https://stardot.org.uk/forums/viewtopic.php?p=117879#p117879
 And also:
 https://www.mups.co.uk/post/2018/01/electron-pal-video-revisited/

 For the odd field, there's a very small gap (~12us) from the HSYNC
 of the last line of the previous field to VSYNC (so about 16.5us
 from HSYNC falling to VSYNC falling), then about 18us from VSYNC
 rising to the next HSYNC (so VSYNC overlays two HSYNCs and most
 of the lines either side).

 For the even field, VSYNC overlays three HSYNCs but has bigger gaps
 either side: about 43us from rising HSYNC to falling VSYNC,
 (so 47.5us from falling HSYNC to falling VSYNC)
 about 49us from rising VSYNC to next HSYNC.

 If VSYNC is precisely 2.5 lines, then the pairs of partial lines ought
 to add up to precisely half a line or 1.5 lines respectively;
 those numbers don't quite, so either Electron VSYNC slightly short
 or (more likely) the measurements are inaccurate.

 Since the VSYNCs should occur at precisely 50Hz, the before/after
 delays for the two VSYNCs should differ by half a line (32us).

 So we use 15us/17us for the odd, 47us/49us for the even,
 referenced in each case to a falling HSYNC

*/


#define	LINE_T	(SYSCLK_MHZ * 64)			// 64us
#define	HSYNC_T	((SYSCLK_MHZ *47) / 10)		// 4.7us
#define	VSYNC_T	(SYSCLK_MHZ * 160)			// 160us = 2.5 lines


// Wrapper function for writing the values to the FIFO:
// combines the low and high times and subtracts 2 from each to
// fit what the PIO program actually does
static __force_inline void write_value(unsigned low, unsigned high)
{
	PIO pio = VIDEO_PIO;
	pio->txf[VIDEO_SYNCGEN_SM] = (low -2) | ((high -2) << 16);
}


// Assumes this is the only interrupt on the video PIO
// (other functions use DMA or polling)
static void __not_in_flash_func(pio_irq0_handler)(void)
{
	// Note that this line_no is counting our pulses (where one VSYNC pulse
	// straddles multiple lines), so it doesn't quite count up to 625.
	static unsigned line_no = 0;
	switch (line_no++)
	{
		case 0:
			// This is the VSYNC of the first field with short gap after it
			// to the first HSYNC (17us) so total 113us
			// Added to the 15us for the trailing line of the last frame,
			// that gives 128 total (2 lines).
			write_value(VSYNC_T, (SYSCLK_MHZ * 17));
			break;
		// 310 ordinary lines in between, total 19840us
		case 311:
			// This is the last HSYNC before the 2nd field VSYNC
			// with a large gap (total 47us)
			write_value(HSYNC_T, (SYSCLK_MHZ * 47) - HSYNC_T);
			break;
		case 312:
			// This is the VSYNC at the top of the 2nd field
			// and the 49us gap to the next HSYNC (total 209)
			// Combined with the 47us just before totals 256us (4 lines)
			write_value(VSYNC_T, (SYSCLK_MHZ * 49));
			break;
		// 309 ordinary lines in between
		case 621:
			// This is the last HSYNC before restarting for the next frame
			// so small gap before the VSYNC (total 15us)
			write_value(HSYNC_T, (SYSCLK_MHZ * 15) - HSYNC_T);
			line_no = 0;
			break;
		default:
			// Standard line, total 64us
			write_value(HSYNC_T, LINE_T - HSYNC_T);	
			break;
	}
}


// Set up the sync generator, which continues to run under interrupts.
// Assumes sync handling is the only thing using irq0 on this PIO instance.
void syncgen_start(void)
{
	unsigned offset;

	// Load the PIO program
	offset = pio_add_program(VIDEO_PIO, &sync_gen_program);

	// Initialise the PIO state machine
	sync_gen_init(VIDEO_PIO, VIDEO_SYNCGEN_SM, offset);

	// Hook the IRQ vector
	irq_set_exclusive_handler((VIDEO_PIO == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0,
		pio_irq0_handler);

	// Enable IRQ in the NVIC
	irq_set_enabled((VIDEO_PIO == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0, true);

	// Enable IRQ in the PIO
	// These enable bits are in order of SM number
	hw_set_bits(&VIDEO_PIO->inte0,
		(PIO_IRQ0_INTE_SM0_TXNFULL_BITS << VIDEO_SYNCGEN_SM));
}
