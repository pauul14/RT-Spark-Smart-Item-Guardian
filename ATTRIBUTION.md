# Attribution and third-party components

No project-level open-source licence has been selected for the original project code. The proposed MIT licence was not approved for publication. The third-party licences listed below remain intact.

| component | where | origin / version | license | how it is used |
|---|---|---|---|---|
| STM32CubeF4 HAL/LL drivers, CMSIS device headers, startup and system files | PlatformIO package `framework-stm32cubef4` 1.28.1 (HAL 1.8.3) | STMicroelectronics | BSD-3-Clause | Compiled by PlatformIO's `stm32cube` builder; not copied into the repo |
| `include/stm32f4xx_hal_conf.h` | generated from ST's `stm32f4xx_hal_conf_template.h` by `tools/build/gen_hal_conf.py` | STMicroelectronics template | BSD-3-Clause | Module enables, HSE value and tick priority changed |
| `boards/ldscripts/rt_spark_f407zg.ld` | structure follows ST's STM32CubeIDE `STM32F407ZGTX_FLASH.ld` | STMicroelectronics | BSD-3-Clause | CCM heap section, `.noinit`, zero newlib heap added |
| FreeRTOS kernel V10.3.1 | `lib/FreeRTOS-Kernel/` (vendored, unmodified) | Amazon.com, Inc. / Real Time Engineers Ltd., as shipped in STM32CubeF4 | MIT | Kernel, `GCC/ARM_CM4F` port, `heap_4` |
| FatFs R0.12c | `lib/FatFs/` (`ff.c`, `ff.h`, `integer.h`, `diskio.h`, vendored unmodified) | ChaN, as shipped in STM32CubeF4 `Middlewares/Third_Party/FatFs` | FatFs licence (BSD-style, 1 clause) | microSD CSV log; configuration `include/ffconf.h` derived from ST's `ffconf_template.h`; disk I/O written for this project (`src/drivers/sd_card.c`) |
| NEC infrared protocol timing | `src/core/ir_nec_codec.c` (code is new) | public NEC protocol description | — | remote control decoding and alert relay frames |
| RT-Spark schematic (Rev HW1.1), board photo | `docs/hardware/reference/` (local copy, git-ignored) | RT-Thread-Studio `sdk-bsp-stm32f407-spark` / RT-Thread `bsp/stm32/stm32f407-rt-spark` | per upstream repository | Pin mapping evidence only (see SHA256SUMS.txt) |
| RT-Spark BSP `board/Kconfig`, `CubeMX_Config.ioc` | `docs/hardware/reference/` | RT-Thread `bsp/stm32/stm32f407-rt-spark` | Apache-2.0 | Clock tree and pin cross-check |
| ST7789V3 panel register values, FSMC timing | `src/drivers/sig_lcd.c` (values only; code is new) | RT-Thread `bsp/.../ports/lcd/drv_lcd.c` | Apache-2.0 | Init sequence values for this exact panel |
| SK6805 bit timing (TIM3 8.4 MHz, 2/7 of 10 ticks) | `src/drivers/led_matrix.c` (values only; code is new) | RT-Thread `bsp/.../ports/led_matrix/drv_matrix_led.c` | Apache-2.0 | Proven bit timing on this board |
| Sensor register sequences | `src/drivers/sensors.c` (code is new) | Device datasheets, cross-checked against RT-Thread packages `icm20608`, `ap3216c`, `aht10` | Apache-2.0 (packages) | Register addresses/values only |
| 5×7 font | `src/drivers/font5x7.c` | Rendition of the classic public-domain 5×7 dot-matrix glyph set | public domain | LCD text |
| pyserial 3.5 | PlatformIO Python environment | Chris Liechti | BSD-3-Clause | Serial access in `tools/hw` |
| Python 3.11.7 standard library | PlatformIO Python | Python Software Foundation | PSF | Training, export and test tools (no third-party ML library) |

All other files were written for this project. Lab 3's architecture and code were not reused; the earlier Lab 3 serial log on this computer was only read to learn the board's prior firmware state.
