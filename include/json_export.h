#pragma once
// Minimal hand-rolled JSON writer for the findings report. A full JSON
// library is overkill for a fixed, known output shape like this — the only
// real risk with hand-rolling JSON is string escaping, which is handled
// explicitly below rather than assumed away.

#include <string>
#include <vector>
#include <sstream>
#include <fstream>

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Matches the Region struct in detection/main.cpp — duplicated here as
// plain fields rather than including detection's header, since this writer
// should stay usable independent of the detection algorithm's internals.
struct FindingRecord {
    int id;
    size_t voxelCount;
    double volumeMm3;
    size_t minX, maxX, minY, maxY, minZ, maxZ;
    double meanHU;
};

inline bool writeFindingsJson(const std::string& path, const std::string& sourceDir,
                               size_t volWidth, size_t volHeight, size_t volDepth,
                               double spacingX, double spacingY, double spacingZ,
                               int thresholdHU, const std::vector<FindingRecord>& findings) {
    std::ofstream out(path);
    if (!out) return false;

    out << "{\n";
    out << "  \"source_directory\": \"" << jsonEscape(sourceDir) << "\",\n";
    out << "  \"volume\": { \"width\": " << volWidth << ", \"height\": " << volHeight
        << ", \"depth\": " << volDepth << " },\n";
    out << "  \"voxel_spacing_mm\": { \"x\": " << spacingX << ", \"y\": " << spacingY
        << ", \"z\": " << spacingZ << " },\n";
    out << "  \"detection_threshold_hu\": " << thresholdHU << ",\n";
    out << "  \"finding_count\": " << findings.size() << ",\n";
    out << "  \"findings\": [\n";

    for (size_t i = 0; i < findings.size(); i++) {
        const auto& f = findings[i];
        out << "    {\n";
        out << "      \"id\": " << f.id << ",\n";
        out << "      \"voxel_count\": " << f.voxelCount << ",\n";
        out << "      \"volume_mm3\": " << f.volumeMm3 << ",\n";
        out << "      \"bounding_box\": { \"x\": [" << f.minX << ", " << f.maxX
            << "], \"y\": [" << f.minY << ", " << f.maxY
            << "], \"z\": [" << f.minZ << ", " << f.maxZ << "] },\n";
        out << "      \"mean_density_hu\": " << f.meanHU << "\n";
        out << "    }" << (i + 1 < findings.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return true;
}
