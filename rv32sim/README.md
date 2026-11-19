# RV32Sim — Simulador del ISA RISC-V RV32I

Simulador en **C++17** del conjunto de instrucciones base **RV32I** (las 37 instrucciones del
anexo A de la Tarea 5 de CS3051), con ejecución paso a paso, inspección de PC, registros y
memoria, breakpoints, desensamblador, *environment calls* estilo SPIM/CPUlator, carga de
archivos ELF y, como extra, la extensión **M** (multiplicación/división).

```
$ ./build/rv32sim tests/bin/riscvtest.bin
"tests/bin/riscvtest.bin" cargado a memoria (raw, 84 bytes en 0x00000000-0x00000053).
PC inicial = 0x00000000
> step
  0x00000000: addi    sp, zero, 5               # li sp, 5
      => sp: 0x00000000 -> 0x00000005
> run
[Bucle infinito detectado en 0x00000050: la instrucción salta a sí misma (fin del programa)]
> mem 0x64 0x67
Memoria (0x00000064-0x00000067): 19 00 00 00
```

## Compilar

Solo necesita un compilador de C++17 (g++, clang++ o MSVC). Sin dependencias externas.

```bash
make                      # -> build/rv32sim
# o con CMake:
cmake -S . -B build && cmake --build build
```

## Uso

```bash
./build/rv32sim programa.bin          # modo interactivo (REPL)
./build/rv32sim programa.elf          # también acepta ELF (muestra etiquetas)
./build/rv32sim -r programa.bin       # ejecuta todo y muestra los registros
./build/rv32sim -x "run; regs a0; mem 0x1000 0x101B" programa.bin   # comandos en lote
```

| Comando | Qué hace |
|---|---|
| `step [n]` / `s` | Ejecuta *n* instrucciones mostrando cada una y su efecto (registro o memoria modificados, salto tomado o no). Enter vacío repite el último `step`. |
| `run [max]` / `r` / `c` | Ejecuta hasta un breakpoint, `ebreak`, `ecall exit`, un bucle infinito (`j .`), un error o *max* instrucciones. |
| `pc` | Muestra el PC (y la etiqueta, si hay ELF). |
| `regs [r ...]` | Tabla de los 32 registros (resalta los que cambiaron) o solo los pedidos (`regs x5 t0 sp`). |
| `mem <ini> [fin]` | Bytes de memoria (hasta 16 en una línea; si son más, volcado hex + ASCII). |
| `words <ini> [n]` | *n* palabras de 32 bits interpretadas en little-endian, en hex y decimal. |
| `disas [dir] [n]` | Desensambla alrededor del PC o desde *dir*. Marca `=>` el PC y `*` los breakpoints. |
| `break <dir\|etiqueta>`, `delete <dir\|all>`, `breaks` | Manejo de breakpoints. |
| `set <reg\|pc> <valor>`, `poke <dir> <valor> [1\|2\|4]` | Modifica registros, PC o memoria. |
| `trace [on\|off]` | Imprime cada instrucción durante `run`. |
| `reset`, `load <archivo>`, `info`, `syms`, `help`, `exit` | Utilidades. |

Los valores aceptan decimal (`42`), hex (`0x2A`), binario (`0b101`), registros (`sp` usa su valor
actual) o etiquetas del ELF (`_qsort`).

## Diseño

```
src/
  memory.hpp   Memoria plana de 32 bits, byte-addressable, little-endian, dispersa (páginas de 4 KiB)
  decode.hpp   Extracción de campos (opcode, rd, rs1, rs2, funct3, funct7) e inmediatos I/S/B/U/J
  cpu.hpp/.cpp Estado arquitectural (PC, x0..x31, memoria) y ciclo fetch-decode-execute
  disasm.*     Desensamblador (nombres ABI, destinos absolutos, pseudo-instrucciones como comentario)
  loader.*     Carga de binarios crudos (en 0x00000000) y ELF32 RISC-V (segmentos PT_LOAD + .symtab)
  symbols.hpp  Tabla de etiquetas para el desensamblador y los breakpoints
  main.cpp     Interfaz de línea de comandos (REPL)
```

* **Memoria:** el espacio de direcciones es de 4 GiB, así que se reservan páginas de 4 KiB solo
  cuando se escriben (`unordered_map<página, array<uint8_t,4096>>`). Leer memoria nunca escrita da 0.
  `read32/write32` arman/desarman las palabras con el byte menos significativo en la dirección menor.
* **CPU:** `Cpu::step()` lee `mem[PC]`, extrae los campos, ejecuta con un `switch` por opcode y
  `funct3/funct7`, escribe `rd` (nunca `x0`) y actualiza el PC. Si la instrucción es inválida o un
  salto/PC está desalineado lanza `SimError` **sin modificar el estado**, y la interfaz lo reporta.
* **Fin de programa:** se detecta `ecall exit` (a7 = 10, 17 o 93), `ebreak`, o una instrucción que
  salta a sí misma (`j .`, `beq x2, x2, done`), que es como terminan los programas de prueba.

### Environment calls (`ecall`, a7 = servicio)

| a7 | Servicio | a7 | Servicio |
|---|---|---|---|
| 1 | print_int(a0) | 11 | print_char(a0) |
| 4 | print_string(a0) | 12 | a0 = read_char() |
| 5 | a0 = read_int() | 17, 93 | exit con código a0 |
| 8 | read_string(a0 = buffer, a1 = tamaño) | 34/35/36 | print hex / binario / unsigned |
| 10 | exit (código 0) | | |

## Pruebas

```bash
make tests     # ensambla tests/ con clang (target riscv32) y ejecuta tests/run_tests.sh
```

Los `.bin` y `.elf` ya compilados están en `tests/bin/`, así que se pueden probar sin toolchain.

| Programa | Qué verifica | Resultado esperado |
|---|---|---|
| `riscvtest` (Harris & Harris) | add, sub, and, or, slt, addi, lw, sw, beq, jal | `mem[0x64] = 25` |
| `quicksort` | recursión, pila, lw/sw, saltos | `{1,2,3,4,6,8,9}` en `0x1000` |
| `symmetric` | recursión con 4 registros salvados | `a0 = 1` |
| `isa_test` | **las 37 instrucciones** del anexo A (36 comprobaciones, incluye casos de signo, little-endian y `x0`) | exit 0 y `0x600DC0DE` en `0x2000` |
| `io_demo` | ecall: print_string, read_int, print_int, print_char; lbu/sb | `fib(10) = 55`, `RISC-V ES GENIAL` |
| `sort_c` | programa **en C** compilado con `clang -O2 --target=riscv32` | arreglo ordenado, exit 140 |
| `mext_test` | extensión M y sus casos borde (división por 0, overflow) | exit 0 |

Para obtener binarios desde CPUlator: *Compile and Load* → pestaña *Memory* → panel derecho →
*Format: Raw*, *Start address: 0000* → *Save to file*.
