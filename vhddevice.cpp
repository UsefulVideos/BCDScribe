#include "vhddevice.h"

#include "bcddevice.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QVector>
#include <limits>

namespace {

struct PartitionEntry {
    QString node;
    QString type;
    QString name;
    QString uuid;
    quint64 start = 0;
    quint64 size = 0;
};

struct PartitionTable {
    QString device;
    QString label;
    QString id;
    quint32 sectorSize = 0;
    QVector<PartitionEntry> partitions;
};

bool runCommand(const QString &program, const QStringList &arguments,
                QByteArray &output, QString &error) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        error = QStringLiteral("Could not start %1: %2")
            .arg(program, process.errorString());
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        error = QStringLiteral("%1 did not finish in time.").arg(program);
        return false;
    }

    output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        error = details.isEmpty()
            ? QStringLiteral("%1 failed with exit code %2.").arg(program).arg(process.exitCode())
            : QStringLiteral("%1 failed: %2").arg(program, details);
        return false;
    }
    return true;
}

bool readPartitionTable(const QString &device, PartitionTable &table, QString &error) {
    QByteArray output;
    if (!runCommand(QStringLiteral("sfdisk"),
                    {QStringLiteral("--json"), device}, output, error))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("sfdisk returned invalid partition-table data for %1.")
            .arg(device);
        return false;
    }

    const QJsonObject partitionTable = document.object()
        .value(QStringLiteral("partitiontable")).toObject();
    const QString label = partitionTable.value(QStringLiteral("label")).toString()
        .toLower();
    if (label != QStringLiteral("gpt") && label != QStringLiteral("dos")) {
        error = QStringLiteral("%1 uses an unsupported partition table (%2); GPT and MBR are supported.")
            .arg(device, label.isEmpty() ? QStringLiteral("unknown") : label);
        return false;
    }

    const double sectorSize = partitionTable.value(QStringLiteral("sectorsize")).toDouble();
    if (sectorSize < 1 || sectorSize > std::numeric_limits<quint32>::max()) {
        error = QStringLiteral("Could not determine the logical sector size for %1.").arg(device);
        return false;
    }

    table.device = partitionTable.value(QStringLiteral("device")).toString(device);
    table.label = label;
    table.id = partitionTable.value(QStringLiteral("id")).toString();
    table.sectorSize = static_cast<quint32>(sectorSize);
    for (const QJsonValue &value : partitionTable.value(QStringLiteral("partitions")).toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const double start = object.value(QStringLiteral("start")).toDouble(-1);
        const double size = object.value(QStringLiteral("size")).toDouble(-1);
        if (start < 0 || size < 0 ||
            start > static_cast<double>(std::numeric_limits<quint64>::max()) ||
            size > static_cast<double>(std::numeric_limits<quint64>::max())) {
            error = QStringLiteral("sfdisk returned an invalid partition range for %1.")
                .arg(device);
            return false;
        }

        PartitionEntry entry;
        entry.node = object.value(QStringLiteral("node")).toString();
        entry.type = object.value(QStringLiteral("type")).toString();
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.uuid = object.value(QStringLiteral("uuid")).toString();
        entry.start = static_cast<quint64>(start);
        entry.size = static_cast<quint64>(size);
        if (!entry.node.isEmpty())
            table.partitions.append(entry);
    }
    return true;
}

