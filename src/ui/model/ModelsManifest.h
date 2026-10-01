#pragma once

#include <string>
#include <vector>

namespace vgo {

// A downloadable KataGo binary variant.
struct KataGoAsset {
    std::string variant;      // "eigen" | "opencl" | "cuda"
    std::string label;        // display name
    std::string officialUrl;  // primary source
    std::string mirrorUrl;    // fallback source (may be empty)
};

// A downloadable neural-network model.
struct ModelAsset {
    std::string id;           // stable key stored in config
    std::string name;         // display name
    std::string description;
    bool humanStyle = false;  // supports humanSLProfile (新手/业余段)
    std::string officialUrl;
    std::string mirrorUrl;    // may be empty
};

// KataGo version we provision by default. URLs follow the GitHub release asset
// naming: katago-v<ver>-<variant>-windows-x64.zip (contains katago.exe).
inline std::string kataGoVersion() { return "1.15.3"; }

// Binary variants for the default version. Eigen is the safe CPU fallback;
// OpenCL is preferred when a GPU is present.
std::vector<KataGoAsset> katagoAssets();

// Neural-network models the user can install. Order = display order.
std::vector<ModelAsset> modelAssets();

// Returns the model whose id matches, or nullptr.
const ModelAsset* findModel(const std::string& id);

}  // namespace vgo
