# io_demo.s — Demostración de environment calls (ecall) estilo SPIM/CPUlator.
#   a7 = 4  -> print_string(a0)      a7 = 5  -> a0 = read_int()
#   a7 = 1  -> print_int(a0)         a7 = 11 -> print_char(a0)
#   a7 = 10 -> exit
# Pide un número n, imprime Fibonacci(n) y la palabra "risc-v" en mayúsculas.
.global _start
_start:
    la      a0, msg_hola
    li      a7, 4
    ecall                        # print_string("¡Hola desde RISC-V!...")

    la      a0, msg_pide
    li      a7, 4
    ecall
    li      a7, 5
    ecall                        # a0 = read_int()
    mv      s0, a0               # s0 = n

    # ---- fib(n) iterativo: a=0, b=1; repetir n veces: (a, b) = (b, a+b)
    li      t0, 0                # a
    li      t1, 1                # b
    mv      t2, s0               # contador
fib_loop:
    beqz    t2, fib_done
    add     t3, t0, t1
    mv      t0, t1
    mv      t1, t3
    addi    t2, t2, -1
    j       fib_loop
fib_done:
    mv      s1, t0               # s1 = fib(n)

    la      a0, msg_fib1
    li      a7, 4
    ecall
    mv      a0, s0
    li      a7, 1
    ecall                        # print_int(n)
    la      a0, msg_fib2
    li      a7, 4
    ecall
    mv      a0, s1
    li      a7, 1
    ecall                        # print_int(fib(n))
    li      a0, '\n'
    li      a7, 11
    ecall                        # print_char('\n')

    # ---- Pasar un string a mayúsculas usando lbu / sb
    la      t0, palabra
upper_loop:
    lbu     t1, 0(t0)
    beqz    t1, upper_done
    li      t2, 'a'
    blt     t1, t2, no_letra
    li      t2, 'z'
    bgt     t1, t2, no_letra
    addi    t1, t1, -32          # 'a' - 'A' = 32
    sb      t1, 0(t0)
no_letra:
    addi    t0, t0, 1
    j       upper_loop
upper_done:
    la      a0, msg_upper
    li      a7, 4
    ecall
    la      a0, palabra
    li      a7, 4
    ecall
    li      a0, '\n'
    li      a7, 11
    ecall

    li      a7, 10
    ecall                        # exit

.data
msg_hola:  .asciz "Hola desde RISC-V! (simulado)\n"
msg_pide:  .asciz "Ingresa n: "
msg_fib1:  .asciz "fib("
msg_fib2:  .asciz ") = "
msg_upper: .asciz "En mayusculas: "
palabra:   .asciz "risc-v es genial"
