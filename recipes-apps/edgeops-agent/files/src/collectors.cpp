#include "collectors.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <cstdlib>
#include <fstream>
#include <sstream>

LinuxTelemetryCollector::LinuxTelemetryCollector() {
    readCpu(); // Prime initial /proc/stat baseline
}

double LinuxTelemetryCollector::parseCpuStat(const std::string& line,
                                             unsigned long long& prevIdle,
                                             unsigned long long& prevTotal) {
    std::istringstream ss(line);
    std::string cpu;
    unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    if (!(ss >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal)) return 0.0;
    unsigned long long idleAll = idle + iowait;
    unsigned long long total = user + nice + sys + idleAll + irq + softirq + steal;
    unsigned long long diffTotal = total - prevTotal;
    unsigned long long diffIdle = idleAll - prevIdle;
    prevTotal = total;
    prevIdle = idleAll;
    if (diffTotal == 0) return 0.0;
    return 100.0 * static_cast<double>(diffTotal - diffIdle) / static_cast<double>(diffTotal);
}

void LinuxTelemetryCollector::parseMemInfo(const std::string& content,
                                           unsigned long& totalKb,
                                           unsigned long& availKb,
                                           double& usedPct) {
    totalKb = 0;
    availKb = 0;
    usedPct = 0.0;
    std::istringstream ss(content);
    std::string key, unit;
    unsigned long val = 0;
    while (ss >> key >> val >> unit) {
        if (key == "MemTotal:") totalKb = val;
        else if (key == "MemAvailable:") availKb = val;
    }
    if (totalKb > 0) {
        unsigned long used = (totalKb > availKb) ? (totalKb - availKb) : 0;
        usedPct = 100.0 * static_cast<double>(used) / static_cast<double>(totalKb);
    }
}

double LinuxTelemetryCollector::readCpu() {
    std::ifstream in("/proc/stat");
    std::string line;
    std::getline(in, line);
    return parseCpuStat(line, prev_idle_, prev_total_);
}

DiskStats LinuxTelemetryCollector::readDisk(const std::string& mount) {
    DiskStats d{mount, 0, 0, 0.0};
    struct statvfs vfs{};
    if (statvfs(mount.c_str(), &vfs) == 0 && vfs.f_blocks > 0) {
        unsigned long long total = static_cast<unsigned long long>(vfs.f_blocks) * vfs.f_frsize;
        unsigned long long avail = static_cast<unsigned long long>(vfs.f_bavail) * vfs.f_frsize;
        unsigned long long freeB = static_cast<unsigned long long>(vfs.f_bfree) * vfs.f_frsize;
        d.total_mb = total / (1024ULL * 1024ULL);
        d.free_mb = avail / (1024ULL * 1024ULL);
        unsigned long long used = (total > freeB) ? (total - freeB) : 0;
        unsigned long long usableTotal = used + avail;
        d.used_pct = usableTotal > 0
            ? 100.0 * static_cast<double>(used) / static_cast<double>(usableTotal)
            : 0.0;
    }
    return d;
}

std::vector<NetStats> LinuxTelemetryCollector::readNetwork() {
    std::vector<NetStats> list;
    DIR* dir = opendir("/sys/class/net");
    if (!dir) return list;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        NetStats n{};
        n.iface = name;
        std::ifstream stIn("/sys/class/net/" + name + "/operstate");
        std::getline(stIn, n.operstate);
        if (n.operstate.empty()) n.operstate = "unknown";
        std::ifstream rxIn("/sys/class/net/" + name + "/statistics/rx_bytes");
        std::ifstream txIn("/sys/class/net/" + name + "/statistics/tx_bytes");
        rxIn >> n.rx_bytes;
        txIn >> n.tx_bytes;
        list.push_back(n);
    }
    closedir(dir);
    return list;
}

SystemMetrics LinuxTelemetryCollector::collect() {
    SystemMetrics m{};
    std::ifstream upIn("/proc/uptime");
    upIn >> m.uptime_sec;
    m.cpu_pct = readCpu();

    std::ifstream memIn("/proc/meminfo");
    std::ostringstream memBuf;
    memBuf << memIn.rdbuf();
    parseMemInfo(memBuf.str(), m.mem_total_kb, m.mem_avail_kb, m.mem_used_pct);

    m.disks.push_back(readDisk("/"));
    struct stat st{};
    if (stat("/data", &st) == 0 && S_ISDIR(st.st_mode)) {
        m.disks.push_back(readDisk("/data"));
    }
    m.network = readNetwork();
    return m;
}
