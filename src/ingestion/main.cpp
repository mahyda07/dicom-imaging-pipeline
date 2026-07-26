#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

// A DICOM file can encode its main data set in one of a few different ways.
// The file itself tells us which one via the TransferSyntaxUID tag — we
// read that first, then switch our parsing behavior to match.
enum class TransferSyntax { ExplicitLittleEndian, ImplicitLittleEndian, ExplicitBigEndian };

// --- Endian helpers -----------------------------------------------------
// Big Endian DICOM stores multi-byte numbers with the most significant byte
// first (opposite of the little-endian x86 CPUs we're running on), so we
// need to manually flip the byte order when that mode is active.
uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
uint32_t swap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}

// Interpret 2 raw bytes as a 16-bit number, respecting the active endianness.
uint16_t toU16(const std::vector<uint8_t>& v, TransferSyntax syntax) {
    if (v.size() < 2) return 0;
    return (syntax == TransferSyntax::ExplicitBigEndian)
               ? (uint16_t(v[0]) << 8) | v[1]
               : v[0] | (uint16_t(v[1]) << 8);
}

struct TagValue {
    uint16_t group = 0, element = 0;
    std::vector<uint8_t> value;
};

// Reads exactly one (tag, VR, length, value) entry from the file, in whatever
// encoding `syntax` specifies. Returns false at end of file.
bool readTag(std::ifstream& file, TransferSyntax syntax, TagValue& out) {
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
        // Implicit VR never writes the VR in the file at all — the reader is
        // expected to already know each tag's type from the DICOM dictionary.
        // We don't need that dictionary here: length is always a plain
        // 4-byte little-endian number, and we only care about a handful of
        // tags whose meaning we hardcode by (group, element) further down.
        file.read(reinterpret_cast<char*>(&length), 4);
    } else {
        // Explicit VR (either endianness): the 2-character VR code is
        // written right in the file, so we read it, then decide how the
        // length field itself is encoded based on which VR it is.
        char vrChars[2];
        file.read(vrChars, 2);
        std::string vr(vrChars, 2);

        // These VRs can hold very large values (like PixelData), so the
        // standard gives them a 4-byte length instead of 2 (2 bytes can only
        // count up to 65,535) — with 2 reserved padding bytes first.
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
        // "Undefined length" — used for sequences with their own internal
        // end marker. We don't support sequences yet, so bail out cleanly
        // rather than reading garbage.
        out = {group, element, {}};
        return true;
    }

    std::vector<uint8_t> value(length);
    if (length > 0) file.read(reinterpret_cast<char*>(value.data()), length);
    if (!file && length > 0) return false; // truncated/corrupt file — stop instead of using partial data

    out = {group, element, std::move(value)};
    return true;
}

