// loader.hpp — Carga de programas a la memoria del simulador.
//
// Formatos soportados:
//   * Binario crudo (.bin): los bytes del archivo se copian tal cual a partir
//     de la dirección 0x00000000 (formato "Raw" que exporta CPUlator).
//   * ELF de 32 bits para RISC-V (.elf): se cargan los segmentos PT_LOAD en
//     sus direcciones, se toma el punto de entrada del encabezado y se lee la
//     tabla de símbolos (para ver etiquetas en el desensamblador).
#pragma once

#include <cstdint>
#include <string>

#include "memory.hpp"
#include "symbols.hpp"

struct LoadResult {
    std::string format;      // "raw" o "elf"
    uint32_t entry = 0;      // PC inicial
    uint32_t low = 0;        // rango de direcciones ocupado por el programa
    uint32_t high = 0;       // (high es exclusivo)
    size_t bytes = 0;        // bytes cargados
};

// Lanza std::runtime_error si el archivo no existe o es inválido.
LoadResult load_program(const std::string& path, Memory& mem, SymbolTable& syms,
                        uint32_t raw_base = 0x00000000);
