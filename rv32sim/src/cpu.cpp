// cpu.cpp — Ejecución de instrucciones RV32I (+ extensión M opcional).
#include "cpu.hpp"

#include <cstdio>
#include <limits>
#include <sstream>

#include "decode.hpp"

using namespace rv;

namespace {

std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", v);
    return buf;
}

}  // namespace

Cpu::Cpu() { reset(0); }

void Cpu::reset(uint32_t entry_pc) {
    for (auto& r : x_) r = 0;
    pc_ = entry_pc;
    halted_ = false;
    exit_code_ = 0;
    instret_ = 0;
}

// Escribe en rd y registra el cambio (para mostrarlo). Escribir en x0 no hace
// nada: en RISC-V x0 está "cableado" a cero.
void Cpu::write_rd(StepInfo& info, int rd_idx, uint32_t value) {
    if (rd_idx == 0) return;
    info.reg_written = true;
    info.rd = rd_idx;
    info.old_val = x_[rd_idx];
    info.new_val = value;
    x_[rd_idx] = value;
}

StepInfo Cpu::step() {
    if (halted_)
        throw SimError(pc_, "el programa ya terminó (usa 'reset' para reiniciarlo)");

    // ------------------------------------------------------------------ FETCH
    // Sin la extensión C (instrucciones comprimidas), todas las instrucciones
    // están alineadas a 4 bytes.
    if (pc_ & 0x3)
        throw SimError(pc_, "PC desalineado " + hex32(pc_) + " (debe ser múltiplo de 4)");

    const uint32_t inst = mem_.read32(pc_);

    StepInfo info;
    info.pc = pc_;
    info.inst = inst;

    // ----------------------------------------------------------------- DECODE
    const uint32_t op = opcode(inst);
    const int      d  = rd(inst);
    const uint32_t f3 = funct3(inst);
    const uint32_t f7 = funct7(inst);
    const uint32_t a  = x_[rs1(inst)];   // valor de rs1
    const uint32_t b  = x_[rs2(inst)];   // valor de rs2

    uint32_t next_pc = pc_ + 4;          // por defecto: siguiente instrucción

    auto illegal = [&]() -> SimError {
        return SimError(pc_, "instrucción ilegal " + hex32(inst) + " en " + hex32(pc_));
    };
    auto check_target = [&](uint32_t target) {
        if (target & 0x3)
            throw SimError(pc_, "salto a dirección desalineada " + hex32(target));
    };

    // ---------------------------------------------------------------- EXECUTE
    switch (op) {
    // ---------------------------------------------------- LUI / AUIPC (tipo U)
    case OP_LUI:   // rd = imm << 12
        write_rd(info, d, static_cast<uint32_t>(imm_u(inst)));
        break;

    case OP_AUIPC: // rd = PC + (imm << 12)
        write_rd(info, d, pc_ + static_cast<uint32_t>(imm_u(inst)));
        break;

    // ------------------------------------------------------- JAL / JALR (saltos)
    case OP_JAL: { // rd = PC + 4; PC = PC + offset
        uint32_t target = pc_ + static_cast<uint32_t>(imm_j(inst));
        check_target(target);
        write_rd(info, d, pc_ + 4);
        next_pc = target;
        break;
    }

    case OP_JALR: { // rd = PC + 4; PC = (rs1 + imm) & ~1
        if (f3 != 0) throw illegal();
        uint32_t target = (a + static_cast<uint32_t>(imm_i(inst))) & ~1u;
        check_target(target);
        write_rd(info, d, pc_ + 4);
        next_pc = target;
        break;
    }

    // ---------------------------------------------- Saltos condicionales (tipo B)
    case OP_BRANCH: {
        bool take;
        switch (f3) {
        case 0: take = (a == b); break;                                         // beq
        case 1: take = (a != b); break;                                         // bne
        case 4: take = (static_cast<int32_t>(a) <  static_cast<int32_t>(b)); break; // blt
        case 5: take = (static_cast<int32_t>(a) >= static_cast<int32_t>(b)); break; // bge
        case 6: take = (a <  b); break;                                         // bltu
        case 7: take = (a >= b); break;                                         // bgeu
        default: throw illegal();
        }
        info.is_branch = true;
        info.taken = take;
        if (take) {
            uint32_t target = pc_ + static_cast<uint32_t>(imm_b(inst));
            check_target(target);
            next_pc = target;
        }
        break;
    }

    // --------------------------------------------------------- Loads (tipo I)
    case OP_LOAD: {
        uint32_t addr = a + static_cast<uint32_t>(imm_i(inst));
        uint32_t v;
        switch (f3) {
        case 0: v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(mem_.read8(addr))));   break; // lb
        case 1: v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(mem_.read16(addr)))); break; // lh
        case 2: v = mem_.read32(addr); break;                                                               // lw
        case 4: v = mem_.read8(addr);  break;                                                               // lbu
        case 5: v = mem_.read16(addr); break;                                                               // lhu
        default: throw illegal();
        }
        write_rd(info, d, v);
        break;
    }

    // -------------------------------------------------------- Stores (tipo S)
    case OP_STORE: {
        uint32_t addr = a + static_cast<uint32_t>(imm_s(inst));
        int size;
        switch (f3) {
        case 0: size = 1; mem_.write8(addr,  static_cast<uint8_t>(b));  break; // sb
        case 1: size = 2; mem_.write16(addr, static_cast<uint16_t>(b)); break; // sh
        case 2: size = 4; mem_.write32(addr, b);                        break; // sw
        default: throw illegal();
        }
        info.mem_written = true;
        info.mem_addr = addr;
        info.mem_size = size;
        info.mem_val = (size == 4) ? b : (b & ((1u << (8 * size)) - 1));
        break;
    }

    // ------------------------------------------- Operaciones con inmediato (tipo I)
    case OP_IMM: {
        const int32_t  imm   = imm_i(inst);
        const uint32_t shamt = static_cast<uint32_t>(rs2(inst));  // inst[24:20]
        uint32_t v;
        switch (f3) {
        case 0: v = a + static_cast<uint32_t>(imm); break;                                   // addi
        case 2: v = (static_cast<int32_t>(a) < imm) ? 1 : 0; break;                          // slti
        case 3: v = (a < static_cast<uint32_t>(imm)) ? 1 : 0; break;                         // sltiu
        case 4: v = a ^ static_cast<uint32_t>(imm); break;                                   // xori
        case 6: v = a | static_cast<uint32_t>(imm); break;                                   // ori
        case 7: v = a & static_cast<uint32_t>(imm); break;                                   // andi
        case 1:                                                                               // slli
            if (f7 != 0x00) throw illegal();
            v = a << shamt;
            break;
        case 5:
            if (f7 == 0x00)      v = a >> shamt;                                              // srli
            else if (f7 == 0x20) v = static_cast<uint32_t>(static_cast<int32_t>(a) >> shamt); // srai
            else throw illegal();
            break;
        default: throw illegal();
        }
        write_rd(info, d, v);
        break;
    }

    // ------------------------------------------ Operaciones registro-registro (tipo R)
    case OP_REG: {
        uint32_t v;
        const uint32_t sh = b & 0x1F;   // en RV32 solo cuentan los 5 bits bajos
        if (f7 == 0x00) {
            switch (f3) {
            case 0: v = a + b; break;                                                         // add
            case 1: v = a << sh; break;                                                       // sll
            case 2: v = (static_cast<int32_t>(a) < static_cast<int32_t>(b)) ? 1 : 0; break;   // slt
            case 3: v = (a < b) ? 1 : 0; break;                                               // sltu
            case 4: v = a ^ b; break;                                                         // xor
            case 5: v = a >> sh; break;                                                       // srl
            case 6: v = a | b; break;                                                         // or
            case 7: v = a & b; break;                                                         // and
            default: throw illegal();
            }
        } else if (f7 == 0x20) {
            switch (f3) {
            case 0: v = a - b; break;                                                         // sub
            case 5: v = static_cast<uint32_t>(static_cast<int32_t>(a) >> sh); break;          // sra
            default: throw illegal();
            }
        } else if (f7 == 0x01 && m_ext_) {
            // ---- Extensión M (extra, no obligatoria) ----
            const int64_t  sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
            const uint64_t ua = a, ub = b;
            const int32_t  ia = static_cast<int32_t>(a), ib = static_cast<int32_t>(b);
            const int32_t  INT_MIN32 = std::numeric_limits<int32_t>::min();
            switch (f3) {
            case 0: v = static_cast<uint32_t>(sa * sb); break;                                         // mul
            case 1: v = static_cast<uint32_t>(static_cast<uint64_t>(sa * sb) >> 32); break;            // mulh
            case 2: v = static_cast<uint32_t>(static_cast<uint64_t>(sa * static_cast<int64_t>(ub)) >> 32); break; // mulhsu
            case 3: v = static_cast<uint32_t>((ua * ub) >> 32); break;                                 // mulhu
            case 4: // div (casos borde definidos por la especificación)
                if (ib == 0) v = 0xFFFFFFFFu;
                else if (ia == INT_MIN32 && ib == -1) v = static_cast<uint32_t>(INT_MIN32);
                else v = static_cast<uint32_t>(ia / ib);
                break;
            case 5: v = (b == 0) ? 0xFFFFFFFFu : a / b; break;                                         // divu
            case 6: // rem
                if (ib == 0) v = a;
                else if (ia == INT_MIN32 && ib == -1) v = 0;
                else v = static_cast<uint32_t>(ia % ib);
                break;
            case 7: v = (b == 0) ? a : a % b; break;                                                   // remu
            default: throw illegal();
            }
        } else {
            throw illegal();
        }
        write_rd(info, d, v);
        break;
    }

    // -------------------------------------------------------------- FENCE
    case OP_FENCE:
        // En un simulador secuencial de un solo hilo no hay nada que ordenar.
        break;

    // -------------------------------------------------------- ECALL / EBREAK
    case OP_SYSTEM:
        if (inst == 0x00000073) {          // ecall
            exec_ecall(info);
        } else if (inst == 0x00100073) {   // ebreak
            info.event = StepEvent::Ebreak;
        } else {
            throw illegal();
        }
        break;

    default:
        throw illegal();
    }

    // ------------------------------------------------------------- WRITEBACK
    x_[0] = 0;          // por si acaso: x0 siempre es 0
    pc_ = next_pc;
    info.next_pc = next_pc;
    ++instret_;
    return info;
}

