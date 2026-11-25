#include "storage.hpp"
#include <iostream>
#include <sstream>

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