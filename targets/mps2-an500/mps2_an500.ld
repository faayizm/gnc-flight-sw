/* =============================================================================
 *  mps2_an500.ld -- where everything goes on the MPS2 AN500 (Cortex-M7).
 *
 *  The memory map, as QEMU 8.2 models the board (`info mtree` in its monitor):
 *
 *    0x00000000  4 MiB  SSRAM1   code and constants; the vector table at 0
 *    0x20000000  4 MiB  SSRAM2/3 initialised data, the main stack, and the
 *                                RAM that survives a reset
 *    0x60000000 16 MiB  PSRAM    zero-initialised data: the spacecraft
 *                                itself, 4 MiB packet store and all
 *
 *  The board's documentation also places a PSRAM at 0x21000000; QEMU maps
 *  nothing there, which is why .bss is at 0x60000000.
 *
 *  SSRAM1 plays the part of flash: .data is stored there after the code and
 *  copied into SSRAM2/3 by the startup code, as it would be from real flash.
 * ===========================================================================*/

MEMORY
{
  CODE   (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 4M - 64K
  NOINIT (rw)  : ORIGIN = 0x203F0000, LENGTH = 64K
  PSRAM  (rw)  : ORIGIN = 0x60000000, LENGTH = 16M
}

ENTRY(Reset_Handler)

/* RAM that keeps its contents through a reset (main.cpp's Retained). Not a
 * section, only an address: a section would land in a loadable segment of
 * the ELF file, and QEMU zero-fills every loadable segment when it reloads
 * the image on reset -- which would wipe exactly what this RAM is for. */
__retained_start = ORIGIN(NOINIT);
__retained_size  = LENGTH(NOINIT);

/* Main stack: used by the startup code, and by every interrupt once FreeRTOS
 * runs. Each task has its own stack, allocated statically in main.cpp. */
__main_stack_size = 8K;

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
    . = ALIGN(8);
  } > CODE

  /* Exception unwinding tables: none should exist (-fno-exceptions), but the
   * C library is built with them, so give them somewhere to go. */
  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > CODE

  .data : ALIGN(8)
  {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    __data_end = .;
  } > RAM AT > CODE
  __data_load = LOADADDR(.data);

  .stack (NOLOAD) : ALIGN(8)
  {
    . += __main_stack_size;
    __main_stack_top = .;
  } > RAM

  .bss (NOLOAD) : ALIGN(8)
  {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > PSRAM

  /DISCARD/ : { *(.ARM.attributes) }
}
