# Third-party components

IRL2FA bundles or derives from the following third-party works. Each keeps its
own licence; the IRL2FA licences (see [README](README.md#license)) cover only
the original work in this repository.

| Component | Where | Licence | Source |
|-|-|-|-|
| Monocypher (X25519, BLAKE2b, ChaCha20) | `firmware/core/third_party/monocypher/` | CC0-1.0 or BSD-2-Clause (dual) | https://monocypher.org/ |
| BIP-39 English wordlist | `firmware/core/wordlist.c` (generated) | MIT | https://github.com/bitcoin/bips/blob/master/bip-0039/english.txt |
| STM32U0 CMSIS device headers (stm32u0xx.h, stm32u083xx.h, system_stm32u0xx.h) | `firmware/port/stm32u0/vendor/cmsis_device_u0/` | Apache-2.0 | https://github.com/STMicroelectronics/STM32CubeU0 (tag v1.3.0) |
| STM32U0 LL driver headers (stm32u0xx_ll_gpio.h, stm32u0xx_ll_adc.h) | `firmware/port/stm32u0/vendor/stm32u0xx_ll/` | BSD-3-Clause | https://github.com/STMicroelectronics/STM32CubeU0 (tag v1.3.0) |
| Arm CMSIS Core(M) 5.9.0 headers | `firmware/port/stm32u0/vendor/cmsis_core/` | Apache-2.0 | https://github.com/ARM-software/CMSIS_5 |
| DejaVu Sans / DejaVu Sans Mono (rasterised into bitmap fonts) | `firmware/core/fonts.c` (generated) | Bitstream Vera licence + public domain changes | https://dejavu-fonts.github.io/License.html |
| KiCad symbol / footprint / 3D libraries (models copied into `hardware/lib/kicad.3dshapes/`, RV-3032 symbol derived from KiCad's RV-3028) | `hardware/lib/` | CC-BY-SA-4.0 with the KiCad libraries exception | https://www.kicad.org/libraries/license/ |

The KiCad libraries exception means using these symbols and footprints in a
design does not by itself put the design under CC-BY-SA; the copied library
files themselves remain CC-BY-SA-4.0.
