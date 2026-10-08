#pragma once

#include "GpuTelemetry.h"
#include <string>
#include <filesystem>

namespace gnumon::control {

class AmdGpuTelemetry : public IGpuTelemetryProvider {
public:
    AmdGpuTelemetry() = default;
    ~AmdGpuTelemetry() override = default;

    bool Initialize() override;
    bool Sample(GpuMetrics& metrics) override;
    const std::string& GetName() const override { return providerName_; }

private:
    std::string providerName_ = "AMD drm/hwmon";
    std::filesystem::path cardPath_;
    std::filesystem::path hwmonPath_;
    bool isInitialized_ = false;

    static double ReadSysfsDouble(const std::filesystem::path& path, double divisor = 1.0);
    static uint64_t ReadSysfsUint64(const std::filesystem::path& path);
    static std::string ReadSysfsString(const std::filesystem::path& path);
};

} // namespace gnumon::control
