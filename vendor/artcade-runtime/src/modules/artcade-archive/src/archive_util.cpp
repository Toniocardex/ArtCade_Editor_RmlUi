#include "artcade-archive/archive_util.h"

#include <algorithm>
#include <array>
#include <fstream>

#if ARTCADE_HAS_CRYPTO
#include "artcade-asset-key.h"
#endif

namespace ArtCade {
namespace {

std::string toHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(len * 2u);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2u] = hex[(data[i] >> 4) & 0xf];
        out[i * 2u + 1u] = hex[data[i] & 0xf];
    }
    return out;
}

// Compact SHA-256 (public domain style).
struct Sha256Ctx {
    uint32_t state[8]{};
    uint64_t bitlen = 0;
    uint8_t data[64]{};
    size_t datalen = 0;
};

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

void sha256Transform(Sha256Ctx& ctx, const uint8_t data[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (static_cast<uint32_t>(data[j]) << 24u) |
               (static_cast<uint32_t>(data[j + 1]) << 16u) |
               (static_cast<uint32_t>(data[j + 2]) << 8u) |
               static_cast<uint32_t>(data[j + 3]);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3u);
        const uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10u);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + S1 + ch + k[i] + m[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void sha256Init(Sha256Ctx& ctx) {
    ctx.state[0] = 0x6a09e667u; ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u; ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu; ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu; ctx.state[7] = 0x5be0cd19u;
    ctx.bitlen = 0;
    ctx.datalen = 0;
}

void sha256Update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx.data[ctx.datalen++] = data[i];
        if (ctx.datalen == 64) {
            sha256Transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

void sha256Final(Sha256Ctx& ctx, uint8_t hash[32]) {
    size_t i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0;
        sha256Transform(ctx, ctx.data);
        std::fill(ctx.data, ctx.data + 56, 0);
    }
    ctx.bitlen += static_cast<uint64_t>(ctx.datalen) * 8u;
    ctx.data[63] = static_cast<uint8_t>(ctx.bitlen);
    ctx.data[62] = static_cast<uint8_t>(ctx.bitlen >> 8);
    ctx.data[61] = static_cast<uint8_t>(ctx.bitlen >> 16);
    ctx.data[60] = static_cast<uint8_t>(ctx.bitlen >> 24);
    ctx.data[59] = static_cast<uint8_t>(ctx.bitlen >> 32);
    ctx.data[58] = static_cast<uint8_t>(ctx.bitlen >> 40);
    ctx.data[57] = static_cast<uint8_t>(ctx.bitlen >> 48);
    ctx.data[56] = static_cast<uint8_t>(ctx.bitlen >> 56);
    sha256Transform(ctx, ctx.data);
    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx.state[0] >> (24u - i * 8u)) & 0xffu;
        hash[i + 4]  = (ctx.state[1] >> (24u - i * 8u)) & 0xffu;
        hash[i + 8]  = (ctx.state[2] >> (24u - i * 8u)) & 0xffu;
        hash[i + 12] = (ctx.state[3] >> (24u - i * 8u)) & 0xffu;
        hash[i + 16] = (ctx.state[4] >> (24u - i * 8u)) & 0xffu;
        hash[i + 20] = (ctx.state[5] >> (24u - i * 8u)) & 0xffu;
        hash[i + 24] = (ctx.state[6] >> (24u - i * 8u)) & 0xffu;
        hash[i + 28] = (ctx.state[7] >> (24u - i * 8u)) & 0xffu;
    }
}

void wr16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void wr32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static const auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1u)) : (value >> 1u);
            }
            values[i] = value;
        }
        return values;
    }();
    uint32_t value = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        value = table[(value ^ data[i]) & 0xFFu] ^ (value >> 8u);
    return value ^ 0xFFFFFFFFu;
}

} // namespace

std::string_view artcadeAssetKeyId() {
#if ARTCADE_HAS_CRYPTO
    return kArtcadeAssetKeyId;
#else
    return "artcade-dev-key-v1";
#endif
}

std::string sha256Hex(const uint8_t* data, size_t len) {
    Sha256Ctx ctx;
    sha256Init(ctx);
    sha256Update(ctx, data, len);
    uint8_t hash[32];
    sha256Final(ctx, hash);
    return toHex(hash, 32);
}

std::string sha256FileHex(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    Sha256Ctx ctx;
    sha256Init(ctx);
    std::array<uint8_t, 65536> buf{};
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto n = static_cast<size_t>(in.gcount());
        if (n == 0) break;
        sha256Update(ctx, buf.data(), n);
    }
    uint8_t hash[32];
    sha256Final(ctx, hash);
    return toHex(hash, 32);
}

bool zipWriteStore(const std::vector<ZipWriteEntry>& entries,
                   std::vector<uint8_t>& outZip) {
    outZip.clear();
    struct LocalMeta {
        uint32_t offset = 0;
        uint32_t crc = 0;
        uint32_t size = 0;
        std::string name;
    };
    std::vector<LocalMeta> metas;
    metas.reserve(entries.size());

    for (const ZipWriteEntry& entry : entries) {
        LocalMeta meta;
        meta.offset = static_cast<uint32_t>(outZip.size());
        meta.name = entry.name;
        meta.size = static_cast<uint32_t>(entry.data.size());
        meta.crc = crc32(entry.data.data(), entry.data.size());
        // Local file header
        wr32(outZip, 0x04034b50u);
        wr16(outZip, 20); // version needed
        wr16(outZip, 0);  // flags
        wr16(outZip, 0);  // store
        wr16(outZip, 0);  // time
        wr16(outZip, 0);  // date
        wr32(outZip, meta.crc);
        wr32(outZip, meta.size);
        wr32(outZip, meta.size);
        wr16(outZip, static_cast<uint16_t>(meta.name.size()));
        wr16(outZip, 0); // extra
        outZip.insert(outZip.end(), meta.name.begin(), meta.name.end());
        outZip.insert(outZip.end(), entry.data.begin(), entry.data.end());
        metas.push_back(std::move(meta));
    }

    const uint32_t cdOffset = static_cast<uint32_t>(outZip.size());
    for (const LocalMeta& meta : metas) {
        wr32(outZip, 0x02014b50u);
        wr16(outZip, 20);
        wr16(outZip, 20);
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr32(outZip, meta.crc);
        wr32(outZip, meta.size);
        wr32(outZip, meta.size);
        wr16(outZip, static_cast<uint16_t>(meta.name.size()));
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr16(outZip, 0);
        wr32(outZip, 0);
        wr32(outZip, meta.offset);
        outZip.insert(outZip.end(), meta.name.begin(), meta.name.end());
    }
    const uint32_t cdSize = static_cast<uint32_t>(outZip.size()) - cdOffset;
    wr32(outZip, 0x06054b50u);
    wr16(outZip, 0);
    wr16(outZip, 0);
    wr16(outZip, static_cast<uint16_t>(metas.size()));
    wr16(outZip, static_cast<uint16_t>(metas.size()));
    wr32(outZip, cdSize);
    wr32(outZip, cdOffset);
    wr16(outZip, 0);
    return true;
}

} // namespace ArtCade