// Renders the parsed pixel data as a small ASCII-art image directly in the
// terminal — no image library involved, just mapping raw brightness values
// to characters of increasing visual density. This lets you *see* the scan
// immediately after parsing, which is a genuinely useful sanity check no
// plain metadata dump gives you.
void printAsciiPreview(const std::vector<uint8_t>& pixelData, uint16_t rows, uint16_t columns,
                        uint16_t bitsAllocated, TransferSyntax syntax) {
    if (bitsAllocated != 16 || pixelData.size() < size_t(rows) * columns * 2) {
        std::cout << "(ASCII preview currently only supports 16-bit pixel data)\n";
        return;
    }

    std::vector<int16_t> pixels(size_t(rows) * columns);
    for (size_t i = 0; i < pixels.size(); i++) {
        uint16_t raw = (syntax == TransferSyntax::ExplicitBigEndian)
                           ? (uint16_t(pixelData[i * 2]) << 8) | pixelData[i * 2 + 1]
                           : pixelData[i * 2] | (uint16_t(pixelData[i * 2 + 1]) << 8);
        pixels[i] = static_cast<int16_t>(raw);
    }

    int16_t minVal = *std::min_element(pixels.begin(), pixels.end());
    int16_t maxVal = *std::max_element(pixels.begin(), pixels.end());
    if (maxVal == minVal) maxVal = minVal + 1; // guard against a divide-by-zero on a blank slice

    static const std::string ramp = " .:-=+*#%@"; // dark -> bright, low density -> high density
    const int previewWidth = 60;
    const int previewHeight = 30;

    std::cout << "\nASCII preview (" << previewWidth << "x" << previewHeight
              << " downsample of the original " << rows << "x" << columns << " slice):\n\n";

    for (int y = 0; y < previewHeight; y++) {
        int srcRow = y * rows / previewHeight;
        for (int x = 0; x < previewWidth; x++) {
            int srcCol = x * columns / previewWidth;
            int16_t px = pixels[size_t(srcRow) * columns + srcCol];
            double normalized = double(px - minVal) / double(maxVal - minVal);
            size_t rampIndex = static_cast<size_t>(normalized * (ramp.size() - 1));
            std::cout << ramp[rampIndex];
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "samples/ct_small.dcm";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file: " << path << "\n";
        return 1;
    }

    file.seekg(128);
    char magic[4];
    file.read(magic, 4);
    if (std::string(magic, 4) != "DICM") {
        std::cerr << "Not a valid DICOM file\n";
        return 1;
    }

    // --- Step 1: read the File Meta Information header ---------------------
    // Per the DICOM standard, this header is ALWAYS Explicit VR Little
    // Endian, regardless of what transfer syntax the main data set below it
    // uses. Its whole job is to tell us that main-data-set syntax via the
    // TransferSyntaxUID tag, so we read it first, unconditionally, this way.
    std::string transferSyntaxUID;
    TagValue tv;
    std::streampos dataSetStart = file.tellg();

    while (true) {
        dataSetStart = file.tellg();
        if (!readTag(file, TransferSyntax::ExplicitLittleEndian, tv)) break;
        if (tv.group != 0x0002) break; // left the meta header, entered the main data set

        if (tv.element == 0x0010) {
            transferSyntaxUID.assign(tv.value.begin(), tv.value.end());
            while (!transferSyntaxUID.empty() &&
                   (transferSyntaxUID.back() == '\0' || transferSyntaxUID.back() == ' '))
                transferSyntaxUID.pop_back();
        }
    }

    TransferSyntax syntax = TransferSyntax::ExplicitLittleEndian; // sane default if UID is missing
    std::string syntaxName = "Explicit VR Little Endian";
    if (transferSyntaxUID == "1.2.840.10008.1.2") {
        syntax = TransferSyntax::ImplicitLittleEndian;
        syntaxName = "Implicit VR Little Endian";
    } else if (transferSyntaxUID == "1.2.840.10008.1.2.2") {
        syntax = TransferSyntax::ExplicitBigEndian;
        syntaxName = "Explicit VR Big Endian";
    } else if (transferSyntaxUID.rfind("1.2.840.10008.1.2.4", 0) == 0 ||
               transferSyntaxUID.rfind("1.2.840.10008.1.2.5", 0) == 0) {
        std::cerr << "Compressed transfer syntax (" << transferSyntaxUID
                  << ") is not supported by this parser — rejecting file.\n";
        return 1;
    }

    std::cout << "Transfer syntax UID: " << transferSyntaxUID << "\n";
    std::cout << "Parsing mode: " << syntaxName << "\n\n";

    // --- Step 2: re-read the main data set using the correct syntax --------
    // We already consumed one tag from the main data set above (the one that
    // told us we'd left the meta header) using the wrong syntax — so we seek
    // back to right before it and re-parse everything from there correctly.
    file.seekg(dataSetStart);

    uint16_t rows = 0, columns = 0, bitsAllocated = 0;
    std::string modality;
    std::vector<uint8_t> pixelData;

    while (readTag(file, syntax, tv)) {
        if (tv.group == 0x0028 && tv.element == 0x0010) rows = toU16(tv.value, syntax);
        if (tv.group == 0x0028 && tv.element == 0x0011) columns = toU16(tv.value, syntax);
        if (tv.group == 0x0028 && tv.element == 0x0100) bitsAllocated = toU16(tv.value, syntax);
        if (tv.group == 0x0008 && tv.element == 0x0060)
            modality.assign(tv.value.begin(), tv.value.end());
        if (tv.group == 0x7FE0 && tv.element == 0x0010)
            pixelData = tv.value;
    }

    std::cout << "Modality: " << modality << "\n";
    std::cout << "Rows: " << rows << ", Columns: " << columns
              << ", BitsAllocated: " << bitsAllocated << "\n";
    std::cout << "PixelData size: " << pixelData.size() << " bytes\n";

    uint64_t expected = uint64_t(rows) * columns * (bitsAllocated / 8);
    bool sizeOk = (expected == pixelData.size());
    std::cout << "Expected size: " << expected << " bytes -> "
              << (sizeOk ? "MATCH" : "MISMATCH — file may be corrupt or partially parsed") << "\n";

    if (sizeOk) printAsciiPreview(pixelData, rows, columns, bitsAllocated, syntax);

    return 0;
}
