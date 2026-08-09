#include "volume.h"
#include <iostream>
#include <vector>
#include <queue>
#include <array>
#include <algorithm>

// One detected region: a connected group of voxels all above the density
// threshold, described the way a radiologist would want to see it — where
// it is, how big it is, and how dense.
struct Region {
    size_t voxelCount = 0;
    size_t minX = SIZE_MAX, maxX = 0, minY = SIZE_MAX, maxY = 0, minZ = SIZE_MAX, maxZ = 0;
    double meanHU = 0.0;
};

// 3D region growing via 6-connected flood fill: starting from any
// above-threshold voxel not yet visited, expand to its 6 face-adjacent
// neighbors (not diagonal) as long as they're also above threshold. Every
// voxel visited this way belongs to the same connected component — this
// flood fill IS the connected-component labeling, not a separate pass.
//
// Threshold rationale: +400 HU sits solidly above soft tissue (roughly
// 0 to +100 HU) and below/at the start of dense bone/calcification
// (roughly +400 and up clinically), so it separates "normal tissue" from
// "worth flagging" without being tuned to this specific test volume.
std::vector<Region> findAnomalousRegions(const VoxelGrid& grid, int16_t threshold,
                                          size_t minRegionVoxels = 8) {
    std::vector<bool> visited(grid.voxels.size(), false);
    std::vector<Region> regions;

    auto index = [&](size_t x, size_t y, size_t z) { return z * grid.width * grid.height + y * grid.width + x; };

    for (size_t z = 0; z < grid.depth; z++) {
        for (size_t y = 0; y < grid.height; y++) {
            for (size_t x = 0; x < grid.width; x++) {
                size_t idx = index(x, y, z);
                if (visited[idx] || grid.voxels[idx] < threshold) continue;

                // Found a new, unvisited above-threshold voxel — grow a
                // fresh region from here via breadth-first flood fill.
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

                    // 6-connected neighbors: +/-1 along each of the 3 axes.
                    static const int dx[] = {1, -1, 0, 0, 0, 0};
                    static const int dy[] = {0, 0, 1, -1, 0, 0};
                    static const int dz[] = {0, 0, 0, 0, 1, -1};

                    for (int n = 0; n < 6; n++) {
                        long long nx = (long long)cx + dx[n];
                        long long ny = (long long)cy + dy[n];
                        long long nz = (long long)cz + dz[n];
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= (long long)grid.width || ny >= (long long)grid.height || nz >= (long long)grid.depth)
                            continue;

                        size_t nIdx = index(nx, ny, nz);
                        if (visited[nIdx] || grid.voxels[nIdx] < threshold) continue;

                        visited[nIdx] = true;
                        frontier.push({(size_t)nx, (size_t)ny, (size_t)nz});
                    }
                }

                region.meanHU = double(huSum) / region.voxelCount;
                // Filter out tiny noise blobs (a handful of stray bright
                // voxels) rather than reporting every single one as a
                // "finding" — a real radiologist wouldn't care about a
                // 2-voxel speckle.
                if (region.voxelCount >= minRegionVoxels) regions.push_back(region);
            }
        }
    }

    std::sort(regions.begin(), regions.end(),
              [](const Region& a, const Region& b) { return a.voxelCount > b.voxelCount; });
    return regions;
}

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "samples/series_large";
    int16_t threshold = (argc > 2) ? static_cast<int16_t>(std::stoi(argv[2])) : 400;

    VoxelGrid grid = loadVolumeFromDirectory(dir);
    if (grid.depth == 0) return 1;

    std::cout << "Volume: " << grid.width << " x " << grid.height << " x " << grid.depth
              << " voxels, spacing " << grid.spacingX << "x" << grid.spacingY << "x" << grid.spacingZ << " mm\n";
    std::cout << "Density threshold: " << threshold << " HU (roughly the start of dense/calcified tissue)\n\n";

    std::vector<Region> regions = findAnomalousRegions(grid, threshold);

    if (regions.empty()) {
        std::cout << "No regions above threshold found.\n";
        return 0;
    }

    std::cout << "Found " << regions.size() << " region(s):\n\n";
    for (size_t i = 0; i < regions.size(); i++) {
        const Region& r = regions[i];
        double physicalVolumeMm3 = r.voxelCount * grid.spacingX * grid.spacingY * grid.spacingZ;
        std::cout << "Region " << (i + 1) << ":\n";
        std::cout << "  Voxel count: " << r.voxelCount << "  (~" << physicalVolumeMm3 << " mm^3)\n";
        std::cout << "  Bounding box: x[" << r.minX << "-" << r.maxX << "] y[" << r.minY << "-" << r.maxY
                  << "] z[" << r.minZ << "-" << r.maxZ << "]\n";
        std::cout << "  Mean density: " << r.meanHU << " HU\n\n";
    }

    return 0;
}
