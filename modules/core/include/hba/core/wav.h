// SPDX-License-Identifier: GPL-3.0-or-later
// Shared spec-compliant 32-bit IEEE float WAV writer (18-byte fmt + fact chunk + UTF-8 path).
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace hba::core {

namespace detail {
inline void writeLe16(std::ofstream& out, std::uint16_t v) {
    const char bytes[2] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8u) & 0xFFu)};
    out.write(bytes, 2);
}

inline void writeLe32(std::ofstream& out, std::uint32_t v) {
    const char bytes[4] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8u) & 0xFFu),
                           static_cast<char>((v >> 16u) & 0xFFu), static_cast<char>((v >> 24u) & 0xFFu)};
    out.write(bytes, 4);
}
} // namespace detail

inline bool writeWavFloat(std::string_view utf8Path, std::span<const float> interleaved, int channels, int sampleRate) {
    if (utf8Path.empty() || channels <= 0 || channels > 16383 || sampleRate <= 0)
        return false;
    const auto ch = static_cast<std::uint16_t>(channels);
    const auto sr = static_cast<std::uint32_t>(sampleRate);
    const std::size_t frameCount = interleaved.size() / static_cast<std::size_t>(ch);
    constexpr std::uint32_t kHeaderPayload = 4u + (8u + 18u) + (8u + 4u) + 8u;
    const std::size_t maxDataBytes = std::numeric_limits<std::uint32_t>::max() - kHeaderPayload;
    const std::size_t blockAlignSz = static_cast<std::size_t>(ch) * sizeof(float);
    if (frameCount > maxDataBytes / blockAlignSz ||
        static_cast<std::uint64_t>(sr) * static_cast<std::uint64_t>(blockAlignSz) >
            std::numeric_limits<std::uint32_t>::max())
        return false;
    const auto frames = static_cast<std::uint32_t>(frameCount);
    const auto dataBytes = static_cast<std::uint32_t>(frameCount * blockAlignSz);
    const auto blockAlign = static_cast<std::uint16_t>(blockAlignSz);
    const auto byteRate = sr * static_cast<std::uint32_t>(blockAlign);

    // RIFF header + 18-byte IEEE float fmt (cbSize=0) + mandatory 4-byte fact chunk + data chunk.
    const std::uint32_t riffSize = kHeaderPayload + dataBytes;

    std::u8string u8Path;
    u8Path.reserve(utf8Path.size());
    for (char c : utf8Path)
        u8Path.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    const std::filesystem::path fsPath(u8Path);
    std::ofstream out(fsPath, std::ios::binary);
    if (!out)
        return false;

    out.write("RIFF", 4);
    detail::writeLe32(out, riffSize);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    detail::writeLe32(out, 18u);
    detail::writeLe16(out, 3u); // WAVE_FORMAT_IEEE_FLOAT
    detail::writeLe16(out, ch);
    detail::writeLe32(out, sr);
    detail::writeLe32(out, byteRate);
    detail::writeLe16(out, blockAlign);
    detail::writeLe16(out, 32u);
    detail::writeLe16(out, 0u); // cbSize = 0

    out.write("fact", 4);
    detail::writeLe32(out, 4u);
    detail::writeLe32(out, frames);

    out.write("data", 4);
    detail::writeLe32(out, dataBytes);
    if (dataBytes > 0u)
        out.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataBytes));
    out.close();
    return out.good();
}

} // namespace hba::core
