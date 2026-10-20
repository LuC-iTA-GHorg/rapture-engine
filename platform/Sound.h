// Sound resources (rrs type 5). CAudioLib::SoundCreate moses.dll 0x10010995.
//   kind 1 = wave (0x1c byte header + ADPCM or PCM)   -> decoded to PcmBuffer
//   kind 2 = compact MIDI stream                      -> kept raw (see Midi.h)
//   kind 3 = reference to an AVI file by name         -> aviName
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Decoded 16-bit PCM, interleaved when channels == 2.
struct PcmBuffer {
    std::vector<int16_t> samples;
    int channels = 1;
    int rate = 22050;
    size_t Frames() const { return channels ? samples.size() / size_t(channels) : 0; }
};

struct Sound {
    enum Kind { kNone = 0, kWave = 1, kMidi = 2, kAvi = 3 };
    Kind kind = kNone;
    uint32_t param = 0;                // second dword of the record (sound id in the object)
    std::shared_ptr<PcmBuffer> pcm;    // kWave
    bool streamFlag = false;           // header byte +0x18 (streamed vs static buffer; unused so far)
    std::vector<uint8_t> midi;         // kMidi: compact stream without the 8 byte header
    std::string aviName;               // kAvi
};

// Parses a type-5 record body (as returned by rrs::Archive::GetData).
bool LoadSound(const uint8_t* data, size_t size, Sound& out, std::string* err = nullptr);

// WP-5: plain RIFF/WAVE files (CD audio tracks as files, tools). PCM 8/16/24/32 bit, IEEE float 32 bit, 1 or 2 channels
// (more channels: the first two are kept). Not part of the rrs sound resource format.
bool LoadWav(const uint8_t* data, size_t size, PcmBuffer& out, std::string* err = nullptr);
bool LoadWavFile(const std::string& path, PcmBuffer& out, std::string* err = nullptr);
