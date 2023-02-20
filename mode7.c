// Which PIO to use.  This should be in a board definition file.
#define	VIDOUT_PIO	pio1
#define	VIDOUT_SM	0

// Back porch delay from falling edge of HSYNC to first pixel, in units
// of the PIO clock.  Can tweak this to get the horizontal position right.
// Official back-porch (rising HSYNC to video) is 5.7us, plus 4.7us for
// HSYNC itself, plus 4.1us that BBC inserts to centre the image
// = 5.7+4.7+4.1 = 14.5us  (14.5 = 29/2)
#define BACK_PORCH	(SYSCLK_MHZ * 29 / 2)

// This should be 20, but the current font (optimised to fit in a 640x480
// VGA screen) has only 19 rows per character line.
#define	ROWS_PER_LINE	19
#define	FONT_ROWS		19

// Rate of flashing, as a count of 50Hz fields on and off
#define	FLASH_RATE		16


#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "mode7.pio.h"


/* Font list, indexed with following bits:
   1 - double height
   2 - 2nd row of double height
   4 - graphics
   8 - separated mode
*/

#include "fonts.c"		// The actual font data

static const uint16_t *font_list[16] = {
  font_std,
  font_std_dh_upper,
  font_std,			/* 2nd row, but this char not double	*/
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

// Generate one field of teletext display, with the PIO program handling
// HSYNC timing and the expansion of 12 horizontal pixels.
void ttxt_display_field(uint8_t *ttxt_buf, bool flash_on)
{
	unsigned line;			// Which line out of the 25? (0..24)
	unsigned row;			// Which pixel row within a line (0..19)
	unsigned ch_pos;		// Which character within the line (0..39)

	// Decode state - should be bits in a word
	bool graphics, separated, dheight, hold_gr, flash, conceal;

	bool dh_this_row, dh_row2;

	// Points to the current character within the 40x25 buffer
	uint8_t  *chp;
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

	dh_row2 = false;
	line = 0;
	row = 0;		// 0 or 1 depending on odd/even field
	do
	{
		// Reset at start of line

		// Colour state, formatted for sending to PIO with pixels added on.
		// Foreground colour is in bits 8..14, background in bits 0..7,
		// flag bit (always 1) in bit 15.
		uint32_t colours = 0x8000 | (7 << 8) | 0;

		font = font_list[0];
		held_cell = font;	// First entry in font is space
		graphics = false;
		separated = false;
		dheight = false;
		dh_this_row = false;
		flash = false;
		conceal = false;
		hold_gr = false;

		font = font_list[dheight | (dh_row2 * 2) |
			(graphics * 4) | separated * 8];

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
				if (graphics && hold_gr) fontp = held_cell;
				else fontp = font_list[0];
			}
			else
			{
				// Not a control character - select the appropriate font cell
				fontp = font + (ch - 0x20) * FONT_ROWS;
			}
	
			// Consider recording this character as a possible future held
			// graphic - only for graphic chars.
			if (graphics && (((ch >= 0x20) && (ch < 0x40)) || (ch > 0x60)))
				held_cell = fontp;

			// In second row of double height, any normal height characters
			// get replaced by spaces (1st cell of all fonts is space).
			if (dh_row2 && !dheight) fontp = font_list[0];

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
			pio_sm_put_blocking(VIDOUT_PIO, VIDOUT_SM,
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
						graphics = false;
						break;

					case 0x08:		// Flash
						flash = true;
						break;

					case 0x09:		// Steady
						flash = false;
						break;

					case 0x0c:		// Normal height
						dheight = false;
						break;

					case 0x0d:		// Double height
						dheight = true;
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
						graphics = true;
						break;

					case 0x18:		// Conceal
						conceal = true;
						break;

					case 0x19:		// Contiguous graphics
						separated = false;
						break;

					case 0x1a:		// Separated graphics
						separated = true;
						break;

					case 0x1e:		// Hold graphics
						hold_gr = true;
						break;

					case 0x1f:		// Release graphics	
						hold_gr = false;
						break;
				}

				// Choose appropriate font resulting from change (if any)
				font = font_list[dheight | (dh_row2 * 2) |
					(graphics * 4) | separated * 8];

			}
		}


		// End-of row processing

		// Tell the PIO to wait for HSYNC before the next row
		// This has the low 16 bits clear to distinguish it from normal
		// pixel data, and has the back-porch delay in the high bits.
		pio_sm_put_blocking(VIDOUT_PIO, VIDOUT_SM, BACK_PORCH << 16);

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
			if (dh_row2)
			{
				dh_row2 = false;
			}
			else if (dh_this_row)
			{
				dh_row2 = true;
			}
		}
	} while (line < 25);
}

