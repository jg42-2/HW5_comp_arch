#!/usr/bin/env bash
# run_tests.sh — Pruebas automáticas: ejecuta cada programa en el simulador y
# compara el estado final (registros / memoria / código de salida) con lo esperado.
set -uo pipefail
cd "$(dirname "$0")/.."
SIM=./build/rv32sim
pass=0; fail=0

check() {   # check <descripción> <salida> <texto esperado>
    if grep -qF -- "$3" <<<"$2"; then
        echo "  [OK]   $1"; pass=$((pass + 1))
    else
        echo "  [FAIL] $1 (se esperaba: $3)"; fail=$((fail + 1))
    fi
}

for fmt in bin elf; do
    echo "== Formato .$fmt =="
    out=$($SIM --no-color -x "run; words 100 1" tests/bin/riscvtest.$fmt)
    check "riscvtest: mem[0x64] = 25" "$out" "0x00000064: 0x00000019  (25)"

    out=$($SIM --no-color -x "run; mem 0x1000 0x101B" tests/bin/quicksort.$fmt)
    check "quicksort: {1,2,3,4,6,8,9} en 0x1000" "$out" \
        "01 00 00 00 02 00 00 00  03 00 00 00 04 00 00 00"
    check "quicksort: ... 6, 8, 9" "$out" "06 00 00 00 08 00 00 00  09 00 00 00"

    out=$($SIM --no-color -x "run; regs a0" tests/bin/symmetric.$fmt)
    check "árbol simétrico: a0 = 1" "$out" "x10 (a0  ) = 0x00000001"

    out=$($SIM --no-color -x "run; words 0x2000 1" tests/bin/isa_test.$fmt)
    check "isa_test: 37 instrucciones (exit 0)" "$out" "código 0]"
    check "isa_test: marca 0x600DC0DE" "$out" "0x600DC0DE"

    out=$(printf '10\n' | $SIM --no-color -x "run" tests/bin/io_demo.$fmt)
    check "io_demo: fib(10) = 55" "$out" "fib(10) = 55"
    check "io_demo: mayúsculas con lbu/sb" "$out" "RISC-V ES GENIAL"

    out=$($SIM --no-color -x "run" tests/bin/sort_c.$fmt)
    check "programa en C: arreglo ordenado" "$out" "Despues: -20, -7, 0, 3, 8, 15, 42, 99"
    check "programa en C: return 140" "$out" "código 140]"

    out=$($SIM --no-color -x "run" tests/bin/mext_test.$fmt)
    check "extensión M: mul/div/rem + casos borde" "$out" "código 0]"
done

echo
echo "Resultado: $pass pruebas OK, $fail fallidas"
[ "$fail" -eq 0 ]
