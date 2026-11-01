#include "bcddevice.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

quint64 readLittleEndian(const QByteArray &data, int offset, int byteCount) {
    quint64 value = 0;
    for (int byte = 0; byte < byteCount; ++byte) {
        value |= static_cast<quint64>(
                     static_cast<unsigned char>(data.at(offset + byte)))
            << (byte * 8);
    }
    return value;
}

bool expect(bool condition, const char *message) {
    if (!condition)
        qCritical() << message;
    return condition;
}

QByteArray sequence(char first) {
    QByteArray bytes;
    for (int index = 0; index < 16; ++index)
        bytes.append(static_cast<char>(first + index));
    return bytes;
}

}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    BcdPartitionIdentity vhdPartition{true, sequence(0x10), sequence(0x20)};
    BcdPartitionIdentity parentPartition{false, sequence(0x30), sequence(0x40)};
    const QString path = QString::fromUtf8("\\Windows\\disk-\xc3\xa9.vhdx");
    QByteArray descriptor;
    QString error;
    if (!expect(buildBcdVhdDeviceDescriptor(path, vhdPartition, parentPartition,
                                            descriptor, error),
                "GPT/MBR descriptor construction failed"))
        return 1;

    const QByteArray pathBytes = [&path]() {
        QByteArray bytes;
        for (const QChar character : path) {
            const quint16 unit = character.unicode();
            bytes.append(static_cast<char>(unit & 0xff));
            bytes.append(static_cast<char>((unit >> 8) & 0xff));
        }
        bytes.append('\0');
        bytes.append('\0');
        return bytes;
    }();
    bool valid = true;
    valid &= expect(descriptor.size() == 176 + pathBytes.size(),
                    "Descriptor length does not match the fixed header and UTF-16LE path");
    valid &= expect(readLittleEndian(descriptor, 0x10, 4) == 6,
                    "Outer descriptor type is incorrect");
    valid &= expect(readLittleEndian(descriptor, 0x18, 4) ==
                        static_cast<quint64>(descriptor.size() - 16),
                    "Outer descriptor size is incorrect");
    valid &= expect(descriptor.mid(0x20, 16) == vhdPartition.partitionId,
                    "Virtual partition identifier is misplaced");
    valid &= expect(readLittleEndian(descriptor, 0x34, 4) == 0,
                    "GPT scheme marker is incorrect");
    valid &= expect(descriptor.mid(0x38, 16) == vhdPartition.diskId,
                    "Virtual disk identifier is misplaced");
    valid &= expect(readLittleEndian(descriptor, 0x50, 8) ==
                        static_cast<quint64>(descriptor.size() - 72),
                    "Nested device offset is incorrect");
    valid &= expect(readLittleEndian(descriptor, 0x60, 4) ==
                        static_cast<quint64>(descriptor.size() - 92),
                    "Parent device offset is incorrect");
    valid &= expect(descriptor.mid(0x78, 16) == parentPartition.partitionId,
                    "Parent partition identifier is misplaced");
    valid &= expect(readLittleEndian(descriptor, 0x8c, 4) == 1,
                    "MBR scheme marker is incorrect");
    valid &= expect(descriptor.mid(0x90, 16) == parentPartition.diskId,
                    "Parent disk identifier is misplaced");
    valid &= expect(descriptor.mid(0xb0, pathBytes.size()) == pathBytes,
                    "VHD path is not UTF-16LE at the expected offset");
    if (!valid)
        return 1;

    BcdPartitionIdentity invalidPartition;
    if (expect(!buildBcdVhdDeviceDescriptor(path, invalidPartition, parentPartition,
                                            descriptor, error),
               "Invalid partition identifiers were accepted") == false)
        return 1;

    const QString optionsGuid = QStringLiteral("ae5534e0-a924-466c-b836-758539a3ee3a");
    if (!expect(buildBcdRamdiskDeviceDescriptor(path, optionsGuid, descriptor, error),
                "Ramdisk descriptor construction failed"))
        return 1;
    const QByteArray expectedOptionsGuid = QByteArray::fromHex(
        "e03455ae24a96c46b836758539a3ee3a");
    valid = true;
    valid &= expect(descriptor.size() == 148 + pathBytes.size() - 2,
                    "Ramdisk descriptor length is incorrect");
    valid &= expect(descriptor.left(16) == expectedOptionsGuid,
                    "Ramdisk options GUID is incorrectly encoded");
    valid &= expect(readLittleEndian(descriptor, 24, 4) ==
                        static_cast<quint64>(126 + pathBytes.size() - 2),
                    "Ramdisk outer size field is incorrect");
    valid &= expect(readLittleEndian(descriptor, 63, 4) ==
                        static_cast<quint64>(86 + pathBytes.size() - 2),
                    "Ramdisk nested size field is incorrect");
    valid &= expect(descriptor.mid(146) == pathBytes,
                    "Ramdisk WIM path is not UTF-16LE at the expected offset");
    if (!valid)
        return 1;

    const QByteArray sdiDevice = buildBcdRamdiskSdiDeviceDescriptor();
    if (!expect(sdiDevice.size() == 88 &&
                    readLittleEndian(sdiDevice, 16, 4) == 5 &&
                    readLittleEndian(sdiDevice, 24, 8) == 0x48,
                "Ramdisk SDI device descriptor is invalid"))
        return 1;

    if (!expect(!buildBcdRamdiskDeviceDescriptor(path, QStringLiteral("invalid-guid"),
                                                  descriptor, error),
                "Invalid ramdisk options GUID was accepted"))
        return 1;
    return 0;
}
