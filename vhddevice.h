#pragma once

#include <QByteArray>
#include <QString>

class QWidget;

enum class VhdSetupResult {
    Ready,
    Cancelled,
    Failed
};

VhdSetupResult promptForVhdDevice(QWidget *parent,
                                  QByteArray &descriptor,
                                  QString &error);
