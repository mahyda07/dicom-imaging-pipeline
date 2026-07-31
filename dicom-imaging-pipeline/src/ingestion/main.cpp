#include "dicom_parser.h"
#include <iostream>
#include <algorithm>

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
    if (maxVal == minVal) maxVal = minVal + 1;

    static const std::string ramp = " .:-=+*#%@";
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

    ParsedSlice slice = parseDicomFile(path);
    if (!slice.ok) {
        std::cerr << "Failed to parse " << path
                  << " (unsupported/compressed transfer syntax, or size mismatch)\n";
        return 1;
    }

    std::cout << "Modality: " << slice.modality << "\n";
    std::cout << "Rows: " << slice.rows << ", Columns: " << slice.columns
              << ", BitsAllocated: " << slice.bitsAllocated << "\n";
    std::cout << "PixelData size: " << slice.pixelData.size() << " bytes -> MATCH\n";
    std::cout << "SliceLocation: " << slice.sliceLocation
              << ", PixelSpacing: " << slice.pixelSpacingRow << " x " << slice.pixelSpacingCol << " mm\n";

    printAsciiPreview(slice.pixelData, slice.rows, slice.columns, slice.bitsAllocated, slice.syntax);
    return 0;
}
