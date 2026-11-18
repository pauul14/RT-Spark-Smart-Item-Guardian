# Dependency versions (as installed and used on 2026-11-17)

| component | version | pinned by |
|---|---|---|
| PlatformIO platform `ststm32` | 19.7.1 | `platformio.ini` (`ststm32@19.7.1`) |
| `framework-stm32cubef4` (STM32CubeF4) | 1.28.1 (HAL driver 1.8.3) | platform 19.7.1 |
| `toolchain-gccarmnoneeabi` | 1.70201.0 = GNU Arm Embedded 7-2017-q4-major (GCC 7.2.1) | platform 19.7.1 |
| `tool-openocd` | xPack OpenOCD 0.12.0+dev-02228 (package 3.1200.7) | platform 19.7.1 |
| `tool-scons` | 4.41101.0 | PlatformIO |
| `tool-ldscripts-ststm32` | 0.2.0 (not used: custom linker script) | platform |
| FreeRTOS kernel | V10.3.1 (vendored in `lib/FreeRTOS-Kernel`) | repository |
| FatFs | R0.12c (vendored in `lib/FatFs`, from STM32CubeF4 1.28.1) | repository |
| `toolchain-gccmingw32` (host checks only) | 1.50100.0 = MinGW-w64 GCC 5.1.0 i686 | PlatformIO package |
| Python | 3.11.7 (PlatformIO penv) | — |
| pyserial | 3.5 | PlatformIO penv |
| Other Python packages | none (standard library only) | — |

Build flags added by the project:

- `-std=gnu11 -Wextra -Wno-unused-parameter`
- `-mfpu=fpv4-sp-d16 -mfloat-abi=hard -ffp-contract=off`
- `-Wl,-Map,... -Wl,--print-memory-usage`

The platform's own flags `-Os -ffunction-sections -fdata-sections -Wall --specs=nano.specs --specs=nosys.specs` are added on top of these.
