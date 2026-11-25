#pragma once
#include "models.hpp"
#include <string>

// Strategy interface for telemetry collection (SOLID: DIP & ISP)
class IMetricCollector {
public:
    virtual ~IMetricCollector() = default;
    virtual SystemMetrics collect() = 0;
};

// Reads real Linux /proc, /sys, and statvfs() telemetry (FR-01, FR-02, FR-03)
class LinuxTelemetryCollector : public IMetricCollector {
public:
    LinuxTelemetryCollector();
    SystemMetrics collect() override;

    static double parseCpuStat(const std::string& statLine,
                               unsigned long long& prevIdle,
                               unsigned long long& prevTotal);
    static void parseMemInfo(const std::string& content,
                             unsigned long& totalKb,
                             unsigned long& availKb,
                             double& usedPct);

private:
    unsigned long long prev_idle_ = 0;
    unsigned long long prev_total_ = 0;
    double readCpu();
    DiskStats readDisk(const std::string& mount);
    std::vector<NetStats> readNetwork();
};
