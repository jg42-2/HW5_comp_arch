// main.cpp — Interfaz de línea de comandos (REPL) del simulador RV32.
//
// Uso:  rv32sim [opciones] programa.bin|programa.elf
// Escribe "help" dentro del simulador para ver los comandos disponibles.
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cpu.hpp"
#include "disasm.hpp"
#include "loader.hpp"
#include "symbols.hpp"

namespace {

// ============================================================================
// Utilidades de formato y colores ANSI
// ============================================================================
bool g_color = true;

const char* C(const char* code) { return g_color ? code : ""; }
#define RESET  C("\033[0m")
#define BOLD   C("\033[1m")
#define DIM    C("\033[2m")
#define RED    C("\033[31m")
#define GREEN  C("\033[32m")
#define YELLOW C("\033[33m")
#define BLUE   C("\033[34m")
#define CYAN   C("\033[36m")

std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

std::string hex2(uint8_t v) {
    char b[4];
    std::snprintf(b, sizeof b, "%02X", v);
    return b;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream ss(s);
    std::vector<std::string> out;
    std::string t;
    while (ss >> t) out.push_back(t);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================================
// Simulador: CPU + programa cargado + estado de la interfaz
// ============================================================================
class Simulator {
public:
    Cpu cpu;
    SymbolTable syms;
    LoadResult prog;
    std::string path;
    uint32_t raw_base = 0;
    std::set<uint32_t> breakpoints;
    bool trace = false;
    uint64_t max_steps = 10'000'000;
    uint32_t prev_regs[33] = {};   // foto de registros antes del último step/run

    bool load(const std::string& file) {
        Memory fresh;
        SymbolTable fresh_syms;
        LoadResult r;
        try {
            r = load_program(file, fresh, fresh_syms, raw_base);
        } catch (const std::exception& e) {
            std::cout << RED << "Error: " << e.what() << RESET << "\n";
            return false;
        }
        cpu.mem() = std::move(fresh);
        syms = std::move(fresh_syms);
        prog = r;
        path = file;
        cpu.reset(r.entry);
        snapshot();
        std::cout << "\"" << file << "\" cargado a memoria (" << r.format << ", " << r.bytes
                  << " bytes en " << hex8(r.low) << "-" << hex8(r.high ? r.high - 1 : 0) << ").\n";
        std::cout << "PC inicial = " << hex8(r.entry);
        if (!syms.empty()) std::cout << "  (" << syms.all().size() << " símbolos cargados)";
        std::cout << "\n";
        return true;
    }

    void snapshot() {
        for (int i = 0; i < 32; ++i) prev_regs[i] = cpu.reg(i);
        prev_regs[32] = cpu.pc();
    }

    // Convierte un texto a número. Acepta: 0x1F, 42, -3, 0b101, un registro
    // (t0, x5, sp...: usa su valor actual), "pc" o una etiqueta del ELF.
    bool parse_value(const std::string& tok, uint32_t& out) const {
        if (tok.empty()) return false;
        if (tok == "pc") { out = cpu.pc(); return true; }
        int r = rv::parse_reg(tok);
        if (r >= 0) { out = cpu.reg(r); return true; }
        if (auto s = syms.lookup(tok)) { out = *s; return true; }
        const char* p = tok.c_str();
        bool neg = false;
        if (*p == '-') { neg = true; ++p; }
        int base = 10;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
        if (*p == '\0') return false;
        char* end = nullptr;
        unsigned long long v = std::strtoull(p, &end, base);
        if (*end != '\0') return false;
        out = static_cast<uint32_t>(neg ? (0 - v) : v);
        return true;
    }

    // ------------------------------------------------------------------ Salida
    std::string label_line(uint32_t addr) const {
        if (const std::string* n = syms.name_at(addr)) return std::string(CYAN) + "<" + *n + ">:" + RESET;
        return "";
    }

    void print_inst_line(uint32_t addr, const char* prefix) const {
        uint32_t inst = cpu.mem().read32(addr);
        std::printf("%s%s%s %s%s%s: %s%08x%s  %s\n", prefix,
                    breakpoints.count(addr) ? RED : "", breakpoints.count(addr) ? "*" : " ", RESET,
                    hex8(addr).c_str(), "", DIM, inst, RESET,
                    rv::disassemble(inst, addr, &syms).c_str());
    }

    void print_step(const StepInfo& s) const {
        std::string line = std::string(DIM) + hex8(s.pc) + RESET + ": " + rv::disassemble(s.inst, s.pc, &syms);
        std::cout << "  " << line;
        std::vector<std::string> effects;
        if (s.reg_written)
            effects.push_back(std::string(YELLOW) + rv::abi_name(s.rd) + RESET + ": " + hex8(s.old_val) +
                              " -> " + BOLD + hex8(s.new_val) + RESET);
        if (s.mem_written) {
            const char* sz = s.mem_size == 1 ? "byte" : s.mem_size == 2 ? "half" : "word";
            effects.push_back(std::string(GREEN) + "mem[" + hex8(s.mem_addr) + "]" + RESET + " <- " +
                              hex8(s.mem_val) + " (" + sz + ")");
        }
        if (s.is_branch) effects.push_back(s.taken ? "salto TOMADO" : "salto no tomado");
        if (s.next_pc != s.pc + 4 && !s.is_branch) effects.push_back("pc -> " + hex8(s.next_pc));
        if (!effects.empty()) {
            std::cout << "\n      " << DIM << "=>" << RESET << " ";
            for (size_t i = 0; i < effects.size(); ++i) std::cout << (i ? ", " : "") << effects[i];
        }
        std::cout << "\n";
    }

    // Reporta eventos especiales después de una instrucción. Devuelve true si
    // la ejecución continua (run) debe detenerse.
    bool report_event(const StepInfo& s) const {
        if (s.event == StepEvent::Exit) {
            std::cout << GREEN << "\n[El programa terminó vía ecall con código " << s.exit_code << "]" << RESET << "\n";
            return true;
        }
        if (s.event == StepEvent::Ebreak) {
            std::cout << YELLOW << "[ebreak en " << hex8(s.pc) << ": ejecución pausada]" << RESET << "\n";
            return true;
        }
        if (s.next_pc == s.pc) {
            std::cout << CYAN << "[Bucle infinito detectado en " << hex8(s.pc)
                      << ": la instrucción salta a sí misma (fin del programa)]" << RESET << "\n";
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------ Comandos
    void cmd_step(uint64_t n) {
        snapshot();
        for (uint64_t i = 0; i < n; ++i) {
            StepInfo s;
            try {
                s = cpu.step();
            } catch (const SimError& e) {
                std::cout << RED << "Error de ejecución: " << e.what() << RESET << "\n";
                return;
            }
            print_step(s);
            if (report_event(s)) return;
            if (i + 1 < n && breakpoints.count(cpu.pc())) {
                std::cout << RED << "Breakpoint en " << hex8(cpu.pc()) << RESET << "\n";
                return;
            }
        }
    }

    void cmd_run(uint64_t limit) {
        snapshot();
        uint64_t count = 0;
        for (; count < limit; ++count) {
            if (count > 0 && breakpoints.count(cpu.pc())) {
                std::cout << RED << "Breakpoint en " << hex8(cpu.pc()) << RESET;
                std::string d = syms.describe(cpu.pc());
                if (!d.empty()) std::cout << " <" << d << ">";
                std::cout << " (tras " << count << " instrucciones)\n";
                print_inst_line(cpu.pc(), "=>");
                return;
            }
            StepInfo s;
            try {
                s = cpu.step();
            } catch (const SimError& e) {
                std::cout << RED << "Error de ejecución: " << e.what() << RESET << "\n";
                return;
            }
            if (trace) print_step(s);
            if (report_event(s)) {
                std::cout << "Instrucciones ejecutadas en este 'run': " << count + 1
                          << " (total: " << cpu.instret() << ")\n";
                return;
            }
        }
        std::cout << YELLOW << "Se alcanzó el límite de " << limit << " instrucciones; PC = " << hex8(cpu.pc())
                  << RESET << "\n";
    }

    void print_reg(int i) const {
        uint32_t v = cpu.reg(i);
        bool changed = v != prev_regs[i];
        std::printf("x%-2d (%-4s) = %s%s%s  (%d)\n", i, rv::abi_name(i), changed ? YELLOW : "",
                    hex8(v).c_str(), RESET, static_cast<int32_t>(v));
    }

    void cmd_regs(const std::vector<std::string>& args) {
        if (args.size() > 1) {
            for (size_t k = 1; k < args.size(); ++k) {
                if (args[k] == "pc") { std::cout << "pc = " << hex8(cpu.pc()) << "\n"; continue; }
                int r = rv::parse_reg(args[k]);
                if (r < 0) { std::cout << RED << "Registro desconocido: " << args[k] << RESET << "\n"; continue; }
                print_reg(r);
            }
            return;
        }
        std::cout << "pc = " << hex8(cpu.pc()) << "\n";
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 4; ++col) {
                int i = row + col * 8;
                uint32_t v = cpu.reg(i);
                bool changed = v != prev_regs[i];
                char name[16];
                std::snprintf(name, sizeof name, "x%d/%s", i, rv::abi_name(i));
                std::printf("%-9s %s%s%s   ", name, changed ? YELLOW : (v ? "" : DIM), hex8(v).c_str(), RESET);
            }
            std::printf("\n");
        }
    }

    void cmd_mem(uint32_t start, uint32_t end) {
        if (end < start) std::swap(start, end);
        uint64_t len = static_cast<uint64_t>(end) - start + 1;
        if (len > 4096) { len = 4096; std::cout << DIM << "(mostrando solo 4096 bytes)" << RESET << "\n"; }
        if (len <= 16) {
            // Formato compacto, como en el ejemplo del enunciado.
            std::cout << "Memoria (" << hex8(start) << "-" << hex8(end) << "):";
            for (uint64_t i = 0; i < len; ++i) std::cout << " " << hex2(cpu.mem().read8(start + i));
            std::cout << "\n";
            return;
        }
        // Volcado hexadecimal de 16 bytes por fila + ASCII.
        uint32_t row_start = start & ~0xFu;
        for (uint64_t a = row_start; a < static_cast<uint64_t>(start) + len; a += 16) {
            std::cout << DIM << hex8(static_cast<uint32_t>(a)) << RESET << ": ";
            std::string ascii;
            for (int k = 0; k < 16; ++k) {
                uint64_t addr = a + k;
                if (addr < start || addr >= start + len) { std::cout << "   "; ascii += ' '; }
                else {
                    uint8_t b = cpu.mem().read8(static_cast<uint32_t>(addr));
                    std::cout << (b ? "" : DIM) << hex2(b) << RESET << " ";
                    ascii += (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
                }
                if (k == 7) std::cout << " ";
            }
            std::cout << " |" << ascii << "|\n";
        }
    }

    void cmd_words(uint32_t start, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t a = start + 4 * i;
            uint32_t v = cpu.mem().read32(a);
            std::printf("%s: %s  (%d)\n", hex8(a).c_str(), hex8(v).c_str(), static_cast<int32_t>(v));
        }
    }

    void cmd_disas(uint32_t start, uint32_t count) {
        start &= ~3u;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t a = start + 4 * i;
            std::string lbl = label_line(a);
            if (!lbl.empty()) std::cout << lbl << "\n";
            print_inst_line(a, a == cpu.pc() ? "=>" : "  ");
        }
    }

    // --------------------------------------------------------- Bucle REPL
    bool execute(const std::string& raw_line);   // false => salir
};

void print_help() {
    std::cout << BOLD << "Comandos disponibles" << RESET << " (los <args> entre [] son opcionales)\n"
        "  Ejecución\n"
        "    step [n]        | s     Ejecuta n instrucciones (por defecto 1) mostrando qué hizo cada una\n"
        "    run [max]       | r, c  Ejecuta hasta: breakpoint, ebreak, ecall exit, bucle infinito, error o max\n"
        "    reset                   Recarga el programa y reinicia PC/registros\n"
        "    load <archivo>          Carga otro programa (binario crudo o ELF)\n"
        "  Inspección\n"
        "    pc                      Muestra el contador de programa\n"
        "    regs [r1 r2 ...]        Todos los registros, o solo los indicados (x5, t0, sp, pc...)\n"
        "    mem <ini> [fin]         Bytes de memoria entre ini y fin (inclusive)\n"
        "    words <ini> [n]         n palabras de 32 bits (little-endian) desde ini\n"
        "    disas [dir] [n]         Desensambla n instrucciones desde dir (por defecto cerca del PC)\n"
        "    info                    Información del programa cargado\n"
        "    syms                    Lista las etiquetas (solo con archivos ELF)\n"
        "  Depuración\n"
        "    break <dir|etiqueta>    | b   Pone un breakpoint\n"
        "    delete <dir|all>        | d   Quita breakpoints\n"
        "    breaks                        Lista los breakpoints\n"
        "    trace [on|off]                Muestra cada instrucción durante 'run'\n"
        "    set <reg|pc> <valor>          Modifica un registro o el PC\n"
        "    poke <dir> <valor> [1|2|4]    Escribe en memoria (por defecto una word)\n"
        "  Otros\n"
        "    help | h, exit | quit | q\n"
        "  Los valores aceptan decimal (42), hex (0x2A), binario (0b101), registros (sp) o etiquetas (_qsort).\n"
        "  Una línea vacía repite el último 'step'.\n";
}

bool Simulator::execute(const std::string& raw_line) {
    std::string line = trim(raw_line);
    if (line.empty() || line[0] == '#') return true;
    auto args = split_ws(line);
    const std::string& cmd = args[0];

    auto need = [&](size_t n) {
        if (args.size() < n + 1) {
            std::cout << RED << "Faltan argumentos. Escribe 'help'." << RESET << "\n";
            return false;
        }
        return true;
    };
    auto val = [&](size_t idx, uint32_t& out) {
        if (!parse_value(args[idx], out)) {
            std::cout << RED << "Valor inválido: " << args[idx] << RESET << "\n";
            return false;
        }
        return true;
    };

    if (cmd == "help" || cmd == "h" || cmd == "?") {
        print_help();
    } else if (cmd == "exit" || cmd == "quit" || cmd == "q") {
        return false;
    } else if (cmd == "step" || cmd == "s" || cmd == "si") {
        uint32_t n = 1;
        if (args.size() > 1 && !val(1, n)) return true;
        cmd_step(n);
    } else if (cmd == "run" || cmd == "r" || cmd == "continue" || cmd == "c") {
        uint32_t lim = static_cast<uint32_t>(max_steps);
        if (args.size() > 1 && !val(1, lim)) return true;
        cmd_run(lim);
    } else if (cmd == "pc") {
        std::cout << "pc = " << hex8(cpu.pc());
        std::string d = syms.describe(cpu.pc());
        if (!d.empty()) std::cout << " <" << d << ">";
        std::cout << "\n";
    } else if (cmd == "regs" || cmd == "reg" || cmd == "registers") {
        cmd_regs(args);
    } else if (cmd == "mem" || cmd == "x") {
        uint32_t a, b;
        if (!need(1) || !val(1, a)) return true;
        b = a + 63;
        if (args.size() > 2 && !val(2, b)) return true;
        cmd_mem(a, b);
    } else if (cmd == "words" || cmd == "memw") {
        uint32_t a, n = 8;
        if (!need(1) || !val(1, a)) return true;
        if (args.size() > 2 && !val(2, n)) return true;
        cmd_words(a, std::min<uint32_t>(n, 1024));
    } else if (cmd == "disas" || cmd == "dis" || cmd == "l") {
        uint32_t a = cpu.pc() >= 8 ? cpu.pc() - 8 : 0, n = 12;
        if (args.size() > 1 && !val(1, a)) return true;
        if (args.size() > 2 && !val(2, n)) return true;
        cmd_disas(a, std::min<uint32_t>(n, 4096));
    } else if (cmd == "break" || cmd == "b") {
        uint32_t a;
        if (!need(1) || !val(1, a)) return true;
        breakpoints.insert(a & ~3u);
        std::cout << "Breakpoint en " << hex8(a & ~3u) << "\n";
    } else if (cmd == "delete" || cmd == "d") {
        if (args.size() < 2 || args[1] == "all") { breakpoints.clear(); std::cout << "Breakpoints eliminados.\n"; }
        else {
            uint32_t a;
            if (!val(1, a)) return true;
            std::cout << (breakpoints.erase(a & ~3u) ? "Breakpoint eliminado.\n" : "No había breakpoint ahí.\n");
        }
    } else if (cmd == "breaks" || cmd == "bl") {
        if (breakpoints.empty()) std::cout << "No hay breakpoints.\n";
        for (uint32_t a : breakpoints) {
            std::cout << "  " << hex8(a);
            std::string d = syms.describe(a);
            if (!d.empty()) std::cout << " <" << d << ">";
            std::cout << "\n";
        }
    } else if (cmd == "trace") {
        if (args.size() > 1) trace = (args[1] == "on" || args[1] == "1");
        else trace = !trace;
        std::cout << "trace " << (trace ? "activado" : "desactivado") << "\n";
    } else if (cmd == "set") {
        uint32_t v;
        if (!need(2) || !val(2, v)) return true;
        if (args[1] == "pc") { cpu.set_pc(v); std::cout << "pc = " << hex8(v) << "\n"; }
        else {
            int r = rv::parse_reg(args[1]);
            if (r < 0) { std::cout << RED << "Registro desconocido: " << args[1] << RESET << "\n"; return true; }
            if (r == 0) { std::cout << "x0 siempre vale 0 (no se puede modificar).\n"; return true; }
            cpu.set_reg(r, v);
            print_reg(r);
        }
    } else if (cmd == "poke") {
        uint32_t a, v, sz = 4;
        if (!need(2) || !val(1, a) || !val(2, v)) return true;
        if (args.size() > 3 && !val(3, sz)) return true;
        if (sz == 1) cpu.mem().write8(a, static_cast<uint8_t>(v));
        else if (sz == 2) cpu.mem().write16(a, static_cast<uint16_t>(v));
        else cpu.mem().write32(a, v);
        std::cout << "mem[" << hex8(a) << "] <- " << hex8(v) << "\n";
    } else if (cmd == "reset") {
        if (!path.empty()) load(path);
    } else if (cmd == "load") {
        if (!need(1)) return true;
        load(args[1]);
    } else if (cmd == "info") {
        std::cout << "Programa: " << (path.empty() ? "(ninguno)" : path) << "\n"
                  << "Formato:  " << prog.format << ", " << prog.bytes << " bytes en " << hex8(prog.low) << "-"
                  << hex8(prog.high ? prog.high - 1 : 0) << "\n"
                  << "Entrada:  " << hex8(prog.entry) << "\n"
                  << "PC:       " << hex8(cpu.pc()) << "\n"
                  << "Instrucciones ejecutadas: " << cpu.instret() << "\n"
                  << "Estado:   " << (cpu.halted() ? "terminado (código " + std::to_string(cpu.exit_code()) + ")" : "listo") << "\n"
                  << "Páginas de memoria en uso: " << cpu.mem().pages_in_use() << " x 4 KiB\n";
    } else if (cmd == "syms") {
        if (syms.empty()) std::cout << "Sin símbolos (los binarios crudos no guardan etiquetas; usa un .elf).\n";
        for (auto& [a, n] : syms.all()) std::cout << "  " << hex8(a) << "  " << n << "\n";
    } else {
        std::cout << RED << "Comando desconocido: " << cmd << RESET << " (escribe 'help')\n";
    }
    return true;
}

void usage(const char* argv0) {
    std::cout <<
        "RV32Sim - simulador del ISA RISC-V RV32I (+M)\n\n"
        "Uso: " << argv0 << " [opciones] programa.bin|programa.elf\n\n"
        "Opciones:\n"
        "  -r, --run            Ejecuta el programa completo (sin REPL) y muestra los registros\n"
        "  -x, --exec \"cmds\"    Ejecuta comandos separados por ';' y termina (p.ej. \"run; regs a0\")\n"
        "  -m, --max-steps N    Límite de instrucciones para 'run' (por defecto 10000000)\n"
        "  -b, --base DIR       Dirección donde cargar un binario crudo (por defecto 0x00000000)\n"
        "  -t, --trace          Activa la traza de instrucciones desde el inicio\n"
        "      --no-color       Desactiva los colores ANSI\n"
        "  -h, --help           Muestra esta ayuda\n";
}

}  // namespace