QString canonicalDevicePath(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

QByteArray guidBytes(const QString &text) {
    const QUuid guid(text);
    const QByteArray rfcBytes = guid.toRfc4122();
    if (rfcBytes.size() != 16 || guid.isNull())
        return {};

    QByteArray bytes;
    bytes.append(rfcBytes.at(3));
    bytes.append(rfcBytes.at(2));
    bytes.append(rfcBytes.at(1));
    bytes.append(rfcBytes.at(0));
    bytes.append(rfcBytes.at(5));
    bytes.append(rfcBytes.at(4));
    bytes.append(rfcBytes.at(7));
    bytes.append(rfcBytes.at(6));
    bytes.append(rfcBytes.mid(8));
    return bytes;
}

QByteArray littleEndianInteger128(quint64 value) {
    QByteArray bytes;
    bytes.reserve(16);
    for (int index = 0; index < 8; ++index)
        bytes.append(static_cast<char>((value >> (index * 8)) & 0xff));
    bytes.append(QByteArray(8, '\0'));
    return bytes;
}

bool partitionIdentity(const PartitionTable &table, const PartitionEntry &partition,
                       BcdPartitionIdentity &identity, QString &error) {
    identity.gpt = table.label == QStringLiteral("gpt");
    if (identity.gpt) {
        identity.diskId = guidBytes(table.id);
        identity.partitionId = guidBytes(partition.uuid);
        if (identity.diskId.size() != 16 || identity.partitionId.size() != 16) {
            error = QStringLiteral("The GPT disk or partition GUID is missing or invalid.");
            return false;
        }
        return true;
    }

    QString diskSignature = table.id;
    if (diskSignature.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        diskSignature.remove(0, 2);
    bool signatureValid = false;
    const quint64 signature = diskSignature.toULongLong(&signatureValid, 16);
    if (!signatureValid || signature == 0 ||
        signature > std::numeric_limits<quint32>::max()) {
        error = QStringLiteral("The MBR disk has no valid nonzero disk signature.");
        return false;
    }
    if (partition.start > std::numeric_limits<quint64>::max() / table.sectorSize) {
        error = QStringLiteral("The MBR partition offset is out of range.");
        return false;
    }

    identity.diskId = littleEndianInteger128(signature);
    identity.partitionId = littleEndianInteger128(partition.start * table.sectorSize);
    return true;
}

bool mountedVolumeForPath(const QString &path, QString &source, QString &mountPoint,
                          QString &fileSystem, QString &error) {
    QByteArray output;
    if (!runCommand(QStringLiteral("findmnt"),
                    {QStringLiteral("--json"), QStringLiteral("--noheadings"),
                     QStringLiteral("--output"), QStringLiteral("SOURCE,TARGET,FSTYPE"),
                     QStringLiteral("--target"), path},
                    output, error))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    const QJsonArray filesystems = document.object()
        .value(QStringLiteral("filesystems")).toArray();
    if (parseError.error != QJsonParseError::NoError || filesystems.isEmpty()) {
        error = QStringLiteral("Could not find the mounted host volume containing the VHD file.");
        return false;
    }
    const QJsonObject filesystem = filesystems.first().toObject();
    source = filesystem.value(QStringLiteral("source")).toString();
    mountPoint = filesystem.value(QStringLiteral("target")).toString();
    fileSystem = filesystem.value(QStringLiteral("fstype")).toString();
    const qsizetype subvolumeSuffix = source.indexOf(QLatin1Char('['));
    if (subvolumeSuffix >= 0)
        source.truncate(subvolumeSuffix);
    if (source.isEmpty() || mountPoint.isEmpty()) {
        error = QStringLiteral("The VHD file is not located on a mounted block-device partition.");
        return false;
    }
    return true;
}

bool hostPartitionForPath(const QString &imagePath, PartitionTable &table,
                          PartitionEntry &partition, QString &windowsPath,
                          QString &fileSystem, QString &error) {
    QString source;
    QString mountPoint;
    if (!mountedVolumeForPath(imagePath, source, mountPoint, fileSystem, error))
        return false;

    const QString sourcePath = canonicalDevicePath(source);
    QByteArray output;
    if (!runCommand(QStringLiteral("lsblk"),
                    {QStringLiteral("--json"), QStringLiteral("--paths"),
                     QStringLiteral("--output"), QStringLiteral("PKNAME"), sourcePath},
                    output, error))
        return false;

    QJsonParseError parseError;
    const QJsonArray devices = QJsonDocument::fromJson(output, &parseError)
        .object().value(QStringLiteral("blockdevices")).toArray();
    if (parseError.error != QJsonParseError::NoError || devices.isEmpty()) {
        error = QStringLiteral("Could not determine the physical disk containing %1.")
            .arg(sourcePath);
        return false;
    }
    QString parentDisk = devices.first().toObject()
        .value(QStringLiteral("pkname")).toString();
    if (parentDisk.isEmpty()) {
        error = QStringLiteral("The VHD host volume is not a partition on a GPT/MBR disk.");
        return false;
    }
    if (!parentDisk.startsWith(QLatin1Char('/')))
        parentDisk = QStringLiteral("/dev/%1").arg(parentDisk);

    if (!readPartitionTable(parentDisk, table, error))
        return false;

    const QString canonicalSource = canonicalDevicePath(sourcePath);
    for (const PartitionEntry &candidate : table.partitions) {
        if (canonicalDevicePath(candidate.node) == canonicalSource) {
            partition = candidate;
            break;
        }
    }
    if (partition.node.isEmpty()) {
        error = QStringLiteral("The mounted VHD host volume is not present in the disk partition table.");
        return false;
    }

    const QString canonicalImage = QFileInfo(imagePath).canonicalFilePath();
    const QString canonicalMount = QFileInfo(mountPoint).canonicalFilePath();
    if (canonicalImage.isEmpty() || canonicalMount.isEmpty()) {
        error = QStringLiteral("Could not resolve the VHD path or its host mount point.");
        return false;
    }
    QString relativePath = QDir(canonicalMount).relativeFilePath(canonicalImage);
    if (relativePath == QStringLiteral("..") ||
        relativePath.startsWith(QStringLiteral("../")) ||
        QDir::isAbsolutePath(relativePath) ||
        relativePath.contains(QLatin1Char(':'))) {
        error = QStringLiteral("The VHD path cannot be represented relative to its host partition.");
        return false;
    }
    relativePath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    windowsPath = QStringLiteral("\\%1").arg(relativePath);
    return true;
}

QString freeNbdDevice(QString &error) {
    const QStringList devices = QDir(QStringLiteral("/sys/class/block"))
        .entryList({QStringLiteral("nbd*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : devices) {
        QFile sizeFile(QStringLiteral("/sys/class/block/%1/size").arg(name));
        if (!sizeFile.open(QIODevice::ReadOnly))
            continue;
        if (sizeFile.readAll().trimmed() == QByteArrayLiteral("0")) {
            const QString device = QStringLiteral("/dev/%1").arg(name);
            if (QFileInfo::exists(device))
                return device;
        }
    }
    error = QStringLiteral("No free NBD device is available. Load the Linux nbd kernel module and install qemu-nbd.");
    return {};
}

bool disconnectNbd(const QString &device, QString &error) {
    QByteArray output;
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("qemu-nbd"));
    if (executable.isEmpty()) {
        error = QStringLiteral("qemu-nbd disappeared before the VHD image could be detached.");
        return false;
    }
    return runCommand(executable, {QStringLiteral("--disconnect"), device}, output, error);
}

bool inspectVhdImage(QWidget *parent, const QString &imagePath,
                     const PartitionTable &hostTable, const PartitionEntry &hostPartition,
                     const QString &windowsPath, BcdPartitionIdentity &vhdIdentity,
                     BcdPartitionIdentity &hostIdentity, QByteArray &descriptor,
                     QString &error, bool &cancelled) {
    cancelled = false;
    const QString qemuNbd = QStandardPaths::findExecutable(QStringLiteral("qemu-nbd"));
    if (qemuNbd.isEmpty()) {
        error = QStringLiteral("VHD/VHDX support requires qemu-nbd (provided by qemu-utils or the distribution's QEMU tools package).");
        return false;
    }

    const QString nbdDevice = freeNbdDevice(error);
    if (nbdDevice.isEmpty())
        return false;

    QByteArray output;
    if (!runCommand(qemuNbd,
                    {QStringLiteral("--read-only"),
                     QStringLiteral("--connect=%1").arg(nbdDevice),
                     QStringLiteral("--fork"), imagePath},
                    output, error))
        return false;

    bool success = false;
    QString operationError;
    PartitionTable vhdTable;
    if (readPartitionTable(nbdDevice, vhdTable, operationError)) {
        if (vhdTable.partitions.isEmpty()) {
            operationError = QStringLiteral("The VHD image contains no GPT/MBR partitions.");
        } else {
            QStringList choices;
            for (int index = 0; index < vhdTable.partitions.size(); ++index) {
                const PartitionEntry &entry = vhdTable.partitions.at(index);
                const QString partitionName = entry.name.isEmpty()
                    ? QFileInfo(entry.node).fileName() : entry.name;
                if (entry.size > std::numeric_limits<quint64>::max() /
                                     vhdTable.sectorSize) {
                    operationError = QStringLiteral("A VHD partition size is out of range.");
                    break;
                }
                const quint64 sizeBytes = entry.size * vhdTable.sectorSize;
                choices.append(QStringLiteral("%1 — %2 — %3 (%4 GiB)")
                    .arg(index + 1)
                    .arg(partitionName, entry.type,
                         QString::number(static_cast<double>(sizeBytes) /
                                         (1024.0 * 1024.0 * 1024.0), 'f', 1)));
            }

            bool accepted = false;
            if (operationError.isEmpty()) {
                const QString selected = QInputDialog::getItem(
                    parent, QStringLiteral("Select Windows partition"),
                    QStringLiteral("Choose the partition inside the VHD that contains the Windows installation:"),
                    choices, 0, false, &accepted);
                if (!accepted) {
                    cancelled = true;
                } else {
                    const int selectedIndex = choices.indexOf(selected);
                    if (selectedIndex < 0) {
                        operationError = QStringLiteral("The selected VHD partition is invalid.");
                    } else {
                        const bool hostIdentityValid =
                            partitionIdentity(hostTable, hostPartition, hostIdentity, operationError);
                        const bool vhdIdentityValid = hostIdentityValid &&
                            partitionIdentity(vhdTable, vhdTable.partitions.at(selectedIndex),
                                              vhdIdentity, operationError);
                        if (vhdIdentityValid) {
                            success = buildBcdVhdDeviceDescriptor(
                                windowsPath, vhdIdentity, hostIdentity, descriptor, operationError);
                        }
                    }
                }
            }
        }
    }

    QString detachError;
    if (!disconnectNbd(nbdDevice, detachError)) {
        error = QStringLiteral("Could not detach the read-only VHD device %1: %2")
            .arg(nbdDevice, detachError);
        return false;
    }
    if (!success && !cancelled)
        error = operationError;
    return success;
}

}

VhdSetupResult promptForVhdDevice(QWidget *parent,
                                  QByteArray &descriptor,
                                  QString &error) {
    descriptor.clear();
    error.clear();

    const QString imagePath = QFileDialog::getOpenFileName(
        parent, QStringLiteral("Select Windows VHD/VHDX image"),
        QDir::homePath(),
        QStringLiteral("Virtual hard disks (*.vhd *.vhdx);;All files (*)"));
    if (imagePath.isEmpty())
        return VhdSetupResult::Cancelled;

    const QFileInfo imageInfo(imagePath);
    const QString suffix = imageInfo.suffix();
    if (!imageInfo.isFile() ||
        (suffix.compare(QStringLiteral("vhd"), Qt::CaseInsensitive) != 0 &&
         suffix.compare(QStringLiteral("vhdx"), Qt::CaseInsensitive) != 0)) {
        error = QStringLiteral("Select an existing .vhd or .vhdx file.");
        return VhdSetupResult::Failed;
    }
    const QString canonicalImage = imageInfo.canonicalFilePath();
    if (canonicalImage.isEmpty()) {
        error = QStringLiteral("Could not resolve the selected VHD/VHDX file.");
        return VhdSetupResult::Failed;
    }

    PartitionTable hostTable;
    PartitionEntry hostPartition;
    QString windowsPath;
    QString fileSystem;
    if (!hostPartitionForPath(canonicalImage, hostTable, hostPartition,
                              windowsPath, fileSystem, error))
        return VhdSetupResult::Failed;

    if (fileSystem.compare(QStringLiteral("ntfs"), Qt::CaseInsensitive) != 0 &&
        fileSystem.compare(QStringLiteral("vfat"), Qt::CaseInsensitive) != 0 &&
        QMessageBox::warning(
            parent, QStringLiteral("Unsupported host filesystem"),
            QStringLiteral("The VHD is stored on %1. Windows Boot Manager may not be able to read a VHD from this filesystem. Continue?")
                .arg(fileSystem.isEmpty() ? QStringLiteral("an unknown filesystem") : fileSystem),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return VhdSetupResult::Cancelled;

    BcdPartitionIdentity vhdIdentity;
    BcdPartitionIdentity hostIdentity;
    bool cancelled = false;
    if (!inspectVhdImage(parent, canonicalImage, hostTable, hostPartition,
                         windowsPath, vhdIdentity, hostIdentity,
                         descriptor, error, cancelled)) {
        return cancelled ? VhdSetupResult::Cancelled : VhdSetupResult::Failed;
    }

    return VhdSetupResult::Ready;
}
