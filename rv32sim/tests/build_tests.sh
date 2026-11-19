#!/usr/bin/env bash
# build_tests.sh — Ensambla los programas de prueba con clang/LLVM (soporta el
# target riscv32 sin instalar toolchains extra) y genera dos archivos por
# programa:
#   tests/bin/<nombre>.elf  -> ELF (con etiquetas)
#   tests/bin/<nombre>.bin  -> binario crudo cargado desde 0x00000000
#                              (el mismo formato "Raw" que exporta CPUlator)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin

CC=${CC_RISCV:-clang}
FLAGS="--target=riscv32 -march=rv32i -mabi=ilp32 -mno-relax -nostdlib -static -fuse-ld=lld"
LDFLAGS="-Wl,-T,link.ld -Wl,--no-relax"

for src in asm/*.s; do
    name=$(basename "$src" .s)
    $CC $FLAGS $LDFLAGS -o "bin/$name.elf" "$src"
    llvm-objcopy -O binary "bin/$name.elf" "bin/$name.bin"
    echo "  $src -> bin/$name.{elf,bin} ($(stat -c %s "bin/$name.bin") bytes)"
done

# Programa en C (crt0 + sort.c), optimizado con -O2
$CC $FLAGS $LDFLAGS -O2 -ffreestanding -o bin/sort_c.elf c/crt0.s c/sort.c
llvm-objcopy -O binary bin/sort_c.elf bin/sort_c.bin
echo "  c/sort.c -> bin/sort_c.{elf,bin} ($(stat -c %s bin/sort_c.bin) bytes)"

# Extensión M (extra): se ensambla con -march=rv32im
FLAGS_M="${FLAGS/rv32i /rv32im }"
for src in asm_m/*.s; do
    name=$(basename "$src" .s)
    $CC $FLAGS_M $LDFLAGS -o "bin/$name.elf" "$src"
    llvm-objcopy -O binary "bin/$name.elf" "bin/$name.bin"
    echo "  $src -> bin/$name.{elf,bin} ($(stat -c %s "bin/$name.bin") bytes)"
done
