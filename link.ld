/* https://stackoverflow.com/questions/31390127/how-can-i-compile-c-code-to-get-a-bare-metal-skeleton-of-a-minimal-risc-v-assemb */
OUTPUT_ARCH( "riscv" )
ENTRY( _start )
SECTIONS
{
/* text: test code section */
. = 0x200;
.init :
{
  *(.init)
}
.text :
{
*(.text)
}
/* data: Initialized data segment */
.data :
{
*(.data)
}
/DISCARD/ : { *(.comment) }
/DISCARD/ : { *(.riscv.attributes) }
/* End of uninitalized data segement */
_end = .;
}
