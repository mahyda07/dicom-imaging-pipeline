#include "dicom_parser.h"
#include "simd_filters.h"
#include <iostream>
#include <chrono>
#include <algorithm>

// Converts one slice's raw pixel bytes into a plain float array (0..65535
// range), the working representation the filters operate on.
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

// Runs `fn` `iterations` times and returns average microseconds per call.
template <typename Fn>
double benchmark(Fn fn, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) fn();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count() / iterations;
}

int main(int argc, char** argv) {
    std::string path = (argc > 1) ? argv[1] : "samples/ct_small.dcm";
    ParsedSlice slice = parseDicomFile(path);
    if (!slice.ok) {
        std::cerr << "Failed to parse " << path << "\n";
        return 1;
    }

    int width = slice.columns, height = slice.rows;
    std::vector<float> img = toFloatImage(slice);

    // --- Correctness check: scalar and AVX2 must agree ---------------------
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

    // --- Benchmark: scalar vs AVX2, same image, same iteration count -------
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

    // --- Histogram equalization (scalar-only, see header comment for why) --
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
