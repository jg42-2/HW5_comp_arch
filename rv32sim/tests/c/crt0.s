# crt0.s — Arranque mínimo para programas en C: inicializa la pila,
# llama a main() y termina con exit2(valor de retorno de main).
.global _start
.section .text.start
_start:
    li      sp, 0x80000
    call    main
    li      a7, 17
    ecall
1:  j       1b
