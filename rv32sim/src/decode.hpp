// decode.hpp — Extracción de campos e inmediatos de una instrucción RV32.
//
// Todas las instrucciones RV32I miden 32 bits. Los campos rd, rs1, rs2,
// funct3 y funct7 SIEMPRE están en la misma posición, lo que simplifica el
// decodificador. Lo que cambia entre formatos (R, I, S, B, U, J) es cómo
// está "repartido" el inmediato dentro de la instrucción.
//
//  31        25 24   20 19   15 14  12 11        7 6      0
// [  funct7   ][  rs2  ][  rs1  ][ f3 ][    rd    ][ opcode ]   R
// [      imm[11:0]     ][  rs1  ][ f3 ][    rd    ][ opcode ]   I
// [ imm[11:5] ][  rs2  ][  rs1  ][ f3 ][ imm[4:0] ][ opcode ]   S
// [imm[12|10:5]][ rs2  ][  rs1  ][ f3 ][imm[4:1|11]][opcode ]   B
// [            imm[31:12]              ][    rd    ][ opcode ]   U
// [     imm[20|10:1|11|19:12]          ][    rd    ][ opcode ]   J
#pragma once

#include <cstdint>

namespace rv {

// Opcodes (bits [6:0]) de RV32I.
enum Opcode : uint32_t {
    OP_LOAD   = 0x03,
    OP_FENCE  = 0x0F,
    OP_IMM    = 0x13,
    OP_AUIPC  = 0x17,
    OP_STORE  = 0x23,
    OP_REG    = 0x33,
    OP_LUI    = 0x37,
    OP_BRANCH = 0x63,
    OP_JALR   = 0x67,
    OP_JAL    = 0x6F,
    OP_SYSTEM = 0x73,
};

inline uint32_t opcode(uint32_t i) { return i & 0x7F; }
inline int      rd(uint32_t i)     { return (i >> 7) & 0x1F; }
inline uint32_t funct3(uint32_t i) { return (i >> 12) & 0x7; }
inline int      rs1(uint32_t i)    { return (i >> 15) & 0x1F; }
inline int      rs2(uint32_t i)    { return (i >> 20) & 0x1F; }
inline uint32_t funct7(uint32_t i) { return i >> 25; }

// Los inmediatos se "extienden en signo": el bit 31 de la instrucción es
// siempre el bit de signo del inmediato. Hacer el shift sobre int32_t
// (shift aritmético) replica ese bit hacia la izquierda automáticamente.

// Tipo I: imm[11:0] = inst[31:20]
inline int32_t imm_i(uint32_t i) { return static_cast<int32_t>(i) >> 20; }

// Tipo S: imm[11:5] = inst[31:25], imm[4:0] = inst[11:7]
inline int32_t imm_s(uint32_t i) {
    return (static_cast<int32_t>(i & 0xFE000000u) >> 20)
         | static_cast<int32_t>((i >> 7) & 0x1F);
}

// Tipo B: imm[12]=inst[31], imm[11]=inst[7], imm[10:5]=inst[30:25],
//         imm[4:1]=inst[11:8], imm[0]=0 (los saltos son múltiplos de 2)
inline int32_t imm_b(uint32_t i) {
    return (static_cast<int32_t>(i & 0x80000000u) >> 19)
         | static_cast<int32_t>((i & 0x80u) << 4)
         | static_cast<int32_t>((i >> 20) & 0x7E0u)
         | static_cast<int32_t>((i >> 7) & 0x1Eu);
}

// Tipo U: imm[31:12] = inst[31:12], los 12 bits bajos en cero.
inline int32_t imm_u(uint32_t i) { return static_cast<int32_t>(i & 0xFFFFF000u); }

// Tipo J: imm[20]=inst[31], imm[19:12]=inst[19:12], imm[11]=inst[20],
//         imm[10:1]=inst[30:21], imm[0]=0
inline int32_t imm_j(uint32_t i) {
    return (static_cast<int32_t>(i & 0x80000000u) >> 11)
         | static_cast<int32_t>(i & 0x000FF000u)
         | static_cast<int32_t>((i >> 9) & 0x800u)
         | static_cast<int32_t>((i >> 20) & 0x7FEu);
}

}  // namespace rv
