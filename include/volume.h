#pragma once
#include "dicom_parser.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;

struct VoxelGrid {
    std::vector<int16_t> voxels;
    size_t width = 0, height = 0, depth = 0;
    double spacingX = 1.0, spacingY = 1.0, spacingZ = 1.0;

    int16_t at(size_t x, size_t y, size_t z) const {
        return voxels[z * width * height + y * width + x];
    }
    void set(size_t x, size_t y, size_t z, int16_t v) {
        voxels[z * width * height + y * width + x] = v;
    }
};

inline std::vector<int16_t> toHounsfieldUnits(const ParsedSlice& slice) {
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

inline VoxelGrid stackSlicesIntoVolume(const std::vector<ParsedSlice>& sortedSlices,
                                       const std::vector<std::vector<int16_t>>& huSlices) {
    VoxelGrid grid;
    grid.width = sortedSlices.front().columns;
    grid.height = sortedSlices.front().rows;
    grid.spacingX = sortedSlices.front().pixelSpacingCol;
    grid.spacingY = sortedSlices.front().pixelSpacingRow;

    double minGap = 1e9;
    for (size_t i = 1; i < sortedSlices.size(); i++) {
        double gap = sortedSlices[i].sliceLocation - sortedSlices[i - 1].sliceLocation;
        if (gap > 1e-6) minGap = std::min(minGap, gap);
    }
    if (minGap > 1e8) minGap = 1.0;

    double firstZ = sortedSlices.front().sliceLocation;
    double lastZ = sortedSlices.back().sliceLocation;
    grid.depth = static_cast<size_t>(std::round((lastZ - firstZ) / minGap)) + 1;
    grid.spacingZ = minGap;

    size_t sliceArea = grid.width * grid.height;
    grid.voxels.resize(sliceArea * grid.depth);

    for (size_t z = 0; z < grid.depth; z++) {
        double targetZ = firstZ + z * minGap;
        size_t lower = 0;
        while (lower + 1 < sortedSlices.size() && sortedSlices[lower + 1].sliceLocation <= targetZ)
            lower++;
        size_t upper = std::min(lower + 1, sortedSlices.size() - 1);

        double z0 = sortedSlices[lower].sliceLocation;
        double z1 = sortedSlices[upper].sliceLocation;
        double t = (z1 > z0) ? (targetZ - z0) / (z1 - z0) : 0.0;

        for (size_t i = 0; i < sliceArea; i++) {
            double blended = huSlices[lower][i] * (1.0 - t) + huSlices[upper][i] * t;
            grid.voxels[z * sliceArea + i] = static_cast<int16_t>(std::lround(blended));
        }
    }
    return grid;
}

inline VoxelGrid loadVolumeFromDirectory(const std::string& dir) {
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

    VoxelGrid empty;
    if (slices.size() < 2) {
        std::cerr << "Need at least 2 valid slices in " << dir << " to reconstruct a volume\n";
        return empty;
    }

    std::sort(slices.begin(), slices.end(),
              [](const ParsedSlice& a, const ParsedSlice& b) { return a.sliceLocation < b.sliceLocation; });

    for (const auto& s : slices) {
        if (s.rows != slices.front().rows || s.columns != slices.front().columns) {
            std::cerr << "Inconsistent slice dimensions across the series — cannot reconstruct\n";
            return empty;
        }
    }

    std::vector<std::vector<int16_t>> huSlices;
    for (const auto& s : slices) huSlices.push_back(toHounsfieldUnits(s));

    return stackSlicesIntoVolume(slices, huSlices);
}
