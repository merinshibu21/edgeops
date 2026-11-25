#include "collectors.hpp"
#include "http_server.hpp"
#include "models.hpp"
#include "storage.hpp"
#include "systemd_mgr.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <thread>

static std::atomic<bool> g_running{true};
static void onSignal(int) { g_running = false; }

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

static Config loadConfig(const std::string& path) {
    Config cfg;
    struct stat st{};
    if (stat("/data", &st) == 0 && S_ISDIR(st.st_mode)) cfg.db_path = "/data/edgeops.db";

    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (k == "port") cfg.port = std::stoi(v);
        else if (k == "cpu_threshold") cfg.cpu_threshold = std::stod(v);
        else if (k == "mem_threshold") cfg.mem_threshold = std::stod(v);
        else if (k == "disk_threshold") cfg.disk_threshold = std::stod(v);
        else if (k == "db_path") cfg.db_path = v;
        else if (k == "web_root") cfg.web_root = v;
        else if (k == "services") {
            cfg.services.clear();
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = trim(item);
                if (!item.empty()) cfg.services.push_back(item);
            }
        }
    }
    return cfg;
}

static void monitorLoop(const Config& cfg, IMetricCollector& collector,
                        IServiceController& svcCtrl, StorageManager& storage) {
    while (g_running) {
        for (int i = 0; i < 5 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!g_running) break;

        SystemMetrics m = collector.collect();
        double diskPct = m.disks.empty() ? 0.0 : m.disks[0].used_pct;

        if (m.cpu_pct > cfg.cpu_threshold) {
            storage.raiseAlarm("CRITICAL", "cpu",
                std::string("CPU usage is ") + std::to_string(m.cpu_pct) + "%");
        } else {
            storage.resolveAlarm("cpu");
        }
        if (m.mem_used_pct > cfg.mem_threshold) {
            storage.raiseAlarm("WARNING", "memory",
                std::string("Memory usage is ") + std::to_string(m.mem_used_pct) + "%");
        } else {
            storage.resolveAlarm("memory");
        }
        if (diskPct > cfg.disk_threshold) {
            storage.raiseAlarm("CRITICAL", "storage",
                std::string("Storage usage is ") + std::to_string(diskPct) + "%");
        } else {
            storage.resolveAlarm("storage");
        }

        for (const auto& svc : cfg.services) {
            std::string st = svcCtrl.getServiceState(svc);
            if (st != "active") {
                storage.raiseAlarm("CRITICAL", "service:" + svc,
                    "Service " + svc + " is " + st);
            } else {
                storage.resolveAlarm("service:" + svc);
            }
        }
        storage.saveSample(APP_VERSION, m.cpu_pct, m.mem_used_pct, diskPct);
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::string cfgPath = "/etc/edgeops/edgeops.conf";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") cfgPath = argv[i + 1];
    }

    Config cfg = loadConfig(cfgPath);
    StorageManager storage;
    if (!storage.open(cfg.db_path)) return 1;

    LinuxTelemetryCollector collector;
    SystemdManager svcCtrl;
    HttpServer server(cfg, collector, svcCtrl, storage);

    std::thread worker(monitorLoop, std::cref(cfg), std::ref(collector), std::ref(svcCtrl), std::ref(storage));
    std::cout << "EdgeOps Agent v" << APP_VERSION << " listening on port " << cfg.port << "\n";
    server.serve(g_running);
    worker.join();
    return 0;
}
