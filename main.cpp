#include <QApplication>
#include <QIcon>
#include <QStyle>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QIcon appIcon = QIcon::fromTheme("preferences-system");
    if (appIcon.isNull())
        appIcon = app.style()->standardIcon(QStyle::SP_DesktopIcon);
    app.setWindowIcon(appIcon);

    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    return app.exec();
}