#pragma once
#include <string>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION "1.0"
#endif

struct Config {
    int port = 8080;
    double cpu_threshold = 80.0;
    double mem_threshold = 80.0;
    double disk_threshold = 85.0;
    std::vector<std::string> services = {"systemd-journald", "systemd-networkd"};
    std::string web_root = "/usr/share/edgeops/web";
    std::string db_path = "/var/lib/edgeops/edgeops.db";
};

struct DiskStats {
    std::string mount;
    unsigned long long total_mb = 0;
    unsigned long long free_mb = 0;
    double used_pct = 0.0;
};

struct NetStats {
    std::string iface;
    std::string operstate;
    unsigned long long rx_bytes = 0;
    unsigned long long tx_bytes = 0;
};

struct SystemMetrics {
    double uptime_sec = 0.0;
    double cpu_pct = 0.0;
    unsigned long mem_total_kb = 0;
    unsigned long mem_avail_kb = 0;
    double mem_used_pct = 0.0;
    std::vector<DiskStats> disks;
    std::vector<NetStats> network;
};

struct ServiceInfo {
    std::string name;
    std::string state;
};
