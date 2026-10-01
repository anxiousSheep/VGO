#include "ModelsManifest.h"

#include <string>

namespace vgo {

namespace {

std::string gh(const std::string& rel) {
    // Official GitHub release asset URL pattern.
    return "https://github.com/lightvector/KataGo/releases/download/" + rel;
}

std::string mirror(const std::string& url) {
    // Generic mirror prefix; a self-hosted mirror can be substituted here.
    // Empty string means "no mirror".
    if (url.empty()) return {};
    return "https://ghproxy.com/" + url;
}

}  // namespace

std::vector<KataGoAsset> katagoAssets() {
    const std::string v = kataGoVersion();
    auto make = [&](const char* variant, const char* label) {
        KataGoAsset a;
        a.variant = variant;
        a.label = label;
        const std::string rel =
            "v" + v + "/katago-v" + v + "-" + variant + "-windows-x64.zip";
        a.officialUrl = gh(rel);
        a.mirrorUrl = mirror(a.officialUrl);
        return a;
    };
    return {make("opencl", "OpenCL（GPU，推荐）"), make("eigen", "Eigen（CPU，保底）"),
            make("cuda", "CUDA（NVIDIA，高级）")};
}

std::vector<ModelAsset> modelAssets() {
    auto net = [](const std::string& file) {
        return "https://media.katagotraining.org/uploaded/networks/models/kata1/" + file;
    };
    std::vector<ModelAsset> out;

    {
        ModelAsset m;
        m.id = "b18c384nbt";
        m.name = "标准模型 b18c384nbt";
        m.description = "职业段（visits）与普通对弈，约 100MB 级";
        m.officialUrl = net("b18c384nbt-uec.bin.gz");
        m.mirrorUrl = mirror(m.officialUrl);
        out.push_back(std::move(m));
    }
    {
        ModelAsset m;
        m.id = "b18c384nbt-humanv0";
        m.name = "人类风格模型 b18c384nbt-humanv0";
        m.description = "新手/业余段（humanSLProfile）真实棋风，约 100MB 级";
        m.humanStyle = true;
        m.officialUrl = net("b18c384nbt-humanv0.bin.gz");
        m.mirrorUrl = mirror(m.officialUrl);
        out.push_back(std::move(m));
    }
    {
        ModelAsset m;
        m.id = "b6c96";
        m.name = "快速小模型 b6c96";
        m.description = "低配机/秒回，强度弱，约 20MB 级";
        m.officialUrl = net("b6c96-uec.bin.gz");
        m.mirrorUrl = mirror(m.officialUrl);
        out.push_back(std::move(m));
    }
    return out;
}

const ModelAsset* findModel(const std::string& id) {
    for (const auto& m : modelAssets()) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

}  // namespace vgo
