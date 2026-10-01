#include "ModelsManifest.h"

#include <string>
#include <utility>

namespace vgo {

namespace {

std::string githubRelease(const std::string& tag, const std::string& file) {
    return "https://github.com/lightvector/KataGo/releases/download/" + tag + "/" + file;
}

// 镜像列表：官方 github 在部分地区不可达时，依次尝试（实测可用）。
std::vector<std::string> mirrorsFor(const std::string& officialUrl) {
    if (officialUrl.empty()) return {};
    return {
        "https://ghfast.top/" + officialUrl,
        "https://gh-proxy.com/" + officialUrl,
    };
}

}  // namespace

std::vector<KataGoAsset> katagoAssets() {
    const std::string v = kataGoVersion();
    const std::string tag = "v" + v;
    auto make = [&](const char* variant, const char* label) {
        KataGoAsset a;
        a.variant = variant;
        a.label = label;
        a.officialUrl = githubRelease(tag, "katago-" + tag + "-" + variant + "-windows-x64.zip");
        a.mirrors = mirrorsFor(a.officialUrl);
        return a;
    };
    return {make("opencl", "OpenCL（GPU，推荐）"),
            make("eigen", "Eigen（CPU，保底）"),
            make("cuda", "CUDA（NVIDIA，高级）")};
}

std::vector<ModelAsset> modelAssets() {
    std::vector<ModelAsset> out;

    {
        ModelAsset m;
        m.id = "b18c384nbt-uec";
        m.name = "标准模型 b18c384nbt-uec";
        m.description = "职业段（visits）与普通对弈，约 97MB";
        m.officialUrl = githubRelease("v1.12.4", "b18c384nbt-uec.bin.gz");
        m.mirrors = mirrorsFor(m.officialUrl);
        out.push_back(std::move(m));
    }
    {
        ModelAsset m;
        m.id = "b18c384nbt-humanv0";
        m.name = "人类风格模型 b18c384nbt-humanv0";
        m.description = "新手/业余段（humanSLProfile）真实棋风，约 99MB";
        m.humanStyle = true;
        m.officialUrl = githubRelease("v1.15.0", "b18c384nbt-humanv0.bin.gz");
        m.mirrors = mirrorsFor(m.officialUrl);
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
