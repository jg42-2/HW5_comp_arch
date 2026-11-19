// memory.hpp — Memoria plana byte-addressable de 32 bits (little-endian).
//
// El espacio de direcciones de RV32 es de 4 GiB (2^32 bytes). Reservar 4 GiB
// de RAM real sería absurdo, así que la memoria se implementa de forma
// "dispersa" (sparse): se divide en páginas de 4 KiB que solo se crean la
// primera vez que se escriben. Leer una dirección que nunca se escribió
// devuelve 0, como si toda la memoria empezara en cero.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class Memory {
public:
    static constexpr uint32_t PAGE_BITS = 12;                // 4 KiB
    static constexpr uint32_t PAGE_SIZE = 1u << PAGE_BITS;
    static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;

    // ---- Acceso a bytes individuales -------------------------------------
    uint8_t read8(uint32_t addr) const {
        auto it = pages_.find(addr >> PAGE_BITS);
        if (it == pages_.end()) return 0;
        return (*it->second)[addr & PAGE_MASK];
    }

    void write8(uint32_t addr, uint8_t value) {
        auto& page = pages_[addr >> PAGE_BITS];
        if (!page) {
            page = std::make_unique<Page>();
            page->fill(0);
        }
        (*page)[addr & PAGE_MASK] = value;
    }

    // ---- Halfwords y words en formato LITTLE-ENDIAN ----------------------
    // Little-endian: el byte MENOS significativo va en la dirección más baja.
    //   Ejemplo: guardar 0x11223344 en 0x1000 deja en memoria
    //     0x1000: 44   0x1001: 33   0x1002: 22   0x1003: 11
    uint16_t read16(uint32_t addr) const {
        return static_cast<uint16_t>(read8(addr) | (read8(addr + 1) << 8));
    }

    uint32_t read32(uint32_t addr) const {
        return static_cast<uint32_t>(read8(addr))
             | (static_cast<uint32_t>(read8(addr + 1)) << 8)
             | (static_cast<uint32_t>(read8(addr + 2)) << 16)
             | (static_cast<uint32_t>(read8(addr + 3)) << 24);
    }

    void write16(uint32_t addr, uint16_t value) {
        write8(addr,     static_cast<uint8_t>(value & 0xFF));
        write8(addr + 1, static_cast<uint8_t>(value >> 8));
    }

    void write32(uint32_t addr, uint32_t value) {
        write8(addr,     static_cast<uint8_t>(value));
        write8(addr + 1, static_cast<uint8_t>(value >> 8));
        write8(addr + 2, static_cast<uint8_t>(value >> 16));
        write8(addr + 3, static_cast<uint8_t>(value >> 24));
    }

    // Copia un bloque de bytes (usado por el cargador de programas).
    void load_bytes(uint32_t base, const std::vector<uint8_t>& data) {
        for (size_t i = 0; i < data.size(); ++i)
            write8(base + static_cast<uint32_t>(i), data[i]);
    }

    void clear() { pages_.clear(); }

    size_t pages_in_use() const { return pages_.size(); }

private:
    using Page = std::array<uint8_t, PAGE_SIZE>;
    std::unordered_map<uint32_t, std::unique_ptr<Page>> pages_;
};
