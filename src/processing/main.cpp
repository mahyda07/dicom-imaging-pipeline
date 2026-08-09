#include "dicom_parser.h"
#include "simd_filters.h"
#include "volume.h"
#include "thread_pool.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

std::vector<float> toFloatImage(const ParsedSlice& slice) {
    size_t n = size_t(slice.rows) * slice.columns;
    std::vector<float> img(n);
    for (size_t i = 0; i < n; i++) {
        uint16_t raw = (slice.syntax == TransferSyntax::ExplicitBigEndian)
                           ? (uint16_t(slice.pixelData[i * 2]) << 8) | slice.pixelData[i * 2 + 1]
                           : slice.pixelData[i * 2] | (uint16_t(slice.pixelData[i * 2 + 1]) << 8);
        img[i] = static_cast<float>(raw);
    }
    return img;
}

void printAsciiPreview(const std::vector<float>& img, int width, int height, const std::string& label) {
    float minVal = *std::min_element(img.begin(), img.end());
    float maxVal = *std::max_element(img.begin(), img.end());
    if (maxVal == minVal) maxVal = minVal + 1;

    static const std::string ramp = " .:-=+*#%@";
    const int pw = 60, ph = 25;
    std::cout << "\n" << label << ":\n\n";
    for (int y = 0; y < ph; y++) {
        int srcY = y * height / ph;
        for (int x = 0; x < pw; x++) {
            int srcX = x * width / pw;
            float v = img[size_t(srcY) * width + srcX];
            double n = (v - minVal) / (maxVal - minVal);
            std::cout << ramp[static_cast<size_t>(n * (ramp.size() - 1))];
        }
        std::cout << "\n";
    }
}

template <typename Fn>
double benchmark(Fn fn, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) fn();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count() / iterations;
}

void blurVolumeSingleThreaded(const VoxelGrid& grid, std::vector<std::vector<float>>& outSlices, int repeatsPerSlice) {
    size_t sliceArea = grid.width * grid.height;
    outSlices.resize(grid.depth);
    for (size_t z = 0; z < grid.depth; z++) {
        std::vector<float> sliceFloat(sliceArea);
        for (size_t i = 0; i < sliceArea; i++) sliceFloat[i] = grid.voxels[z * sliceArea + i];
        // Repeating the blur `repeatsPerSlice` times is a diagnostic knob,
        // not real pipeline behavior: it artificially inflates the work
        // per slice so we can tell whether poor speedup is caused by the
        // thread pool itself, or simply by there not being enough real
        // work per task for parallelism to pay off yet.
        for (int r = 0; r < repeatsPerSlice; r++) gaussianBlur(sliceFloat, outSlices[z], grid.width, grid.height, true);
    }
}

// Distributes work across the thread pool in CHUNKS of contiguous slices,
// not one task per slice, to amortize fixed per-task overhead over more
// real work. `repeatsPerSlice` is the same diagnostic knob as above.
void blurVolumeThreaded(const VoxelGrid& grid, std::vector<std::vector<float>>& outSlices, ThreadPool& pool,
                         int repeatsPerSlice) {
    size_t sliceArea = grid.width * grid.height;
    outSlices.resize(grid.depth);

    size_t numChunks = std::min(pool.threadCount(), grid.depth);
    size_t chunkSize = (grid.depth + numChunks - 1) / numChunks; // ceiling division

    std::vector<std::future<void>> futures;
    for (size_t chunkStart = 0; chunkStart < grid.depth; chunkStart += chunkSize) {
        size_t chunkEnd = std::min(chunkStart + chunkSize, grid.depth);
        futures.push_back(pool.submit([&grid, &outSlices, chunkStart, chunkEnd, sliceArea, repeatsPerSlice] {
            for (size_t z = chunkStart; z < chunkEnd; z++) {
                std::vector<float> sliceFloat(sliceArea);
                for (size_t i = 0; i < sliceArea; i++) sliceFloat[i] = grid.voxels[z * sliceArea + i];
                for (int r = 0; r < repeatsPerSlice; r++) gaussianBlur(sliceFloat, outSlices[z], grid.width, grid.height, true);
            }
        }));
    }
    for (auto& f : futures) f.get();
}

// Runs one single-threaded-vs-pooled comparison at a given synthetic
// workload size, and prints the result labeled with that size — used twice
// below (light, then heavy) to isolate whether limited speedup is caused by
// the pool itself or simply by too little real work per task.
void runOneBenchmark(const VoxelGrid& grid, ThreadPool& pool, unsigned int cores, int repeatsPerSlice, const char* label) {
    std::vector<std::vector<float>> resultSingle, resultPooled;
    const int iterations = 5;

    double singleUs = benchmark([&] { blurVolumeSingleThreaded(grid, resultSingle, repeatsPerSlice); }, iterations);
    double pooledUs = benchmark([&] { blurVolumeThreaded(grid, resultPooled, pool, repeatsPerSlice); }, iterations);

    float maxDiff = 0;
    for (size_t z = 0; z < grid.depth; z++)
        for (size_t i = 0; i < resultSingle[z].size(); i++)
            maxDiff = std::max(maxDiff, std::abs(resultSingle[z][i] - resultPooled[z][i]));

    double speedup = singleUs / pooledUs;
    std::cout << label << " (" << repeatsPerSlice << " blur pass(es) per slice, "
              << grid.depth << " slices, chunked into " << std::min((size_t)cores, grid.depth) << " tasks):\n";
    std::cout << "  Single-threaded: " << (singleUs / 1000.0) << " ms\n";
    std::cout << "  Thread pool (" << pool.threadCount() << " threads): " << (pooledUs / 1000.0) << " ms\n";
    std::cout << "  Speedup: " << speedup << "x  (ideal max with " << cores << " cores: " << cores << "x)\n";
    std::cout << "  Correctness: pooled vs single-threaded max diff: " << maxDiff
              << (maxDiff < 0.01f ? "  [MATCH]" : "  [MISMATCH]") << "\n\n";
}

