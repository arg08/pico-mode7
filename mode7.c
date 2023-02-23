
#include "mode7_demo.h"

#include "mode7.pio.h"

// Adjust BACK_PORCH and VERTICAL_POS to position the display on screen.

// Back porch delay from falling edge of HSYNC to first pixel, in units
// of the PIO clock.  Can tweak this to get the horizontal position right.
// Official back-porch (rising HSYNC to video) is 5.7us, plus 4.7us for
// HSYNC itself leaves 53.6us for active video, of which we actually use 40
// so 13.6 spare, put 6.8 either side to centre it.
// = 5.7+4.7+6.8 = 17.2us
#define BACK_PORCH	(SYSCLK_MHZ * 172 / 10)

// There are 320 video lines after the VSYNC, of which we output on 250.
// So there's 70 blank lines to distribute in top/bottom border.
// Even split would be 35, but I believe the gap at the top is normally
// a bit smaller.  This is also not going to be 100% right between
// Electron-style syncs (normal HSYNCs start right after VSYNC)
// and proper PAL where there are equalising pulses.
// This constant defines the number of skipped lines after the first
// HSYNC detected after VSYNC.
#define	VERTICAL_POS	30

// Note that the current font was optimised to fit in a 640x480 VGA screen,
// so had only 19 rows per character line.  It has now been stretched by
// duplicating the last row (which is always zero on alpha characters anyhow),
// but may not be right in the case of graphics.
#define	ROWS_PER_LINE	20
#define	FONT_ROWS		20



/* Font list, indexed with following bits:
   1 - double height
   2 - 2nd row of double height
   4 - graphics
   8 - separated mode
*/
#define	BIT_DBL_HEIGHT	1
#define	BIT_2ND_ROW_DH	2
#define	BIT_GRAPHICS	4
#define	BIT_SEPARATED	8
static const uint16_t * const font_list[16] = {
	font_std,
	font_std_dh_upper,
	font_std,                    /* 2nd row, but this char not double    */
	font_std_dh_lower,
	font_graphic,
	font_graphic_dh_upper,
	font_graphic,
	font_graphic_dh_lower,
	font_std,
	font_std_dh_upper,
	font_std,
	font_std_dh_lower,
	font_sep_graphic,
	font_sep_graphic_dh_upper,
	font_sep_graphic,
	font_sep_graphic_dh_lower
};

// Wait for VSYNC and feed an appropriate number of dummy lines
// into the PIO so that the next thing to go in the FIFO is the
// pixel data for the first line.
// Returns true if it's the odd field, false if even field
static bool __force_inline wait_for_vsync(void)
{
	uint32_t falling, rising, vsync_end, width;
	bool got_vsync = false;

	// Measure the width of low-going pulses.  Note that we don't check
	// for the signal being high before entering the loop: worst thing that
	// can happen is we see a pulse shorter than it really is, but we are
	// first looking for a VSYNC - so we might mistake our VSYNC for an HSYNC,
	// but otherwise we'd have missed it altogether so the effect is the same.
	for (;;)
	{
		// Record timestamps of the rising and falling edges
		while (gpio_get(PIN_SYNC_IN) != 0)
			;
		falling = time_us_32();
		while (gpio_get(PIN_SYNC_IN) == 0)
			;
		rising = time_us_32();
		width = rising - falling;	// Pulse width in microseconds approx
		if (width > 25)
		{
			// Seems like a VSYNC
			got_vsync = true;
			vsync_end = rising;
		}
		else if ((width < 7) && (width > 2))
		{
			// Seems like an HSYNC.  If we've already seen a VSYNC
			// we are ready to go.
			if (got_vsync) break;
		}
	}
	// Sync has just gone high after the first HSYNC, so we can tell the
	// PIO to start counting HSYNCs from here.
	for (unsigned u = 0; u < VERTICAL_POS; u++)
		pio_sm_put_blocking(VIDEO_PIO, VIDEO_MODE7_SM, BACK_PORCH << 16);

	// Here with vsync_end=timestamp of the rising edge of the VSYNC,
	// falling=/ the falling edge of HSYNC.  For an Electron, they should be
	// either 17 or 49us apart, indicating which field; however on
	// proper video with equalising pulses there could be a multiple
	// of 64us extra so we take the number mod 64
	return (((falling - vsync_end) % 64) < 32);
}


