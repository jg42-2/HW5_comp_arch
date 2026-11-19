// cpu.hpp — Estado arquitectural de una CPU RV32I y ejecución de instrucciones.
//
// El "estado arquitectural" es todo lo que un programa puede observar:
//   * el contador de programa (PC),
//   * los 32 registros enteros x0..x31 (x0 siempre vale 0),
//   * la memoria.
// Ejecutar una instrucción = fetch (leer mem[PC]) -> decode -> execute ->
// actualizar el estado (registros, memoria y PC).
#pragma once

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "memory.hpp"

// Error de ejecución (instrucción ilegal, PC desalineado, etc.).
struct SimError : std::runtime_error {
    uint32_t pc;
    SimError(uint32_t pc_, const std::string& msg) : std::runtime_error(msg), pc(pc_) {}
};

// Qué pasó "especial" al ejecutar la instrucción.
enum class StepEvent {
    None,    // instrucción normal
    Exit,    // el programa pidió terminar (ecall exit)
    Ebreak,  // instrucción ebreak (breakpoint por software)
};

// Información de lo que hizo una instrucción, para mostrársela al usuario.
struct StepInfo {
    uint32_t pc = 0;          // dirección de la instrucción ejecutada
    uint32_t inst = 0;        // código de máquina
    uint32_t next_pc = 0;     // PC después de ejecutar

    bool reg_written = false; // ¿se modificó un registro?
    int rd = 0;
    uint32_t old_val = 0, new_val = 0;

    bool mem_written = false; // ¿se escribió memoria? (stores)
    uint32_t mem_addr = 0;
    int mem_size = 0;         // 1, 2 o 4 bytes
    uint32_t mem_val = 0;

    bool is_branch = false;   // ¿era un salto condicional?
    bool taken = false;       // ¿se tomó?

    StepEvent event = StepEvent::None;
    int exit_code = 0;
};

class Cpu {
public:
    Cpu();

    // Pone registros y PC en su valor inicial (no toca la memoria).
    void reset(uint32_t entry_pc = 0);

    // Ejecuta UNA instrucción. Lanza SimError si algo sale mal; en ese caso
    // el estado queda como estaba ANTES de la instrucción fallida.
    StepInfo step();

    // ---- Acceso al estado ------------------------------------------------
    uint32_t pc() const { return pc_; }
    void set_pc(uint32_t v) { pc_ = v; }

    uint32_t reg(int i) const { return x_[i & 31]; }
    void set_reg(int i, uint32_t v) { if ((i & 31) != 0) x_[i & 31] = v; }

    Memory& mem() { return mem_; }
    const Memory& mem() const { return mem_; }

    bool halted() const { return halted_; }
    int exit_code() const { return exit_code_; }
    uint64_t instret() const { return instret_; }  // instrucciones ejecutadas

    // Flujos para las environment calls (por defecto stdin/stdout).
    void set_io(std::istream* in, std::ostream* out) { in_ = in; out_ = out; }

    // Habilita/deshabilita la extensión M (mul/div). Por defecto: habilitada.
    void set_m_extension(bool on) { m_ext_ = on; }

private:
    void exec_ecall(StepInfo& info);
    void write_rd(StepInfo& info, int rd, uint32_t value);

    uint32_t pc_ = 0;
    uint32_t x_[32] = {};
    Memory mem_;

    bool halted_ = false;
    int exit_code_ = 0;
    uint64_t instret_ = 0;
    bool m_ext_ = true;

    std::istream* in_ = &std::cin;
    std::ostream* out_ = &std::cout;
};
