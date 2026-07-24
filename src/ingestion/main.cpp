#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

int main() {
    std::ifstream file("samples/ct_small.dcm", std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file\n";
        return 1;
    }

    file.seekg(128);
    char magic[4];
    file.read(magic, 4);
    if (std::string(magic, 4) != "DICM") {
        std::cerr << "Not a valid DICOM file\n";
        return 1;
    }

    uint16_t rows = 0, columns = 0, bitsAllocated = 0;
    std::string modality;
    std::vector<uint8_t> pixelData;

    // These VRs store their length as 4 bytes (with 2 reserved bytes first)
    // instead of the usual 2 bytes — mainly because their values can be huge
    // (like PixelData), and 2 bytes can only count up to 65,535.
    static const std::vector<std::string> longVRs = {"OB", "OW", "OF", "SQ", "UT", "UN"};

    while (file.peek() != EOF) {
        uint16_t group, element;
        file.read(reinterpret_cast<char*>(&group), 2);
        file.read(reinterpret_cast<char*>(&element), 2);
        if (!file) break;

        char vrChars[2];
        file.read(vrChars, 2);
        std::string vr(vrChars, 2);

        uint32_t length = 0;
        bool isLongVR = std::find(longVRs.begin(), longVRs.end(), vr) != longVRs.end();

        if (isLongVR) {
            uint16_t reserved;
            file.read(reinterpret_cast<char*>(&reserved), 2); // 2 unused bytes, must still be read
            file.read(reinterpret_cast<char*>(&length), 4);
        } else {
            uint16_t shortLength;
            file.read(reinterpret_cast<char*>(&shortLength), 2);
            length = shortLength;
        }

        std::vector<uint8_t> value(length);
        if (length > 0) file.read(reinterpret_cast<char*>(value.data()), length);

        // (0028,0010) Rows — a 16-bit number stored as 2 raw bytes, little-endian
        if (group == 0x0028 && element == 0x0010 && value.size() >= 2)
            rows = value[0] | (value[1] << 8);
        // (0028,0011) Columns
        if (group == 0x0028 && element == 0x0011 && value.size() >= 2)
            columns = value[0] | (value[1] << 8);
        // (0028,0100) BitsAllocated
        if (group == 0x0028 && element == 0x0100 && value.size() >= 2)
            bitsAllocated = value[0] | (value[1] << 8);
        // (0008,0060) Modality — this one's plain text, not a number
        if (group == 0x0008 && element == 0x0060)
            modality = std::string(value.begin(), value.end());
        // (7FE0,0010) PixelData — the big one
        if (group == 0x7FE0 && element == 0x0010)
            pixelData = value;
    }

    std::cout << "Modality: " << modality << "\n";
    std::cout << "Rows: " << rows << ", Columns: " << columns
              << ", BitsAllocated: " << bitsAllocated << "\n";
    std::cout << "PixelData size: " << pixelData.size() << " bytes\n";

    uint64_t expected = uint64_t(rows) * columns * (bitsAllocated / 8);
    std::cout << "Expected size: " << expected << " bytes -> "
              << (expected == pixelData.size() ? "MATCH" : "MISMATCH") << "\n";

    return 0;
}