int main(int argc, char** argv) {
    g_color = isatty(STDOUT_FILENO);
    Simulator sim;
    std::string file, exec_cmds;
    bool run_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Falta el valor de " << a << "\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "-r" || a == "--run") run_mode = true;
        else if (a == "-x" || a == "--exec") exec_cmds = next();
        else if (a == "-m" || a == "--max-steps") sim.max_steps = std::strtoull(next().c_str(), nullptr, 0);
        else if (a == "-b" || a == "--base") sim.raw_base = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 0));
        else if (a == "-t" || a == "--trace") sim.trace = true;
        else if (a == "--no-color") g_color = false;
        else if (a == "--color") g_color = true;
        else if (!a.empty() && a[0] == '-') { std::cerr << "Opción desconocida: " << a << "\n"; usage(argv[0]); return 2; }
        else file = a;
    }

    if (file.empty() && (run_mode || !exec_cmds.empty())) {
        std::cerr << "Falta el archivo del programa.\n";
        usage(argv[0]);
        return 2;
    }

    if (!file.empty() && !sim.load(file)) return 1;

    // ---- Modo no interactivo -------------------------------------------------
    if (run_mode || !exec_cmds.empty()) {
        std::string cmds = run_mode ? "run; regs" : exec_cmds;
        std::stringstream ss(cmds);
        std::string c;
        while (std::getline(ss, c, ';')) {
            c = trim(c);
            if (c.empty()) continue;
            std::cout << BOLD << "> " << c << RESET << "\n";
            if (!sim.execute(c)) break;
        }
        return sim.cpu.halted() ? sim.cpu.exit_code() : 0;
    }

    // ---- Modo interactivo (REPL) ---------------------------------------------
    const bool interactive = isatty(STDIN_FILENO);
    std::cout << BOLD << "RV32Sim" << RESET << " - simulador RISC-V RV32I. Escribe 'help' para ver los comandos.\n";
    std::string line, last_step;
    while (true) {
        std::cout << BLUE << "> " << RESET << std::flush;
        if (!std::getline(std::cin, line)) { std::cout << "\n"; break; }
        if (!interactive) std::cout << line << "\n";          // eco al leer de un script
        std::string t = trim(line);
        if (t.empty() && !last_step.empty()) t = last_step;   // Enter repite el último step
        auto first = split_ws(t);
        if (!first.empty() && (first[0] == "step" || first[0] == "s" || first[0] == "si")) last_step = t;
        else if (!t.empty()) last_step.clear();
        if (!sim.execute(t)) break;
    }
    std::cout << "¡Hasta la próxima!\n";
    if (sim.cpu.halted()) {
        std::cout << "El programa terminó con código " << sim.cpu.exit_code() << ".\n";
        return sim.cpu.exit_code();
    }
    return 0;
}
