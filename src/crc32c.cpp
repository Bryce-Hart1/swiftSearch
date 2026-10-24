#include "crc32c.hpp"

#include <array>
#include <cstddef>

#include "bytes.hpp"

#if defined(__ARM_FEATURE_CRC32)
#define SS_CRC32C_HW 1
#define SS_CRC32C_HW_ARM 1
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#define SS_CRC32C_HW 1
#define SS_CRC32C_HW_X86 1
#endif

namespace swiftsearch {
namespace {

// Reflected Castagnoli polynomial.
constexpr std::uint32_t POLY = 0x82F63B78u;
constexpr std::uint32_t XOR_MASK = 0xFFFFFFFFu;

// Built at compile time from the polynomial rather than pasted in as a literal
// table, so the parameterisation is verifiable by reading these six lines.
constexpr std::array<std::uint32_t, 256> makeTable() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1u) ? (c >> 1) ^ POLY : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> TABLE = makeTable();

// Both implementations below operate on the raw CRC register: no initial and no
// final XOR. That is also exactly what the hardware instructions compute, which
// is what lets the two share one prologue/epilogue in crc32c_continue().
std::uint32_t stepSoftware(std::uint32_t reg, std::span<const std::uint8_t> data) noexcept {
    for (std::uint8_t byte : data) {
        reg = TABLE[(reg ^ byte) & 0xFFu] ^ (reg >> 8);
    }
    return reg;
}

#if defined(SS_CRC32C_HW)
std::uint32_t stepHardware(std::uint32_t reg, std::span<const std::uint8_t> data) noexcept {
    const std::uint8_t* p = data.data();
    std::size_t left = data.size();

#if defined(SS_CRC32C_HW_ARM)
    while (left >= 8) {
        reg = __builtin_arm_crc32cd(reg, load_u64_le(p));
        p += 8;
        left -= 8;
    }
    if (left >= 4) {
        reg = __builtin_arm_crc32cw(reg, load_u32_le(p));
        p += 4;
        left -= 4;
    }
    if (left >= 2) {
        reg = __builtin_arm_crc32ch(reg, load_u16_le(p));
        p += 2;
        left -= 2;
    }
    if (left >= 1) {
        reg = __builtin_arm_crc32cb(reg, *p);
    }
#else  // SS_CRC32C_HW_X86
    while (left >= 8) {
        reg = static_cast<std::uint32_t>(_mm_crc32_u64(reg, load_u64_le(p)));
        p += 8;
        left -= 8;
    }
    while (left >= 1) {
        reg = _mm_crc32_u8(reg, *p);
        ++p;
        --left;
    }
#endif
    return reg;
}
#endif  // SS_CRC32C_HW

std::uint32_t step(std::uint32_t reg, std::span<const std::uint8_t> data) noexcept {
#if defined(SS_CRC32C_HW)
    return stepHardware(reg, data);
#else
    return stepSoftware(reg, data);
#endif
}

}  // namespace

std::uint32_t crc32c_continue(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept {
    return step(crc ^ XOR_MASK, data) ^ XOR_MASK;
}

std::uint32_t crc32c(std::span<const std::uint8_t> data) noexcept {
    return crc32c_continue(0u, data);
}

std::uint32_t crc32c_software(std::span<const std::uint8_t> data) noexcept {
    return stepSoftware(0u ^ XOR_MASK, data) ^ XOR_MASK;
}

bool crc32c_uses_hardware() noexcept {
#if defined(SS_CRC32C_HW)
    return true;
#else
    return false;
#endif
}

}  // namespace swiftsearch
