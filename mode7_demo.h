#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"

// Which PIO to use.  This should be in a board definition file.
#define	VIDEO_PIO			pio1
#define	VIDEO_MODE7_SM		0
#define	VIDEO_SYNCGEN_SM	3


// test_pages.c
#define NOOF_TEST_PAGES 4
extern const uint8_t * const test_pages[NOOF_TEST_PAGES];

// fonts.c
extern const uint16_t font_std[96*20];
extern const uint16_t font_std_dh_upper[96*20];
extern const uint16_t font_std_dh_lower[96*20];
extern const uint16_t font_graphic[96*20];
extern const uint16_t font_graphic_dh_upper[96*20];
extern const uint16_t font_graphic_dh_lower[96*20];
extern const uint16_t font_sep_graphic[96*20];
extern const uint16_t font_sep_graphic_dh_upper[96*20];
extern const uint16_t font_sep_graphic_dh_lower[96*20];

// mode7.c
extern void mode7_display_field(const uint8_t *ttxt_buf, bool flash_on);
extern void mode7_init(void);

// makesyncs.c
extern void syncgen_start(void);
