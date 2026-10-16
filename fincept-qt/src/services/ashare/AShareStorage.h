#pragma once
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace fincept::services::ashare {
// Local data directory for the board's own files (portfolio ledger, notes).
// Defaults to the per-user app data folder; tests and smoke runs redirect it.
inline QString& data_dir_override() { static QString path; return path; }
inline void set_data_dir(const QString& path) { data_dir_override() = path; }
inline QString data_dir() {
    QString path = data_dir_override();
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}
inline QString data_file(const QString& name) { return QDir(data_dir()).filePath(name); }
} // namespace fincept::services::ashare
