// RLE16 DIB encoder / raw decoder (WP-7). Inverse of DecodeDib (Dib.h); format: docs/rrs_format.md sec. 3.
//
// DibRaw keeps the exact RGB555 words (the ARGB8888 of Dib is lossy), so decode -> encode is lossless.
// The encoder reproduces the choices of the original authoring tool, inferred from all 4659 DIBs of the
// shipped archives (measured: 100% byte-identical re-encode, see docs/WP7.md):
//   * a run (count byte 1..127 + pixel) is emitted for every group of >= 2 equal pixels, split in chunks
//     of 127 (a remainder of 1 is still emitted as a run); everything else is gathered in literals (0x80|n, n <= 127);
//   * a trailing run of >= 2 transparent pixels (0x0000) of a row is omitted (a single trailing 0 stays);
//   * the line table holds dwords (flag bit2) only when the data does not fit 16 bit offsets (> 0xffff);
//   * flags = 2 (compressed) | 4 (dword table) | 1 (no transparent pixel at all) | 8 (all pixels transparent).
// Rows are stored bottom-up in the file; DibRaw::px is TOP-DOWN like Dib.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DibRaw {
    int w = 0, h = 0;
    int ox = 0, oy = 0;          // hot-spot (DIBREC +4/+6)
    int flags = 0;               // DIBREC flags byte as read by DecodeDibRaw (informative; EncodeDib recomputes it)
    std::vector<uint16_t> px;    // w*h RGB555 words, top-down; 0x0000 = transparent, opaque pixels have bit15 set
};

// Parses a type-2 record body into raw RGB555 words.
bool DecodeDibRaw(const uint8_t* data, size_t size, DibRaw& out, std::string* err = nullptr);

// Flags the original encoder writes for this image; `encodedBytesAfterHeader` = line table + rows size.
int DibFlagsFor(const DibRaw& d, size_t encodedBytesAfterHeader);

// Encodes to a complete type-2 record body (DIBREC + line table + rows).
std::vector<uint8_t> EncodeDib(const DibRaw& d);

// Same but with caller-chosen DIBREC flags (bit1 forced on, bit2 always recomputed from the data size).
std::vector<uint8_t> EncodeDibWithFlags(const DibRaw& d, int flags);

// ARGB8888 (top-down, as in Dib::Pixels) -> RGB555: alpha < 128 -> 0x0000; opaque -> 0x8000 | rgb (opaque black = 0x8000).
uint16_t Argb8888ToRgb555(uint32_t argb);
DibRaw DibRawFromArgb(const uint32_t* argb, int w, int h, int ox, int oy);