// Generate one field of teletext display, with the PIO program handling
// HSYNC timing and the expansion of 12 horizontal pixels.
// This waits for VSYNC before starting, measures the relative HSYNC/VSYNC
// timing to determine whether it is the odd or even field, and exits
// after the last visible line.  Hence it should be called in a loop
// to produce a continuous display, with the caller having a little time
// to do some housekeeping between calls and still get there in time for
// the next VSYNC.
void mode7_display_field(const uint8_t *ttxt_buf, bool flash_on)
{
	unsigned line;			// Which line out of the 25? (0..24)
	unsigned row;			// Which pixel row within a line (0..19)
	unsigned ch_pos;		// Which character within the line (0..39)

	// Decode state - should be bits in a word
	unsigned font_mode;		// BIT_DBL_HEIGHT etc.

	bool hold_gr, flash, conceal;

	bool dh_this_row;

	// Points to the current character within the 40x25 buffer
	const uint8_t  *chp;
	// Character value retrieved from *chp
	unsigned ch;

	// Pointer to start of the currently selected font - each font starts
	// at character value 0x20 and contains 96 characters, formatted
	// as one row of pixels per 16-bit word.
	const uint16_t *font;		
	// fontp is the current character within the font, used only momentarily
	const uint16_t *fontp;
	// held_cell is a stashed fontp value for the character that is being
	// 'held' in Hold Graphics mode
	const uint16_t *held_cell;

#ifdef CONCEAL_SUPPORT
	int enable_conceal = false;
#endif

	// Start on row 0 or 1 depending on whether this is odd or even field
	if (wait_for_vsync()) row = 0;
	else row = 1;

	font_mode = 0;
	line = 0;
	do
	{
		// Reset at start of line

		// Colour state, formatted for sending to PIO with pixels added on.
		// Foreground colour is in bits 8..14, background in bits 0..7,
		// flag bit (always 1) in bit 15.
		uint32_t colours = 0x8000 | (7 << 8) | 0;

		font = font_list[0];
		held_cell = font;	// First entry in font is space
		dh_this_row = false;
		flash = false;
		conceal = false;
		hold_gr = false;
		// Reset font mode apart from BIT_2ND_ROW_DH
		font_mode &= ~(BIT_DBL_HEIGHT | BIT_GRAPHICS | BIT_SEPARATED);

		font = font_list[font_mode];

		// Index the 40x25 buffer.  Note that we scan along each character line
		// multiple times as we do the pixel rows, so we can't just keep
		// incrementing chp from line to line.
		chp = ttxt_buf + line * 40;

		for (ch_pos = 0; ch_pos < 40; ch_pos++)
		{
			ch = (*chp++) & 0x7f;
			if (ch < 0x20)
			{
				// Only background changes take effect in the same cell as
				// they occur - all others occur after rendering the cell.
				switch (ch)
				{
					case 0x1c:		// Black background
						colours = colours & ~7;	// Clear low 3 bits
						break;
	
					case 0x1d:		// New background
						// The new background colour is the current foreground,
						// so copy down the bits
						colours = (colours & ~7) | ((colours >> 8) & 7);
						break;
				}
	
				// Select the font cell for this character - normally space,
				// unless hold graphics in effect
				if ((font_mode & BIT_GRAPHICS) && hold_gr) fontp = held_cell;
				else fontp = font_list[0];
			}
			else
			{
				// Not a control character - select the appropriate font cell
				fontp = font + (ch - 0x20) * FONT_ROWS;
			}
	
			// Consider recording this character as a possible future held
			// graphic - only for graphic chars.
			if ((font_mode & BIT_GRAPHICS)
				&& (((ch >= 0x20) && (ch < 0x40)) || (ch > 0x60)))
			{
				held_cell = fontp;
			}

			// In second row of double height, any normal height characters
			// get replaced by spaces (1st cell of all fonts is space).
			if (font_mode & (BIT_2ND_ROW_DH | BIT_DBL_HEIGHT)
				== BIT_2ND_ROW_DH)
			{
				fontp = font_list[0];
			}

			// If this character is flashing and the flash state is 'off'
			// replace it with a blank.
			// NB. spaces don't flash, but other
			// control chars might do so due to hold graphics
			if (flash && !flash_on) fontp = font_list[0];

#ifdef CONCEAL_SUPPORT
			// Replace concealed chars with spaces if enabled
			if (conceal && enable_conceal) fontp = font_list[0];
#endif

			// Output the pixels of this character to the PIO
			// Value written has the foreground/background colours,
			// a flag bit, and the pixel data.
			pio_sm_put_blocking(VIDEO_PIO, VIDEO_MODE7_SM,
				colours | (fontp[row] << 16));


			// Most of the control characters take effect after the cell
			if (ch < 0x20)
			{
				switch (ch)
				{
					case 0x01:		// Alpha red
					case 0x02:		// Alpha green
					case 0x03:		// Alpha yellow
					case 0x04:		// Alpha blue
					case 0x05:		// Alpha magenta
					case 0x06:		// Alpha cyan
					case 0x07:		// Alpha white
						// Change the foreground colour (in bits 8..10)
						// to the value of the control character.
						colours = (colours & ~0x700) | (ch << 8);
						font_mode &= ~BIT_GRAPHICS;
						break;

					case 0x08:		// Flash
						flash = true;
						break;

					case 0x09:		// Steady
						flash = false;
						break;

					case 0x0c:		// Normal height
						font_mode &= ~BIT_DBL_HEIGHT;
						break;

					case 0x0d:		// Double height
						font_mode |= BIT_DBL_HEIGHT;
						dh_this_row = true;
						break;

					case 0x11:		// Mosaic red
					case 0x12:		// Mosaic green
					case 0x13:		// Mosaic yellow
					case 0x14:		// Mosaic blue
					case 0x15:		// Mosaic magenta
					case 0x16:		// Mosaic cyan	
					case 0x17:		// Mosaic white	
						// Change the foreground colour (in bits 8..10)
						// to the value of the low bits of the ctrl character.
						colours = (colours & ~0x700) | ((ch & 7) << 8);
						font_mode |= BIT_GRAPHICS;
						break;

					case 0x18:		// Conceal
						conceal = true;
						break;

					case 0x19:		// Contiguous graphics
						font_mode &= ~BIT_SEPARATED;
						break;

					case 0x1a:		// Separated graphics
						font_mode |= BIT_SEPARATED;
						break;

					case 0x1e:		// Hold graphics
						hold_gr = true;
						break;

					case 0x1f:		// Release graphics	
						hold_gr = false;
						break;
				}

				// Choose appropriate font resulting from change (if any)
				font = font_list[font_mode];

			}
		}

		// End-of row processing

		// Tell the PIO to wait for HSYNC before the next row
		// This has the low 16 bits clear to distinguish it from normal
		// pixel data, and has the back-porch delay in the high bits.
		pio_sm_put_blocking(VIDEO_PIO, VIDEO_MODE7_SM, BACK_PORCH << 16);

		row += 2;		// Interlaced display, so step on by two rows

		// If there's another row in the same line, just go round and do
		// it all again to get the next row out of the font and the
		// same characters.  Else step on to next line.
		if (row >= ROWS_PER_LINE)
		{
			row -= ROWS_PER_LINE;
			line++;

			// Fix up for double height.  If we were doing a proper display,
			// we'd repeat the previous line; however, this is emulating
			// a BBC micro that requires the user to fill in the 2nd line
			// so we just set the flags that will cause us to select the
			// lower-half font next time round.
			if (font_mode & BIT_2ND_ROW_DH)
			{
				font_mode &= ~BIT_2ND_ROW_DH;
			}
			else if (dh_this_row)
			{
				font_mode |= BIT_2ND_ROW_DH;
			}
		}
	} while (line < 25);
}


// Initialise PIO etc. ready to call mode7_display_field()
void mode7_init(void)
{
	unsigned offset;

	// Load the PIO program
	offset = pio_add_program(VIDEO_PIO, &mode7_output_program);

	// Initialise and start the PIO state machine
	mode7_output_init(VIDEO_PIO, VIDEO_MODE7_SM, offset);

	// PIO will be blocked waiting for something in the FIFO before
	// it does anything.  Feed it an end-of-line, which will force
	// the outputs to black while it waits for the HSYNC (it will then
	// get blocked again until we get around to starting up properly).
	pio_sm_put_blocking(VIDEO_PIO, VIDEO_MODE7_SM, BACK_PORCH << 16);

	// Now safe to enable the outputs
	gpio_put(PIN_RGB_EN, 1);			// Active low enable on the passthrough
	pio_gpio_init(VIDEO_PIO, PIN_RGB_RO);
	pio_gpio_init(VIDEO_PIO, PIN_RGB_GO);
	pio_gpio_init(VIDEO_PIO, PIN_RGB_BO);
}
