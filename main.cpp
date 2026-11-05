#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
#include "console.h"
#include "mainwindow.h"

#ifdef Q_OS_LINUX
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

QString userConfigDirectory() {
    QString path = qEnvironmentVariable("BCDSCRIBE_USER_CONFIG_HOME").trimmed();
    if (!path.isEmpty())
        return QDir::cleanPath(path);

    QString home = QDir::homePath();
#ifdef Q_OS_LINUX
    bool resolvedSessionHome = false;
    if (geteuid() == 0) {
        bool validUid = false;
        uint uid = qEnvironmentVariable("PKEXEC_UID").toUInt(&validUid);
        if (!validUid)
            uid = qEnvironmentVariable("SUDO_UID").toUInt(&validUid);
        const passwd *user = validUid ? getpwuid(static_cast<uid_t>(uid)) : nullptr;
        if (user && user->pw_dir) {
            home = QString::fromLocal8Bit(user->pw_dir);
            resolvedSessionHome = true;
        }
    }
#else
    const bool resolvedSessionHome = false;
#endif
    path = qEnvironmentVariable("XDG_CONFIG_HOME").trimmed();
    if (!path.isEmpty()) {
        if (!resolvedSessionHome)
            return QDir::cleanPath(path);
        const QString relativePath = QDir(home).relativeFilePath(path);
        if (QDir::isAbsolutePath(path) && relativePath != QStringLiteral("..") &&
            !relativePath.startsWith(QStringLiteral("../")))
            return QDir::cleanPath(path);
    }
    return QDir(home).filePath(QStringLiteral(".config"));
}

bool isConsoleInvocation(const QStringList &arguments) {
    if (arguments.isEmpty())
        return false;
    const QString first = arguments.first().toLower();
    return first == QStringLiteral("--help") || first == QStringLiteral("-h") ||
        first == QStringLiteral("/?") || first == QStringLiteral("--version") ||
        first == QStringLiteral("--store") || first == QStringLiteral("/store") ||
        first == QStringLiteral("/enum") ||
        first == QStringLiteral("/export") || first == QStringLiteral("/import") ||
        first == QStringLiteral("/create") || first == QStringLiteral("/set") ||
        first == QStringLiteral("/delete") || first == QStringLiteral("/deletevalue") ||
        first == QStringLiteral("/timeout") || first == QStringLiteral("/default") ||
        first == QStringLiteral("/displayorder") ||
        first == QStringLiteral("/bootsequence") ||
        first == QStringLiteral("/toolsdisplayorder") ||
        first == QStringLiteral("/bootdebug") || first == QStringLiteral("/debug") ||
        first == QStringLiteral("/ems") || first == QStringLiteral("/event") ||
        first == QStringLiteral("/badmemoryaccess");
}

bool shouldUseWaylandPortal() {
    const QString platform = qEnvironmentVariable("QT_QPA_PLATFORM").trimmed().toLower();
    if (!platform.isEmpty())
        return platform.startsWith(QStringLiteral("wayland"));
    return !qEnvironmentVariable("WAYLAND_DISPLAY").trimmed().isEmpty();
}

bool portalThemePluginAvailable() {
    QStringList pluginPaths = QCoreApplication::libraryPaths();
    pluginPaths.append(QLibraryInfo::path(QLibraryInfo::PluginsPath));
    const QString configuredPaths = qEnvironmentVariable("QT_PLUGIN_PATH").trimmed();
    if (!configuredPaths.isEmpty())
        pluginPaths.append(configuredPaths.split(QDir::listSeparator(), Qt::SkipEmptyParts));

    for (const QString &pluginPath : pluginPaths) {
        if (QFileInfo::exists(QDir(pluginPath).filePath(
                QStringLiteral("platformthemes/libqxdgdesktopportal.so"))))
            return true;
    }
    return false;
}

