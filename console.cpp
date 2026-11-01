#include "console.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QUuid>
#include <hivex.h>

#include <cstdlib>
#include <limits>

#ifndef BCDSCRIBE_VERSION
#define BCDSCRIBE_VERSION "development"
#endif

namespace {

constexpr const char *bootManagerGuid =
    "{9dea862c-5cdd-4e70-acc1-f32b344d4795}";
constexpr const char *badMemoryGuid =
    "{5189b25c-5558-4bf2-bca4-289b11bd29e2}";
constexpr quint32 osLoaderType = 0x10200003u;

QString nodeName(hive_h *hive, hive_node_h node) {
    char *rawName = hivex_node_name(hive, node);
    const QString name = rawName ? QString::fromUtf8(rawName) : QString();
    free(rawName);
    return name;
}

hive_node_h childNode(hive_h *hive, hive_node_h parent, const QString &name) {
    hive_node_h *children = hivex_node_children(hive, parent);
    if (!children)
        return 0;

    hive_node_h result = 0;
    for (hive_node_h *child = children; *child; ++child) {
        if (nodeName(hive, *child).compare(name, Qt::CaseInsensitive) == 0) {
            result = *child;
            break;
        }
    }
    free(children);
    return result;
}

QByteArray utf16Data(const QString &text) {
    QByteArray data;
    data.reserve((text.size() + 1) * 2);
    for (const QChar character : text) {
        const quint16 codeUnit = character.unicode();
        data.append(static_cast<char>(codeUnit & 0xff));
        data.append(static_cast<char>((codeUnit >> 8) & 0xff));
    }
    data.append('\0');
    data.append('\0');
    return data;
}

bool setValue(hive_h *hive, hive_node_h node, const char *name,
              hive_type type, const QByteArray &data) {
    QByteArray key(name);
    hive_set_value value = {
        key.data(), type, static_cast<size_t>(data.size()),
        const_cast<char *>(data.constData())
    };
    return hivex_node_set_value(hive, node, &value, 0) != -1;
}

bool setElement(hive_h *hive, hive_node_h elements, const QString &id,
                hive_type type, const QByteArray &data) {
    const QByteArray encodedId = id.toUtf8();
    hive_node_h element = childNode(hive, elements, id);
    if (!element)
        element = hivex_node_add_child(hive, elements, encodedId.constData());
    return element && setValue(hive, element, "Element", type, data);
}

bool readValue(hive_h *hive, hive_value_h value, hive_type &type,
               QByteArray &data) {
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData)
        return false;
    if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
        free(rawData);
        return false;
    }
    data = QByteArray(rawData, static_cast<int>(length));
    free(rawData);
    return true;
}

QString displayValue(const QByteArray &data, hive_type type) {
    if (type == hive_t_REG_SZ || type == hive_t_REG_EXPAND_SZ) {
        QString text;
        for (int index = 0; index + 1 < data.size(); index += 2) {
            const ushort codeUnit = static_cast<ushort>(
                static_cast<unsigned char>(data.at(index)) |
                (static_cast<unsigned char>(data.at(index + 1)) << 8));
            if (codeUnit == 0)
                break;
            text.append(QChar(codeUnit));
        }
        return text;
    }
    return QStringLiteral("hex:%1").arg(QString::fromLatin1(data.toHex(',')));
}

