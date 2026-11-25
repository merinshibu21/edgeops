#include "systemd_mgr.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <sys/wait.h>

bool SystemdManager::isSafeUnitName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '-' && c != '_' && c != '.' && c != '@') {
            return false;
        }
    }
    return true;
}

std::string SystemdManager::runCommand(const std::string& cmd, int* exitCode) {
    std::string out;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        if (exitCode) *exitCode = -1;
        return "";
    }
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp)) out += buf;
    int rc = pclose(fp);
    if (exitCode) *exitCode = WEXITSTATUS(rc);
    return out;
}

std::string SystemdManager::getServiceState(const std::string& unit) {
    if (!isSafeUnitName(unit)) return "invalid";
    std::string out = runCommand("systemctl is-active " + unit + " 2>/dev/null");
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out.empty() ? "unknown" : out;
}

bool SystemdManager::controlService(const std::string& unit, const std::string& action) {
    if (!isSafeUnitName(unit)) return false;
    if (action != "start" && action != "stop" && action != "restart") return false;
    int rc = -1;
    runCommand("systemctl " + action + " " + unit + " 2>&1", &rc);
    return rc == 0;
}

std::vector<std::string> SystemdManager::getRecentLogs(int lines) {
    std::vector<std::string> entries;
    std::string raw = runCommand("journalctl -n " + std::to_string(lines) + " --no-pager -o short-iso 2>/dev/null");
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty()) entries.push_back(line);
    }
    return entries;
}
