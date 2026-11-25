#include "http_server.hpp"
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

HttpServer::HttpServer(const Config& cfg, IMetricCollector& collector,
                       IServiceController& svcCtrl, StorageManager& storage)
    : cfg_(cfg), collector_(collector), svcCtrl_(svcCtrl), storage_(storage) {}

std::string HttpServer::jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else o << c;
    }
    return o.str();
}

void HttpServer::sendHttp(int fd, int code, const std::string& status,
                          const std::string& type, const std::string& body) {
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << code << " " << status << "\r\n"
        << "Content-Type: " << type << "\r\n"
        << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n";
    std::string full = hdr.str() + body;
    send(fd, full.data(), full.size(), 0);
}

std::string HttpServer::buildStatusJson() {
    SystemMetrics m = collector_.collect();
    std::ostringstream o;
    o << "{\"version\":\"" << APP_VERSION << "\",\"uptime_sec\":" << m.uptime_sec
      << ",\"cpu_pct\":" << m.cpu_pct
      << ",\"memory\":{\"total_kb\":" << m.mem_total_kb
      << ",\"available_kb\":" << m.mem_avail_kb << ",\"used_pct\":" << m.mem_used_pct << "},\"disks\":[";
    for (size_t i = 0; i < m.disks.size(); ++i) {
        if (i) o << ",";
        o << "{\"mount\":\"" << jsonEscape(m.disks[i].mount) << "\",\"total_mb\":" << m.disks[i].total_mb
          << ",\"free_mb\":" << m.disks[i].free_mb << ",\"used_pct\":" << m.disks[i].used_pct << "}";
    }
    o << "],\"network\":[";
    for (size_t i = 0; i < m.network.size(); ++i) {
        if (i) o << ",";
        o << "{\"iface\":\"" << jsonEscape(m.network[i].iface) << "\",\"operstate\":\""
          << jsonEscape(m.network[i].operstate) << "\",\"rx_bytes\":" << m.network[i].rx_bytes
          << ",\"tx_bytes\":" << m.network[i].tx_bytes << "}";
    }
    o << "],\"services\":[";
    for (size_t i = 0; i < cfg_.services.size(); ++i) {
        if (i) o << ",";
        o << "{\"name\":\"" << jsonEscape(cfg_.services[i]) << "\",\"state\":\""
          << jsonEscape(svcCtrl_.getServiceState(cfg_.services[i])) << "\"}";
    }
    o << "]}";
    return o.str();
}

std::string HttpServer::buildLogsJson() {
    auto logs = svcCtrl_.getRecentLogs(25);
    std::ostringstream o;
    o << "{\"entries\":[";
    for (size_t i = 0; i < logs.size(); ++i) {
        if (i) o << ",";
        o << "\"" << jsonEscape(logs[i]) << "\"";
    }
    o << "]}";
    return o.str();
}

void HttpServer::handleClient(int fd) {
    char buf[2048];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';
    std::istringstream req(buf);
    std::string method, path;
    req >> method >> path;

    if (method == "GET" && path == "/api/status") {
        sendHttp(fd, 200, "OK", "application/json", buildStatusJson());
    } else if (method == "GET" && path == "/api/version") {
        sendHttp(fd, 200, "OK", "application/json", std::string("{\"version\":\"") + APP_VERSION + "\"}");
    } else if (method == "GET" && path == "/api/history") {
        sendHttp(fd, 200, "OK", "application/json", storage_.getHistoryJson());
    } else if (method == "GET" && path == "/api/alarms") {
        sendHttp(fd, 200, "OK", "application/json", storage_.getAlarmsJson());
    } else if (method == "GET" && path == "/api/alarm-history") {
        sendHttp(fd, 200, "OK", "application/json", storage_.getAlarmHistoryJson());
    } else if (method == "GET" && path == "/api/logs") {
        sendHttp(fd, 200, "OK", "application/json", buildLogsJson());
    } else if (method == "POST" && path.rfind("/api/services/", 0) == 0) {
        std::string rest = path.substr(std::strlen("/api/services/"));
        auto slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string unit = rest.substr(0, slash);
            std::string action = rest.substr(slash + 1);
            bool ok = svcCtrl_.controlService(unit, action);
            std::string state = svcCtrl_.getServiceState(unit);
            sendHttp(fd, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json",
                     "{\"service\":\"" + jsonEscape(unit) + "\",\"state\":\"" + state + "\"}");
            close(fd);
            return;
        }
        sendHttp(fd, 400, "Bad Request", "application/json", "{\"error\":\"bad request\"}");
    } else if (method == "GET" && (path == "/" || path == "/index.html")) {
        std::ifstream html(cfg_.web_root + "/index.html");
        if (html.is_open()) {
            std::ostringstream ss;
            ss << html.rdbuf();
            sendHttp(fd, 200, "OK", "text/html; charset=utf-8", ss.str());
        } else {
            sendHttp(fd, 200, "OK", "application/json",
                     "{\"message\":\"EdgeOps REST API active (web UI disabled)\",\"version\":\"" APP_VERSION "\"}");
        }
    } else {
        sendHttp(fd, 404, "Not Found", "application/json", "{\"error\":\"not found\"}");
    }
    close(fd);
}

void HttpServer::serve(std::atomic<bool>& running) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return;
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(cfg_.port));
    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(srv);
        return;
    }
    listen(srv, 16);
    while (running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        timeval tv{1, 0};
        if (select(srv + 1, &fds, nullptr, nullptr, &tv) > 0 && FD_ISSET(srv, &fds)) {
            int cfd = accept(srv, nullptr, nullptr);
            if (cfd >= 0) std::thread(&HttpServer::handleClient, this, cfd).detach();
        }
    }
    close(srv);
}
