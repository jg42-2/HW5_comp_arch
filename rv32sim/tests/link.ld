/* link.ld — Mapa de memoria de los programas de prueba.
   Todo el programa empieza en 0x00000000 (como exige el enunciado):
   primero el código y luego los datos, sin huecos grandes. */
ENTRY(_start)
SECTIONS
{
  . = 0x00000000;
  .text   : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
  .data   : { *(.data .data.* .sdata .sdata.*) }
  .bss    : { *(.bss .bss.* .sbss .sbss.* COMMON) }
}
