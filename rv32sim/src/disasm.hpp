// disasm.hpp — Desensamblador sencillo de RV32I(M).
#pragma once

#include <cstdint>
#include <string>

#include "symbols.hpp"

namespace rv {

// Nombre ABI de un registro: 0 -> "zero", 1 -> "ra", 2 -> "sp", 10 -> "a0"...
const char* abi_name(int reg);

// Convierte "x5", "t0", "fp", "zero"... al índice 0..31. Devuelve -1 si no es válido.
int parse_reg(const std::string& name);

// Traduce una instrucción a texto, p. ej. "addi    sp, sp, -16".
// `pc` se usa para calcular destinos absolutos de saltos.
// Si la instrucción corresponde a una pseudo-instrucción conocida (li, mv,
// ret, j, call...), se agrega como comentario.
std::string disassemble(uint32_t inst, uint32_t pc, const SymbolTable* syms = nullptr);

}  // namespace rv
