#include "dicom_parser.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

// The reconstructed 3D volume: a flat array standing in for a 3D grid, plus
// the physical spacing that lets us relate a voxel index back to real
// millimeters. We store Hounsfield Units (int16_t is enough — real HU
// values run roughly -1000 to +3000), not raw pixel brightness, since HU is
// the value that's actually comparable across different scans and scanners.
struct VoxelGrid {
    std::vector<int16_t> voxels; // flattened, index = z * (width*height) + y * width + x
    size_t width = 0, height = 0, depth = 0;
    double spacingX = 1.0, spacingY = 1.0, spacingZ = 1.0; // mm per voxel, each axis

    int16_t at(size_t x, size_t y, size_t z) const {
        return voxels[z * width * height + y * width + x];
    }
};

// Converts one slice's raw pixel bytes into Hounsfield Units using the
// per-file rescale formula: HU = rawPixel * RescaleSlope + RescaleIntercept.
// This is what actually makes density values comparable across slices and
// scanners — two scanners can store wildly different raw numbers for the
// same real tissue density, but their HU values will agree.
std::vector<int16_t> toHounsfieldUnits(const ParsedSlice& slice) {
    size_t pixelCount = size_t(slice.rows) * slice.columns;
    std::vector<int16_t> hu(pixelCount);
    for (size_t i = 0; i < pixelCount; i++) {
        uint16_t raw = (slice.syntax == TransferSyntax::ExplicitBigEndian)
                           ? (uint16_t(slice.pixelData[i * 2]) << 8) | slice.pixelData[i * 2 + 1]
                           : slice.pixelData[i * 2] | (uint16_t(slice.pixelData[i * 2 + 1]) << 8);
        int16_t rawSigned = static_cast<int16_t>(raw);
        hu[i] = static_cast<int16_t>(std::lround(rawSigned * slice.rescaleSlope + slice.rescaleIntercept));
    }
    return hu;
}

// Stacks a sorted series of slices into one uniform-spacing 3D volume.
// Real scanner output doesn't always guarantee perfectly even spacing
// between slices, so rather than assuming it, we resample onto a uniform Z
// axis using linear interpolation between the two nearest real slices —
// this is the core idea "trilinear interpolation" refers to (linear
// interpolation applied along each of the 3 axes; here we interpolate
// along Z since X/Y are already a uniform pixel grid within each slice).
VoxelGrid stackSlicesIntoVolume(const std::vector<ParsedSlice>& sortedSlices,
                                 const std::vector<std::vector<int16_t>>& huSlices) {
    VoxelGrid grid;
    grid.width = sortedSlices.front().columns;
    grid.height = sortedSlices.front().rows;
    grid.spacingX = sortedSlices.front().pixelSpacingCol;
    grid.spacingY = sortedSlices.front().pixelSpacingRow;

    // Determine the uniform Z spacing to resample onto: the smallest real
    // gap between consecutive slices, so we never lose detail by upsampling
    // too coarsely.
    double minGap = 1e9;
    for (size_t i = 1; i < sortedSlices.size(); i++) {
        double gap = sortedSlices[i].sliceLocation - sortedSlices[i - 1].sliceLocation;
        if (gap > 1e-6) minGap = std::min(minGap, gap);
    }
    if (minGap > 1e8) minGap = 1.0; // only one slice, or all at the same location — fallback

    double firstZ = sortedSlices.front().sliceLocation;
    double lastZ = sortedSlices.back().sliceLocation;
    grid.depth = static_cast<size_t>(std::round((lastZ - firstZ) / minGap)) + 1;
    grid.spacingZ = minGap;

    size_t sliceArea = grid.width * grid.height;
    grid.voxels.resize(sliceArea * grid.depth);

    for (size_t z = 0; z < grid.depth; z++) {
        double targetZ = firstZ + z * minGap;

        // Find the two real slices this resampled position falls between.
        size_t lower = 0;
        while (lower + 1 < sortedSlices.size() && sortedSlices[lower + 1].sliceLocation <= targetZ)
            lower++;
        size_t upper = std::min(lower + 1, sortedSlices.size() - 1);

        double z0 = sortedSlices[lower].sliceLocation;
        double z1 = sortedSlices[upper].sliceLocation;
        double t = (z1 > z0) ? (targetZ - z0) / (z1 - z0) : 0.0; // interpolation weight, 0..1

        for (size_t i = 0; i < sliceArea; i++) {
            // Linear interpolation between the two bracketing slices at
            // this (x,y) position: t=0 gives the lower slice exactly,
            // t=1 gives the upper slice exactly, in between blends smoothly.
            double blended = huSlices[lower][i] * (1.0 - t) + huSlices[upper][i] * t;
            grid.voxels[z * sliceArea + i] = static_cast<int16_t>(std::lround(blended));
        }
    }

    return grid;
}

