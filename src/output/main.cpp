#include "volume.h"
#include "json_export.h"
#include "png_export.h"
#include <iostream>
#include <vector>
#include <queue>
#include <array>
#include <algorithm>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

// Same region-growing detection as src/detection/main.cpp — duplicated
// intentionally rather than #including detection's main.cpp (which has its
// own `main`). A future cleanup could pull this into a shared
// include/detection.h the way ingestion/reconstruction were factored out;
// noted honestly here rather than silently living with the duplication.
struct Region {
    size_t voxelCount = 0;
    size_t minX = SIZE_MAX, maxX = 0, minY = SIZE_MAX, maxY = 0, minZ = SIZE_MAX, maxZ = 0;
    double meanHU = 0.0;
};

std::vector<Region> findAnomalousRegions(const VoxelGrid& grid, int16_t threshold, size_t minRegionVoxels = 8) {
    std::vector<bool> visited(grid.voxels.size(), false);
    std::vector<Region> regions;
    auto index = [&](size_t x, size_t y, size_t z) { return z * grid.width * grid.height + y * grid.width + x; };

    for (size_t z = 0; z < grid.depth; z++) {
        for (size_t y = 0; y < grid.height; y++) {
            for (size_t x = 0; x < grid.width; x++) {
                size_t idx = index(x, y, z);
                if (visited[idx] || grid.voxels[idx] < threshold) continue;

                Region region;
                long long huSum = 0;
                std::queue<std::array<size_t, 3>> frontier;
                frontier.push({x, y, z});
                visited[idx] = true;

                while (!frontier.empty()) {
                    auto [cx, cy, cz] = frontier.front();
                    frontier.pop();
                    region.voxelCount++;
                    huSum += grid.at(cx, cy, cz);
                    region.minX = std::min(region.minX, cx); region.maxX = std::max(region.maxX, cx);
                    region.minY = std::min(region.minY, cy); region.maxY = std::max(region.maxY, cy);
                    region.minZ = std::min(region.minZ, cz); region.maxZ = std::max(region.maxZ, cz);

                    static const int dx[] = {1, -1, 0, 0, 0, 0};
                    static const int dy[] = {0, 0, 1, -1, 0, 0};
                    static const int dz[] = {0, 0, 0, 0, 1, -1};
                    for (int n = 0; n < 6; n++) {
                        long long nx = (long long)cx + dx[n], ny = (long long)cy + dy[n], nz = (long long)cz + dz[n];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= (long long)grid.width ||
                            ny >= (long long)grid.height || nz >= (long long)grid.depth) continue;
                        size_t nIdx = index(nx, ny, nz);
                        if (visited[nIdx] || grid.voxels[nIdx] < threshold) continue;
                        visited[nIdx] = true;
                        frontier.push({(size_t)nx, (size_t)ny, (size_t)nz});
                    }
                }
                region.meanHU = double(huSum) / region.voxelCount;
                if (region.voxelCount >= minRegionVoxels) regions.push_back(region);
            }
        }
    }
    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.voxelCount > b.voxelCount; });
    return regions;
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "samples/series_large";
    int16_t threshold = (argc > 2) ? static_cast<int16_t>(std::stoi(argv[2])) : 400;
    std::string outDir = (argc > 3) ? argv[3] : "output";

    VoxelGrid grid = loadVolumeFromDirectory(dir);
    if (grid.depth == 0) return 1;

    std::cout << "Volume: " << grid.width << " x " << grid.height << " x " << grid.depth << " voxels\n";

    std::vector<Region> regions = findAnomalousRegions(grid, threshold);
    std::cout << "Found " << regions.size() << " region(s) at threshold " << threshold << " HU\n";

    fs::create_directories(outDir);

    // --- JSON findings report ---
    std::vector<FindingRecord> records;
    for (size_t i = 0; i < regions.size(); i++) {
        const auto& r = regions[i];
        double volMm3 = r.voxelCount * grid.spacingX * grid.spacingY * grid.spacingZ;
        records.push_back({static_cast<int>(i + 1), r.voxelCount, volMm3,
                            r.minX, r.maxX, r.minY, r.maxY, r.minZ, r.maxZ, r.meanHU});
    }
    std::string jsonPath = outDir + "/findings.json";
    bool jsonOk = writeFindingsJson(jsonPath, dir, grid.width, grid.height, grid.depth,
                                     grid.spacingX, grid.spacingY, grid.spacingZ, threshold, records);
    std::cout << (jsonOk ? "Wrote " : "FAILED to write ") << jsonPath << "\n";

    // --- Annotated PNG slices ---
    std::vector<BoundingBox> boxes;
    for (const auto& r : regions) boxes.push_back({r.minX, r.maxX, r.minY, r.maxY, r.minZ, r.maxZ});

    // Only export slices that actually intersect a detected region, plus
    // a couple of surrounding context slices — exporting all 40 slices when
    // only 8 have findings would bury the useful ones in noise.
    std::vector<bool> exportSlice(grid.depth, false);
    for (const auto& box : boxes) {
        for (size_t z = (box.minZ > 2 ? box.minZ - 2 : 0); z <= std::min(box.maxZ + 2, grid.depth - 1); z++)
            exportSlice[z] = true;
    }

    int pngCount = 0;
    for (size_t z = 0; z < grid.depth; z++) {
        if (!exportSlice[z]) continue;
        std::vector<unsigned char> rgb = renderAnnotatedSlice(grid, z, boxes);
        std::string pngPath = outDir + "/slice_" + std::to_string(z) + ".png";
        int ok = stbi_write_png(pngPath.c_str(), static_cast<int>(grid.width), static_cast<int>(grid.height),
                                 3, rgb.data(), static_cast<int>(grid.width * 3));
        if (ok) pngCount++;
    }
    std::cout << "Wrote " << pngCount << " annotated PNG slice(s) to " << outDir << "/\n";

    return 0;
}
