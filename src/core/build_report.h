#pragma once
// Build report (roadmap 1.14 / 1.16): what a compiled extraction carries, budget warnings for it, a text summary, a diff
// against an earlier report (the Compile panel's "what changed since the last build") and a JSON save / load.
// Every count is read from the files compile_iw8.cpp writes; a missing file counts as zero.
#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

struct ImageInfo {
    std::string name;     // file name without .dds
    uint64_t bytes = 0;   // file size on disk (DDS data, compressed where the format is)
    uint32_t width = 0;   // from the DDS header; 0 when the file is not a DDS
    uint32_t height = 0;
    std::string format;   // FourCC, "DXGI n" (DX10 header), "RGB<bits>" (uncompressed), or "unknown"
    bool cubemap = false; // DDSCAPS2_CUBEMAP (the sky is a cube)
};

struct ClassCount {
    std::string classname;  // "(none)" for entities without a classname
    uint64_t count = 0;
};

struct BuildReport {
    std::string mapId;

    uint64_t materials = 0;             // materials\**\*.json
    uint64_t images = 0;                // images\*.dds
    uint64_t imageBytes = 0;            // total .dds file bytes (a proxy for image memory, not GPU memory)
    std::vector<ImageInfo> largestImages;  // up to 10, largest file first
    uint32_t maxImageSide = 0;          // largest width or height of any image
    std::string maxImageSideName;       // the image that has it
    std::vector<ImageInfo> largeImages; // every image over the 4096 px editor guideline (the budget checks these)

    uint64_t xmodels = 0;               // xmodel\<name>.json (one per model)
    uint64_t xmodelVerticesLod0 = 0;    // summed over every model's LOD 0 surfaces (xmodel\<name>.replay.json)
    uint64_t xmodelTrianglesLod0 = 0;   // indices / 3 of the same surfaces

    uint64_t worldSurfaces = 0;         // surfaces of the world render json
    uint64_t worldTriangles = 0;        // indices / 3 of those surfaces
    uint64_t worldModels = 0;           // static model placements (world json "models")
    uint64_t reflectionProbes = 0;      // world json "reflection_probes"
    uint64_t sunPrimaryLightIndex = 0;  // world json "sun_primary_light_index"

    uint64_t collisionSubmodels = 0;    // collision json "submodels"
    uint64_t collisionBrushes = 0;      // collision json "brushes"
    uint64_t collisionTriangles = 0;    // collision json "triangles" (0 when the build writes none)

    uint64_t entities = 0;              // entity blocks in the .ents text
    std::vector<ClassCount> entityClasses;  // top 10 by count, then name

    uint64_t primaryLights = 0;         // comworld json "primary_lights"

    uint64_t scriptBytes = 0;           // maps\mp\<id>.gsc
};

// Counts what the extraction carries for map `mapId`.
BuildReport makeBuildReport(const std::string& extractedDir, const std::string& mapId);

// Budget warnings for a report. Each starts with "native limit:" (a limit x64-zt enforces, cited in build_report.cpp) or
// "guideline:" (an editor threshold, not a measured engine limit). Empty when nothing is over.
std::vector<std::string> budgetWarnings(const BuildReport& r);

// Budget gauges (1.16, the Build Budget panel): each figure against its limit. native = a limit x64-zt enforces,
// otherwise an editor guideline.
struct BudgetGauge {
    std::string label;   // "World triangles"
    double value = 0;
    double limit = 0;
    bool native = false;
    std::string unit;    // "", "MB", "px", "KB"
    double fraction() const { return limit > 0 ? value / limit : 0; }
};
std::vector<BudgetGauge> budgetGauges(const BuildReport& r);

// Plain text summary, one line per figure.
std::string formatBuildReport(const BuildReport& r);

// Lines for every field that changed from `before` to `after`, e.g. "images: 120 -> 134 (+14), 210.5 MB -> 230.1 MB".
// "no changes" when nothing differs.
std::string diffBuildReports(const BuildReport& before, const BuildReport& after);

// JSON with schema 1 (written by hand, read back by loadBuildReport).
bool saveBuildReport(const BuildReport& r, const std::string& jsonPath, std::string* err = nullptr);
bool loadBuildReport(const std::string& jsonPath, BuildReport& out, std::string* err = nullptr);

}  // namespace iw8r
