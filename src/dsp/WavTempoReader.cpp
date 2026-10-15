#include "WavTempoReader.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace spawn::dsp
{

namespace
{

// Read a big-endian uint32 from a 4-char buffer.
uint32_t readBeU32(const unsigned char* p)
{
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         |  static_cast<uint32_t>(p[3]);
}

// Match the 4-byte ASCII tag 'tempo', 'LMTC', or 'MIDI'. These
// come up as both uppercase (standard) and lowercase (some DAWs
// emit 'tempo' even though the convention prefers 'tempo'). We
// match ASCII case-insensitively to be lenient.
bool tagMatches(const unsigned char* p, const char* tag)
{
    for (int i = 0; i < 4; ++i)
    {
        const char a = static_cast<char>(p[i]);
        const char b = tag[i];
        const char loA = (a >= 'A' && a <= 'Z') ? (a - 'A' + 'a') : a;
        const char loB = (b >= 'A' && b <= 'Z') ? (b - 'A' + 'a') : b;
        if (loA != loB)
            return false;
    }
    return true;
}

// Convert microseconds-per-quarter-note to BPM, with bounds
// checking. Below 200_000 µs/QN (~300 BPM) or above 1_500_000 µs/QN
// (~40 BPM) we treat the value as corrupt and return std::nullopt.
// These are the same clamps Clock applies on the model side.
std::optional<double> microsecondsPerQuarterToBpm(uint32_t usPerQuarter)
{
    if (usPerQuarter < 200000u || usPerQuarter > 1500000u)
        return std::nullopt;
    const double bpm = 60'000'000.0 / static_cast<double>(usPerQuarter);
    if (!std::isfinite(bpm))
        return std::nullopt;
    return bpm;
}

} // namespace

std::optional<double> WavTempoReader::readBpm(const juce::File& file)
{
    if (!file.existsAsFile())
        return std::nullopt;
    const std::string filePath = file.getFullPathName().toStdString();

    // Read the whole file into memory. We're a hint, not the
    // decoder — the file is already being decoded by
    // SampleDecoder — so slurping is cheap relative to the
    // existing I/O. Cap at 100 MB to avoid pathological cases.
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f)
        return std::nullopt;
    const std::streamsize size = f.tellg();
    constexpr std::streamsize kMaxScanBytes = 100 * 1024 * 1024;
    if (size <= 0 || size > kMaxScanBytes)
        return std::nullopt;
    f.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size))
        return std::nullopt;

    // RIFF/WAVE container check. The first 12 bytes are
    // 'RIFF<size-WAVE'. If they don't match, this isn't a WAV;
    // fall back to nullopt — BpmDetector will handle non-WAV.
    if (bytes.size() < 12)
        return std::nullopt;
    if (!tagMatches(bytes.data(), "riff"))
        return std::nullopt;
    if (!tagMatches(bytes.data() + 8, "wave"))
        return std::nullopt;

    // Walk chunks. Each chunk is 8-byte header (4 tag + 4
    // little-endian size) followed by payload + optional
    // pad-byte. We only care about chunks whose tag is one of the
    // three tempo tags; everything else we skip past.
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size())
    {
        const unsigned char* header = bytes.data() + pos;
        const uint32_t payloadSize = static_cast<uint32_t>(
            header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24));
        const std::size_t payloadStart = pos + 8;

        if (tagMatches(header, "temo")
            || tagMatches(header, "lmtc")
            || tagMatches(header, "midi"))
        {
            if (payloadStart + 4 <= bytes.size())
            {
                const uint32_t usPerQuarter = readBeU32(bytes.data() + payloadStart);
                auto bpm = microsecondsPerQuarterToBpm(usPerQuarter);
                if (bpm.has_value())
                    return bpm;
            }
        }

        // Chunks are word-aligned (size padded to even).
        const std::size_t payloadEnd = payloadStart + payloadSize + (payloadSize & 1u);
        if (payloadEnd <= pos)
            break; // malformed: non-advancing chunk, avoid infinite loop
        pos = payloadEnd;
    }

    return std::nullopt;
}

} // namespace spawn::dsp