// ASCII cross-section through the middle of the volume along the Z axis —
// i.e. a sagittal-style slice showing the stack from the *side*, not the
// original top-down view. This is only possible once slices are genuinely
// stacked into a 3D grid — a nice concrete proof reconstruction actually
// worked, not just that files were read.
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
    const int previewWidth = 60;  // maps to depth (Z)
    const int previewHeight = 20; // maps to height (Y)

    std::cout << "\nSagittal cross-section preview (side view through x=" << midX
              << ", " << previewWidth << "x" << previewHeight << "):\n\n";

    for (int row = 0; row < previewHeight; row++) {
        size_t srcY = row * grid.height / previewHeight;
        for (int col = 0; col < previewWidth; col++) {
            size_t srcZ = col * grid.depth / previewWidth;
            int16_t v = grid.at(midX, srcY, srcZ);
            double normalized = double(v - minVal) / double(maxVal - minVal);
            size_t rampIndex = static_cast<size_t>(normalized * (ramp.size() - 1));
            std::cout << ramp[rampIndex];
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "samples/series";

    std::vector<ParsedSlice> slices;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".dcm") continue;
        ParsedSlice s = parseDicomFile(entry.path().string());
        if (!s.ok) {
            std::cerr << "Skipping unparseable file: " << entry.path() << "\n";
            continue;
        }
        slices.push_back(s);
    }

    if (slices.size() < 2) {
        std::cerr << "Need at least 2 valid slices in " << dir << " to reconstruct a volume\n";
        return 1;
    }

    // Sort by physical position, not filename — filenames aren't guaranteed
    // to reflect scan order, SliceLocation always does.
    std::sort(slices.begin(), slices.end(),
              [](const ParsedSlice& a, const ParsedSlice& b) { return a.sliceLocation < b.sliceLocation; });

    // Reject inconsistent geometry up front rather than silently stacking
    // mismatched slices into a nonsensical volume.
    for (const auto& s : slices) {
        if (s.rows != slices.front().rows || s.columns != slices.front().columns) {
            std::cerr << "Inconsistent slice dimensions across the series — cannot reconstruct\n";
            return 1;
        }
    }

    std::vector<std::vector<int16_t>> huSlices;
    for (const auto& s : slices) huSlices.push_back(toHounsfieldUnits(s));

    VoxelGrid grid = stackSlicesIntoVolume(slices, huSlices);

    std::cout << "Loaded " << slices.size() << " slices, sorted by SliceLocation\n";
    std::cout << "Slice range: " << slices.front().sliceLocation << " to "
              << slices.back().sliceLocation << " mm\n";
    std::cout << "Reconstructed volume: " << grid.width << " x " << grid.height
              << " x " << grid.depth << " voxels\n";
    std::cout << "Voxel spacing: " << grid.spacingX << " x " << grid.spacingY
              << " x " << grid.spacingZ << " mm\n";

    printSagittalPreview(grid);
    return 0;
}
