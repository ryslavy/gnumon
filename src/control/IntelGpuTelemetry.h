#pragma once

#include "GpuTelemetry.h"
#include <string>
#include <filesystem>

namespace gnumon::control {

class IntelGpuTelemetry : public IGpuTelemetryProvider {
public:
    IntelGpuTelemetry() = default;
    ~IntelGpuTelemetry() override = default;

    bool Initialize() override;
    bool Sample(GpuMetrics& metrics) override;
    const std::string& GetName() const override { return providerName_; }

private:
    std::string providerName_ = "Intel DRM/sysfs";
    std::string gpuModelName_ = "Intel Graphics";
    std::filesystem::path cardPath_;
    std::filesystem::path hwmonPath_;
    std::filesystem::path freqPath_;
    bool isInitialized_ = false;

    static double ReadSysfsDouble(const std::filesystem::path& path, double divisor = 1.0);
    static uint64_t ReadSysfsUint64(const std::filesystem::path& path);
    static std::string ReadSysfsString(const std::filesystem::path& path);
};

} // namespace gnumon::control
