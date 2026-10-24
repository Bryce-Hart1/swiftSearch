#include "sha256.hpp"

#include <cstring>

namespace swiftsearch {
namespace {

// FIPS 180-4 §4.2.2: the first 32 bits of the fractional parts of the cube
// roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

// SHA-256 is big-endian on the wire, unlike every other integer in this project.
// Kept local so nothing reaches for bytes.hpp's little-endian helpers by mistake.
constexpr std::uint32_t loadBe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | static_cast<std::uint32_t>(p[3]);
}

constexpr void storeBe32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

constexpr void storeBe64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
    }
}

}  // namespace

Sha256::Sha256() noexcept
    : _h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::compress(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] = loadBe32(block + i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = _h[0], b = _h[1], c = _h[2], d = _h[3];
    std::uint32_t e = _h[4], f = _h[5], g = _h[6], h = _h[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    _h[0] += a; _h[1] += b; _h[2] += c; _h[3] += d;
    _h[4] += e; _h[5] += f; _h[6] += g; _h[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
    _total += data.size();
    std::size_t i = 0;

    // Top up a partial block first.
    if (_buffered != 0) {
        const std::size_t want = 64 - _buffered;
        const std::size_t take = (data.size() < want) ? data.size() : want;
        std::memcpy(_buf.data() + _buffered, data.data(), take);
        _buffered += take;
        i = take;
        if (_buffered < 64) return;   // still not a full block
        compress(_buf.data());
        _buffered = 0;
    }

    // Whole blocks straight from the input, no copy.
    while (data.size() - i >= 64) {
        compress(data.data() + i);
        i += 64;
    }

    // Keep the remainder for next time.
    if (i < data.size()) {
        _buffered = data.size() - i;
        std::memcpy(_buf.data(), data.data() + i, _buffered);
    }
}

Digest Sha256::finish() noexcept {
    // §5.1.1: append 0x80, then zeros, so that the length lands in the last 8
    // bytes of a block. When _buffered is 56..63 that needs a second block --
    // which is why the 55/56/63/64/65 byte test cases exist.
    const std::uint64_t bits = _total * 8;

    _buf[_buffered++] = 0x80;
    if (_buffered > 56) {
        while (_buffered < 64) _buf[_buffered++] = 0;
        compress(_buf.data());
        _buffered = 0;
    }
    while (_buffered < 56) _buf[_buffered++] = 0;
    storeBe64(_buf.data() + 56, bits);
    compress(_buf.data());

    Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        storeBe32(out.data() + i * 4, _h[i]);
    }
    return out;
}

Digest sha256(std::span<const std::uint8_t> data) noexcept {
    Sha256 h;
    h.update(data);
    return h.finish();
}

std::string to_hex(const Digest& d) {
    static constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : d) {
        out.push_back(DIGITS[b >> 4]);
        out.push_back(DIGITS[b & 0x0F]);
    }
    return out;
}

}  // namespace swiftsearch
