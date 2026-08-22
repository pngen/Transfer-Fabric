#pragma once

#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/platform/cuda.hpp"

namespace transfer_fabric {

// Real CUDA capability detection. This queries the actual driver/toolkit rather
// than assuming the hardware exists, and it distinguishes "no device" from
// "usable device". It is honest about the absence of a device.
TF_API bool cuda_available() noexcept;
TF_API std::vector<platform::cuda::DeviceInfo> cuda_devices() noexcept;

// A compact, tool-friendly summary string for the CLI / info commands.
TF_API std::string cuda_summary() noexcept;

} // namespace transfer_fabric
