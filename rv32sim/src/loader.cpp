// loader.cpp — Carga de binarios crudos y archivos ELF32 RISC-V.
#include "loader.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("no se pudo abrir \"" + path + "\"");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Lectura little-endian desde un buffer, verificando límites.
uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) throw std::runtime_error("ELF truncado o corrupto");
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    if (off + 2 > b.size()) throw std::runtime_error("ELF truncado o corrupto");
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

bool is_elf(const std::vector<uint8_t>& b) {
    return b.size() >= 4 && b[0] == 0x7F && b[1] == 'E' && b[2] == 'L' && b[3] == 'F';
}

LoadResult load_elf(const std::vector<uint8_t>& b, Memory& mem, SymbolTable& syms) {
    if (b.size() < 52) throw std::runtime_error("ELF demasiado pequeño");
    if (b[4] != 1) throw std::runtime_error("el ELF no es de 32 bits (ELFCLASS32)");
    if (b[5] != 1) throw std::runtime_error("el ELF no es little-endian");
    if (rd16(b, 18) != 243) throw std::runtime_error("el ELF no es para RISC-V (e_machine != 243)");

    LoadResult r;
    r.format = "elf";
    r.entry = rd32(b, 24);
    r.low = 0xFFFFFFFF;
    r.high = 0;

    // ---- Program headers: segmentos a cargar --------------------------------
    const uint32_t phoff = rd32(b, 28);
    const uint16_t phentsize = rd16(b, 42), phnum = rd16(b, 44);
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + static_cast<size_t>(i) * phentsize;
        if (rd32(b, ph) != 1) continue;               // solo PT_LOAD
        const uint32_t offset = rd32(b, ph + 4), vaddr = rd32(b, ph + 8);
        const uint32_t filesz = rd32(b, ph + 16), memsz = rd32(b, ph + 20);
        if (static_cast<size_t>(offset) + filesz > b.size())
            throw std::runtime_error("segmento ELF fuera del archivo");
        for (uint32_t k = 0; k < filesz; ++k) mem.write8(vaddr + k, b[offset + k]);
        for (uint32_t k = filesz; k < memsz; ++k) mem.write8(vaddr + k, 0);   // .bss
        if (memsz) {
            r.low = std::min(r.low, vaddr);
            r.high = std::max(r.high, vaddr + memsz);
            r.bytes += memsz;
        }
    }
    if (r.bytes == 0) { r.low = r.high = 0; }

    // ---- Section headers: buscamos .symtab para las etiquetas ----------------
    const uint32_t shoff = rd32(b, 32);
    const uint16_t shentsize = rd16(b, 46), shnum = rd16(b, 48);
    for (uint16_t i = 0; shoff && i < shnum; ++i) {
        const size_t sh = shoff + static_cast<size_t>(i) * shentsize;
        if (rd32(b, sh + 4) != 2) continue;           // SHT_SYMTAB
        const uint32_t sym_off = rd32(b, sh + 16), sym_size = rd32(b, sh + 20);
        const uint32_t link = rd32(b, sh + 24);       // índice de su tabla de strings
        const size_t strsh = shoff + static_cast<size_t>(link) * shentsize;
        const uint32_t str_off = rd32(b, strsh + 16), str_size = rd32(b, strsh + 20);

        for (uint32_t s = 16; s + 16 <= sym_size; s += 16) {   // el símbolo 0 es nulo
            const size_t sym = sym_off + s;
            const uint32_t name_off = rd32(b, sym);
            const uint32_t value = rd32(b, sym + 4);
            const uint8_t type = b.at(sym + 12) & 0xF;
            const uint16_t shndx = rd16(b, sym + 14);
            if (shndx == 0 || name_off == 0 || name_off >= str_size) continue;
            if (type != 0 && type != 1 && type != 2) continue;    // NOTYPE, OBJECT, FUNC
            std::string name;
            for (size_t p = str_off + name_off; p < b.size() && b[p]; ++p) name.push_back(static_cast<char>(b[p]));
            if (name.empty() || name[0] == '$') continue;          // símbolos de mapeo
            syms.add(name, value);
        }
    }
    return r;
}

}  // namespace

LoadResult load_program(const std::string& path, Memory& mem, SymbolTable& syms, uint32_t raw_base) {
    const std::vector<uint8_t> data = read_file(path);
    if (data.empty()) throw std::runtime_error("el archivo \"" + path + "\" está vacío");

    if (is_elf(data)) return load_elf(data, mem, syms);

    // Binario crudo: copiar todo a partir de raw_base.
    mem.load_bytes(raw_base, data);
    LoadResult r;
    r.format = "raw";
    r.entry = raw_base;
    r.low = raw_base;
    r.high = raw_base + static_cast<uint32_t>(data.size());
    r.bytes = data.size();
    return r;
}