void configureWaylandPortalTheme() {
    if (shouldUseWaylandPortal() && portalThemePluginAvailable())
        qputenv("QT_QPA_PLATFORMTHEME", QByteArrayLiteral("xdgdesktopportal"));
}

}

int main(int argc, char *argv[]) {
    QStringList originalArguments;
    for (int index = 1; index < argc; ++index)
        originalArguments.append(QString::fromLocal8Bit(argv[index]));
    if (isConsoleInvocation(originalArguments))
        return runConsoleCommand(argc, argv);

    configureWaylandPortalTheme();

    const QString configDirectory = userConfigDirectory();
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configDirectory);

#ifdef Q_OS_LINUX
    if (geteuid() != 0 && !qEnvironmentVariableIsSet("BCDSCRIBE_ELEVATED")) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            QMessageBox::critical(nullptr, "Administrator authorization unavailable",
                                  "BCDScribe requires administrator access. Install PolicyKit, "
                                  "or launch it from a terminal with sudo.");
            return 1;
        }

        const QString env = QStandardPaths::findExecutable(QStringLiteral("env"));
        const QString sessionBusAddress =
            qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").trimmed();
        QStringList arguments;
        if (sessionBusAddress.isEmpty()) {
            const QString dbusRunSession = QStandardPaths::findExecutable(
                QStringLiteral("dbus-run-session"));
            if (!dbusRunSession.isEmpty())
                arguments << dbusRunSession << QStringLiteral("--");
        }
        arguments << env << QStringLiteral("BCDSCRIBE_ELEVATED=1");
        arguments.append(QStringLiteral("BCDSCRIBE_USER_CONFIG_HOME=%1")
                             .arg(configDirectory));
        const QStringList displayVariables = {
            QStringLiteral("DISPLAY"),
            QStringLiteral("WAYLAND_DISPLAY"),
            QStringLiteral("XDG_RUNTIME_DIR"),
            QStringLiteral("XDG_SESSION_TYPE"),
            QStringLiteral("XDG_CURRENT_DESKTOP"),
            QStringLiteral("XDG_SESSION_DESKTOP"),
            QStringLiteral("XDG_CONFIG_DIRS"),
            QStringLiteral("XDG_DATA_DIRS"),
            QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
            QStringLiteral("QT_QPA_PLATFORMTHEME"),
            QStringLiteral("QT_STYLE_OVERRIDE"),
            QStringLiteral("GTK_THEME"),
            QStringLiteral("KDE_SESSION_VERSION"),
            QStringLiteral("KDE_FULL_SESSION"),
            QStringLiteral("XAUTHORITY"),
            QStringLiteral("QT_QPA_PLATFORM")
        };
        for (const QString &variable : displayVariables) {
            if (variable == QStringLiteral("DBUS_SESSION_BUS_ADDRESS") &&
                sessionBusAddress.isEmpty())
                continue;
            if (qEnvironmentVariableIsSet(variable.toLocal8Bit().constData()))
                arguments.append(variable + QLatin1Char('=') +
                                 qEnvironmentVariable(variable.toLocal8Bit().constData()));
        }

        QString executable = qEnvironmentVariable("APPIMAGE");
        if (!executable.isEmpty()) {
            arguments.append(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN=1"));
        } else {
            executable = QCoreApplication::applicationFilePath();
        }
        arguments.append(executable);
        arguments.append(originalArguments);

        if (!QProcess::startDetached(pkexec, arguments, QDir::currentPath())) {
            QMessageBox::critical(nullptr, "Administrator authorization failed",
                                  "Could not start BCDScribe through the PolicyKit authorization prompt.");
            return 1;
        }
        return 0;
    }
#endif

    QApplication app(argc, argv);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configDirectory);

    QIcon appIcon = QIcon::fromTheme("preferences-system");
    if (appIcon.isNull())
        appIcon = app.style()->standardIcon(QStyle::SP_DesktopIcon);
    app.setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}