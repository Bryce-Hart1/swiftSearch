#pragma once
/**
 * CRC32C (Castagnoli), the checksum the design notes call for.
 *
 * Parameterization: reflected polynomial 0x82F63B78, init 0xFFFFFFFF,
 * final XOR 0xFFFFFFFF, reflect in and out. This is CRC-32/ISCSI, whose
 * published check value for "123456789" is 0xE3069283.
 *
 * Chaining: crc32c_continue(0, data) == crc32c(data), and
 * crc32c_continue(crc32c_continue(0, a), b) == crc32c(a ++ b).
 * The record trailer relies on that second identity to checksum
 * (key ++ payload) without concatenating them into a temporary buffer.
 *
 * Why these are declared here and defined in a .cpp rather than inline: the
 * hardware/software selection is a preprocessor branch. An inline function whose
 * body depends on compiler flags has one mangled name and two different
 * definitions, which is an ODR violation the linker resolves arbitrarily and
 * -Wall -Wextra -Wpedantic says nothing about. One translation unit makes that
 * structurally impossible.
 */

#include <cstdint>
#include <span>

namespace swiftsearch {

/// CRC32C over `data`, fully finalized.
std::uint32_t crc32c(std::span<const std::uint8_t> data) noexcept;

/// Continue a CRC32C. Pass 0 to start; the return value is always finalized.
std::uint32_t crc32c_continue(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept;

/// The portable table implementation, always compiled even where hardware
/// instructions exist. Exposed only so a test can assert the two agree --
/// that differential test is the one thing that can catch an intrinsic misuse.
std::uint32_t crc32c_software(std::span<const std::uint8_t> data) noexcept;

/// True when crc32c() is using hardware instructions on this build.
bool crc32c_uses_hardware() noexcept;

}  // namespace swiftsearch
