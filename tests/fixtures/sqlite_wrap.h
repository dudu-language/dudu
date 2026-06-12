#pragma once

#include <sqlite3.h>

inline constexpr int DUDU_SQLITE_OK = SQLITE_OK;
inline constexpr int DUDU_SQLITE_ROW = SQLITE_ROW;

inline int dudu_sqlite_open_memory(sqlite3** db) {
    return sqlite3_open(":memory:", db);
}

inline int dudu_sqlite_exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

inline int dudu_sqlite_prepare(sqlite3* db, const char* sql, sqlite3_stmt** stmt) {
    return sqlite3_prepare_v2(db, sql, -1, stmt, nullptr);
}

inline int dudu_sqlite_bind_i64(sqlite3_stmt* stmt, int index, long long value) {
    return sqlite3_bind_int64(stmt, index, value);
}

inline int dudu_sqlite_step(sqlite3_stmt* stmt) {
    return sqlite3_step(stmt);
}

inline long long dudu_sqlite_column_i64(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int64(stmt, column);
}

inline const char* dudu_sqlite_column_text(sqlite3_stmt* stmt, int column) {
    return reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
}

inline int dudu_sqlite_column_i32(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int(stmt, column);
}

inline void dudu_sqlite_finalize(sqlite3_stmt* stmt) {
    sqlite3_finalize(stmt);
}

inline void dudu_sqlite_close(sqlite3* db) {
    sqlite3_close(db);
}
