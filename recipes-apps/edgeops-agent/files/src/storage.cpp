#include "storage.hpp"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

StorageManager::~StorageManager() {
    if (db_) sqlite3_close(db_);
}

void StorageManager::ensureDir(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if (path[i] == '/' && i > 0) mkdir(cur.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
}

std::string StorageManager::jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:   o << c;      break;
        }
    }
    return o.str();
}

bool StorageManager::open(const std::string& dbPath) {
    auto slash = dbPath.find_last_of('/');
    if (slash != std::string::npos) ensureDir(dbPath.substr(0, slash));

    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "SQLite open failed: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }

    const char* schema =
        "CREATE TABLE IF NOT EXISTS metrics ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,"
        "  version TEXT NOT NULL, cpu_pct REAL NOT NULL, mem_pct REAL NOT NULL, disk_pct REAL NOT NULL);"
        "CREATE TABLE IF NOT EXISTS alarms ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,"
        "  severity TEXT NOT NULL, source TEXT NOT NULL, message TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'unknown', recovered_ts INTEGER);";

    char* err = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQLite schema error: " << (err ? err : "") << "\n";
        sqlite3_free(err);
        return false;
    }

    // Check whether an existing database needs migration.
    bool hasStatus = false;
    bool hasRecoveredTs = false;
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(alarms);", -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "SQLite migration check failed: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name && std::string(name) == "status") hasStatus = true;
        if (name && std::string(name) == "recovered_ts") hasRecoveredTs = true;
    }
    sqlite3_finalize(stmt);

    if (!hasStatus) {
        if (sqlite3_exec(db_,
            "ALTER TABLE alarms ADD COLUMN status TEXT NOT NULL DEFAULT 'unknown';",
            nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "SQLite status migration failed: " << (err ? err : "") << "\n";
            sqlite3_free(err);
            return false;
        }
    }

    if (!hasRecoveredTs) {
        if (sqlite3_exec(db_,
            "ALTER TABLE alarms ADD COLUMN recovered_ts INTEGER;",
            nullptr, nullptr, &err) != SQLITE_OK) {
            std::cerr << "SQLite recovery timestamp migration failed: " << (err ? err : "") << "\n";
            sqlite3_free(err);
            return false;
        }
    }

    return true;
}

void StorageManager::saveSample(const std::string& version, double cpu, double mem, double disk) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db_) return;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO metrics (ts, version, cpu_pct, mem_pct, disk_pct) VALUES (strftime('%s','now'), ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, version.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, cpu);
        sqlite3_bind_double(stmt, 3, mem);
        sqlite3_bind_double(stmt, 4, disk);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void StorageManager::raiseAlarm(const std::string& severity,
                                const std::string& source,
                                const std::string& msg) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db_) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "INSERT INTO alarms (ts, severity, source, message, status) "
        "SELECT strftime('%s','now'), ?, ?, ?, 'active' "
        "WHERE NOT EXISTS ("
        "SELECT 1 FROM alarms WHERE source = ? AND status = 'active');";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, severity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, msg.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, source.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "SQLite alarm insert failed: " << sqlite3_errmsg(db_) << "\n";
        }
        sqlite3_finalize(stmt);
    }
}

void StorageManager::resolveAlarm(const std::string& source) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!db_) return;

    sqlite3_stmt* stmt = nullptr;

    const char* sql =
        "UPDATE alarms SET status = 'recovered', "
        "recovered_ts = strftime('%s','now') "
        "WHERE source = ? AND status = 'active';";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "SQLite alarm recovery failed: " << sqlite3_errmsg(db_) << "\n";
        }
        sqlite3_finalize(stmt);
    }
}

std::string StorageManager::getHistoryJson(int limit) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream o;
    o << "{\"samples\":[";
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT ts, version, cpu_pct, mem_pct, disk_pct FROM metrics ORDER BY id DESC LIMIT ?;";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            bool first = true;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!first) o << ",";
                first = false;
                const char* ver = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                o << "{\"ts\":" << sqlite3_column_int64(stmt, 0)
                  << ",\"version\":\"" << jsonEscape(ver ? ver : "") << "\""
                  << ",\"cpu_pct\":" << sqlite3_column_double(stmt, 2)
                  << ",\"mem_pct\":" << sqlite3_column_double(stmt, 3)
                  << ",\"disk_pct\":" << sqlite3_column_double(stmt, 4) << "}";
            }
            sqlite3_finalize(stmt);
        }
    }
    o << "]}";
    return o.str();
}

std::string StorageManager::getAlarmsJson(int limit) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream o;
    o << "{\"alarms\":[";

    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT id, ts, severity, source, message, status, recovered_ts "
            "FROM alarms WHERE status = 'active' ORDER BY id DESC LIMIT ?;";

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            bool first = true;

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!first) o << ",";
                first = false;

                const char* sev = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                const char* msg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

                o << "{\"id\":" << sqlite3_column_int64(stmt, 0)
                  << ",\"ts\":" << sqlite3_column_int64(stmt, 1)
                  << ",\"severity\":\"" << jsonEscape(sev ? sev : "") << "\""
                  << ",\"source\":\"" << jsonEscape(src ? src : "") << "\""
                  << ",\"message\":\"" << jsonEscape(msg ? msg : "") << "\""
                  << ",\"status\":\"" << jsonEscape(status ? status : "") << "\""
                  << ",\"recovered_ts\":null}";
            }
            sqlite3_finalize(stmt);
        }
    }

    o << "]}";
    return o.str();
}

std::string StorageManager::getAlarmHistoryJson(int limit) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::ostringstream o;
    o << "{\"alarms\":[";

    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT id, ts, severity, source, message, status, recovered_ts "
            "FROM alarms WHERE status IN ('recovered', 'unknown') "
            "ORDER BY id DESC LIMIT ?;";

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            bool first = true;

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!first) o << ",";
                first = false;

                const char* sev = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                const char* msg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

                o << "{\"id\":" << sqlite3_column_int64(stmt, 0)
                  << ",\"ts\":" << sqlite3_column_int64(stmt, 1)
                  << ",\"severity\":\"" << jsonEscape(sev ? sev : "") << "\""
                  << ",\"source\":\"" << jsonEscape(src ? src : "") << "\""
                  << ",\"message\":\"" << jsonEscape(msg ? msg : "") << "\""
                  << ",\"status\":\"" << jsonEscape(status ? status : "") << "\""
                  << ",\"recovered_ts\":";

                if (sqlite3_column_type(stmt, 6) == SQLITE_NULL) {
                    o << "null";
                } else {
                    o << sqlite3_column_int64(stmt, 6);
                }

                o << "}";
            }
            sqlite3_finalize(stmt);
        }
    }

    o << "]}";
    return o.str();
}