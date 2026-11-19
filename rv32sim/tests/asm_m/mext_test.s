# mext_test.s — Prueba de la extensión M (extra): mul/div/rem y sus casos borde.
# Termina con exit2(0) si todo pasa, o con el número de la prueba fallida.
.global _start
.macro CHECK reg, expected
    li      t6, \expected
    addi    s0, s0, 1
    bne     \reg, t6, fail
.endm
_start:
    li      s0, 0
    li      t0, -6
    li      t1, 7
    mul     t2, t0, t1            # -42
    CHECK   t2, -42
    li      t0, 0x80000000
    li      t1, 0x80000000
    mulh    t2, t0, t1            # (-2^31)^2 = 2^62 -> parte alta 0x40000000
    CHECK   t2, 0x40000000
    mulhu   t2, t0, t1            # (2^31)^2 = 2^62 -> 0x40000000
    CHECK   t2, 0x40000000
    li      t0, -1
    li      t1, 0xFFFFFFFF
    mulhsu  t2, t0, t1            # -1 * (2^32-1) -> parte alta 0xFFFFFFFF
    CHECK   t2, -1
    li      t0, -7
    li      t1, 2
    div     t2, t0, t1            # -3 (trunca hacia cero)
    CHECK   t2, -3
    rem     t2, t0, t1            # -1
    CHECK   t2, -1
    divu    t2, t0, t1            # 0x7FFFFFFC
    CHECK   t2, 0x7FFFFFFC
    remu    t2, t0, t1            # 1
    CHECK   t2, 1
    div     t2, t0, zero          # división por cero -> -1
    CHECK   t2, -1
    rem     t2, t0, zero          # resto por cero -> dividendo
    CHECK   t2, -7
    li      t0, 0x80000000
    li      t1, -1
    div     t2, t0, t1            # overflow -> -2^31
    CHECK   t2, 0x80000000
    rem     t2, t0, t1            # overflow -> 0
    CHECK   t2, 0
    li      a0, 0
    li      a7, 17
    ecall
fail:
    mv      a0, s0
    li      a7, 17
    ecall
