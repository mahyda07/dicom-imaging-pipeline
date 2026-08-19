#pragma once
// Exports volume slices as PNG images with detected regions outlined,
// using stb_image_write.h (public domain, nothings/stb) for the actual PNG
// encoding — hand-rolling the DEFLATE/PNG chunk format ourselves would be
// encoding-format busywork, not DICOM-domain engineering, so a small,
// well-established public-domain library is used for that one piece
// (same reasoning as using DCMTK as a reference tool in the ingestion
// layer, except this one IS linked into the build since we need to
// actually produce PNG bytes, not just validate against them).

#include "volume.h"
#include <vector>
#include <string>
#include <algorithm>

struct BoundingBox {
    size_t minX, maxX, minY, maxY, minZ, maxZ;
};

// Renders one Z-slice of the volume as an 8-bit grayscale image, windowed
// to a typical CT "soft tissue" display range, with any region whose
// bounding box includes this slice outlined in red.
inline std::vector<unsigned char> renderAnnotatedSlice(const VoxelGrid& grid, size_t z,
                                                          const std::vector<BoundingBox>& boxes) {
    // Windowing: map [-200, 400] HU to [0, 255] grayscale — a standard
    // "soft tissue" CT window. Values outside the range clip, same as a
    // real PACS viewer's windowing does.
    const int windowMin = -200, windowMax = 400;

    std::vector<unsigned char> rgb(grid.width * grid.height * 3);
    for (size_t y = 0; y < grid.height; y++) {
        for (size_t x = 0; x < grid.width; x++) {
            int16_t hu = grid.at(x, y, z);
            int clamped = std::clamp(static_cast<int>(hu), windowMin, windowMax);
            unsigned char gray = static_cast<unsigned char>(
                (clamped - windowMin) * 255 / (windowMax - windowMin));
            size_t idx = (y * grid.width + x) * 3;
            rgb[idx] = rgb[idx + 1] = rgb[idx + 2] = gray;
        }
    }

    // Draw a red outline for any box whose Z range includes this slice.
    for (const auto& box : boxes) {
        if (z < box.minZ || z > box.maxZ) continue;
        for (size_t x = box.minX; x <= box.maxX; x++) {
            for (size_t yy : {box.minY, box.maxY}) {
                if (yy >= grid.height || x >= grid.width) continue;
                size_t idx = (yy * grid.width + x) * 3;
                rgb[idx] = 255; rgb[idx + 1] = 0; rgb[idx + 2] = 0;
            }
        }
        for (size_t y = box.minY; y <= box.maxY; y++) {
            for (size_t xx : {box.minX, box.maxX}) {
                if (xx >= grid.width || y >= grid.height) continue;
                size_t idx = (y * grid.width + xx) * 3;
                rgb[idx] = 255; rgb[idx + 1] = 0; rgb[idx + 2] = 0;
            }
        }
    }

    return rgb;
}