// ----------------------------------------------------------------------------
// Environment calls (subconjunto de las syscalls de SPIM/CPUlator).
// Convención: a7 (x17) = número de servicio, a0 (x10)/a1 (x11) = argumentos.
// ----------------------------------------------------------------------------
void Cpu::exec_ecall(StepInfo& info) {
    const uint32_t service = x_[17];   // a7
    const uint32_t a0 = x_[10];
    const uint32_t a1 = x_[11];
    std::ostream& out = *out_;
    std::istream& in = *in_;

    switch (service) {
    case 1:   // print_int
        out << static_cast<int32_t>(a0) << std::flush;
        break;
    case 4: { // print_string (a0 = dirección de un string terminado en '\0')
        std::string s;
        for (uint32_t p = a0; s.size() < (1u << 20); ++p) {
            char c = static_cast<char>(mem_.read8(p));
            if (c == '\0') break;
            s.push_back(c);
        }
        out << s << std::flush;
        break;
    }
    case 5: { // read_int -> a0
        long long v = 0;
        std::string line;
        if (std::getline(in, line)) {
            std::istringstream ss(line);
            ss >> v;
        }
        write_rd(info, 10, static_cast<uint32_t>(v));
        break;
    }
    case 8: { // read_string (a0 = buffer, a1 = tamaño máximo incluyendo '\0')
        std::string line;
        std::getline(in, line);
        if (a1 == 0) break;
        uint32_t n = 0;
        for (; n + 1 < a1 && n < line.size(); ++n)
            mem_.write8(a0 + n, static_cast<uint8_t>(line[n]));
        if (n + 1 < a1) mem_.write8(a0 + n++, '\n');
        mem_.write8(a0 + n, 0);
        break;
    }
    case 10:  // exit
        halted_ = true;
        exit_code_ = 0;
        info.event = StepEvent::Exit;
        info.exit_code = 0;
        break;
    case 11:  // print_char
        out << static_cast<char>(a0 & 0xFF) << std::flush;
        break;
    case 12: { // read_char -> a0
        int c = in.get();
        write_rd(info, 10, c == EOF ? 0xFFFFFFFFu : static_cast<uint32_t>(c));
        break;
    }
    case 17:  // exit2 (a0 = código de salida)
    case 93:  // exit estilo Linux (a0 = código de salida)
        halted_ = true;
        exit_code_ = static_cast<int32_t>(a0);
        info.event = StepEvent::Exit;
        info.exit_code = exit_code_;
        break;
    case 34: { // print_int_hex
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08x", a0);
        out << buf << std::flush;
        break;
    }
    case 35: { // print_int_binary
        std::string s;
        for (int i = 31; i >= 0; --i) s.push_back(((a0 >> i) & 1) ? '1' : '0');
        out << s << std::flush;
        break;
    }
    case 36:  // print_int_unsigned
        out << a0 << std::flush;
        break;
    default:
        (void)a1;
        throw SimError(pc_, "ecall con servicio desconocido a7=" + std::to_string(service));
    }
}
