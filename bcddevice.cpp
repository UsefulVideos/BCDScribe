#include "bcddevice.h"

#include <QUuid>
#include <limits>

namespace {

void appendLittleEndian(QByteArray &data, quint64 value, int byteCount) {
    for (int byte = 0; byte < byteCount; ++byte)
        data.append(static_cast<char>((value >> (byte * 8)) & 0xff));
}

bool validIdentity(const BcdPartitionIdentity &identity) {
    return identity.diskId.size() == 16 && identity.partitionId.size() == 16;
}

bool validWindowsPath(const QString &windowsPath) {
    if (!windowsPath.startsWith(QLatin1Char('\\')) ||
        windowsPath.endsWith(QLatin1Char('\\')) ||
        windowsPath.contains(QLatin1Char('/')))
        return false;

    const QString invalidWindowsCharacters = QStringLiteral("<>:\"|?*");
    for (const QChar character : windowsPath) {
        if (character.unicode() < 0x20 || invalidWindowsCharacters.contains(character))
            return false;
    }
    return true;
}

QByteArray utf16LePath(const QString &windowsPath) {
    QByteArray pathData;
    pathData.reserve((windowsPath.size() + 1) * 2);
    for (const QChar character : windowsPath)
        appendLittleEndian(pathData, character.unicode(), 2);
    pathData.append('\0');
    pathData.append('\0');
    return pathData;
}

}

bool buildBcdVhdDeviceDescriptor(const QString &windowsPath,
                                 const BcdPartitionIdentity &vhdPartition,
                                 const BcdPartitionIdentity &parentPartition,
                                 QByteArray &descriptor,
                                 QString &error) {
    descriptor.clear();
    error.clear();

    if (!validWindowsPath(windowsPath)) {
        error = QStringLiteral("The VHD path must be a Windows path relative to its host partition.");
        return false;
    }
    if (!validIdentity(vhdPartition) || !validIdentity(parentPartition)) {
        error = QStringLiteral("The GPT/MBR disk and partition identifiers are incomplete.");
        return false;
    }

    const QByteArray pathData = utf16LePath(windowsPath);

    constexpr quint64 fixedSize = 176;
    const quint64 totalSize = fixedSize + static_cast<quint64>(pathData.size());
    if (totalSize > std::numeric_limits<quint32>::max()) {
        error = QStringLiteral("The VHD path is too long for a BCD device descriptor.");
        return false;
    }

    descriptor.reserve(static_cast<qsizetype>(totalSize));
    descriptor.append(QByteArray(16, '\0'));
    appendLittleEndian(descriptor, 6, 4);
    appendLittleEndian(descriptor, 0, 4);
    appendLittleEndian(descriptor, totalSize - 16, 4);
    appendLittleEndian(descriptor, 0, 4);

    descriptor.append(vhdPartition.partitionId);
    appendLittleEndian(descriptor, 6, 4);
    appendLittleEndian(descriptor, vhdPartition.gpt ? 0 : 1, 4);
    descriptor.append(vhdPartition.diskId);
    descriptor.append(QByteArray(8, '\0'));
    appendLittleEndian(descriptor, totalSize - 72, 8);
    appendLittleEndian(descriptor, 5, 4);
    appendLittleEndian(descriptor, 1, 4);
    appendLittleEndian(descriptor, totalSize - 92, 4);
    appendLittleEndian(descriptor, 5, 4);
    appendLittleEndian(descriptor, 6, 4);
    appendLittleEndian(descriptor, 0, 4);
    appendLittleEndian(descriptor, 0x48, 8);
    descriptor.append(parentPartition.partitionId);
    appendLittleEndian(descriptor, 0, 4);
    appendLittleEndian(descriptor, parentPartition.gpt ? 0 : 1, 4);
    descriptor.append(parentPartition.diskId);
    descriptor.append(QByteArray(16, '\0'));
    descriptor.append(pathData);

    if (static_cast<quint64>(descriptor.size()) != totalSize) {
        descriptor.clear();
        error = QStringLiteral("The generated VHD device descriptor has an invalid size.");
        return false;
    }
    return true;
}

bool buildBcdRamdiskDeviceDescriptor(const QString &windowsPath,
                                     const QString &optionsGuid,
                                     QByteArray &descriptor,
                                     QString &error) {
    descriptor.clear();
    error.clear();

    const QUuid options(optionsGuid);
    if (options.isNull()) {
        error = QStringLiteral("The ramdisk options identifier is invalid.");
        return false;
    }
    if (!validWindowsPath(windowsPath)) {
        error = QStringLiteral("The WIM path must be a Windows path relative to its host partition.");
        return false;
    }

    const QByteArray pathData = utf16LePath(windowsPath);
    const quint64 pathSize = static_cast<quint64>(pathData.size() - 2);
    if (pathSize > std::numeric_limits<quint32>::max() - 126) {
        error = QStringLiteral("The WIM path is too long for a BCD ramdisk descriptor.");
        return false;
    }

    const QByteArray guid = options.toRfc4122();
    descriptor.reserve(146 + pathData.size());
    descriptor.append(guid.at(3));
    descriptor.append(guid.at(2));
    descriptor.append(guid.at(1));
    descriptor.append(guid.at(0));
    descriptor.append(guid.at(5));
    descriptor.append(guid.at(4));
    descriptor.append(guid.at(7));
    descriptor.append(guid.at(6));
    descriptor.append(guid.mid(8));
    descriptor.append(QByteArray(4, '\0'));
    appendLittleEndian(descriptor, 1, 4);
    appendLittleEndian(descriptor, 126 + pathSize, 4);
    descriptor.append(QByteArray(7, '\0'));
    appendLittleEndian(descriptor, 3, 4);
    descriptor.append(QByteArray(20, '\0'));
    appendLittleEndian(descriptor, 1, 4);
    appendLittleEndian(descriptor, 86 + pathSize, 4);
    descriptor.append(QByteArray(3, '\0'));
    appendLittleEndian(descriptor, 5, 4);
    appendLittleEndian(descriptor, 5, 4);
    descriptor.append(QByteArray(4, '\0'));
    appendLittleEndian(descriptor, 0x48, 8);
    descriptor.append(QByteArray(56, '\0'));
    descriptor.append(pathData);

    if (descriptor.size() != 148 + static_cast<qsizetype>(pathSize)) {
        descriptor.clear();
        error = QStringLiteral("The generated ramdisk device descriptor has an invalid size.");
        return false;
    }
    return true;
}

QByteArray buildBcdRamdiskSdiDeviceDescriptor() {
    QByteArray descriptor(16, '\0');
    appendLittleEndian(descriptor, 5, 4);
    appendLittleEndian(descriptor, 0, 4);
    appendLittleEndian(descriptor, 0x48, 8);
    descriptor.append(QByteArray(56, '\0'));
    return descriptor;
}
