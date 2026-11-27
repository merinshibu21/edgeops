#pragma once
#include "collectors.hpp"
#include "models.hpp"
#include "storage.hpp"
#include "systemd_mgr.hpp"
#include <atomic>
#include <string>

// Embedded REST API and web dashboard HTTP server (FR-07)
class HttpServer {
public:
    HttpServer(const Config& cfg,
               IMetricCollector& collector,
               IServiceController& svcCtrl,
               StorageManager& storage);
    void serve(std::atomic<bool>& running);

private:
    const Config& cfg_;
    IMetricCollector& collector_;
    IServiceController& svcCtrl_;
    StorageManager& storage_;

    void handleClient(int clientFd);
    std::string buildStatusJson();
    std::string buildLogsJson();
    static std::string jsonEscape(const std::string& s);
    static void sendHttp(int fd, int code, const std::string& status,
                         const std::string& type, const std::string& body);
};
