/*
 * Linker script for RT-Spark (STM32F407ZGT6).
 *
 * Structure follows ST's STM32CubeIDE-generated STM32F407ZGTX_FLASH.ld
 * (BSD-3-Clause, (c) STMicroelectronics). Changes for this project:
 *   - CCMRAM (64 KB @ 0x10000000, CPU/D-bus only, NOT reachable by DMA) holds
 *     the FreeRTOS heap (.ccmram, NOLOAD). Task stacks therefore live in CCM,
 *     so no DMA buffer may ever be placed on a task stack.
 *   - .noinit (NOLOAD) in SRAM keeps the fault record across a soft reset.
 *   - newlib heap is 0 bytes: the firmware never calls malloc (FreeRTOS
 *     heap_4 is the only allocator); _sbrk is stubbed to fail loudly.
 *
 * Memory map from RM0090 section 2.3 / DS8626 (STM32F407xG):
 *   Flash 1024 KB @ 0x08000000, SRAM1+SRAM2 112+16 KB contiguous @ 0x20000000,
 *   CCM data RAM 64 KB @ 0x10000000.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* MSP top: end of SRAM2 */

_Min_Heap_Size  = 0x0;     /* newlib heap unused */
_Min_Stack_Size = 0x1000;  /* MSP: startup code, main() before scheduler, all ISRs */

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  /* Marks the end of the flash image (used by the footprint report). */
  _eflash_image = LOADADDR(.data) + SIZEOF(.data);

  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    _snoinit = .;
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
    _enoinit = .;
  } >RAM

  /* Reserve the MSP region; the link fails if SRAM cannot hold it. */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    _sstack_reserved = .;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  .ccmram (NOLOAD) :
  {
    . = ALIGN(8);
    _sccmram = .;
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(8);
    _eccmram = .;
  } >CCMRAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
