#pragma once
// Shared DICOM parsing core, used by both the ingestion layer (single-slice
// inspection) and the reconstruction layer (multi-slice stacking). Pulling
// this into one header means both modules stay correct together instead of
// two copies of the same binary-format logic silently drifting apart.

#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <optional>

enum class TransferSyntax { ExplicitLittleEndian, ImplicitLittleEndian, ExplicitBigEndian };

// --- Endian helpers ---------------------------------------------------
inline uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
inline uint32_t swap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}

inline uint16_t toU16(const std::vector<uint8_t>& v, TransferSyntax syntax) {
    if (v.size() < 2) return 0;
    return (syntax == TransferSyntax::ExplicitBigEndian)
               ? (uint16_t(v[0]) << 8) | v[1]
               : v[0] | (uint16_t(v[1]) << 8);
}

// Parses a DICOM decimal string (DS) VR — plain ASCII text holding a number,
// e.g. "1.5" or "-125.3". Used for SliceLocation, RescaleSlope/Intercept,
// and PixelSpacing, all of which DICOM stores as text, not binary numbers.
inline double toDouble(const std::vector<uint8_t>& v) {
    std::string s(v.begin(), v.end());
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && s[start] == ' ') start++;
    if (start >= s.size()) return 0.0;
    try { return std::stod(s.substr(start)); } catch (...) { return 0.0; }
}

struct TagValue {
    uint16_t group = 0, element = 0;
    std::vector<uint8_t> value;
};

// Reads exactly one (tag, VR, length, value) entry, in whatever encoding
// `syntax` specifies. Returns false at end of file or on a truncated read.
inline bool readTag(std::ifstream& file, TransferSyntax syntax, TagValue& out) {
    uint16_t group, element;
    file.read(reinterpret_cast<char*>(&group), 2);
    file.read(reinterpret_cast<char*>(&element), 2);
    if (!file) return false;
    if (syntax == TransferSyntax::ExplicitBigEndian) {
        group = swap16(group);
        element = swap16(element);
    }

    uint32_t length = 0;

    if (syntax == TransferSyntax::ImplicitLittleEndian) {
        file.read(reinterpret_cast<char*>(&length), 4);
    } else {
        char vrChars[2];
        file.read(vrChars, 2);
        std::string vr(vrChars, 2);

        static const std::vector<std::string> longVRs = {"OB", "OW", "OF", "SQ", "UT", "UN"};
        bool isLongVR = std::find(longVRs.begin(), longVRs.end(), vr) != longVRs.end();

        if (isLongVR) {
            uint16_t reserved;
            file.read(reinterpret_cast<char*>(&reserved), 2);
            file.read(reinterpret_cast<char*>(&length), 4);
            if (syntax == TransferSyntax::ExplicitBigEndian) length = swap32(length);
        } else {
            uint16_t shortLength;
            file.read(reinterpret_cast<char*>(&shortLength), 2);
            if (syntax == TransferSyntax::ExplicitBigEndian) shortLength = swap16(shortLength);
            length = shortLength;
        }
    }

    if (length == 0xFFFFFFFFu) {
        out = {group, element, {}}; // undefined length (sequences) — not supported yet
        return true;
    }

    std::vector<uint8_t> value(length);
    if (length > 0) file.read(reinterpret_cast<char*>(value.data()), length);
    if (!file && length > 0) return false;

    out = {group, element, std::move(value)};
    return true;
}

// Full result of parsing one DICOM slice: everything reconstruction needs.
struct ParsedSlice {
    bool ok = false;
    std::string modality;
    uint16_t rows = 0, columns = 0, bitsAllocated = 0;
    std::vector<uint8_t> pixelData;
    TransferSyntax syntax = TransferSyntax::ExplicitLittleEndian;

