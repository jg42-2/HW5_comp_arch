// symbols.hpp — Tabla de símbolos (etiquetas) leída de un archivo ELF.
// Con un binario crudo la tabla queda vacía: el código de máquina no
// guarda los nombres de las etiquetas.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

class SymbolTable {
public:
    void add(const std::string& name, uint32_t addr) {
        by_name_[name] = addr;
        // Si hay varios símbolos en la misma dirección, nos quedamos con el
        // primero que no empiece con '.' (etiquetas locales del ensamblador).
        auto it = by_addr_.find(addr);
        if (it == by_addr_.end() || (it->second[0] == '.' && name[0] != '.'))
            by_addr_[addr] = name;
    }

    std::optional<uint32_t> lookup(const std::string& name) const {
        auto it = by_name_.find(name);
        if (it == by_name_.end()) return std::nullopt;
        return it->second;
    }

    // Nombre exacto en esa dirección (si existe).
    const std::string* name_at(uint32_t addr) const {
        auto it = by_addr_.find(addr);
        return it == by_addr_.end() ? nullptr : &it->second;
    }

    // "etiqueta+offset" para la etiqueta más cercana hacia atrás.
    std::string describe(uint32_t addr) const {
        if (by_addr_.empty()) return "";
        auto it = by_addr_.upper_bound(addr);
        if (it == by_addr_.begin()) return "";
        --it;
        uint32_t off = addr - it->first;
        if (off == 0) return it->second;
        if (off > 0x1000) return "";
        return it->second + "+" + std::to_string(off);
    }

    bool empty() const { return by_name_.empty(); }
    void clear() { by_name_.clear(); by_addr_.clear(); }
    const std::map<uint32_t, std::string>& all() const { return by_addr_; }

private:
    std::map<std::string, uint32_t> by_name_;
    std::map<uint32_t, std::string> by_addr_;
};