void runVolumeBenchmark(const std::string& dir) {
    VoxelGrid grid = loadVolumeFromDirectory(dir);
    if (grid.depth == 0) return;

    unsigned int cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    std::cout << "Volume: " << grid.width << "x" << grid.height << "x" << grid.depth
              << " slices. Detected " << cores << " hardware threads.\n\n";

    ThreadPool pool(cores);

    // Light: 1 blur pass per slice — this is what a real single-pass
    // pipeline actually does. Total work here is genuinely tiny
    // (well under 2ms for the whole volume), so if speedup is modest here
    // but much better below, that CONFIRMS the limiting factor is workload
    // size relative to fixed overhead, not a flaw in the pool.
    runOneBenchmark(grid, pool, cores, 1, "LIGHT workload");

    // Heavy: 30 blur passes per slice — same code path, same pool, same
    // chunking, just artificially more real work per task. This isolates
    // the variable cleanly: if this speedup is much closer to `cores`,
    // the pool itself scales fine; the light case was simply too small a
    // workload for parallelism to pay off yet.
    runOneBenchmark(grid, pool, cores, 30, "HEAVY workload (diagnostic only)");
}

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "samples/ct_small.dcm";

    if (fs::is_directory(path)) {
        runVolumeBenchmark(path);
        return 0;
    }

    ParsedSlice slice = parseDicomFile(path);
    if (!slice.ok) {
        std::cerr << "Failed to parse " << path << "\n";
        return 1;
    }

    int width = slice.columns, height = slice.rows;
    std::vector<float> img = toFloatImage(slice);

    std::vector<float> blurScalar, blurAVX2, edgesScalar, edgesAVX2;
    gaussianBlur(img, blurScalar, width, height, false);
    gaussianBlur(img, blurAVX2,   width, height, true);
    sobelEdges(img, edgesScalar, width, height, false);
    sobelEdges(img, edgesAVX2,   width, height, true);

    float maxBlurDiff = 0, maxEdgeDiff = 0;
    for (size_t i = 0; i < img.size(); i++) {
        maxBlurDiff = std::max(maxBlurDiff, std::abs(blurScalar[i] - blurAVX2[i]));
        maxEdgeDiff = std::max(maxEdgeDiff, std::abs(edgesScalar[i] - edgesAVX2[i]));
    }
    std::cout << "Correctness check (scalar vs AVX2):\n";
    std::cout << "  Gaussian blur  max abs difference: " << maxBlurDiff
              << (maxBlurDiff < 0.01f ? "  [MATCH]" : "  [MISMATCH]") << "\n";
    std::cout << "  Sobel edges    max abs difference: " << maxEdgeDiff
              << (maxEdgeDiff < 0.01f ? "  [MATCH]" : "  [MISMATCH]") << "\n\n";

    const int iterations = 200;
    std::vector<float> tmp;

    double blurScalarUs = benchmark([&]{ gaussianBlur(img, tmp, width, height, false); }, iterations);
    double blurAVX2Us   = benchmark([&]{ gaussianBlur(img, tmp, width, height, true);  }, iterations);
    double edgeScalarUs = benchmark([&]{ sobelEdges(img, tmp, width, height, false);   }, iterations);
    double edgeAVX2Us   = benchmark([&]{ sobelEdges(img, tmp, width, height, true);    }, iterations);

    std::cout << "Benchmark (" << width << "x" << height << " image, " << iterations << " iterations):\n";
    std::cout << "  Gaussian blur  scalar: " << blurScalarUs << " us   AVX2: " << blurAVX2Us
              << " us   speedup: " << (blurScalarUs / blurAVX2Us) << "x\n";
    std::cout << "  Sobel edges    scalar: " << edgeScalarUs << " us   AVX2: " << edgeAVX2Us
              << " us   speedup: " << (edgeScalarUs / edgeAVX2Us) << "x\n\n";

    std::vector<int16_t> hu(img.size());
    for (size_t i = 0; i < img.size(); i++) hu[i] = static_cast<int16_t>(img[i]);
    int16_t minV = *std::min_element(hu.begin(), hu.end());
    int16_t maxV = *std::max_element(hu.begin(), hu.end());
    std::vector<int16_t> equalized;
    histogramEqualize(hu, equalized, minV, maxV);

    printAsciiPreview(img, width, height, "Original");
    printAsciiPreview(blurAVX2, width, height, "After Gaussian blur (AVX2)");
    printAsciiPreview(edgesAVX2, width, height, "After Sobel edge detection (AVX2)");

    std::vector<float> equalizedFloat(equalized.begin(), equalized.end());
    printAsciiPreview(equalizedFloat, width, height, "After histogram equalization");

    return 0;
}
