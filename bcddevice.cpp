#include "bcddevice.h"

#include <limits>

namespace {

void appendLittleEndian(QByteArray &data, quint64 value, int byteCount) {
    for (int byte = 0; byte < byteCount; ++byte)
        data.append(static_cast<char>((value >> (byte * 8)) & 0xff));
}

bool validIdentity(const BcdPartitionIdentity &identity) {
    return identity.diskId.size() == 16 && identity.partitionId.size() == 16;
}

}

bool buildBcdVhdDeviceDescriptor(const QString &windowsPath,
                                 const BcdPartitionIdentity &vhdPartition,
                                 const BcdPartitionIdentity &parentPartition,
                                 QByteArray &descriptor,
                                 QString &error) {
    descriptor.clear();
    error.clear();

    bool invalidPathCharacter = false;
    const QString invalidWindowsCharacters = QStringLiteral("<>:\"|?*");
    for (const QChar character : windowsPath) {
        if (character.unicode() < 0x20 || invalidWindowsCharacters.contains(character)) {
            invalidPathCharacter = true;
            break;
        }
    }
    if (!windowsPath.startsWith(QLatin1Char('\\')) ||
        windowsPath.endsWith(QLatin1Char('\\')) ||
        windowsPath.contains(QLatin1Char('/')) || invalidPathCharacter) {
        error = QStringLiteral("The VHD path must be a Windows path relative to its host partition.");
        return false;
    }
    if (!validIdentity(vhdPartition) || !validIdentity(parentPartition)) {
        error = QStringLiteral("The GPT/MBR disk and partition identifiers are incomplete.");
        return false;
    }

    QByteArray pathData;
    pathData.reserve((windowsPath.size() + 1) * 2);
    for (const QChar character : windowsPath) {
        const quint16 codeUnit = character.unicode();
        appendLittleEndian(pathData, codeUnit, 2);
    }
    pathData.append('\0');
    pathData.append('\0');

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
