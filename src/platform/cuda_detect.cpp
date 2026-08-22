#include "transfer_fabric/platform/cuda_detect.hpp"

#include <sstream>

namespace transfer_fabric {

bool cuda_available() noexcept {
    return platform::cuda::device_count() > 0;
}

std::vector<platform::cuda::DeviceInfo> cuda_devices() noexcept {
    int n = platform::cuda::device_count();
    std::vector<platform::cuda::DeviceInfo> out;
    if (n <= 0) return out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        platform::cuda::DeviceInfo info;
        if (platform::cuda::get_device_info(i, info)) out.push_back(info);
    }
    return out;
}

std::string cuda_summary() noexcept {
    int n = platform::cuda::device_count();
    std::ostringstream os;
    os << "cuda_devices=" << n;
    if (n > 0) {
        platform::cuda::DeviceInfo d;
        if (platform::cuda::get_device_info(0, d)) {
            os << " name=\"" << d.name << "\" vram_bytes=" << d.vram_bytes
               << " compute=" << d.compute_major << "." << d.compute_minor;
        }
    }
    return os.str();
}

} // namespace transfer_fabric
