#pragma once

#include <QString>

namespace vgo {

// Best-effort GPU capability detection, used to pick a KataGo binary variant.
enum class GpuKind { Cpu, OpenCl, Cuda };

GpuKind detectGpuKind();
QString gpuKindName(GpuKind kind);

}  // namespace vgo
