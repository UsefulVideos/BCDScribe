#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
#include "mainwindow.h"

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

#ifdef Q_OS_LINUX
    if (geteuid() != 0 && !qEnvironmentVariableIsSet("BCDSCRIBE_ELEVATED")) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            QMessageBox::critical(nullptr, "Administrator authorization unavailable",
                                  "BCDScribe requires administrator access. Install PolicyKit, "
                                  "or launch it from a terminal with sudo.");
            return 1;
        }

        QStringList arguments = {
            QStandardPaths::findExecutable(QStringLiteral("env")),
            QStringLiteral("BCDSCRIBE_ELEVATED=1")
        };
        const QStringList displayVariables = {
            QStringLiteral("DISPLAY"),
            QStringLiteral("WAYLAND_DISPLAY"),
            QStringLiteral("XDG_RUNTIME_DIR"),
            QStringLiteral("XDG_SESSION_TYPE"),
            QStringLiteral("XAUTHORITY"),
            QStringLiteral("QT_QPA_PLATFORM")
        };
        for (const QString &variable : displayVariables) {
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
        arguments.append(QCoreApplication::arguments().mid(1));

        if (!QProcess::startDetached(pkexec, arguments, QDir::currentPath())) {
            QMessageBox::critical(nullptr, "Administrator authorization failed",
                                  "Could not start BCDScribe through the PolicyKit authorization prompt.");
            return 1;
        }
        return 0;
    }
#endif

    QIcon appIcon = QIcon::fromTheme("preferences-system");
    if (appIcon.isNull())
        appIcon = app.style()->standardIcon(QStyle::SP_DesktopIcon);
    app.setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}