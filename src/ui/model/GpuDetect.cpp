#include "GpuDetect.h"

#include <windows.h>

namespace vgo {

namespace {

using ClGetPlatformIDsFn = int (*)(unsigned, void*, unsigned*);

bool hasOpenCl() {
    HMODULE dll = LoadLibraryW(L"OpenCL.dll");
    if (!dll) return false;
    auto fn = reinterpret_cast<ClGetPlatformIDsFn>(
        reinterpret_cast<void*>(GetProcAddress(dll, "clGetPlatformIDs")));
    unsigned count = 0;
    const bool ok = fn && fn(0, nullptr, &count) == 0 && count > 0;
    FreeLibrary(dll);
    return ok;
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
