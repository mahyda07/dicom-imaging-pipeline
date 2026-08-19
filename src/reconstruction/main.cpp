#include "volume.h"
#include <iostream>

void printSagittalPreview(const VoxelGrid& grid) {
    if (grid.depth < 2) {
        std::cout << "(Need at least 2 slices for a sagittal preview)\n";
        return;
    }
    size_t midX = grid.width / 2;

    int16_t minVal = 32767, maxVal = -32768;
    for (size_t z = 0; z < grid.depth; z++)
        for (size_t y = 0; y < grid.height; y++) {
            int16_t v = grid.at(midX, y, z);
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
        }
    if (maxVal == minVal) maxVal = minVal + 1;

    static const std::string ramp = " .:-=+*#%@";
    const int previewWidth = 60, previewHeight = 20;

    std::cout << "\nSagittal cross-section preview (side view through x=" << midX
              << ", " << previewWidth << "x" << previewHeight << "):\n\n";

    for (int row = 0; row < previewHeight; row++) {
        size_t srcY = row * grid.height / previewHeight;
        for (int col = 0; col < previewWidth; col++) {
            size_t srcZ = col * grid.depth / previewWidth;
            int16_t v = grid.at(midX, srcY, srcZ);
            double normalized = double(v - minVal) / double(maxVal - minVal);
            std::cout << ramp[static_cast<size_t>(normalized * (ramp.size() - 1))];
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "samples/series";

    VoxelGrid grid = loadVolumeFromDirectory(dir);
    if (grid.depth == 0) return 1;

    std::cout << "Reconstructed volume: " << grid.width << " x " << grid.height
              << " x " << grid.depth << " voxels\n";
    std::cout << "Voxel spacing: " << grid.spacingX << " x " << grid.spacingY
              << " x " << grid.spacingZ << " mm\n";

    printSagittalPreview(grid);
    return 0;
}
