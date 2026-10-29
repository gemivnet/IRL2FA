/* Good Display GDEY0213B74 (SSD1680, 122 x 250) on SPI1.
 * SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef IRL2FA_EPD_SSD1680_H
#define IRL2FA_EPD_SSD1680_H

#include <stdint.h>

#include "hal.h"

/* Powers the panel up, draws `fb` (hal.h layout) and powers it off again. */
void epd_show(const uint8_t *fb, display_mode_t mode);

/* Every panel line driven low (or analog for BUSY): the state for an
 * unpowered panel. Called at boot and after each update. */
void epd_lines_off(void);

#endif