    double sliceLocation = 0.0;      // (0020,1041) — position along the scan axis
    double rescaleSlope = 1.0;       // (0028,1053) — defaults per DICOM spec if absent
    double rescaleIntercept = 0.0;   // (0028,1052)
    double pixelSpacingRow = 1.0;    // (0028,0030) first value — mm per pixel, row direction
    double pixelSpacingCol = 1.0;    // (0028,0030) second value — mm per pixel, column direction
};

inline ParsedSlice parseDicomFile(const std::string& path) {
    ParsedSlice out;
    std::ifstream file(path, std::ios::binary);
    if (!file) return out;

    file.seekg(128);
    char magic[4];
    file.read(magic, 4);
    if (std::string(magic, 4) != "DICM") return out;

    std::string transferSyntaxUID;
    TagValue tv;
    std::streampos dataSetStart = file.tellg();

    while (true) {
        dataSetStart = file.tellg();
        if (!readTag(file, TransferSyntax::ExplicitLittleEndian, tv)) break;
        if (tv.group != 0x0002) break;
        if (tv.element == 0x0010) {
            transferSyntaxUID.assign(tv.value.begin(), tv.value.end());
            while (!transferSyntaxUID.empty() &&
                   (transferSyntaxUID.back() == '\0' || transferSyntaxUID.back() == ' '))
                transferSyntaxUID.pop_back();
        }
    }

    TransferSyntax syntax = TransferSyntax::ExplicitLittleEndian;
    if (transferSyntaxUID == "1.2.840.10008.1.2") {
        syntax = TransferSyntax::ImplicitLittleEndian;
    } else if (transferSyntaxUID == "1.2.840.10008.1.2.2") {
        syntax = TransferSyntax::ExplicitBigEndian;
    } else if (transferSyntaxUID.rfind("1.2.840.10008.1.2.4", 0) == 0 ||
               transferSyntaxUID.rfind("1.2.840.10008.1.2.5", 0) == 0) {
        return out; // compressed — unsupported, leave ok=false
    }
    out.syntax = syntax;

    file.seekg(dataSetStart);
    std::vector<uint8_t> pixelSpacingRaw;

    while (readTag(file, syntax, tv)) {
        if (tv.group == 0x0028 && tv.element == 0x0010) out.rows = toU16(tv.value, syntax);
        if (tv.group == 0x0028 && tv.element == 0x0011) out.columns = toU16(tv.value, syntax);
        if (tv.group == 0x0028 && tv.element == 0x0100) out.bitsAllocated = toU16(tv.value, syntax);
        if (tv.group == 0x0008 && tv.element == 0x0060)
            out.modality.assign(tv.value.begin(), tv.value.end());
        if (tv.group == 0x7FE0 && tv.element == 0x0010) out.pixelData = tv.value;
        if (tv.group == 0x0020 && tv.element == 0x1041) out.sliceLocation = toDouble(tv.value);
        if (tv.group == 0x0028 && tv.element == 0x1053) out.rescaleSlope = toDouble(tv.value);
        if (tv.group == 0x0028 && tv.element == 0x1052) out.rescaleIntercept = toDouble(tv.value);
        if (tv.group == 0x0028 && tv.element == 0x0030) pixelSpacingRaw = tv.value;
    }

    if (!pixelSpacingRaw.empty()) {
        // PixelSpacing is a DS-VR multi-value field: "rowSpacing\colSpacing"
        std::string s(pixelSpacingRaw.begin(), pixelSpacingRaw.end());
        size_t backslash = s.find('\\');
        if (backslash != std::string::npos) {
            std::vector<uint8_t> rowPart(s.begin(), s.begin() + backslash);
            std::vector<uint8_t> colPart(s.begin() + backslash + 1, s.end());
            out.pixelSpacingRow = toDouble(rowPart);
            out.pixelSpacingCol = toDouble(colPart);
        }
    }

    uint64_t expectedSize = uint64_t(out.rows) * out.columns * (out.bitsAllocated / 8);
    out.ok = (out.rows > 0 && out.columns > 0 && expectedSize == out.pixelData.size());
    return out;
}
