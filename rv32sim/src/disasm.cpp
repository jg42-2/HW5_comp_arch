// disasm.cpp — Desensamblador sencillo de RV32I(M).
#include "disasm.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "decode.hpp"

namespace rv {

namespace {

const char* const ABI[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

#if defined(__GNUC__)
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
#endif
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string target(uint32_t addr, const SymbolTable* syms) {
    std::string s = fmt("0x%08x", addr);
    if (syms) {
        std::string d = syms->describe(addr);
        if (!d.empty()) s += " <" + d + ">";
    }
    return s;
}

// Mnemónico rellenado a 8 caracteres + operandos.
std::string ins(const char* mnem, const std::string& ops) {
    return fmt("%-8s%s", mnem, ops.c_str());
}

std::string with_hint(const std::string& base, const std::string& hint) {
    if (hint.empty()) return base;
    return fmt("%-34s# %s", base.c_str(), hint.c_str());
}

}  // namespace

const char* abi_name(int reg) { return ABI[reg & 31]; }

int parse_reg(const std::string& name) {
    if (name.empty()) return -1;
    if (name == "fp") return 8;                 // fp es alias de s0
    if (name[0] == 'x' && name.size() >= 2) {
        char* end = nullptr;
        long v = std::strtol(name.c_str() + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v < 32) return static_cast<int>(v);
    }
    for (int i = 0; i < 32; ++i)
        if (name == ABI[i]) return i;
    return -1;
}

std::string disassemble(uint32_t inst, uint32_t pc, const SymbolTable* syms) {
    const uint32_t op = opcode(inst);
    const int d = rd(inst), s1 = rs1(inst), s2 = rs2(inst);
    const uint32_t f3 = funct3(inst), f7 = funct7(inst);
    const char* RD = ABI[d];
    const char* RS1 = ABI[s1];
    const char* RS2 = ABI[s2];
    const std::string unknown = fmt(".word   0x%08x  (instrucción desconocida)", inst);

    switch (op) {
    case OP_LUI:
        return ins("lui", fmt("%s, 0x%x", RD, inst >> 12));
    case OP_AUIPC:
        return ins("auipc", fmt("%s, 0x%x", RD, inst >> 12));

    case OP_JAL: {
        uint32_t t = pc + static_cast<uint32_t>(imm_j(inst));
        std::string base = ins("jal", fmt("%s, %s", RD, target(t, syms).c_str()));
        if (d == 0) return with_hint(base, "j " + target(t, syms));
        return base;
    }
    case OP_JALR: {
        if (f3 != 0) return unknown;
        int32_t imm = imm_i(inst);
        std::string base = ins("jalr", fmt("%s, %d(%s)", RD, imm, RS1));
        if (d == 0 && s1 == 1 && imm == 0) return with_hint(base, "ret");
        if (d == 0 && imm == 0) return with_hint(base, fmt("jr %s", RS1));
        return base;
    }

    case OP_BRANCH: {
        static const char* const names[8] = {"beq", "bne", nullptr, nullptr,
                                             "blt", "bge", "bltu", "bgeu"};
        if (!names[f3]) return unknown;
        uint32_t t = pc + static_cast<uint32_t>(imm_b(inst));
        std::string base = ins(names[f3], fmt("%s, %s, %s", RS1, RS2, target(t, syms).c_str()));
        if (s2 == 0 && f3 == 0) return with_hint(base, fmt("beqz %s", RS1));
        if (s2 == 0 && f3 == 1) return with_hint(base, fmt("bnez %s", RS1));
        return base;
    }

    case OP_LOAD: {
        static const char* const names[8] = {"lb", "lh", "lw", nullptr,
                                             "lbu", "lhu", nullptr, nullptr};
        if (!names[f3]) return unknown;
        return ins(names[f3], fmt("%s, %d(%s)", RD, imm_i(inst), RS1));
    }
    case OP_STORE: {
        static const char* const names[8] = {"sb", "sh", "sw", nullptr,
                                             nullptr, nullptr, nullptr, nullptr};
        if (!names[f3]) return unknown;
        return ins(names[f3], fmt("%s, %d(%s)", RS2, imm_s(inst), RS1));
    }

    case OP_IMM: {
        int32_t imm = imm_i(inst);
        switch (f3) {
        case 0: {
            std::string base = ins("addi", fmt("%s, %s, %d", RD, RS1, imm));
            if (d == 0 && s1 == 0 && imm == 0) return with_hint(base, "nop");
            if (s1 == 0) return with_hint(base, fmt("li %s, %d", RD, imm));
            if (imm == 0) return with_hint(base, fmt("mv %s, %s", RD, RS1));
            return base;
        }
        case 2: return ins("slti", fmt("%s, %s, %d", RD, RS1, imm));
        case 3: {
            std::string base = ins("sltiu", fmt("%s, %s, %d", RD, RS1, imm));
            if (imm == 1) return with_hint(base, fmt("seqz %s, %s", RD, RS1));
            return base;
        }
        case 4: {
            std::string base = ins("xori", fmt("%s, %s, %d", RD, RS1, imm));
            if (imm == -1) return with_hint(base, fmt("not %s, %s", RD, RS1));
            return base;
        }
        case 6: return ins("ori", fmt("%s, %s, %d", RD, RS1, imm));
        case 7: return ins("andi", fmt("%s, %s, %d", RD, RS1, imm));
        case 1:
            if (f7 != 0) return unknown;
            return ins("slli", fmt("%s, %s, %d", RD, RS1, s2));
        case 5:
            if (f7 == 0x00) return ins("srli", fmt("%s, %s, %d", RD, RS1, s2));
            if (f7 == 0x20) return ins("srai", fmt("%s, %s, %d", RD, RS1, s2));
            return unknown;
        }
        return unknown;
    }

    case OP_REG: {
        const char* m = nullptr;
        if (f7 == 0x00) {
            static const char* const n[8] = {"add", "sll", "slt", "sltu", "xor", "srl", "or", "and"};
            m = n[f3];
        } else if (f7 == 0x20) {
            if (f3 == 0) m = "sub";
            if (f3 == 5) m = "sra";
        } else if (f7 == 0x01) {
            static const char* const n[8] = {"mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem", "remu"};
            m = n[f3];
        }
        if (!m) return unknown;
        std::string base = ins(m, fmt("%s, %s, %s", RD, RS1, RS2));
        if (f7 == 0x20 && f3 == 0 && s1 == 0) return with_hint(base, fmt("neg %s, %s", RD, RS2));
        if (f7 == 0x00 && f3 == 3 && s1 == 0) return with_hint(base, fmt("snez %s, %s", RD, RS2));
        return base;
    }

    case OP_FENCE:
        return "fence";

    case OP_SYSTEM:
        if (inst == 0x00000073) return "ecall";
        if (inst == 0x00100073) return "ebreak";
        return unknown;
    }
    return unknown;
}

}  // namespace rv
