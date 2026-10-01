#include "GpuDetect.h"

#include <windows.h>

namespace vgo {

namespace {

// 只检查 OpenCL.dll 是否可加载，绝不调用 clGetPlatformIDs——
// 后者会枚举各厂商 ICD/驱动，遇到损坏的 ICD 会内存损坏/崩溃，且时机不定。
// 实际 OpenCL 平台由 KataGo 启动时自行检测，失败则回退离线引擎。
bool hasOpenCl() {
    HMODULE dll = LoadLibraryW(L"OpenCL.dll");
    if (!dll) return false;
    FreeLibrary(dll);
    return true;
}

bool hasCuda() {
    HMODULE dll = LoadLibraryW(L"nvcuda.dll");
    if (!dll) return false;
    FreeLibrary(dll);
    return true;
}

}  // namespace

GpuKind detectGpuKind() {
    if (hasOpenCl()) return GpuKind::OpenCl;
    if (hasCuda()) return GpuKind::Cuda;
    return GpuKind::Cpu;
}

QString gpuKindName(GpuKind kind) {
    switch (kind) {
        case GpuKind::OpenCl: return QStringLiteral("OpenCL");
        case GpuKind::Cuda:   return QStringLiteral("CUDA");
        default:              return QStringLiteral("CPU");
    }
}

}  // namespace vgo
