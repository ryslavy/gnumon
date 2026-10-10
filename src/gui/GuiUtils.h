#pragma once

#include <QString>
#include <QStringList>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDesktopServices>
#include <QUrl>

namespace gnumon::gui {

inline void LaunchHostFileManager(const QString& targetPath = QString()) {
    QString path = targetPath.trimmed();
    if (path.isEmpty()) {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (docs.isEmpty()) docs = QDir::homePath() + "/Documents";
        path = docs + "/gnumon/captures";
    }
    QDir().mkpath(path);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString appDir = env.value("APPDIR");
    QString ld = env.value("LD_LIBRARY_PATH");
    if (!ld.isEmpty()) {
        QStringList kept;
        for (const QString& part : ld.split(':', Qt::SkipEmptyParts)) {
            if (!appDir.isEmpty() && part.startsWith(appDir)) continue;
            if (part.contains(".mount_") || part.contains("build-appimage")) continue;
            kept << part;
        }
        if (kept.isEmpty()) {
            env.remove("LD_LIBRARY_PATH");
        } else {
            env.insert("LD_LIBRARY_PATH", kept.join(':'));
        }
    }
    env.remove("QT_PLUGIN_PATH");
    env.remove("QT_QPA_PLATFORMTHEME");

    QString xdgOpen = QStandardPaths::findExecutable("xdg-open");
    if (xdgOpen.isEmpty()) xdgOpen = "/usr/bin/xdg-open";
    QProcess proc;
    proc.setProgram(xdgOpen);
    proc.setArguments({path});
    proc.setWorkingDirectory(path);
    proc.setProcessEnvironment(env);

    qint64 pid = 0;
    bool ok = proc.startDetached(&pid);
    if (!ok) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

} // namespace gnumon::gui
