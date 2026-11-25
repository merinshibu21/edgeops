#pragma once
#include "models.hpp"
#include <mutex>
#include <sqlite3.h>
#include <string>

// Persistence layer for metrics and alarm history (FR-06, FR-10)
class StorageManager {
public:
    StorageManager() = default;
    ~StorageManager();

    bool open(const std::string& dbPath);

    void saveSample(const std::string& version, double cpu, double mem, double disk);

    void raiseAlarm(const std::string& severity,
                    const std::string& source,
                    const std::string& msg);

    void resolveAlarm(const std::string& source);

    std::string getHistoryJson(int limit = 30);
    std::string getAlarmsJson(int limit = 30);
    std::string getAlarmHistoryJson(int limit = 30);

private:
    sqlite3* db_ = nullptr;
    std::mutex mutex_;

    static void ensureDir(const std::string& path);
    static std::string jsonEscape(const std::string& s);
};
