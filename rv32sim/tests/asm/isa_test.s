# isa_test.s — Prueba automática de las 37 instrucciones de RV32I del anexo A.
# Cada CHECK incrementa s0 (número de prueba) y compara un registro con el
# valor esperado. Si alguna falla, salta a "fail" con s0 = número de la prueba.
# Al final:  a0 = 0 si todo pasó (y s0 = cantidad de pruebas), se escribe
# 0x600DC0DE en la dirección 0x2000 y se termina con ecall exit2 (a7=17).
.global _start

.macro CHECK reg, expected
    li      t6, \expected
    addi    s0, s0, 1
    bne     \reg, t6, fail
.endm

_start:
    li      s0, 0
    li      sp, 0x10000
    li      s1, 0x3000           # zona de datos para loads/stores

# ---------------------------------------------------------------- tipo U
    lui     t0, 0x12345          # t0 = 0x12345000
    CHECK   t0, 0x12345000
here:
    auipc   t1, 0                # t1 = dirección de "here"
    la      t2, here
    sub     t3, t1, t2
    CHECK   t3, 0

# ---------------------------------------------------- aritmética inmediata
    li      t0, 100
    addi    t1, t0, -150         # -50
    CHECK   t1, -50
    slti    t2, t1, 0            # -50 < 0 (con signo) -> 1
    CHECK   t2, 1
    sltiu   t2, t1, 5            # 0xFFFFFFCE < 5 (sin signo) -> 0
    CHECK   t2, 0
    xori    t2, t0, 0xFF         # 100 ^ 255 = 155
    CHECK   t2, 155
    ori     t2, t0, 0x0F         # 100 | 15 = 111
    CHECK   t2, 111
    andi    t2, t0, 0x0F         # 100 & 15 = 4
    CHECK   t2, 4
    li      t0, 0x80000001
    slli    t2, t0, 4            # 0x00000010
    CHECK   t2, 0x00000010
    srli    t2, t0, 4            # 0x08000000 (rellena con ceros)
    CHECK   t2, 0x08000000
    srai    t2, t0, 4            # 0xF8000000 (replica el signo)
    CHECK   t2, 0xF8000000

# ------------------------------------------------- registro-registro (R)
    li      t0, 7
    li      t1, -3
    add     t2, t0, t1           # 4
    CHECK   t2, 4
    sub     t2, t0, t1           # 10
    CHECK   t2, 10
    li      t3, 3
    sll     t2, t0, t3           # 56
    CHECK   t2, 56
    slt     t2, t1, t0           # -3 < 7 -> 1
    CHECK   t2, 1
    sltu    t2, t1, t0           # 0xFFFFFFFD < 7 -> 0
    CHECK   t2, 0
    xor     t2, t0, t1           # 7 ^ -3 = -6
    CHECK   t2, -6
    srl     t2, t1, t3           # 0xFFFFFFFD >> 3 = 0x1FFFFFFF
    CHECK   t2, 0x1FFFFFFF
    sra     t2, t1, t3           # -3 >> 3 = -1
    CHECK   t2, -1
    or      t2, t0, t1           # 7 | -3 = -1
    CHECK   t2, -1
    and     t2, t0, t1           # 7 & -3 = 5
    CHECK   t2, 5
    li      t4, 33               # en RV32 solo cuentan 5 bits: 33 -> 1
    sll     t2, t0, t4
    CHECK   t2, 14

# ------------------------------------------------------- stores y loads
    li      t0, 0x8899AABB
    sw      t0, 0(s1)            # mem[0x3000..3] = BB AA 99 88 (little-endian)
    lw      t2, 0(s1)
    CHECK   t2, 0x8899AABB
    lb      t2, 0(s1)            # 0xBB -> sign-extend -> 0xFFFFFFBB
    CHECK   t2, 0xFFFFFFBB
    lbu     t2, 0(s1)            # 0x000000BB
    CHECK   t2, 0xBB
    lh      t2, 2(s1)            # 0x8899 -> 0xFFFF8899
    CHECK   t2, 0xFFFF8899
    lhu     t2, 2(s1)            # 0x00008899
    CHECK   t2, 0x8899
    lb      t2, 3(s1)            # byte más significativo = 0x88
    CHECK   t2, 0xFFFFFF88
    li      t0, 0x7F
    sb      t0, 1(s1)            # mem = BB 7F 99 88
    lw      t2, 0(s1)
    CHECK   t2, 0x88997FBB
    li      t0, 0x1234
    sh      t0, 2(s1)            # mem = BB 7F 34 12
    lw      t2, 0(s1)
    CHECK   t2, 0x12347FBB
    sw      t0, -4(s1)           # offset negativo
    lw      t2, -4(s1)
    CHECK   t2, 0x1234

# ----------------------------------------------------------------- saltos
    li      t0, -1
    li      t1, 1
    li      t2, 0                # contará los saltos correctos
    beq     t0, t0, 1f
    j       fail
1:  addi    t2, t2, 1
    bne     t0, t1, 2f
    j       fail
2:  addi    t2, t2, 1
    blt     t0, t1, 3f           # -1 < 1 con signo
    j       fail
3:  addi    t2, t2, 1
    bge     t1, t0, 4f           # 1 >= -1 con signo
    j       fail
4:  addi    t2, t2, 1
    bltu    t1, t0, 5f           # 1 < 0xFFFFFFFF sin signo
    j       fail
5:  addi    t2, t2, 1
    bgeu    t0, t1, 6f           # 0xFFFFFFFF >= 1 sin signo
    j       fail
6:  addi    t2, t2, 1
    beq     t0, t1, fail         # NO deben tomarse:
    bne     t0, t0, fail
    blt     t1, t0, fail
    bge     t0, t1, fail
    bltu    t0, t1, fail
    bgeu    t1, t0, fail
    CHECK   t2, 6

# ---------------------------------------------------------- jal / jalr
    jal     ra, func             # ra = dirección de retorno
    CHECK   a0, 42
    la      t0, func
    jalr    ra, 0(t0)            # llamada indirecta
    CHECK   a0, 42
    la      t0, after
    jalr    t1, 4(t0)            # salta a after+4 (se salta el "j fail")
after:
    j       fail
    la      t2, after
    sub     t3, t1, t2           # t1 = dirección siguiente al jalr = after
    CHECK   t3, 0

# x0 siempre es cero
    addi    zero, zero, 123
    CHECK   zero, 0

# ---------------------------------------------------------------- fin
pass:
    li      t0, 0x600DC0DE
    li      t1, 0x2000
    sw      t0, 0(t1)
    li      a0, 0
    li      a7, 17
    ecall                        # exit2(0)

fail:
    mv      a0, s0               # código de salida = número de la prueba fallida
    li      a7, 17
    ecall

func:
    li      a0, 42
    ret
