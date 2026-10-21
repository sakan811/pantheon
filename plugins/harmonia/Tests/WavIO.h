#pragma once

// Minimal WAV reader/writer for the offline tools (PCM 8/16/24/32 and float32 in, float32 out).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wavio
{
struct Wav
{
    int sampleRate = 44100;
    int channels = 1;
    std::vector<std::vector<float>> data; // [channel][frame]
};

inline bool read (const std::string& path, Wav& w)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f)
        return false;
    std::vector<uint8_t> b;
    uint8_t tmp[65536];
    size_t n;
    while ((n = std::fread (tmp, 1, sizeof tmp, f)) > 0)
        b.insert (b.end(), tmp, tmp + n);
    std::fclose (f);

    auto u16 = [&] (size_t o) { return (uint16_t) (b[o] | (b[o + 1] << 8)); };
    auto u32 = [&] (size_t o) { return (uint32_t) (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t) b[o + 3] << 24)); };
    if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
        return false;

    int fmtTag = 1, bits = 16;
    size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= b.size();)
    {
        const uint32_t len = u32 (o + 4);
        if (std::memcmp (b.data() + o, "fmt ", 4) == 0)
        {
            fmtTag = u16 (o + 8);
            w.channels = u16 (o + 10);
            w.sampleRate = (int) u32 (o + 12);
            bits = u16 (o + 22);
            if (fmtTag == 0xFFFE && len >= 26)
                fmtTag = u16 (o + 32);
        }
        else if (std::memcmp (b.data() + o, "data", 4) == 0)
        {
            dataOff = o + 8;
            dataLen = std::min<size_t> (len, b.size() - dataOff);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (dataOff == 0 || w.channels < 1)
        return false;

    const int bytes = bits / 8;
    const size_t frames = dataLen / (size_t) (bytes * w.channels);
    w.data.assign ((size_t) w.channels, std::vector<float> (frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c)
        {
            const size_t o = dataOff + (i * (size_t) w.channels + (size_t) c) * (size_t) bytes;
            float v = 0.0f;
            if (fmtTag == 3 && bits == 32)
            {
                uint32_t u = u32 (o);
                std::memcpy (&v, &u, 4);
            }
            else if (bits == 16)
                v = (float) (int16_t) u16 (o) / 32768.0f;
            else if (bits == 24)
                v = (float) ((int32_t) ((uint32_t) b[o] << 8 | (uint32_t) b[o + 1] << 16 | (uint32_t) b[o + 2] << 24) >> 8) / 8388608.0f;
            else if (bits == 32)
                v = (float) (int32_t) u32 (o) / 2147483648.0f;
            else if (bits == 8)
                v = ((float) b[o] - 128.0f) / 128.0f;
            w.data[(size_t) c][i] = v;
        }
    return true;
}

inline bool write (const std::string& path, const Wav& w)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f)
        return false;
    const uint32_t frames = (uint32_t) w.data[0].size();
    const uint32_t dataLen = frames * (uint32_t) w.channels * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    w32 (36 + dataLen);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16);
    w16 (3);
    w16 ((uint16_t) w.channels);
    w32 ((uint32_t) w.sampleRate);
    w32 ((uint32_t) w.sampleRate * (uint32_t) w.channels * 4);
    w16 ((uint16_t) (w.channels * 4));
    w16 (32);
    std::fwrite ("data", 1, 4, f);
    w32 (dataLen);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c)
            std::fwrite (&w.data[(size_t) c][i], 4, 1, f);
    std::fclose (f);
    return true;
}
} // namespace wavio
