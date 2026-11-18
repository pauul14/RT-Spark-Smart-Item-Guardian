# Reference documents (local copies, not redistributed)

These files were downloaded for pin-mapping verification. The PDF and PNG are git-ignored; check them against `SHA256SUMS.txt`.

| file | source URL |
|---|---|
| `spark1_schematic_v1_0.pdf` (title blocks: Rev HW1.1, 2023-06-10) | https://github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark/tree/main/docs (星火1号开发板原理图_V1_0.pdf) |
| `rt-spark_board_photo_vendor.png` | https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark/figures/board.png |
| `rt-spark_CubeMX_Config.ioc` | https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark/board/CubeMX_Config |
| `rt-spark_board_Kconfig.txt` | https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark/board/Kconfig |

The documents disagree in two places, and the hardware session settles both. The vendor photo labels the T/RH sensor "AHT20" and the flash "8MB NorFlash", while the schematic says AHT21 and W25Q128 (16 MB). The firmware reports the AHT status byte and the flash JEDEC ID.