bool parseType(const QString &name, hive_type &type) {
    if (name.compare(QStringLiteral("string"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("sz"), Qt::CaseInsensitive) == 0) {
        type = hive_t_REG_SZ;
        return true;
    }
    if (name.compare(QStringLiteral("binary"), Qt::CaseInsensitive) == 0 ||
        name.compare(QStringLiteral("hex"), Qt::CaseInsensitive) == 0) {
        type = hive_t_REG_BINARY;
        return true;
    }
    if (name.compare(QStringLiteral("dword"), Qt::CaseInsensitive) == 0) {
        type = hive_t_REG_DWORD;
        return true;
    }
    return false;
}

bool parseData(const QString &text, hive_type type, QByteArray &data) {
    if (type == hive_t_REG_SZ) {
        data = utf16Data(text);
        return true;
    }
    if (type == hive_t_REG_BINARY) {
        QString hex = text;
        if (hex.startsWith(QStringLiteral("hex:"), Qt::CaseInsensitive))
            hex.remove(0, 4);
        hex.remove(QLatin1Char(','));
        hex.remove(QLatin1Char(' '));
        if (hex.size() % 2 != 0)
            return false;
        for (const QChar character : hex) {
            if (!character.isDigit() &&
                !(character.toLower() >= QLatin1Char('a') &&
                  character.toLower() <= QLatin1Char('f')))
                return false;
        }
        data = QByteArray::fromHex(hex.toLatin1());
        return true;
    }
    bool valid = false;
    quint32 number = text.toUInt(&valid, 0);
    if (!valid)
        return false;
    data.resize(4);
    for (int index = 0; index < 4; ++index)
        data[index] = static_cast<char>((number >> (index * 8)) & 0xff);
    return true;
}

bool commit(hive_h *hive, const QString &path) {
    const QByteArray encodedPath = QFile::encodeName(path);
    return hivex_commit(hive, encodedPath.constData(), 0) != -1;
}

QByteArray referenceData(const QString &guid) {
    const QString normalized = guid.startsWith(QLatin1Char('{'))
        ? guid : QStringLiteral("{%1}").arg(guid);
    QByteArray data;
    for (const QChar character : normalized) {
        const quint16 codeUnit = character.unicode();
        data.append(static_cast<char>(codeUnit & 0xff));
        data.append(static_cast<char>((codeUnit >> 8) & 0xff));
    }
    data.append('\0');
    data.append('\0');
    return data;
}

int enumerateStore(hive_h *hive) {
    QTextStream out(stdout);
    const hive_node_h objects = childNode(hive, hivex_root(hive), QStringLiteral("Objects"));
    if (!objects) {
        QTextStream(stderr) << "BCD store has no Objects key.\n";
        return 1;
    }

    hive_node_h *objectNodes = hivex_node_children(hive, objects);
    if (!objectNodes) {
        QTextStream(stderr) << "Could not enumerate BCD objects.\n";
        return 1;
    }
    for (hive_node_h *object = objectNodes; *object; ++object) {
        const QString guid = nodeName(hive, *object);
        const hive_node_h description = childNode(hive, *object, QStringLiteral("Description"));
        const hive_value_h typeValue = description
            ? hivex_node_get_value(hive, description, "Type") : 0;
        const quint32 objectType = typeValue
            ? static_cast<quint32>(hivex_value_dword(hive, typeValue)) : 0;
        out << guid << "  Type 0x" << QString::number(objectType, 16).rightJustified(8, '0');
        const hive_node_h elements = childNode(hive, *object, QStringLiteral("Elements"));
        const hive_node_h titleElement = elements
            ? childNode(hive, elements, QStringLiteral("12000004")) : 0;
        const hive_value_h titleValue = titleElement
            ? hivex_node_get_value(hive, titleElement, "Element") : 0;
        if (titleValue) {
            hive_type valueType;
            QByteArray valueData;
            if (readValue(hive, titleValue, valueType, valueData))
                out << "  " << displayValue(valueData, valueType);
        }
        out << Qt::endl;

        if (!elements)
            continue;
        hive_node_h *elementNodes = hivex_node_children(hive, elements);
        if (!elementNodes)
            continue;
        for (hive_node_h *element = elementNodes; *element; ++element) {
            const QString id = nodeName(hive, *element);
            const hive_value_h elementValue =
                hivex_node_get_value(hive, *element, "Element");
            if (!elementValue)
                continue;
            hive_type valueType;
            QByteArray valueData;
            if (!readValue(hive, elementValue, valueType, valueData))
                continue;
            out << "  " << id << " = " << displayValue(valueData, valueType) << Qt::endl;
        }
        free(elementNodes);
    }
    free(objectNodes);
    return 0;
}

int runCommand(const QStringList &arguments) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (arguments.size() == 1 && arguments.first() == QStringLiteral("--version")) {
        out << "BCDScribe " << BCDSCRIBE_VERSION << Qt::endl;
        return 0;
    }
    if (arguments.isEmpty() || arguments.contains(QStringLiteral("--help")) ||
        arguments.contains(QStringLiteral("-h")) || arguments.contains(QStringLiteral("/?"))) {
        out << "BCDScribe console usage:\n"
               "  BCDScribe --help\n"
               "  BCDScribe --store FILE /enum\n"
               "  BCDScribe --store FILE /export DESTINATION\n"
               "  BCDScribe --store FILE /import SOURCE --write\n"
               "  BCDScribe --store FILE /create DESCRIPTION [--type DWORD] --write\n"
               "  BCDScribe --store FILE /set GUID ELEMENT string|binary|dword VALUE --write\n"
               "  BCDScribe --store FILE /delete GUID --write\n"
               "  BCDScribe --store FILE /deletevalue GUID ELEMENT --write\n"
               "  BCDScribe --store FILE /timeout SECONDS --write\n"
               "  BCDScribe --store FILE /default GUID --write\n"
               "  BCDScribe --store FILE /displayorder GUID... --write\n"
               "  BCDScribe --store FILE /bootsequence GUID... --write\n"
               "  BCDScribe --store FILE /toolsdisplayorder GUID... --write\n"
               "  BCDScribe --store FILE /bootdebug GUID on|off --write\n"
               "  BCDScribe --store FILE /debug GUID on|off --write\n"
               "  BCDScribe --store FILE /ems GUID on|off --write\n"
               "  BCDScribe --store FILE /event GUID on|off --write\n"
               "  BCDScribe --store FILE /badmemoryaccess on|off --write\n\n"
               "Commands edit an offline BCD hive. Mutating commands require --write.\n"
               "Binary values use hex bytes, for example: 01,00,ff.\n"
               "The console does not request GUI/PolicyKit authorization; use a writable\n"
               "store or run the command with the appropriate system permissions.\n";
        return arguments.isEmpty() ? 2 : 0;
    }

    QString storePath;
    QString command;
    QStringList commandArgs;
    bool writeEnabled = arguments.contains(QStringLiteral("--write"));
    int index = 0;
    while (index < arguments.size()) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--write")) {
            writeEnabled = true;
            ++index;
        } else if (argument == QStringLiteral("--store") ||
                   argument == QStringLiteral("/store")) {
            if (index + 1 >= arguments.size()) {
                err << argument << " requires a file path.\n";
                return 2;
            }
            storePath = arguments.at(index + 1);
            index += 2;
        } else {
            command = argument.toLower();
            commandArgs = arguments.mid(index + 1);
            break;
        }
    }
    commandArgs.removeAll(QStringLiteral("--write"));
    if (storePath.isEmpty()) {
        err << "Specify an offline store with --store FILE.\n";
        return 2;
    }
    if (command.isEmpty()) {
        err << "Specify a command. Run BCDScribe --help for usage.\n";
        return 2;
    }

    const bool mutating = command == QStringLiteral("/create") ||
        command == QStringLiteral("/set") ||
        command == QStringLiteral("/delete") ||
        command == QStringLiteral("/deletevalue") ||
        command == QStringLiteral("/timeout") ||
        command == QStringLiteral("/default") ||
        command == QStringLiteral("/displayorder") ||
        command == QStringLiteral("/bootsequence") ||
        command == QStringLiteral("/toolsdisplayorder") ||
        command == QStringLiteral("/bootdebug") ||
        command == QStringLiteral("/debug") ||
        command == QStringLiteral("/ems") ||
        command == QStringLiteral("/event") ||
        command == QStringLiteral("/badmemoryaccess");
    const bool transfersStore = command == QStringLiteral("/import");
    const bool mutatesStore = mutating || transfersStore;
    if (mutatesStore && !writeEnabled) {
        err << "This command modifies the BCD store. Add --write to confirm.\n";
        return 2;
    }

    const QByteArray encodedPath = QFile::encodeName(storePath);
    hive_h *hive = hivex_open(encodedPath.constData(),
                               mutatesStore ? HIVEX_OPEN_WRITE : 0);
    if (!hive) {
        err << "Could not open BCD store: " << storePath << Qt::endl;
        return 1;
    }

    int result = 0;
    const hive_node_h objects = childNode(
        hive, hivex_root(hive), QStringLiteral("Objects"));
    if (command == QStringLiteral("/enum")) {
        if (!commandArgs.isEmpty()) {
            err << "/enum does not take arguments.\n";
            result = 2;
        } else {
            result = enumerateStore(hive);
        }
    } else if (!objects) {
        err << "BCD store has no Objects key.\n";
        result = 1;
    } else if (command == QStringLiteral("/create")) {
        if (commandArgs.isEmpty()) {
            err << "/create requires a description.\n";
            result = 2;
        } else {
            quint32 objectType = osLoaderType;
            const int typeIndex = commandArgs.indexOf(QStringLiteral("--type"));
            if (typeIndex >= 0) {
                bool valid = false;
                if (typeIndex + 1 >= commandArgs.size()) {
                    err << "--type requires a DWORD value.\n";
                    result = 2;
                } else {
                    objectType = commandArgs.at(typeIndex + 1).toUInt(&valid, 0);
                    if (!valid) {
                        err << "Invalid --type DWORD value.\n";
                        result = 2;
                    }
                }
                if (typeIndex >= 0 && result == 0) {
                    commandArgs.removeAt(typeIndex + 1);
                    commandArgs.removeAt(typeIndex);
                }
            }
            if (result == 0) {
                const QString descriptionText = commandArgs.join(QLatin1Char(' '));
                const QString guid = QUuid::createUuid()
                    .toString(QUuid::WithoutBraces).toLower();
                const QByteArray encodedGuid = QStringLiteral("{%1}").arg(guid).toUtf8();
                const hive_node_h object = hivex_node_add_child(
                    hive, objects, encodedGuid.constData());
                const hive_node_h description = object
                    ? hivex_node_add_child(hive, object, "Description") : 0;
                const hive_node_h elements = object
                    ? hivex_node_add_child(hive, object, "Elements") : 0;
                const QByteArray typeData = QByteArray(1, static_cast<char>(objectType & 0xff)) +
                    QByteArray(1, static_cast<char>((objectType >> 8) & 0xff)) +
                    QByteArray(1, static_cast<char>((objectType >> 16) & 0xff)) +
                    QByteArray(1, static_cast<char>((objectType >> 24) & 0xff));
                if (!object || !description || !elements ||
                    !setValue(hive, description, "Type", hive_t_REG_DWORD, typeData) ||
                    !setElement(hive, elements, QStringLiteral("12000004"),
                                hive_t_REG_SZ, utf16Data(descriptionText))) {
                    if (object)
                        hivex_node_delete_child(hive, object);
                    err << "Could not create BCD object.\n";
                    result = 1;
                } else {
                    QTextStream(stdout) << "Created {" << guid << "}." << Qt::endl;
                }
            }
        }
    } else if (command == QStringLiteral("/set")) {
        if (commandArgs.size() < 4) {
            err << "Usage: /set GUID ELEMENT string|binary|dword VALUE\n";
            result = 2;
        } else {
            const hive_node_h object = childNode(
                hive, objects, commandArgs.at(0));
            const hive_node_h elements = object
                ? childNode(hive, object, QStringLiteral("Elements")) : 0;
            hive_type type;
            QByteArray data;
            if (!object || !elements) {
                err << "BCD object or Elements key not found.\n";
                result = 1;
            } else if (!parseType(commandArgs.at(2), type) ||
                       !parseData(commandArgs.mid(3).join(QLatin1Char(' ')), type, data)) {
                err << "Invalid type or value. Binary data must be comma-separated hex bytes.\n";
                result = 2;
            } else if (!setElement(hive, elements, commandArgs.at(1), type, data)) {
                err << "Could not set BCD element.\n";
                result = 1;
            }
        }
    } else if (command == QStringLiteral("/delete")) {
        if (commandArgs.size() != 1) {
            err << "Usage: /delete GUID\n";
            result = 2;
        } else {
            const hive_node_h object = childNode(hive, objects, commandArgs.first());
            if (!object) {
                err << "BCD object not found.\n";
                result = 1;
            } else if (nodeName(hive, object).compare(
                           QString::fromLatin1(bootManagerGuid), Qt::CaseInsensitive) == 0) {
                err << "The Windows Boot Manager object is protected.\n";
                result = 1;
            } else if (hivex_node_delete_child(hive, object) == -1) {
                err << "Could not delete BCD object.\n";
                result = 1;
            }
        }
    } else if (command == QStringLiteral("/deletevalue")) {
        if (commandArgs.size() != 2) {
            err << "Usage: /deletevalue GUID ELEMENT\n";
            result = 2;
        } else {
            const hive_node_h object = childNode(
                hive, objects, commandArgs.at(0));
            const hive_node_h elements = object
                ? childNode(hive, object, QStringLiteral("Elements")) : 0;
            const hive_node_h element = elements
                ? childNode(hive, elements, commandArgs.at(1)) : 0;
            if (!element || hivex_node_delete_child(hive, element) == -1) {
                err << "Could not find or delete BCD element.\n";
                result = 1;
            }
        }
    } else if (command == QStringLiteral("/timeout")) {
        bool valid = false;
        const quint32 seconds = commandArgs.value(0).toUInt(&valid, 10);
        if (commandArgs.size() != 1 || !valid) {
            err << "Usage: /timeout SECONDS\n";
            result = 2;
        } else {
            const hive_node_h manager = childNode(
                hive, objects, QString::fromLatin1(bootManagerGuid));
            hive_node_h elements = manager
                ? childNode(hive, manager, QStringLiteral("Elements")) : 0;
            if (manager && !elements)
                elements = hivex_node_add_child(hive, manager, "Elements");
            QByteArray data(8, '\0');
            for (int byte = 0; byte < 4; ++byte)
                data[byte] = static_cast<char>((seconds >> (byte * 8)) & 0xff);
            if (!elements || !setElement(hive, elements, QStringLiteral("25000004"),
                                         hive_t_REG_BINARY, data)) {
                err << "Could not set boot-menu timeout.\n";
                result = 1;
            }
        }
    } else if (command == QStringLiteral("/default") ||
               command == QStringLiteral("/displayorder") ||
               command == QStringLiteral("/bootsequence") ||
               command == QStringLiteral("/toolsdisplayorder")) {
        const bool orderCommand = command != QStringLiteral("/default");
        if (commandArgs.isEmpty() || (!orderCommand && commandArgs.size() != 1)) {
            err << "This command requires a target GUID"
                   " (and one or more GUIDs for ordering commands).\n";
            result = 2;
        } else {
            bool validTargets = true;
            for (const QString &guid : commandArgs) {
                const QString objectName = guid.startsWith(QLatin1Char('{'))
                    ? guid : QStringLiteral("{%1}").arg(guid);
                if (!childNode(hive, objects, objectName)) {
                    err << "BCD object not found: " << objectName << Qt::endl;
                    validTargets = false;
                }
            }
            const hive_node_h manager = childNode(
                hive, objects, QString::fromLatin1(bootManagerGuid));
            hive_node_h elements = manager
                ? childNode(hive, manager, QStringLiteral("Elements")) : 0;
            if (manager && !elements)
                elements = hivex_node_add_child(hive, manager, "Elements");
            QString elementId = QStringLiteral("23000003");
            if (command == QStringLiteral("/displayorder"))
                elementId = QStringLiteral("24000001");
            else if (command == QStringLiteral("/bootsequence"))
                elementId = QStringLiteral("24000002");
            else if (command == QStringLiteral("/toolsdisplayorder"))
                elementId = QStringLiteral("24000010");

            QByteArray data;
            for (const QString &guid : commandArgs)
                data.append(referenceData(guid));
            if (orderCommand) {
                data.append('\0');
                data.append('\0');
            }
            if (!validTargets || !elements ||
                !setElement(hive, elements, elementId, hive_t_REG_BINARY, data))
                result = 1;
        }
    } else if (command == QStringLiteral("/bootdebug") ||
               command == QStringLiteral("/debug") ||
               command == QStringLiteral("/ems") ||
               command == QStringLiteral("/event") ||
               command == QStringLiteral("/badmemoryaccess")) {
        const bool badMemory = command == QStringLiteral("/badmemoryaccess");
        if (commandArgs.size() != (badMemory ? 1 : 2)) {
            err << "Usage: " << command
                << (badMemory ? " on|off" : " GUID on|off") << Qt::endl;
            result = 2;
        } else {
            const QString state = commandArgs.last().toLower();
            if (state != QStringLiteral("on") && state != QStringLiteral("off")) {
                err << "Boolean value must be on or off.\n";
                result = 2;
            } else {
                const QString objectName = badMemory
                    ? QString::fromLatin1(badMemoryGuid)
                    : commandArgs.first().startsWith(QLatin1Char('{'))
                        ? commandArgs.first()
                        : QStringLiteral("{%1}").arg(commandArgs.first());
                const hive_node_h object = childNode(hive, objects, objectName);
                hive_node_h elements = object
                    ? childNode(hive, object, QStringLiteral("Elements")) : 0;
                if (object && !elements)
                    elements = hivex_node_add_child(hive, object, "Elements");
                QString elementId;
                if (command == QStringLiteral("/bootdebug"))
                    elementId = QStringLiteral("16000010");
                else if (command == QStringLiteral("/debug"))
                    elementId = QStringLiteral("260000A0");
                else if (command == QStringLiteral("/ems"))
                    elementId = QStringLiteral("260000B0");
                else if (command == QStringLiteral("/event"))
                    elementId = QStringLiteral("26000141");
                else
                    elementId = QStringLiteral("1600000B");
                const QByteArray data(1, state == QStringLiteral("on") ? '\1' : '\0');
                if (!object || !elements ||
                    !setElement(hive, elements, elementId, hive_t_REG_BINARY, data)) {
                    err << "Could not set " << command << " on " << objectName << ".\n";
                    result = 1;
                }
            }
        }
    } else if (command == QStringLiteral("/export")) {
        if (commandArgs.size() != 1) {
            err << "Usage: /export DESTINATION\n";
            result = 2;
        } else if (QFileInfo::exists(commandArgs.first())) {
            err << "Export destination already exists: " << commandArgs.first() << Qt::endl;
            result = 1;
        } else if (!QFile::copy(storePath, commandArgs.first())) {
            err << "Could not export BCD store to " << commandArgs.first() << ".\n";
            result = 1;
        }
    } else if (command == QStringLiteral("/import")) {
        if (commandArgs.size() != 1) {
            err << "Usage: /import SOURCE\n";
            result = 2;
        } else {
            const QByteArray sourcePath = QFile::encodeName(commandArgs.first());
            hive_h *sourceHive = hivex_open(sourcePath.constData(), 0);
            if (!sourceHive) {
                err << "Import source is not a readable registry hive: "
                    << commandArgs.first() << Qt::endl;
                result = 1;
            } else {
                const bool validBcd = childNode(
                    sourceHive, hivex_root(sourceHive), QStringLiteral("Objects")) != 0;
                hivex_close(sourceHive);
                if (!validBcd) {
                    err << "Import source is not a BCD store (missing Objects key).\n";
                    result = 1;
                }
            }
            if (result == 0) {
                QFile source(commandArgs.first());
                if (!source.open(QIODevice::ReadOnly)) {
                    err << "Could not read import source: " << commandArgs.first() << Qt::endl;
                    result = 1;
                } else {
                    const QByteArray sourceData = source.readAll();
                    if (source.error() != QFileDevice::NoError) {
                        err << "Could not read import source: " << commandArgs.first() << Qt::endl;
                        result = 1;
                    }
                    QSaveFile destination(storePath);
                    if (result == 0 &&
                        (!destination.open(QIODevice::WriteOnly) ||
                         destination.write(sourceData) != sourceData.size() ||
                         !destination.commit())) {
                        destination.cancelWriting();
                        err << "Could not replace the target BCD store.\n";
                        result = 1;
                    }
                }
            }
        }
    } else {
        err << "Unsupported console command: " << command
            << ". Run BCDScribe --help for the supported commands.\n";
        result = 2;
    }

    if (result == 0 && mutating && !commit(hive, storePath)) {
        err << "Could not commit changes to " << storePath << ".\n";
        result = 1;
    }
    hivex_close(hive);
    return result;
}

}

int runConsoleCommand(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QStringList arguments = app.arguments().mid(1);
    return runCommand(arguments);
}
