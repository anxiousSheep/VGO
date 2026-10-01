#pragma once

#include <string>
#include <vector>

namespace vgo {

// A downloadable KataGo binary variant.
struct KataGoAsset {
    std::string variant;      // "eigen" | "opencl" | "cuda"
    std::string label;        // display name
    std::string officialUrl;  // primary source (GitHub release)
    std::vector<std::string> mirrors;  // fallback mirrors, tried before official
};

// A downloadable neural-network model.
struct ModelAsset {
    std::string id;           // stable key stored in config
    std::string name;         // display name
    std::string description;
    bool humanStyle = false;  // supports humanSLProfile (新手/业余段)
    std::string officialUrl;
    std::vector<std::string> mirrors;
};

// KataGo version we provision. v1.18.1 is the latest that ships OpenCL/Eigen
// (CPU/GPU) Windows builds; v1.18.2 dropped them (CUDA-only).
inline std::string kataGoVersion() { return "1.18.1"; }

// Binary variants for the default version. Eigen is the safe CPU fallback;
// OpenCL is preferred when a GPU is present.
std::vector<KataGoAsset> katagoAssets();

// Neural-network models the user can install. Order = display order.
std::vector<ModelAsset> modelAssets();

// Returns the model whose id matches, or nullptr.
const ModelAsset* findModel(const std::string& id);

}  // namespace vgo
