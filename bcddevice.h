#pragma once

#include <QByteArray>
#include <QString>

struct BcdPartitionIdentity {
    bool gpt = false;
    QByteArray diskId;
    QByteArray partitionId;
};

bool buildBcdVhdDeviceDescriptor(const QString &windowsPath,
                                 const BcdPartitionIdentity &vhdPartition,
                                 const BcdPartitionIdentity &parentPartition,
                                 QByteArray &descriptor,
                                 QString &error);
