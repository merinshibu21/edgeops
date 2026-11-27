#pragma once
#include "models.hpp"
#include <string>
#include <vector>

// Interface for systemd supervision and journald access (SOLID: ISP)
class IServiceController {
public:
    virtual ~IServiceController() = default;
    virtual std::string getServiceState(const std::string& unit) = 0;
    virtual bool controlService(const std::string& unit, const std::string& action) = 0;
    virtual std::vector<std::string> getRecentLogs(int lines = 25) = 0;
};

class SystemdManager : public IServiceController {
public:
    static bool isSafeUnitName(const std::string& name);
    std::string getServiceState(const std::string& unit) override;
    bool controlService(const std::string& unit, const std::string& action) override;
    std::vector<std::string> getRecentLogs(int lines = 25) override;

private:
    static std::string runCommand(const std::string& cmd, int* exitCode = nullptr);
};
