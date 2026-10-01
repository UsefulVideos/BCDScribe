#include "mainwindow.h"
#include <QAbstractItemView>
#include <QByteArray>
#include <QComboBox>
#include <QClipboard>
#include <QCompleter>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSettings>
#include <QShortcut>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>
#include <climits>

static QString bcd_element_name(const QString &id, quint32 objectType);
static QString bcdedit_option_name(const QString &id, quint32 objectType);
static bool is_bcd_boolean_element(const QString &id);
static QString bcd_boolean_value_text(hive_h *hive, hive_value_h value);
static QString bcd_policy_value_text(hive_h *hive, hive_value_h value, const QString &id);
static QString bcd_display_message_value_text(hive_h *hive, hive_value_h value);
static QString bcd_numeric_value_text(hive_h *hive, hive_value_h value);
static QStringList bcd_reference_values(hive_h *hive, hive_value_h value);
static QString bcd_reference_value_text(hive_h *hive, hive_value_h value);
static QString value_data_text(hive_h *hive, hive_value_h value);

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUI();
}

MainWindow::~MainWindow() {
    if (hive)
        hivex_close(hive);
}

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 10);
    mainLayout->setSpacing(8);

    auto *heading = new QHBoxLayout();
    auto *titleLabel = new QLabel("BCD Store", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    heading->addWidget(titleLabel);
    heading->addStretch();
    heading->addWidget(new QLabel("Windows Boot Configuration Data", this));
    mainLayout->addLayout(heading);

    QHBoxLayout *fileLayout = new QHBoxLayout();
    pathComboBox = new QComboBox(this);
    pathComboBox->setEditable(true);
    pathComboBox->setInsertPolicy(QComboBox::NoInsert);
    pathComboBox->setMaxVisibleItems(12);
    pathComboBox->setPlaceholderText("Type a path; existing folders will be suggested");
    auto *directoryModel = new QFileSystemModel(pathComboBox);
    directoryModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    directoryModel->setRootPath(QDir::rootPath());
    auto *pathCompleter = new QCompleter(directoryModel, pathComboBox);
    pathCompleter->setCompletionMode(QCompleter::PopupCompletion);
    pathCompleter->setCaseSensitivity(Qt::CaseSensitive);
    pathComboBox->setCompleter(pathCompleter);
    const QStringList recentPaths = QSettings(QStringLiteral("BCDScribe"),
                                               QStringLiteral("BCDScribe"))
        .value(QStringLiteral("recentBcdStores")).toStringList();
    pathComboBox->addItems(recentPaths);
    connect(pathComboBox->lineEdit(), &QLineEdit::textEdited, this,
            [this, recentPaths](const QString &typedPath) {
                QDir directory(typedPath.trimmed());
                const QFileInfoList subdirectories = directory.exists()
                    ? directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
                    : QFileInfoList();

                const QSignalBlocker blocker(pathComboBox);
                pathComboBox->clear();
                pathComboBox->addItems(recentPaths);
                for (const QFileInfo &subdirectory : subdirectories) {
                    const QString path = subdirectory.absoluteFilePath();
                    if (pathComboBox->findText(path) < 0)
                        pathComboBox->addItem(path);
                }
                pathComboBox->setEditText(typedPath);
            });

    openButton = new QPushButton("Open Store...", this);
    saveButton = new QPushButton("Save", this);
    createStoreButton = new QPushButton("Create BCD Store...", this);
    fileLayout->addWidget(pathComboBox);
    fileLayout->addWidget(openButton);
    fileLayout->addWidget(saveButton);
    fileLayout->addWidget(createStoreButton);
    mainLayout->addLayout(fileLayout);

    modeTabs = new QTabWidget(this);
    auto makeTable = [](const QStringList &headers, QWidget *parent) {
        auto *table = new QTableWidget(parent);
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        if (headers.size() > 1)
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        return table;
    };

    auto *bootPage = new QWidget(modeTabs);
    auto *bootLayout = new QVBoxLayout(bootPage);
    bootLayout->setContentsMargins(0, 8, 0, 0);
    auto *bootSplitter = new QSplitter(bootPage);
    bootTree = new QTreeWidget(bootSplitter);
    bootTree->setHeaderLabel("Boot objects");
    bootTree->setMinimumWidth(260);
    bootTree->setContextMenuPolicy(Qt::CustomContextMenu);
    bootTable = makeTable({"BCDScribe field", "Windows setting", "Stored data"}, bootSplitter);
    bootTable->setContextMenuPolicy(Qt::CustomContextMenu);
    bootSplitter->addWidget(bootTree);
    bootSplitter->addWidget(bootTable);
    bootSplitter->setStretchFactor(0, 1);
    bootSplitter->setStretchFactor(1, 3);
    bootLayout->addWidget(bootSplitter);
    modeTabs->addTab(bootPage, "BCD Professional Mode");

    auto *referencePage = new QWidget(modeTabs);
    auto *referenceLayout = new QVBoxLayout(referencePage);
    referenceLayout->setContentsMargins(0, 8, 0, 0);
    auto *referenceSplitter = new QSplitter(referencePage);
    auto *commandList = new QListWidget(referenceSplitter);
    auto *commandDetails = new QTextBrowser(referenceSplitter);
    commandDetails->setOpenExternalLinks(true);
    const QList<QPair<QString, QString>> referenceItems = {
        {"/?", "Displays Windows BCD command help. Use <code>bcdedit /? command</code> for command-specific help."},
        {"/createstore", "Creates an empty BCD store. The new store is not the active system store."},
        {"/export", "Exports the system store to a backup file."},
        {"/import", "Restores a previously exported system store. Existing entries are replaced."},
        {"/store", "Selects the BCD store a command operates on."},
        {"/sysstore", "Sets the system store device on EFI systems; this selection is temporary."},
        {"/copy", "Creates a copy of an existing boot entry."},
        {"/create", "Creates a boot entry. Application, inherit, or device type may be required."},
        {"/delete", "Deletes a boot entry or an element, depending on the command form."},
        {"/mirror", "Creates a mirror of entries in the store."},
        {"/deletevalue", "Removes a named element from a boot entry."},
        {"/set", "Sets an element value on a boot entry."},
        {"/bootsequence", "Sets the one-time boot order used for the next startup."},
        {"/badmemoryaccess", "Enables or disables application access to the bad memory list."},
        {"/default", "Selects the default boot entry."},
        {"/displayorder", "Sets the order shown in the boot menu."},
        {"/timeout", "Sets how many seconds the boot manager waits before choosing the default."},
        {"/toolsdisplayorder", "Sets the order of entries in the boot manager Tools menu."},
        {"/enum", "Lists entries in a store. /v displays full identifiers."},
        {"/bootdebug", "Enables or disables boot debugging for a boot application."},
        {"/debug", "Enables or disables kernel debugging for an operating system entry."},
        {"/dbgsettings", "Displays or configures global debugger settings."},
        {"/ems", "Enables or disables Emergency Management Services for an operating system entry."},
        {"/emssettings", "Configures global Emergency Management Services settings."},
        {"/event", "Enables or disables remote event logging for an operating system entry."},
        {"/eventsettings", "Configures global remote event logging settings."},
        {"/hypervisorsettings", "Displays or configures hypervisor debugger settings."},
        {"Edit boot options", "Common tasks include changing boot parameters, adding entries, choosing the default entry, setting the menu timeout, and changing a boot entry's friendly description."}
    };
    for (const auto &entry : referenceItems) {
        auto *item = new QListWidgetItem(entry.first, commandList);
        item->setData(Qt::UserRole, entry.second);
    }
    connect(commandList, &QListWidget::currentItemChanged, this,
            [commandDetails](QListWidgetItem *current, QListWidgetItem *) {
                commandDetails->setHtml(current
                    ? QStringLiteral("<h2>%1</h2><p>%2</p><p><b>Platform note:</b> This is the BCDScribe command reference. The Linux application edits offline BCD stores and does not run the Windows bcdedit.exe program.</p>")
                        .arg(current->text().toHtmlEscaped(), current->data(Qt::UserRole).toString())
                    : QString());
            });
    referenceSplitter->addWidget(commandList);
    referenceSplitter->addWidget(commandDetails);
    referenceSplitter->setStretchFactor(0, 1);
    referenceSplitter->setStretchFactor(1, 2);
    referenceLayout->addWidget(referenceSplitter);
    modeTabs->addTab(referencePage, "BCDScribe reference");
    mainLayout->addWidget(modeTabs, 1);

    statusLabel = new QLabel("Open a BCD store to view boot objects and settings.", this);
    mainLayout->addWidget(statusLabel);

    setCentralWidget(centralWidget);
    resize(1120, 720);
    setWindowTitle("BCDScribe - BCD Store Editor");

    connect(openButton, &QPushButton::clicked, this, &MainWindow::openBcdFile);
    connect(pathComboBox->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::openBcdPath);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveBcdFileAs);
    connect(createStoreButton, &QPushButton::clicked, this, &MainWindow::createBcdStore);
    connect(bootTree, &QTreeWidget::currentItemChanged, this, &MainWindow::showBootObjectValues);
        connect(bootTree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::showBootTreeContextMenu);
        connect(bootTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showBootTableContextMenu);
        for (const QKeySequence &sequence : {QKeySequence(Qt::Key_Menu),
                                             QKeySequence(Qt::SHIFT | Qt::Key_F10)}) {
            auto *treeShortcut = new QShortcut(sequence, bootTree);
            treeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(treeShortcut, &QShortcut::activated, this, [this]() {
                QTreeWidgetItem *current = bootTree->currentItem();
                if (!current)
                    return;
                const QRect rect = bootTree->visualItemRect(current);
                showBootTreeContextMenu(rect.center());
            });

            auto *tableShortcut = new QShortcut(sequence, bootTable);
            tableShortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(tableShortcut, &QShortcut::activated, this, [this]() {
                const int row = bootTable->currentRow();
                if (row < 0)
                    return;
                const QRect rect = bootTable->visualRect(bootTable->model()->index(row, 0));
                showBootTableContextMenu(rect.center());
            });
        }
    connect(modeTabs, &QTabWidget::currentChanged, this, [this](int) { updateActions(); });
    connect(bootTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { updateActions(); });
    connect(bootTable, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { editSelectedValue(); });
    updateActions();
}

void MainWindow::openBcdFile() {
    if (!confirmDiscardChanges())
        return;

    const QString filePath = QFileDialog::getOpenFileName(
        this, "Open Windows BCD Store", QDir::homePath(), "BCD stores and registry hives (*)");
    if (!filePath.isEmpty())
        loadBcdFile(filePath);
}

void MainWindow::openBcdPath() {
    const QString filePath = pathComboBox->currentText().trimmed();
    if (filePath.isEmpty())
        return;
    if (!confirmDiscardChanges())
        return;
    loadBcdFile(filePath);
}

void MainWindow::createBcdStore() {
    if (!confirmDiscardChanges())
        return;

    const QString outputPath = QFileDialog::getSaveFileName(
        this, "Create BCD Store", QDir::home().filePath(QStringLiteral("BCD")),
        "BCD stores and registry hives (*)");
    if (outputPath.isEmpty())
        return;
    if (QFileInfo::exists(outputPath)) {
        QMessageBox::warning(this, "File already exists",
                             "Choose a new file name. Creating a BCD store will not overwrite an existing file.");
        return;
    }

    QFile seedResource(QStringLiteral(":/templates/minimal-hive.qcompress.b64"));
    if (!seedResource.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Create failed", "The embedded registry-hive template is unavailable.");
        return;
    }
    const QByteArray compressedSeed = QByteArray::fromBase64(seedResource.readAll().trimmed());
    const QByteArray seed = qUncompress(compressedSeed);
    if (seed.isEmpty()) {
        QMessageBox::critical(this, "Create failed", "The embedded registry-hive template is invalid.");
        return;
    }

    QTemporaryFile temporaryHive(QDir::temp().filePath(QStringLiteral("BCDScribe-hive-XXXXXX")));
    if (!temporaryHive.open() || temporaryHive.write(seed) != seed.size() || !temporaryHive.flush()) {
        QMessageBox::critical(this, "Create failed", "Could not prepare the registry-hive template.");
        return;
    }
    const QByteArray temporaryPath = temporaryHive.fileName().toUtf8();
    temporaryHive.close();

    hive_h *newHive = hivex_open(temporaryPath.constData(), HIVEX_OPEN_WRITE);
    if (!newHive) {
        QMessageBox::critical(this, "Create failed", "libhivex could not open the registry-hive template.");
        return;
    }

    const hive_node_h root = hivex_root(newHive);
    const hive_node_h objects = root ? hivex_node_add_child(newHive, root, "Objects") : 0;
    const hive_node_h bootManager = objects
        ? hivex_node_add_child(newHive, objects, "{9dea862c-5cdd-4e70-acc1-f32b344d4795}") : 0;
    const hive_node_h description = bootManager
        ? hivex_node_add_child(newHive, bootManager, "Description") : 0;
    const hive_node_h elements = bootManager
        ? hivex_node_add_child(newHive, bootManager, "Elements") : 0;
    if (!root || !objects || !bootManager || !description || !elements) {
        hivex_close(newHive);
        QMessageBox::critical(this, "Create failed", "Could not initialize the BCD store structure.");
        return;
    }

    QByteArray typeKey = QByteArrayLiteral("Type");
    QByteArray typeData(4, '\0');
    const quint32 bootManagerType = 0x10100002u;
    for (int index = 0; index < 4; ++index)
        typeData[index] = static_cast<char>((bootManagerType >> (index * 8)) & 0xff);
    hive_set_value typeValue = {typeKey.data(), hive_t_REG_DWORD,
                                static_cast<size_t>(typeData.size()), typeData.data()};

    const hive_node_h timeoutElement = hivex_node_add_child(newHive, elements, "25000004");
    QByteArray elementKey = QByteArrayLiteral("Element");
    QByteArray timeoutData(8, '\0');
    timeoutData[0] = 30;
    hive_set_value timeoutValue = {elementKey.data(), hive_t_REG_BINARY,
                                   static_cast<size_t>(timeoutData.size()), timeoutData.data()};
    if (!timeoutElement || hivex_node_set_value(newHive, description, &typeValue, 0) == -1 ||
        hivex_node_set_value(newHive, timeoutElement, &timeoutValue, 0) == -1) {
        hivex_close(newHive);
        QMessageBox::critical(this, "Create failed", "Could not initialize the BCD Boot Manager settings.");
        return;
    }

    const QByteArray encodedOutputPath = QFileInfo(outputPath).absoluteFilePath().toUtf8();
    if (hivex_commit(newHive, encodedOutputPath.constData(), 0) == -1) {
        hivex_close(newHive);
        QFile::remove(outputPath);
        QMessageBox::critical(this, "Create failed", "libhivex could not write the new BCD store.");
        return;
    }
    hivex_close(newHive);

    if (loadBcdFile(outputPath)) {
        statusLabel->setText(QStringLiteral("Created a new empty BCD store: %1. Add a boot entry before using it.")
                                 .arg(outputPath));
        updateActions();
    }
}

bool MainWindow::loadBcdFile(const QString &filePath) {
    hive_h *newHive = hivex_open(filePath.toUtf8().constData(), HIVEX_OPEN_WRITE);
    if (!newHive) {
        QMessageBox::critical(this, "Open failed",
                              "Could not open this file as a BCD/registry hive.");
        return false;
    }

    const hive_node_h root = hivex_root(newHive);
    if (!root) {
        hivex_close(newHive);
        QMessageBox::critical(this, "Open failed", "The hive has no root node.");
        return false;
    }

    if (hive)
        hivex_close(hive);
    hive = newHive;
    hivePath = QFileInfo(filePath).absoluteFilePath();
    setWindowTitle(QStringLiteral("BCDScribe - BCD Store Editor - %1").arg(hivePath));
    modified = false;
    refreshPartitionMounts();
    pathComboBox->setEditText(hivePath);
    const int oldIndex = pathComboBox->findText(hivePath);
    if (oldIndex >= 0)
        pathComboBox->removeItem(oldIndex);
    pathComboBox->insertItem(0, hivePath);
    while (pathComboBox->count() > 12)
        pathComboBox->removeItem(pathComboBox->count() - 1);
    QSettings(QStringLiteral("BCDScribe"), QStringLiteral("BCDScribe"))
        .setValue(QStringLiteral("recentBcdStores"), [&]() {
            QStringList paths;
            for (int index = 0; index < pathComboBox->count(); ++index)
                paths.append(pathComboBox->itemText(index));
            return paths;
        }());
    bootTree->clear();
    bootTable->setRowCount(0);
    populateBootObjects(root);
    modeTabs->setCurrentIndex(0);
    statusLabel->setText("Store loaded. Select a boot object to inspect its BCD settings.");
    updateActions();
    return true;
}

void MainWindow::refreshPartitionMounts() {
    partitionMounts.clear();

    QProcess process;
    process.start(QStringLiteral("lsblk"), {
        QStringLiteral("--json"),
        QStringLiteral("--fs"),
        QStringLiteral("--output"),
        QStringLiteral("PATH,FSTYPE,UUID,PARTUUID,MOUNTPOINTS,LABEL")
    });
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const auto toBcdGuidBytes = [](const QString &partUuid) {
        const QUuid uuid(partUuid);
        const QByteArray rfcBytes = uuid.toRfc4122();
        QByteArray bcdBytes;
        if (rfcBytes.size() != 16)
            return bcdBytes;

        bcdBytes.append(rfcBytes.at(3));
        bcdBytes.append(rfcBytes.at(2));
        bcdBytes.append(rfcBytes.at(1));
        bcdBytes.append(rfcBytes.at(0));
        bcdBytes.append(rfcBytes.at(5));
        bcdBytes.append(rfcBytes.at(4));
        bcdBytes.append(rfcBytes.at(7));
        bcdBytes.append(rfcBytes.at(6));
        bcdBytes.append(rfcBytes.mid(8));
        return bcdBytes;
    };

    std::function<void(const QJsonObject &)> addDeviceAndChildren;
    addDeviceAndChildren = [this, &addDeviceAndChildren, &toBcdGuidBytes](const QJsonObject &device) {
        const QString partUuid = device.value(QStringLiteral("partuuid")).toString();
        const QByteArray bcdGuidBytes = toBcdGuidBytes(partUuid);
        if (!bcdGuidBytes.isEmpty()) {
            QStringList mountpoints;
            const QJsonValue mountsValue = device.value(QStringLiteral("mountpoints"));
            if (mountsValue.isArray()) {
                for (const QJsonValue &mount : mountsValue.toArray()) {
                    const QString mountpoint = mount.toString();
                    if (!mountpoint.isEmpty() && mountpoint != QStringLiteral("[SWAP]"))
                        mountpoints.append(mountpoint);
                }
            } else if (mountsValue.isString() && !mountsValue.toString().isEmpty()) {
                mountpoints.append(mountsValue.toString());
            }

            const QString devicePath = device.value(QStringLiteral("path")).toString();
            const QString currentMount = mountpoints.join(QStringLiteral(", "));
            const QString location = currentMount.isEmpty()
                ? QStringLiteral("%1 (not mounted)").arg(devicePath)
                : QStringLiteral("%1 (%2)").arg(currentMount, devicePath);
            partitionMounts.insert(bcdGuidBytes, location);
        }

        const QJsonValue childrenValue = device.value(QStringLiteral("children"));
        if (childrenValue.isArray()) {
            for (const QJsonValue &child : childrenValue.toArray()) {
                if (child.isObject())
                    addDeviceAndChildren(child.toObject());
            }
        }
    };

    const QJsonArray devices = document.object().value(QStringLiteral("blockdevices")).toArray();
    for (const QJsonValue &device : devices) {
        if (device.isObject())
            addDeviceAndChildren(device.toObject());
    }
}

QString MainWindow::deviceValueText(hive_value_h value) const {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData)
        return QStringLiteral("(empty device descriptor)");

    const QByteArray bytes(rawData, static_cast<int>(length));
    free(rawData);
    for (auto mount = partitionMounts.cbegin(); mount != partitionMounts.cend(); ++mount) {
        if (!mount.key().isEmpty() && bytes.contains(mount.key()))
            return mount.value();
    }

    return QStringLiteral("No matching Linux partition (BCD device data: %1)")
        .arg(QString::fromLatin1(bytes.toHex(' ')));
}

hive_node_h MainWindow::findChildNode(hive_node_h parent, const QString &name) const {
    hive_node_h *children = hivex_node_children(hive, parent);
    if (!children)
        return 0;

    hive_node_h found = 0;
    for (hive_node_h *child = children; *child != 0; ++child) {
        char *rawName = hivex_node_name(hive, *child);
        const QString childName = rawName ? QString::fromUtf8(rawName) : QString();
        free(rawName);
        if (childName.compare(name, Qt::CaseInsensitive) == 0) {
            found = *child;
            break;
        }
    }
    free(children);
    return found;
}

void MainWindow::populateBootObjects(hive_node_h root) {
    const hive_node_h objects = findChildNode(root, QStringLiteral("Objects"));
    if (!objects) {
        statusLabel->setText("No BCD Objects key found in this store.");
        return;
    }

    hive_node_h *objectNodes = hivex_node_children(hive, objects);
    if (!objectNodes)
        return;

    auto *storeGroup = new QTreeWidgetItem(bootTree);
    storeGroup->setText(0, QStringLiteral("BCD Store"));

    QHash<QString, QTreeWidgetItem *> groups;
    groups.insert(QStringLiteral("BCD Store"), storeGroup);
    const QStringList sectionNames = {
        QStringLiteral("Application objects"),
        QStringLiteral("Windows resume objects"),
        QStringLiteral("Tools objects"),
        QStringLiteral("Inheritable objects"),
        QStringLiteral("Device objects")
    };
    for (const QString &sectionName : sectionNames) {
        auto *group = new QTreeWidgetItem(bootTree);
        group->setText(0, sectionName);
        groups.insert(sectionName, group);
    }

    const auto groupForType = [&groups, this](quint32 type) {
        QString groupName;
        if (type == 0x10100002u)
            groupName = QStringLiteral("BCD Store");
        else if (type == 0x10200003u)
            groupName = QStringLiteral("Application objects");
        else if (type == 0x10200004u)
            groupName = QStringLiteral("Windows resume objects");
        else if (type == 0x10200005u)
            groupName = QStringLiteral("Tools objects");
        else if ((type & 0xF0000000u) == 0x20000000u)
            groupName = QStringLiteral("Inheritable objects");
        else if ((type & 0xF0000000u) == 0x30000000u)
            groupName = QStringLiteral("Device objects");
        else
            groupName = QStringLiteral("Application objects");

        auto group = groups.value(groupName, nullptr);
        if (!group) {
            group = new QTreeWidgetItem(bootTree);
            group->setText(0, groupName);
            groups.insert(groupName, group);
        }
        return group;
    };

    int firmwareObjectsSkipped = 0;
    QTreeWidgetItem *firstEntry = nullptr;
    for (hive_node_h *object = objectNodes; *object != 0; ++object) {
        const hive_node_h objectDescription = findChildNode(*object, QStringLiteral("Description"));
        const hive_value_h objectTypeValue = objectDescription
            ? hivex_node_get_value(hive, objectDescription, "Type") : 0;
        quint32 objectType = 0;
        if (objectTypeValue) {
            hive_type valueType;
            size_t valueLength = 0;
            if (hivex_value_type(hive, objectTypeValue, &valueType, &valueLength) == 0 &&
                valueType == hive_t_REG_DWORD && valueLength >= sizeof(quint32))
                objectType = static_cast<quint32>(hivex_value_dword(hive, objectTypeValue));
            if (objectType == 0x10100001u || objectType == 0x101fffffu) {
                ++firmwareObjectsSkipped;
                continue;
            }
        }

        char *rawGuid = hivex_node_name(hive, *object);
        const QString guid = rawGuid ? QString::fromUtf8(rawGuid) : QStringLiteral("(unknown ID)");
        free(rawGuid);

        QString normalizedGuid = guid;
        normalizedGuid.remove(QLatin1Char('{'));
        normalizedGuid.remove(QLatin1Char('}'));
        QString label = normalizedGuid.compare(
            QStringLiteral("9dea862c-5cdd-4e70-acc1-f32b344d4795"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("Windows Boot Manager")
            : QStringLiteral("BCD object");

        const hive_node_h elements = findChildNode(*object, QStringLiteral("Elements"));
        const hive_node_h description = elements
            ? findChildNode(elements, QStringLiteral("12000004")) : 0;
        const hive_value_h descriptionValue = description
            ? hivex_node_get_value(hive, description, "Element") : 0;
        if (descriptionValue) {
            hive_type type;
            size_t length = 0;
            if (hivex_value_type(hive, descriptionValue, &type, &length) == 0 &&
                (type == hive_t_REG_SZ || type == hive_t_REG_EXPAND_SZ)) {
                char *text = hivex_value_string(hive, descriptionValue);
                if (text && *text)
                    label = QString::fromUtf8(text);
                free(text);
            }
        }

        if (normalizedGuid.compare(QStringLiteral("1afa9c49-16ab-4a5c-901b-212802da9460"),
                                   Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{resumeloadersettings}");
        else if (normalizedGuid.compare(QStringLiteral("6efb52bf-1766-41db-a6b3-0ee5eff72bd7"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{bootloadersettings}");
        else if (normalizedGuid.compare(QStringLiteral("7ff607e0-4395-11db-b0de-0800200c9a66"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{hypervisorsettings}");
        else if (normalizedGuid.compare(QStringLiteral("0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{emssettings}");
        else if (normalizedGuid.compare(QStringLiteral("4636856e-540f-4170-a130-a84776f4c654"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{dbgsettings}");
        else if (normalizedGuid.compare(QStringLiteral("5189b25c-5558-4bf2-bca4-289b11bd29e2"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{badmemory}");
        else if (normalizedGuid.compare(QStringLiteral("7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{globalsettings}");
        else if (normalizedGuid.compare(QStringLiteral("ae5534e0-a924-466c-b836-758539a3ee3a"),
                                        Qt::CaseInsensitive) == 0)
            label = QStringLiteral("{ramdiskoptions}");

        auto *item = new QTreeWidgetItem(groupForType(objectType));
        if (!firstEntry)
            firstEntry = item;
        item->setText(0, label);
        item->setToolTip(0, QStringLiteral("Boot entry ID: %1").arg(guid));
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(*object));
        item->setData(0, Qt::UserRole + 1, guid);
    }
    free(objectNodes);

    for (int index = 0; index < bootTree->topLevelItemCount(); ++index) {
        QTreeWidgetItem *group = bootTree->topLevelItem(index);
        group->setExpanded(true);
    }
    if (firstEntry)
        bootTree->setCurrentItem(firstEntry);
    else if (firmwareObjectsSkipped > 0)
        statusLabel->setText("No non-firmware boot entries found. UEFI firmware entries are hidden here.");
}

bool MainWindow::captureNode(hive_node_h node, HiveNodeSnapshot &snapshot) const {
    char *rawName = hivex_node_name(hive, node);
    if (!rawName)
        return false;
    snapshot.name = QString::fromUtf8(rawName);
    free(rawName);

    hive_value_h *values = hivex_node_values(hive, node);
    if (values) {
        for (hive_value_h *value = values; *value != 0; ++value) {
            HiveValueSnapshot valueSnapshot;
            char *rawKey = hivex_value_key(hive, *value);
            valueSnapshot.name = rawKey ? QString::fromUtf8(rawKey) : QString();
            free(rawKey);

            size_t length = 0;
            char *rawData = hivex_value_value(hive, *value, &valueSnapshot.type, &length);
            if (!rawData) {
                free(values);
                return false;
            }
            valueSnapshot.data = QByteArray(rawData, static_cast<int>(length));
            free(rawData);
            snapshot.values.append(valueSnapshot);
        }
        free(values);
    }

    hive_node_h *children = hivex_node_children(hive, node);
    if (children) {
        for (hive_node_h *child = children; *child != 0; ++child) {
            HiveNodeSnapshot childSnapshot;
            if (!captureNode(*child, childSnapshot)) {
                free(children);
                return false;
            }
            snapshot.children.append(childSnapshot);
        }
        free(children);
    }
    return true;
}

hive_node_h MainWindow::cloneNode(const HiveNodeSnapshot &snapshot, hive_node_h parent,
                                  const QString &overrideName) {
    const QString nodeName = overrideName.isEmpty() ? snapshot.name : overrideName;
    const QByteArray encodedName = nodeName.toUtf8();
    const hive_node_h node = hivex_node_add_child(hive, parent, encodedName.constData());
    if (!node)
        return 0;

    for (const HiveValueSnapshot &value : snapshot.values) {
        QByteArray key = value.name.toUtf8();
        hive_set_value setValue = {
            key.data(),
            value.type,
            static_cast<size_t>(value.data.size()),
            const_cast<char *>(value.data.constData())
        };
        if (hivex_node_set_value(hive, node, &setValue, 0) == -1) {
            hivex_node_delete_child(hive, node);
            return 0;
        }
    }

    for (const HiveNodeSnapshot &child : snapshot.children) {
        if (!cloneNode(child, node)) {
            hivex_node_delete_child(hive, node);
            return 0;
        }
    }
    return node;
}

void MainWindow::refreshBootTree(const QString &selectGuid) {
    bootTree->clear();
    bootTable->setRowCount(0);
    populateBootObjects(hivex_root(hive));
    if (!selectGuid.isEmpty()) {
        for (int groupIndex = 0; groupIndex < bootTree->topLevelItemCount(); ++groupIndex) {
            QTreeWidgetItem *group = bootTree->topLevelItem(groupIndex);
            for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
                QTreeWidgetItem *entry = group->child(childIndex);
                if (entry->data(0, Qt::UserRole + 1).toString() == selectGuid) {
                    bootTree->setCurrentItem(entry);
                    return;
                }
            }
        }
    }
}

void MainWindow::showBootTreeContextMenu(const QPoint &position) {
    QTreeWidgetItem *item = bootTree->itemAt(position);
    if (item)
        bootTree->setCurrentItem(item);

    QMenu menu(this);
    const bool isObject = item && item->data(0, Qt::UserRole).toULongLong() != 0;
    const QString groupName = item && item->parent() ? item->parent()->text(0)
                                                      : (item ? item->text(0) : QString());
    QAction *createAction = nullptr;
    if (!item || groupName == QStringLiteral("Application objects"))
        createAction = menu.addAction("Create Windows boot entry...");

    QAction *copyIdAction = nullptr;
    QAction *copyEntryAction = nullptr;
    QAction *cutEntryAction = nullptr;
    QAction *deleteEntryAction = nullptr;
    if (isObject) {
        if (!menu.isEmpty())
            menu.addSeparator();
        copyIdAction = menu.addAction("Copy identifier");
        copyEntryAction = menu.addAction("Copy entry");
        cutEntryAction = menu.addAction("Cut entry");
        menu.addSeparator();
        deleteEntryAction = menu.addAction("Delete entry...");
    }

    QAction *pasteEntryAction = nullptr;
    if (hasObjectClipboard) {
        if (!menu.isEmpty())
            menu.addSeparator();
        pasteEntryAction = menu.addAction("Paste entry");
    }
    if (menu.isEmpty())
        return;

    QAction *chosen = menu.exec(bootTree->viewport()->mapToGlobal(position));
    if (chosen == createAction)
        createBootEntry();
    else if (chosen == copyIdAction && item)
        QGuiApplication::clipboard()->setText(item->data(0, Qt::UserRole + 1).toString());
    else if (chosen == copyEntryAction)
        copySelectedBootEntry();
    else if (chosen == cutEntryAction)
        cutSelectedBootEntry();
    else if (chosen == deleteEntryAction)
        deleteSelectedBootEntry();
    else if (chosen == pasteEntryAction)
        pasteBootEntry();
}

void MainWindow::showBootTableContextMenu(const QPoint &position) {
    const QModelIndex index = bootTable->indexAt(position);
    if (index.isValid())
        bootTable->setCurrentCell(index.row(), index.column());

    QMenu menu(this);
    QAction *newFieldAction = menu.addAction("New field...");
    QAction *copyFieldAction = nullptr;
    QAction *copyValueAction = nullptr;
    const bool isValueRow = index.isValid() && bootTable->item(index.row(), 0) &&
        bootTable->item(index.row(), 0)->data(Qt::UserRole).toULongLong() != 0;
    if (index.isValid()) {
        copyFieldAction = menu.addAction("Copy field name");
        copyValueAction = menu.addAction("Copy value");
    }
    QAction *editAction = nullptr;
    QAction *copyElementAction = nullptr;
    QAction *cutElementAction = nullptr;
    QAction *deleteElementAction = nullptr;
    QAction *pasteElementAction = nullptr;
    if (isValueRow) {
        menu.addSeparator();
        editAction = menu.addAction("Edit...");
        copyElementAction = menu.addAction("Copy element");
        cutElementAction = menu.addAction("Cut element");
        deleteElementAction = menu.addAction("Delete element...");
    }
    if (hasElementClipboard) {
        menu.addSeparator();
        pasteElementAction = menu.addAction("Paste element");
    }

    QAction *chosen = menu.exec(bootTable->viewport()->mapToGlobal(position));
    if (chosen == newFieldAction)
        createNewField();
    else if (copyFieldAction && chosen == copyFieldAction)
        QGuiApplication::clipboard()->setText(bootTable->item(index.row(), 0)->text());
    else if (copyValueAction && chosen == copyValueAction)
        QGuiApplication::clipboard()->setText(bootTable->item(index.row(), 2)->text());
    else if (editAction && chosen == editAction)
        editSelectedValue();
    else if (copyElementAction && chosen == copyElementAction) {
        const hive_node_h element = static_cast<hive_node_h>(
            bootTable->item(index.row(), 0)->data(Qt::UserRole + 1).toULongLong());
        hasElementClipboard = element && captureNode(element, elementClipboard);
    } else if (cutElementAction && chosen == cutElementAction)
        cutSelectedElement();
    else if (deleteElementAction && chosen == deleteElementAction)
        deleteSelectedElement();
    else if (pasteElementAction && chosen == pasteElementAction)
        pasteElement();
}

void MainWindow::createBootEntry() {
    if (!hive)
        return;

    bool accepted = false;
    const QString description = QInputDialog::getText(
        this, "Create Windows boot entry", "Description:", QLineEdit::Normal,
        QStringLiteral("New Windows entry"), &accepted).trimmed();
    if (!accepted || description.isEmpty())
        return;

    const hive_node_h objects = findChildNode(hivex_root(hive), QStringLiteral("Objects"));
    if (!objects) {
        QMessageBox::critical(this, "Create failed", "The BCD store has no Objects key.");
        return;
    }

    const QString guid = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    const QByteArray objectName = QStringLiteral("{%1}").arg(guid).toUtf8();
    const hive_node_h object = hivex_node_add_child(hive, objects, objectName.constData());
    if (!object) {
        QMessageBox::critical(this, "Create failed", "Could not create the BCD object.");
        return;
    }

    const hive_node_h objectDescription = hivex_node_add_child(hive, object, "Description");
    const hive_node_h elements = hivex_node_add_child(hive, object, "Elements");
    const hive_node_h descriptionElement = elements
        ? hivex_node_add_child(hive, elements, "12000004") : 0;
    if (!objectDescription || !elements || !descriptionElement) {
        hivex_node_delete_child(hive, object);
        QMessageBox::critical(this, "Create failed", "Could not initialize the BCD object structure.");
        return;
    }

    QByteArray typeData;
    const quint32 objectType = 0x10200003u;
    for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
        typeData.append(static_cast<char>((objectType >> (byteIndex * 8)) & 0xff));
    QByteArray typeName = QByteArrayLiteral("Type");
    hive_set_value typeValue = {typeName.data(), hive_t_REG_DWORD,
                                static_cast<size_t>(typeData.size()), typeData.data()};

    QByteArray textData;
    for (const QChar character : description) {
        const quint16 codeUnit = character.unicode();
        textData.append(static_cast<char>(codeUnit & 0xff));
        textData.append(static_cast<char>((codeUnit >> 8) & 0xff));
    }
    textData.append('\0');
    textData.append('\0');
    QByteArray elementName = QByteArrayLiteral("Element");
    hive_set_value descriptionValue = {elementName.data(), hive_t_REG_SZ,
                                       static_cast<size_t>(textData.size()), textData.data()};
    if (hivex_node_set_value(hive, objectDescription, &typeValue, 0) == -1 ||
        hivex_node_set_value(hive, descriptionElement, &descriptionValue, 0) == -1) {
        hivex_node_delete_child(hive, object);
        QMessageBox::critical(this, "Create failed", "Could not write the BCD object metadata.");
        return;
    }

    modified = true;
    refreshBootTree(guid);
    statusLabel->setText("Created a new OS loader object. Add its device and path settings before using it.");
    updateActions();
}

void MainWindow::copySelectedBootEntry() {
    QTreeWidgetItem *item = bootTree->currentItem();
    if (!item)
        return;
    const hive_node_h object = static_cast<hive_node_h>(item->data(0, Qt::UserRole).toULongLong());
    hasObjectClipboard = object && captureNode(object, objectClipboard);
}

void MainWindow::cutSelectedBootEntry() {
    copySelectedBootEntry();
    if (hasObjectClipboard)
        deleteSelectedBootEntry();
}

void MainWindow::deleteSelectedBootEntry() {
    QTreeWidgetItem *item = bootTree->currentItem();
    if (!item)
        return;
    const hive_node_h object = static_cast<hive_node_h>(item->data(0, Qt::UserRole).toULongLong());
    const QString guid = item->data(0, Qt::UserRole + 1).toString();
    if (!object || guid.compare(QStringLiteral("{9dea862c-5cdd-4e70-acc1-f32b344d4795}"),
                                 Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, "Cannot delete entry", "The Windows Boot Manager object is protected.");
        return;
    }
    if (QMessageBox::warning(this, "Delete boot entry",
                             QStringLiteral("Delete '%1' (%2) from the in-memory BCD store?")
                                 .arg(item->text(0), guid),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (hivex_node_delete_child(hive, object) == -1) {
        QMessageBox::critical(this, "Delete failed", "libhivex could not delete the selected BCD object.");
        return;
    }
    modified = true;
    refreshBootTree();
    updateActions();
}

void MainWindow::pasteBootEntry() {
    if (!hive || !hasObjectClipboard)
        return;
    const hive_node_h objects = findChildNode(hivex_root(hive), QStringLiteral("Objects"));
    if (!objects)
        return;
    const QString guid = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    const hive_node_h newObject = cloneNode(objectClipboard, objects,
                                             QStringLiteral("{%1}").arg(guid));
    if (!newObject) {
        QMessageBox::critical(this, "Paste failed", "Could not copy the BCD object into this store.");
        return;
    }
    modified = true;
    refreshBootTree(guid);
    updateActions();
}

void MainWindow::cutSelectedElement() {
    const int row = bootTable->currentRow();
    if (row < 0 || !bootTable->item(row, 0))
        return;
    const hive_node_h element = static_cast<hive_node_h>(
        bootTable->item(row, 0)->data(Qt::UserRole + 1).toULongLong());
    if (!element || !captureNode(element, elementClipboard))
        return;
    hasElementClipboard = true;
    deleteSelectedElement();
}

void MainWindow::deleteSelectedElement() {
    const int row = bootTable->currentRow();
    if (row < 0 || !bootTable->item(row, 0))
        return;
    const hive_node_h element = static_cast<hive_node_h>(
        bootTable->item(row, 0)->data(Qt::UserRole + 1).toULongLong());
    if (!element)
        return;
    if (QMessageBox::warning(this, "Delete BCD element",
                             QStringLiteral("Delete '%1' from this object in the in-memory BCD store?")
                                 .arg(bootTable->item(row, 0)->text()),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (hivex_node_delete_child(hive, element) == -1) {
        QMessageBox::critical(this, "Delete failed", "libhivex could not delete the selected element.");
        return;
    }
    modified = true;
    showBootObjectValues(bootTree->currentItem(), nullptr);
}

void MainWindow::pasteElement() {
    if (!hive || !hasElementClipboard)
        return;
    const int row = bootTable->currentRow();
    if (row < 0 || !bootTable->item(row, 0))
        return;
    QTreeWidgetItem *bootObject = bootTree->currentItem();
    const hive_node_h object = bootObject
        ? static_cast<hive_node_h>(bootObject->data(0, Qt::UserRole).toULongLong()) : 0;
    const hive_node_h parent = object
        ? findChildNode(object, QStringLiteral("Elements")) : 0;
    if (!parent)
        return;
    const QByteArray elementName = elementClipboard.name.toUtf8();
    const hive_node_h existing = hivex_node_get_child(hive, parent, elementName.constData());
    if (existing) {
        if (QMessageBox::question(this, "Replace BCD element",
                                  QStringLiteral("Replace the existing element '%1'?")
                                      .arg(elementClipboard.name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (hivex_node_delete_child(hive, existing) == -1)
            return;
    }
    if (!cloneNode(elementClipboard, parent)) {
        QMessageBox::critical(this, "Paste failed", "Could not paste this BCD element.");
        return;
    }
    modified = true;
    showBootObjectValues(bootTree->currentItem(), nullptr);
}

void MainWindow::createNewField() {
    if (!hive)
        return;

    QTreeWidgetItem *bootObject = bootTree->currentItem();
    const hive_node_h object = bootObject
        ? static_cast<hive_node_h>(bootObject->data(0, Qt::UserRole).toULongLong()) : 0;
    const hive_node_h elements = object
        ? findChildNode(object, QStringLiteral("Elements")) : 0;
    if (!elements) {
        QMessageBox::warning(this, "Cannot create field",
                             "Select a boot object with an Elements section first.");
        return;
    }

    struct FieldChoice {
        QString label;
        QString id;
    };
    const QList<FieldChoice> fields = {
        {QStringLiteral("Application path"), QStringLiteral("12000002")},
        {QStringLiteral("Description"), QStringLiteral("12000004")},
        {QStringLiteral("Locale"), QStringLiteral("12000005")},
        {QStringLiteral("Default boot entry"), QStringLiteral("23000003")},
        {QStringLiteral("Inherit"), QStringLiteral("14000006")},
        {QStringLiteral("Recovery sequence"), QStringLiteral("14000008")},
        {QStringLiteral("Display message override"), QStringLiteral("15000066")},
        {QStringLiteral("Boot debug"), QStringLiteral("16000010")},
        {QStringLiteral("Kernel debug"), QStringLiteral("16000011")},
        {QStringLiteral("Emergency Management Services"), QStringLiteral("16000020")},
        {QStringLiteral("VGA mode"), QStringLiteral("16000040")},
        {QStringLiteral("Boot log"), QStringLiteral("16000041")},
        {QStringLiteral("Advanced options"), QStringLiteral("16000042")},
        {QStringLiteral("Options edit"), QStringLiteral("16000043")},
        {QStringLiteral("FVE boot"), QStringLiteral("16000044")},
        {QStringLiteral("Legacy APIC mode"), QStringLiteral("16000045")},
        {QStringLiteral("Console extended input"), QStringLiteral("16000050")},
        {QStringLiteral("Recovery enabled"), QStringLiteral("16000009")},
        {QStringLiteral("No integrity checks"), QStringLiteral("16000048")},
        {QStringLiteral("Test signing"), QStringLiteral("16000049")},
        {QStringLiteral("Isolated execution context"), QStringLiteral("16000060")},
        {QStringLiteral("Allowed in-memory settings"), QStringLiteral("17000077")},
        {QStringLiteral("NX policy"), QStringLiteral("25000020")},
        {QStringLiteral("Boot menu timeout"), QStringLiteral("25000004")},
        {QStringLiteral("Boot menu policy"), QStringLiteral("25000008")},
        {QStringLiteral("Hypervisor launch type"), QStringLiteral("250000F0")},
        {QStringLiteral("Load options string"), QStringLiteral("12000030")},
        {QStringLiteral("Debugger ignore user-mode exceptions"), QStringLiteral("16000017")},
        {QStringLiteral("Debugger start policy"), QStringLiteral("15000018")},
        {QStringLiteral("USB debugger target name"), QStringLiteral("12000016")},
        {QStringLiteral("Debugger bus parameters"), QStringLiteral("12000019")},
        {QStringLiteral("Network debugger host IP"), QStringLiteral("1500001A")},
        {QStringLiteral("Network debugger port"), QStringLiteral("1500001B")},
        {QStringLiteral("Network debugger DHCP"), QStringLiteral("1600001C")},
        {QStringLiteral("Network debugger key"), QStringLiteral("1200001D")},
        {QStringLiteral("EMS port"), QStringLiteral("15000022")},
        {QStringLiteral("EMS baud rate"), QStringLiteral("15000023")},
        {QStringLiteral("Graphics mode disabled"), QStringLiteral("16000046")},
        {QStringLiteral("Graphics resolution"), QStringLiteral("15000052")},
        {QStringLiteral("Restart on failure"), QStringLiteral("16000053")},
        {QStringLiteral("Boot UX disabled"), QStringLiteral("1600006C")},
        {QStringLiteral("Boot shutdown disabled"), QStringLiteral("16000074")},
        {QStringLiteral("Force FIPS crypto"), QStringLiteral("16000079")},
        {QStringLiteral("Flight signing"), QStringLiteral("1600007E")},
        {QStringLiteral("Detect kernel and HAL"), QStringLiteral("26000010")},
        {QStringLiteral("Kernel path"), QStringLiteral("22000011")},
        {QStringLiteral("HAL path"), QStringLiteral("22000012")},
        {QStringLiteral("Debugger transport path"), QStringLiteral("22000013")},
        {QStringLiteral("PAE policy"), QStringLiteral("25000021")},
        {QStringLiteral("Disable crash auto-reboot"), QStringLiteral("26000024")},
        {QStringLiteral("Use last known good settings"), QStringLiteral("26000025")},
        {QStringLiteral("Allow prerelease signatures"), QStringLiteral("26000027")},
        {QStringLiteral("No low memory"), QStringLiteral("26000030")},
        {QStringLiteral("Remove memory"), QStringLiteral("25000031")},
        {QStringLiteral("Increase user virtual address"), QStringLiteral("25000032")},
        {QStringLiteral("Use VGA driver"), QStringLiteral("26000040")},
        {QStringLiteral("Disable boot display"), QStringLiteral("26000041")},
        {QStringLiteral("Disable VESA BIOS"), QStringLiteral("26000042")},
        {QStringLiteral("Disable VGA mode"), QStringLiteral("26000043")},
        {QStringLiteral("Use physical destination"), QStringLiteral("26000051")},
        {QStringLiteral("Restrict APIC cluster"), QStringLiteral("25000052")},
        {QStringLiteral("Use legacy APIC mode"), QStringLiteral("26000054")},
        {QStringLiteral("X2APIC policy"), QStringLiteral("25000055")},
        {QStringLiteral("Use boot processor only"), QStringLiteral("26000060")},
        {QStringLiteral("Number of processors"), QStringLiteral("25000061")},
        {QStringLiteral("Force maximum processors"), QStringLiteral("26000062")},
        {QStringLiteral("Processor configuration flags"), QStringLiteral("25000063")},
        {QStringLiteral("Maximize processor groups"), QStringLiteral("26000064")},
        {QStringLiteral("Force group awareness"), QStringLiteral("26000065")},
        {QStringLiteral("Processor group size"), QStringLiteral("25000066")},
        {QStringLiteral("Use firmware PCI settings"), QStringLiteral("26000070")},
        {QStringLiteral("MSI policy"), QStringLiteral("25000071")},
        {QStringLiteral("Safe boot"), QStringLiteral("25000080")},
        {QStringLiteral("Safe boot alternate shell"), QStringLiteral("26000081")},
        {QStringLiteral("Boot log initialization"), QStringLiteral("26000090")},
        {QStringLiteral("Verbose object load mode"), QStringLiteral("26000091")},
        {QStringLiteral("Kernel debugger enabled"), QStringLiteral("260000A0")},
        {QStringLiteral("Debugger HAL breakpoint"), QStringLiteral("260000A1")},
        {QStringLiteral("Use platform clock"), QStringLiteral("260000A2")},
        {QStringLiteral("Force legacy platform"), QStringLiteral("260000A3")},
        {QStringLiteral("Disable dynamic tick"), QStringLiteral("260000A5")},
        {QStringLiteral("TSC synchronization policy"), QStringLiteral("250000A6")},
        {QStringLiteral("EMS enabled"), QStringLiteral("260000B0")},
        {QStringLiteral("Driver load failure policy"), QStringLiteral("250000C1")},
        {QStringLiteral("Boot status policy"), QStringLiteral("250000E0")},
        {QStringLiteral("Disable ELAM drivers"), QStringLiteral("260000E1")},
        {QStringLiteral("Hypervisor debugger enabled"), QStringLiteral("260000F2")},
        {QStringLiteral("Hypervisor debugger type"), QStringLiteral("250000F3")},
        {QStringLiteral("Hypervisor debugger port"), QStringLiteral("250000F4")},
        {QStringLiteral("Hypervisor debugger baud rate"), QStringLiteral("250000F5")},
        {QStringLiteral("Hypervisor debugger channel"), QStringLiteral("250000F6")},
        {QStringLiteral("Boot UX policy"), QStringLiteral("250000F7")},
        {QStringLiteral("Hypervisor processor count"), QStringLiteral("250000FA")},
        {QStringLiteral("Hypervisor root processors per node"), QStringLiteral("250000FB")},
        {QStringLiteral("Hypervisor use large VT-TLB"), QStringLiteral("260000FC")},
        {QStringLiteral("TPM boot entropy policy"), QStringLiteral("25000100")},
        {QStringLiteral("Hypervisor IOMMU policy"), QStringLiteral("25000115")},
        {QStringLiteral("Attempt resume"), QStringLiteral("26000005")},
        {QStringLiteral("Display boot menu"), QStringLiteral("26000020")},
        {QStringLiteral("Suppress error display"), QStringLiteral("26000021")},
        {QStringLiteral("BCD device"), QStringLiteral("21000022")},
        {QStringLiteral("BCD file path"), QStringLiteral("22000023")},
        {QStringLiteral("Process custom actions first"), QStringLiteral("26000028")},
        {QStringLiteral("Custom actions list"), QStringLiteral("27000030")},
        {QStringLiteral("Persist boot sequence"), QStringLiteral("26000031")},
        {QStringLiteral("Truncate physical memory"), QStringLiteral("15000007")},
        {QStringLiteral("Bad memory list"), QStringLiteral("1700000A")},
        {QStringLiteral("Allow bad memory access"), QStringLiteral("1600000B")},
        {QStringLiteral("First megabyte policy"), QStringLiteral("1500000C")},
        {QStringLiteral("Relocate physical memory"), QStringLiteral("1500000D")},
        {QStringLiteral("Avoid low physical memory"), QStringLiteral("1500000E")},
        {QStringLiteral("Serial debugger port address"), QStringLiteral("15000012")},
        {QStringLiteral("Serial debugger port"), QStringLiteral("15000013")},
        {QStringLiteral("Serial debugger baud rate"), QStringLiteral("15000014")},
        {QStringLiteral("1394 debugger channel"), QStringLiteral("15000015")},
        {QStringLiteral("BSD log device"), QStringLiteral("11000043")},
        {QStringLiteral("BSD log path"), QStringLiteral("12000044")},
        {QStringLiteral("Configuration access policy"), QStringLiteral("15000047")},
        {QStringLiteral("Font path"), QStringLiteral("1200004A")},
        {QStringLiteral("SI policy"), QStringLiteral("1500004B")},
        {QStringLiteral("FVE band ID"), QStringLiteral("1500004C")},
        {QStringLiteral("Graphics force highest mode"), QStringLiteral("16000054")},
        {QStringLiteral("Hypervisor debugger bus parameters"), QStringLiteral("220000F9")},
        {QStringLiteral("Hypervisor network debugger host IP"), QStringLiteral("250000FD")},
        {QStringLiteral("Hypervisor network debugger port"), QStringLiteral("250000FE")},
        {QStringLiteral("Hypervisor network debugger key"), QStringLiteral("22000110")},
        {QStringLiteral("Hypervisor network debugger DHCP"), QStringLiteral("26000114")},
        {QStringLiteral("Disable XSAVE"), QStringLiteral("2500012B")}
    };
    QStringList fieldLabels;
    QHash<QString, QString> idByField;
    for (const FieldChoice &field : fields) {
        fieldLabels.append(field.label);
        idByField.insert(field.label, field.id);
    }

    bool accepted = false;
    const QString fieldLabel = QInputDialog::getItem(
        this, "Create new BCD field", "Field:", fieldLabels, 0, false, &accepted);
    if (!accepted)
        return;
    const QString elementId = idByField.value(fieldLabel);
    const QByteArray elementName = elementId.toUtf8();
    if (hivex_node_get_child(hive, elements, elementName.constData())) {
        QMessageBox::warning(this, "Field already exists",
                             QStringLiteral("The field '%1' already exists on this boot object.")
                                 .arg(fieldLabel));
        return;
    }

    QByteArray data;
    hive_type dataType = hive_t_REG_BINARY;
    const bool isBoolean = is_bcd_boolean_element(elementId);
    const bool isReference = elementId == QStringLiteral("14000006") ||
        elementId == QStringLiteral("14000008") || elementId == QStringLiteral("23000003");
    const bool isString = elementId.startsWith(QStringLiteral("12"));
    const bool isNumeric = elementId.startsWith(QStringLiteral("15")) ||
        elementId.startsWith(QStringLiteral("17")) ||
        elementId.startsWith(QStringLiteral("25"));

    if (isBoolean) {
        const QStringList options = {QStringLiteral("No"), QStringLiteral("Yes")};
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Value:",
            options, 0, false, &accepted);
        if (!accepted)
            return;
        data.append(selected == QStringLiteral("Yes") ? '\x01' : '\x00');
    } else if (isReference) {
        const QString targetGroup = elementId == QStringLiteral("14000006")
            ? QStringLiteral("Inheritable objects") : QStringLiteral("Application objects");
        QStringList choices;
        QHash<QString, QString> referenceByChoice;
        QTreeWidgetItem *group = nullptr;
        for (int index = 0; index < bootTree->topLevelItemCount(); ++index) {
            QTreeWidgetItem *candidate = bootTree->topLevelItem(index);
            if (candidate->text(0) == targetGroup) {
                group = candidate;
                break;
            }
        }
        if (group) {
            for (int index = 0; index < group->childCount(); ++index) {
                QTreeWidgetItem *item = group->child(index);
                if (elementId == QStringLiteral("14000008") &&
                    !item->text(0).contains(QStringLiteral("recovery"), Qt::CaseInsensitive))
                    continue;
                const QString guid = item->data(0, Qt::UserRole + 1).toString();
                const QString choice = item->text(0);
                if (!guid.isEmpty()) {
                    choices.append(choice);
                    referenceByChoice.insert(choice, guid);
                }
            }
        }
        if (choices.isEmpty() && elementId == QStringLiteral("14000008") && group) {
            for (int index = 0; index < group->childCount(); ++index) {
                QTreeWidgetItem *item = group->child(index);
                const QString guid = item->data(0, Qt::UserRole + 1).toString();
                if (!guid.isEmpty()) {
                    choices.append(item->text(0));
                    referenceByChoice.insert(item->text(0), guid);
                }
            }
        }
        if (choices.isEmpty()) {
            QMessageBox::warning(this, "No matching BCD objects",
                                 elementId == QStringLiteral("14000006")
                                     ? "This store has no inheritable objects to choose from."
                                     : elementId == QStringLiteral("14000008")
                                         ? "This store has no recovery entries to choose from."
                                         : "This store has no application entries to choose from.");
            return;
        }
        choices.sort(Qt::CaseInsensitive);
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Object:",
            choices, 0, false, &accepted);
        if (!accepted)
            return;
        const QString reference = referenceByChoice.value(selected);
        for (const QChar character : reference) {
            const quint16 codeUnit = character.unicode();
            data.append(static_cast<char>(codeUnit & 0xff));
            data.append(static_cast<char>((codeUnit >> 8) & 0xff));
        }
        data.append('\0');
        data.append('\0');
    } else if (isString) {
        const QString value = QInputDialog::getText(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Value:",
            QLineEdit::Normal, QString(), &accepted);
        if (!accepted)
            return;
        dataType = hive_t_REG_SZ;
        for (const QChar character : value) {
            const quint16 codeUnit = character.unicode();
            data.append(static_cast<char>(codeUnit & 0xff));
            data.append(static_cast<char>((codeUnit >> 8) & 0xff));
        }
        data.append('\0');
        data.append('\0');
    } else if (elementId == QStringLiteral("15000066")) {
        const QStringList options = {
            QStringLiteral("None"), QStringLiteral("Recovery"),
            QStringLiteral("Startup Repair"), QStringLiteral("Recovery and Startup Repair")
        };
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Value:",
            options, 0, false, &accepted);
        if (!accepted)
            return;
        data = QByteArray(8, '\0');
        data[0] = static_cast<char>(options.indexOf(selected));
    } else if (elementId == QStringLiteral("25000020") ||
               elementId == QStringLiteral("25000008") ||
               elementId == QStringLiteral("250000F0")) {
        QStringList options;
        if (elementId == QStringLiteral("25000020"))
            options = {"OptIn", "OptOut", "AlwaysOn", "AlwaysOff"};
        else if (elementId == QStringLiteral("25000008"))
            options = {"Legacy", "Standard"};
        else
            options = {"Off", "Auto"};
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Value:",
            options, 0, false, &accepted);
        if (!accepted)
            return;
        data = QByteArray(8, '\0');
        data[0] = static_cast<char>(options.indexOf(selected));
    } else if (isNumeric) {
        const int number = QInputDialog::getInt(
            this, QStringLiteral("Create %1").arg(fieldLabel), "Decimal value:",
            0, 0, INT_MAX, 1, &accepted);
        if (!accepted)
            return;
        data = QByteArray(8, '\0');
        for (int index = 0; index < 4; ++index)
            data[index] = static_cast<char>((number >> (index * 8)) & 0xff);
    } else {
        QMessageBox::warning(this, "Unsupported field", "This field type is not available in the creation dialog.");
        return;
    }

    const hive_node_h element = hivex_node_add_child(hive, elements, elementName.constData());
    if (!element) {
        QMessageBox::critical(this, "Create failed", "Could not create the BCD field.");
        return;
    }
    QByteArray valueName = QByteArrayLiteral("Element");
    hive_set_value value = {valueName.data(), dataType,
                            static_cast<size_t>(data.size()), data.data()};
    if (hivex_node_set_value(hive, element, &value, 0) == -1) {
        hivex_node_delete_child(hive, element);
        QMessageBox::critical(this, "Create failed", "Could not write the new BCD field.");
        return;
    }
    modified = true;
    showBootObjectValues(bootObject, nullptr);
    statusLabel->setText(QStringLiteral("Created '%1'. Unsaved changes.").arg(fieldLabel));
    updateActions();
}

void MainWindow::showBootObjectValues(QTreeWidgetItem *current, QTreeWidgetItem *) {
    bootTable->setRowCount(0);
    if (!hive || !current)
        return;

    const hive_node_h object = static_cast<hive_node_h>(
        current->data(0, Qt::UserRole).toULongLong());
    if (object == 0) {
        statusLabel->setText(current->text(0));
        updateActions();
        return;
    }
    const hive_node_h objectDescription = findChildNode(object, QStringLiteral("Description"));
    const hive_value_h objectTypeValue = objectDescription
        ? hivex_node_get_value(hive, objectDescription, "Type") : 0;
    const quint32 objectType = objectTypeValue
        ? static_cast<quint32>(hivex_value_dword(hive, objectTypeValue)) : 0;
    const QString guid = current->data(0, Qt::UserRole + 1).toString();
    bootTable->insertRow(0);
    auto *identifierItem = new QTableWidgetItem("Identifier");
    identifierItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(0));
    identifierItem->setData(Qt::UserRole + 1, QVariant::fromValue<qulonglong>(object));
    identifierItem->setToolTip("Boot entry identifier for this object");
    bootTable->setItem(0, 0, identifierItem);
    bootTable->setItem(0, 1, new QTableWidgetItem("identifier"));
    bootTable->setItem(0, 2, new QTableWidgetItem(guid));

    const hive_node_h elements = findChildNode(object, QStringLiteral("Elements"));
    if (!elements) {
        statusLabel->setText("This object has no Elements key.");
        updateActions();
        return;
    }

    hive_node_h *elementNodes = hivex_node_children(hive, elements);
    if (!elementNodes)
        return;
    for (hive_node_h *element = elementNodes; *element != 0; ++element) {
        char *rawId = hivex_node_name(hive, *element);
        const QString id = rawId ? QString::fromUtf8(rawId) : QStringLiteral("?");
        free(rawId);

        hive_value_h *values = hivex_node_values(hive, *element);
        if (!values)
            continue;
        for (hive_value_h *value = values; *value != 0; ++value) {
            char *rawName = hivex_value_key(hive, *value);
            const QString valueName = rawName ? QString::fromUtf8(rawName) : QStringLiteral("(default)");
            free(rawName);

            const int row = bootTable->rowCount();
            bootTable->insertRow(row);
            const QString label = bcd_element_name(id, objectType);
            const QString option = bcdedit_option_name(id, objectType);
            auto *nameItem = new QTableWidgetItem(label);
            nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(*value));
            nameItem->setData(Qt::UserRole + 1, QVariant::fromValue<qulonglong>(*element));
            const QString setting = option.isEmpty()
                ? QStringLiteral("Other BCD setting")
                : QStringLiteral("/%1").arg(option);
            nameItem->setToolTip(QStringLiteral("%1\nBCD element ID: %2\nHive value: %3")
                .arg(option.isEmpty() ? QStringLiteral("No Windows setting mapping")
                                      : QStringLiteral("Windows setting: %1").arg(setting),
                     id, valueName));
            bootTable->setItem(row, 0, nameItem);
            auto *settingItem = new QTableWidgetItem(setting);
            settingItem->setToolTip(QStringLiteral("Windows setting %1\nBCD element ID: %2")
                .arg(setting, id));
            bootTable->setItem(row, 1, settingItem);
            QString storedData = id.startsWith(QStringLiteral("11")) ||
                id.startsWith(QStringLiteral("21"))
                ? deviceValueText(*value)
                : is_bcd_boolean_element(id)
                    ? bcd_boolean_value_text(hive, *value)
                    : (option == QStringLiteral("bootmenupolicy") ||
                       option == QStringLiteral("nx") ||
                       option == QStringLiteral("hypervisorlaunchtype"))
                        ? bcd_policy_value_text(hive, *value,
                            option == QStringLiteral("bootmenupolicy")
                                ? QStringLiteral("25000008")
                                : option == QStringLiteral("nx")
                                    ? QStringLiteral("25000020")
                                    : QStringLiteral("250000F0"))
                    : id == QStringLiteral("15000066")
                        ? bcd_display_message_value_text(hive, *value)
                    : id == QStringLiteral("17000077")
                        ? bcd_numeric_value_text(hive, *value)
                    : (id == QStringLiteral("25000004"))
                        ? bcd_numeric_value_text(hive, *value)
                    : (id == QStringLiteral("14000006") || id == QStringLiteral("14000008") ||
                       id == QStringLiteral("23000003") || id == QStringLiteral("23000006") ||
                       id == QStringLiteral("24000001") || id == QStringLiteral("24000010"))
                        ? bcd_reference_value_text(hive, *value)
                    : value_data_text(hive, *value);
            if (id == QStringLiteral("14000006") || id == QStringLiteral("14000008") ||
                id == QStringLiteral("23000003") || id == QStringLiteral("23000006") ||
                id == QStringLiteral("24000001") || id == QStringLiteral("24000010")) {
                QStringList friendlyReferences;
                for (const QString &reference : bcd_reference_values(hive, *value)) {
                    QString friendlyName = reference;
                    for (int groupIndex = 0; groupIndex < bootTree->topLevelItemCount(); ++groupIndex) {
                        QTreeWidgetItem *group = bootTree->topLevelItem(groupIndex);
                        for (int objectIndex = 0; objectIndex < group->childCount(); ++objectIndex) {
                            QTreeWidgetItem *objectItem = group->child(objectIndex);
                            const QString guid = objectItem->data(0, Qt::UserRole + 1).toString();
                            if (guid.compare(reference, Qt::CaseInsensitive) == 0) {
                                friendlyName = objectItem->text(0);
                                break;
                            }
                        }
                    }
                    friendlyReferences.append(friendlyName);
                }
                storedData = friendlyReferences.isEmpty()
                    ? QStringLiteral("Not configured") : friendlyReferences.join(QStringLiteral(", "));
            }
            bootTable->setItem(row, 2, new QTableWidgetItem(storedData));
        }
        free(values);
    }
    free(elementNodes);
    statusLabel->setText(QStringLiteral("%1  |  %2 setting(s)")
        .arg(current->text(0)).arg(bootTable->rowCount()));
    updateActions();
}

void MainWindow::editSelectedValue() {
    QTableWidget *table = bootTable;
    const int row = table->currentRow();
    if (!hive || row < 0 || !table->item(row, 0))
        return;

    QTableWidgetItem *selectedItem = table->item(row, 0);
    const hive_node_h node = static_cast<hive_node_h>(
        selectedItem->data(Qt::UserRole + 1).toULongLong());
    const hive_value_h value = static_cast<hive_value_h>(
        selectedItem->data(Qt::UserRole).toULongLong());
    if (value == 0)
        return;
    hive_type type;
    size_t length = 0;
    if (hivex_value_type(hive, value, &type, &length) == -1) {
        QMessageBox::critical(this, "Edit failed", "Could not read the selected value's type.");
        return;
    }
    bool accepted = false;
    QByteArray newData;
    QString elementId;
    char *rawElementId = hivex_node_name(hive, node);
    if (rawElementId) {
        elementId = QString::fromUtf8(rawElementId).toUpper();
        free(rawElementId);
    }

    const bool isDeviceElement = elementId == QStringLiteral("11000001") ||
        elementId == QStringLiteral("21000001");
    const bool isInheritElement = elementId == QStringLiteral("14000006");
    const bool isRecoverySequenceElement = elementId == QStringLiteral("14000008");
    const bool isBootMenuPolicyElement = elementId == QStringLiteral("25000008") ||
        elementId == QStringLiteral("250000C2");
    const bool isBootMenuTimeoutElement = elementId == QStringLiteral("25000004");
    const bool isNxPolicyElement = elementId == QStringLiteral("25000020");
    const bool isHypervisorLaunchElement = elementId == QStringLiteral("250000F0");
    const bool isDisplayMessageOverrideElement = elementId == QStringLiteral("15000066");
    const bool isAllowedInMemoryElement = elementId == QStringLiteral("17000077");
    const bool isDefaultEntryElement = elementId == QStringLiteral("23000003");
    const bool isResumeObjectElement = elementId == QStringLiteral("23000006");
    const bool isDisplayOrderElement = elementId == QStringLiteral("24000001");
    const bool isToolsDisplayOrderElement = elementId == QStringLiteral("24000010");
    if (is_bcd_boolean_element(elementId)) {
        char *rawData = hivex_value_value(hive, value, &type, &length);
        const QByteArray bytes = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        if (type != hive_t_REG_BINARY || bytes.size() != 1 ||
            (static_cast<unsigned char>(bytes.at(0)) != 0 &&
             static_cast<unsigned char>(bytes.at(0)) != 1)) {
            QMessageBox::warning(this, "Unsupported Boolean value",
                                 "This Boolean element is not stored as a single 00 or 01 byte. "
                                 "It was left unchanged.");
            return;
        }

        const QStringList options = {QStringLiteral("No"), QStringLiteral("Yes")};
        const int currentIndex = static_cast<unsigned char>(bytes.at(0)) == 0 ? 0 : 1;
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Edit %1").arg(selectedItem->text()),
            QStringLiteral("%1:").arg(selectedItem->text()),
            options, currentIndex, false, &accepted);
        if (!accepted)
            return;

        newData.append(selected == QStringLiteral("Yes") ? '\x01' : '\x00');
    } else if (isDefaultEntryElement || isResumeObjectElement || isDisplayOrderElement ||
               isToolsDisplayOrderElement) {
        const bool isOrder = isDisplayOrderElement || isToolsDisplayOrderElement;
        const QString groupName = isResumeObjectElement
            ? QStringLiteral("Windows resume objects")
            : isToolsDisplayOrderElement
                ? QStringLiteral("Tools objects")
                : QStringLiteral("Application objects");
        QTreeWidgetItem *group = nullptr;
        for (int index = 0; index < bootTree->topLevelItemCount(); ++index) {
            QTreeWidgetItem *candidate = bootTree->topLevelItem(index);
            if (candidate->text(0) == groupName) {
                group = candidate;
                break;
            }
        }

        QList<QPair<QString, QString>> entries;
        if (group) {
            for (int index = 0; index < group->childCount(); ++index) {
                QTreeWidgetItem *objectItem = group->child(index);
                const QString guid = objectItem->data(0, Qt::UserRole + 1).toString();
                if (!guid.isEmpty())
                    entries.append(qMakePair(objectItem->text(0), guid));
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
            return QString::compare(left.first, right.first, Qt::CaseInsensitive) < 0;
        });
        if (entries.isEmpty()) {
            QMessageBox::warning(this, "No matching BCD objects",
                                 QStringLiteral("This store has no entries in '%1'.").arg(groupName));
            return;
        }

        const QStringList currentReferences = bcd_reference_values(hive, value);

        if (isOrder) {
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Edit %1").arg(selectedItem->text()));
            auto *layout = new QVBoxLayout(&dialog);
            auto *entryList = new QListWidget(&dialog);
            entryList->setSelectionMode(QAbstractItemView::SingleSelection);
            QHash<QString, QString> labelByGuid;
            for (const auto &entry : entries)
                labelByGuid.insert(entry.second, entry.first);
            for (const QString &guid : currentReferences) {
                if (!labelByGuid.contains(guid))
                    continue;
                auto *item = new QListWidgetItem(labelByGuid.value(guid), entryList);
                item->setData(Qt::UserRole, guid);
                item->setToolTip(guid);
                item->setCheckState(Qt::Checked);
            }
            for (const auto &entry : entries) {
                if (currentReferences.contains(entry.second))
                    continue;
                auto *item = new QListWidgetItem(entry.first, entryList);
                item->setData(Qt::UserRole, entry.second);
                item->setToolTip(entry.second);
                item->setCheckState(Qt::Unchecked);
            }
            layout->addWidget(entryList);
            auto *orderButtons = new QHBoxLayout();
            auto *moveUpButton = new QPushButton("Move up", &dialog);
            auto *moveDownButton = new QPushButton("Move down", &dialog);
            orderButtons->addWidget(moveUpButton);
            orderButtons->addWidget(moveDownButton);
            orderButtons->addStretch();
            layout->addLayout(orderButtons);
            const auto moveCurrentItem = [entryList](int offset) {
                const int row = entryList->currentRow();
                const int destination = row + offset;
                if (row < 0 || destination < 0 || destination >= entryList->count())
                    return;
                QListWidgetItem *item = entryList->takeItem(row);
                entryList->insertItem(destination, item);
                entryList->setCurrentRow(destination);
            };
            connect(moveUpButton, &QPushButton::clicked, &dialog,
                    [moveCurrentItem]() { moveCurrentItem(-1); });
            connect(moveDownButton, &QPushButton::clicked, &dialog,
                    [moveCurrentItem]() { moveCurrentItem(1); });
            auto *buttonBox = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttonBox);
            connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            for (int index = 0; index < entryList->count(); ++index) {
                QListWidgetItem *item = entryList->item(index);
                if (item->checkState() != Qt::Checked)
                    continue;
                const QString guid = item->data(Qt::UserRole).toString();
                for (const QChar character : guid) {
                    const quint16 codeUnit = character.unicode();
                    newData.append(static_cast<char>(codeUnit & 0xff));
                    newData.append(static_cast<char>((codeUnit >> 8) & 0xff));
                }
                newData.append('\0');
                newData.append('\0');
            }
            newData.append('\0');
            newData.append('\0');
        } else {
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("Edit %1").arg(selectedItem->text()));
            auto *layout = new QVBoxLayout(&dialog);
            auto *entryCombo = new QComboBox(&dialog);
            entryCombo->addItem("Not configured", QString());
            for (const auto &entry : entries) {
                const int index = entryCombo->count();
                entryCombo->addItem(entry.first, entry.second);
                entryCombo->setItemData(index, entry.second, Qt::ToolTipRole);
            }
            const QString currentReference = currentReferences.value(0);
            const int currentIndex = entryCombo->findData(currentReference);
            entryCombo->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
            layout->addWidget(entryCombo);
            auto *buttonBox = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttonBox);
            connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QString guid = entryCombo->currentData().toString();
            for (const QChar character : guid) {
                const quint16 codeUnit = character.unicode();
                newData.append(static_cast<char>(codeUnit & 0xff));
                newData.append(static_cast<char>((codeUnit >> 8) & 0xff));
            }
            newData.append('\0');
            newData.append('\0');
        }
    } else if (isBootMenuTimeoutElement) {
        if (type != hive_t_REG_BINARY || length < sizeof(quint32)) {
            QMessageBox::warning(this, "Unsupported timeout value",
                                 "This boot-menu timeout is not stored as a supported binary number.");
            return;
        }
        char *rawData = hivex_value_value(hive, value, &type, &length);
        newData = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        const quint32 currentValue = static_cast<quint32>(static_cast<unsigned char>(newData.at(0))) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(1))) << 8) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(2))) << 16) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(3))) << 24);
        const int selectedValue = QInputDialog::getInt(
            this, QStringLiteral("Edit %1").arg(selectedItem->text()),
            "Timeout in seconds:", static_cast<int>(qMin(currentValue, quint32(INT_MAX))),
            0, INT_MAX, 1, &accepted);
        if (!accepted)
            return;
        for (int index = 0; index < 4; ++index)
            newData[index] = static_cast<char>((static_cast<quint32>(selectedValue) >> (index * 8)) & 0xff);
    } else if (isDeviceElement) {
        if (type != hive_t_REG_BINARY) {
            QMessageBox::warning(this, "Unsupported device value",
                                 "This BCD device element is not stored as a binary device descriptor.");
            return;
        }

        char *rawData = hivex_value_value(hive, value, &type, &length);
        newData = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        if (newData.isEmpty()) {
            QMessageBox::warning(this, "Invalid device value", "The BCD device descriptor is empty.");
            return;
        }

        int partitionGuidOffset = -1;
        QString currentMount;
        QStringList mountChoices;
        QHash<QString, QByteArray> guidByChoice;
        for (auto mount = partitionMounts.cbegin(); mount != partitionMounts.cend(); ++mount) {
            if (mount.key().size() != 16)
                continue;
            if (newData.contains(mount.key())) {
                partitionGuidOffset = newData.indexOf(mount.key());
                currentMount = mount.value();
            }
            if (mount.value().endsWith(QStringLiteral(" (not mounted)")))
                continue;
            if (!guidByChoice.contains(mount.value())) {
                mountChoices.append(mount.value());
                guidByChoice.insert(mount.value(), mount.key());
            }
        }

        if (partitionGuidOffset < 0) {
            QMessageBox::warning(this, "Unknown device descriptor",
                                 "The descriptor does not contain a PARTUUID known to lsblk. "
                                 "It was left unchanged to avoid corrupting the BCD entry.");
            return;
        }
        if (mountChoices.isEmpty()) {
            QMessageBox::warning(this, "No mounted partitions",
                                 "lsblk did not report any mounted partitions to choose from.");
            return;
        }
        mountChoices.sort(Qt::CaseInsensitive);

bool accepted = false;
    QString selectedMount;

    {
        QDialog dialog(this);
        dialog.setWindowTitle(elementId == QStringLiteral("11000001")
            ? "Choose application device" : "Choose boot device");
        
        auto *layout = new QVBoxLayout(&dialog);

        layout->addWidget(new QLabel(QStringLiteral("Linux mountpoint for this BCD device:\nCurrent: %1")
            .arg(currentMount), &dialog));

        // Upper dropdown: Disks / SSDs
        layout->addWidget(new QLabel("Target Disk / Drive:", &dialog));
        auto *diskCombo = new QComboBox(&dialog);
        
        QStringList diskChoices;
        for (const QString &choice : mountChoices) {
            QString devPath;
            int devIdx = choice.indexOf("/dev/");
            if (devIdx != -1) {
                int endDev = devIdx;
                while (endDev < choice.length() && !choice.at(endDev).isSpace() && choice.at(endDev) != ')')
                    endDev++;
                devPath = choice.mid(devIdx, endDev - devIdx);
            }

            QString disk = devPath;
            if (!disk.isEmpty()) {
                if (disk.contains(QRegularExpression("nvme[0-9]+n[0-9]+p[0-9]+")) || disk.contains(QRegularExpression("mmcblk[0-9]+p[0-9]+"))) {
                    int pIdx = disk.lastIndexOf('p');
                    if (pIdx != -1) {
                        disk = disk.left(pIdx);
                    }
                } else {
                    int idx = disk.length() - 1;
                    while (idx >= 0 && disk.at(idx).isDigit()) {
                        idx--;
                    }
                    disk = disk.left(idx + 1);
                }
            }
            if (disk.isEmpty()) {
                disk = devPath;
            }

            if (!diskChoices.contains(disk) && !disk.isEmpty()) {
                diskChoices.append(disk);
            }
        }
        if (diskChoices.isEmpty()) diskChoices.append("System Disks");
        diskCombo->addItems(diskChoices);
        layout->addWidget(diskCombo);

        // Lower dropdown: Partitions / Mountpoints
        layout->addWidget(new QLabel("Target Partition / Mountpoint:", &dialog));
        auto *partitionCombo = new QComboBox(&dialog);
        layout->addWidget(partitionCombo); // Added to layout so it positions properly under the label

        QString currentDisk;
        for (const QString &disk : diskChoices) {
            if (currentMount.contains(disk)) {
                currentDisk = disk;
                break;
            }
        }
        if (!currentDisk.isEmpty()) {
            diskCombo->setCurrentText(currentDisk);
        }

        auto updatePartitions = [partitionCombo, mountChoices](const QString &selectedDisk) {
            partitionCombo->clear();
            for (const QString &choice : mountChoices) {
                if (choice.contains(selectedDisk)) {
                    partitionCombo->addItem(choice);
                }
            }
            if (partitionCombo->count() == 0) {
                partitionCombo->addItems(mountChoices);
            }
        };

        updatePartitions(diskCombo->currentText());
        
        int currentIndex = partitionCombo->findText(currentMount);
        if (currentIndex != -1) {
            partitionCombo->setCurrentIndex(currentIndex);
        } else if (partitionCombo->count() > 0) {
            partitionCombo->setCurrentIndex(0);
        }

        QObject::connect(diskCombo, &QComboBox::currentTextChanged, updatePartitions);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttonBox);

        QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            accepted = true;
            selectedMount = partitionCombo->currentText();
        }
    }
    
    if (!accepted)
        return;

    const QByteArray newPartitionGuid = guidByChoice.value(selectedMount);
    if (newPartitionGuid.size() != 16) {
        QMessageBox::warning(this, "Invalid partition", "Could not resolve the selected partition identifier.");
        return;
    }
    newData.replace(partitionGuidOffset, newPartitionGuid.size(), newPartitionGuid);
    } else if (isInheritElement || isRecoverySequenceElement) {
        char *rawData = hivex_value_value(hive, value, &type, &length);
        const QByteArray bytes = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);

        QString currentReference;
        if (bytes.size() >= 2) {
            for (int index = 0; index + 1 < bytes.size(); index += 2) {
                const ushort codeUnit = static_cast<ushort>(
                    static_cast<unsigned char>(bytes.at(index)) |
                    (static_cast<unsigned char>(bytes.at(index + 1)) << 8));
                if (codeUnit == 0)
                    break;
                currentReference.append(QChar(codeUnit));
            }
        }

        QStringList choices;
        QHash<QString, QString> referenceByChoice;
        const QString targetGroup = isInheritElement
            ? QStringLiteral("Inheritable objects")
            : QStringLiteral("Application objects");
        QTreeWidgetItem *group = nullptr;
        for (int index = 0; index < bootTree->topLevelItemCount(); ++index) {
            QTreeWidgetItem *candidate = bootTree->topLevelItem(index);
            if (candidate->text(0) == targetGroup) {
                group = candidate;
                break;
            }
        }
        if (group) {
            for (int index = 0; index < group->childCount(); ++index) {
                QTreeWidgetItem *objectItem = group->child(index);
                if (isRecoverySequenceElement &&
                    !objectItem->text(0).contains(QStringLiteral("recovery"), Qt::CaseInsensitive))
                    continue;
                const QString guid = objectItem->data(0, Qt::UserRole + 1).toString();
                if (guid.isEmpty())
                    continue;
                const QString choice = QStringLiteral("%1  %2")
                    .arg(objectItem->text(0), guid);
                choices.append(choice);
                referenceByChoice.insert(choice, guid);
            }
        }
        if (choices.isEmpty() && isRecoverySequenceElement && group) {
            for (int index = 0; index < group->childCount(); ++index) {
                QTreeWidgetItem *objectItem = group->child(index);
                const QString guid = objectItem->data(0, Qt::UserRole + 1).toString();
                if (guid.isEmpty())
                    continue;
                const QString choice = QStringLiteral("%1  %2")
                    .arg(objectItem->text(0), guid);
                choices.append(choice);
                referenceByChoice.insert(choice, guid);
            }
        }
        if (choices.isEmpty()) {
            QMessageBox::warning(this, "No matching BCD objects",
                                 isInheritElement
                                     ? "This store has no inheritable objects to choose from."
                                     : "This store has no recovery entries to choose from.");
            return;
        }
        choices.sort(Qt::CaseInsensitive);
        int currentIndex = 0;
        for (int index = 0; index < choices.size(); ++index) {
            if (referenceByChoice.value(choices.at(index)) == currentReference) {
                currentIndex = index;
                break;
            }
        }

        const QString selected = QInputDialog::getItem(
            this, isInheritElement ? "Choose inherited settings" : "Choose recovery entry",
            isInheritElement ? "Inheritable object:" : "Recovery sequence:",
            choices, currentIndex, false, &accepted);
        if (!accepted)
            return;

        const QString reference = referenceByChoice.value(selected);
        for (const QChar character : reference) {
            const quint16 codeUnit = character.unicode();
            newData.append(static_cast<char>(codeUnit & 0xff));
            newData.append(static_cast<char>((codeUnit >> 8) & 0xff));
        }
        newData.append('\0');
        newData.append('\0');
    } else if (isBootMenuPolicyElement || isNxPolicyElement || isHypervisorLaunchElement) {
        if (type != hive_t_REG_BINARY || length < sizeof(quint32)) {
            QMessageBox::warning(this, "Unsupported policy value",
                                 "This BCD policy is not stored as a supported binary value.");
            return;
        }

        char *rawData = hivex_value_value(hive, value, &type, &length);
        newData = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        if (newData.size() < static_cast<int>(sizeof(quint32))) {
            QMessageBox::warning(this, "Invalid policy value", "The BCD policy value is empty.");
            return;
        }

        const quint32 currentValue = static_cast<quint32>(static_cast<unsigned char>(newData.at(0))) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(1))) << 8) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(2))) << 16) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(3))) << 24);
        QStringList options;
        QHash<QString, quint32> valueByOption;
        const auto addOption = [&options, &valueByOption](const QString &label, quint32 optionValue) {
            options.append(label);
            valueByOption.insert(label, optionValue);
        };
        if (isBootMenuPolicyElement) {
            addOption(QStringLiteral("Legacy"), 0);
            addOption(QStringLiteral("Standard"), 1);
        } else if (isNxPolicyElement) {
            addOption(QStringLiteral("OptIn"), 0);
            addOption(QStringLiteral("OptOut"), 1);
            addOption(QStringLiteral("AlwaysOn"), 2);
            addOption(QStringLiteral("AlwaysOff"), 3);
        } else {
            addOption(QStringLiteral("Off"), 0);
            addOption(QStringLiteral("Auto"), 1);
        }

        int currentIndex = 0;
        for (int index = 0; index < options.size(); ++index) {
            if (valueByOption.value(options.at(index)) == currentValue) {
                currentIndex = index;
                break;
            }
        }
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Edit %1").arg(selectedItem->text()),
            QStringLiteral("%1:").arg(selectedItem->text()),
            options, currentIndex, false, &accepted);
        if (!accepted)
            return;

        const quint32 selectedValue = valueByOption.value(selected);
        newData[0] = static_cast<char>(selectedValue & 0xff);
        newData[1] = static_cast<char>((selectedValue >> 8) & 0xff);
        newData[2] = static_cast<char>((selectedValue >> 16) & 0xff);
        newData[3] = static_cast<char>((selectedValue >> 24) & 0xff);
    } else if (isDisplayMessageOverrideElement) {
        if (type != hive_t_REG_BINARY || length < sizeof(quint32)) {
            QMessageBox::warning(this, "Unsupported display message value",
                                 "This setting is not stored as a supported binary value.");
            return;
        }
        char *rawData = hivex_value_value(hive, value, &type, &length);
        newData = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        const quint32 currentValue = static_cast<quint32>(static_cast<unsigned char>(newData.at(0))) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(1))) << 8) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(2))) << 16) |
            (static_cast<quint32>(static_cast<unsigned char>(newData.at(3))) << 24);
        const QStringList options = {
            QStringLiteral("None"),
            QStringLiteral("Recovery"),
            QStringLiteral("Startup Repair"),
            QStringLiteral("Recovery and Startup Repair")
        };
        const int currentIndex = currentValue < static_cast<quint32>(options.size())
            ? static_cast<int>(currentValue) : 0;
        const QString selected = QInputDialog::getItem(
            this, QStringLiteral("Edit %1").arg(selectedItem->text()),
            QStringLiteral("%1:").arg(selectedItem->text()),
            options, currentIndex, false, &accepted);
        if (!accepted)
            return;
        const quint32 selectedValue = static_cast<quint32>(options.indexOf(selected));
        newData[0] = static_cast<char>(selectedValue & 0xff);
        newData[1] = static_cast<char>((selectedValue >> 8) & 0xff);
        newData[2] = static_cast<char>((selectedValue >> 16) & 0xff);
        newData[3] = static_cast<char>((selectedValue >> 24) & 0xff);
    } else if (isAllowedInMemoryElement) {
        if (type != hive_t_REG_BINARY || length < sizeof(quint32)) {
            QMessageBox::warning(this, "Unsupported numeric value",
                                 "This setting is not stored as a supported binary number.");
            return;
        }
        char *rawData = hivex_value_value(hive, value, &type, &length);
        newData = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);
        quint64 currentValue = 0;
        const int valueBytes = qMin(newData.size(), static_cast<int>(sizeof(quint64)));
        for (int index = 0; index < valueBytes; ++index)
            currentValue |= static_cast<quint64>(static_cast<unsigned char>(newData.at(index))) << (index * 8);
        bool numberAccepted = false;
        const int selectedValue = QInputDialog::getInt(
            this, QStringLiteral("Edit %1").arg(selectedItem->text()),
            QStringLiteral("Decimal value:"), static_cast<int>(qMin(currentValue, quint64(INT_MAX))),
            0, INT_MAX, 1, &numberAccepted);
        if (!numberAccepted)
            return;
        const quint64 replacement = static_cast<quint64>(selectedValue);
        for (int index = 0; index < valueBytes; ++index)
            newData[index] = static_cast<char>((replacement >> (index * 8)) & 0xff);
    } else if (type == hive_t_REG_SZ || type == hive_t_REG_EXPAND_SZ) {
        char *rawText = hivex_value_string(hive, value);
        const QString originalText = rawText ? QString::fromUtf8(rawText) : QString();
        free(rawText);

        const QString entered = QInputDialog::getText(
            this, type == hive_t_REG_EXPAND_SZ ? "Edit expandable string" : "Edit string",
            type == hive_t_REG_EXPAND_SZ ? "Text (environment variables are preserved):" : "Text:",
            QLineEdit::Normal, originalText, &accepted);
        if (!accepted)
            return;

        for (const QChar character : entered) {
            const quint16 codeUnit = character.unicode();
            newData.append(static_cast<char>(codeUnit & 0xff));
            newData.append(static_cast<char>((codeUnit >> 8) & 0xff));
        }
        newData.append('\0');
        newData.append('\0');
    } else {
        char *rawData = hivex_value_value(hive, value, &type, &length);
        const QByteArray bytes = rawData ? QByteArray(rawData, static_cast<int>(length)) : QByteArray();
        free(rawData);

        const QString entered = QInputDialog::getText(
            this, "Edit raw value data", "Edit stored bytes as hexadecimal pairs (example: 01 02 FF):",
            QLineEdit::Normal, QString::fromLatin1(bytes.toHex(' ')), &accepted);
        if (!accepted)
            return;

        static const QRegularExpression hexBytes(
            QStringLiteral("^(?:[0-9A-Fa-f]{2}(?:\\s+[0-9A-Fa-f]{2})*)?$"));
        if (!hexBytes.match(entered.trimmed()).hasMatch()) {
            QMessageBox::warning(this, "Invalid data", "Enter complete hexadecimal byte pairs separated by spaces.");
            return;
        }

        QString compact = entered;
        compact.remove(QRegularExpression(QStringLiteral("\\s")));
        newData = QByteArray::fromHex(compact.toLatin1());
    }

    char *rawKey = hivex_value_key(hive, value);
    hive_set_value replacement = {
        rawKey,
        type,
        static_cast<size_t>(newData.size()),
        newData.data()
    };
    const int result = hivex_node_set_value(hive, node, &replacement, 0);
    free(rawKey);
    if (result == -1) {
        QMessageBox::critical(this, "Edit failed", "libhivex could not update this value.");
        return;
    }

    modified = true;
    showBootObjectValues(bootTree->currentItem(), nullptr);
    statusLabel->setText("Unsaved changes. Use Save to write a separate BCD store.");
    updateActions();
}

void MainWindow::saveBcdFileAs() {
    if (!hive || !modified)
        return;

    const QFileInfo source(hivePath);
    const QString suggested = source.absolutePath() + QDir::separator() +
        source.fileName() + QStringLiteral(".edited");
    const QString outputPath = QFileDialog::getSaveFileName(
        this, "Save BCD Store", suggested, "BCD stores and registry hives (*)");
    if (outputPath.isEmpty())
        return;
    if (QFileInfo(outputPath).absoluteFilePath() == source.absoluteFilePath()) {
        QMessageBox::warning(this, "Choose another file",
                             "Save As will not overwrite the source BCD hive.");
        return;
    }
    if (hivex_commit(hive, outputPath.toUtf8().constData(), 0) == -1) {
        QMessageBox::critical(this, "Save failed", "libhivex could not write the edited hive.");
        return;
    }

    modified = false;
    statusLabel->setText(QString("Saved edited hive: %1").arg(outputPath));
    updateActions();
}

bool MainWindow::confirmDiscardChanges() {
    if (!modified)
        return true;

    const auto answer = QMessageBox::warning(
        this, "Unsaved changes", "The current hive has unsaved changes.",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;

    saveBcdFileAs();
    return !modified;
}

void MainWindow::updateActions() {
    saveButton->setEnabled(hive && modified);
    const int row = modeTabs->currentIndex() == 0 ? bootTable->currentRow() : -1;
    const bool hasEditableValue = row >= 0 && bootTable->item(row, 0) &&
        bootTable->item(row, 0)->data(Qt::UserRole).toULongLong() != 0;
}

static QString bcd_element_name(const QString &id, quint32 objectType) {
    const QString normalized = id.toUpper();
    if (normalized == QStringLiteral("11000001")) return QStringLiteral("Application device");
    if (normalized == QStringLiteral("12000002")) return QStringLiteral("Application path");
    if (normalized == QStringLiteral("12000004")) return QStringLiteral("Description");
    if (normalized == QStringLiteral("12000005")) return QStringLiteral("Locale");
    if (normalized == QStringLiteral("14000006")) return QStringLiteral("Inherit");
    if (normalized == QStringLiteral("14000008")) return QStringLiteral("Recovery sequence");
    if (normalized == QStringLiteral("15000065")) return QStringLiteral("Display message");
    if (normalized == QStringLiteral("15000066")) return QStringLiteral("Display message override");
    if (normalized == QStringLiteral("16000010")) return QStringLiteral("Boot debug");
    if (normalized == QStringLiteral("16000011")) return QStringLiteral("Kernel debug");
    if (normalized == QStringLiteral("16000020")) return QStringLiteral("Emergency Management Services");
    if (normalized == QStringLiteral("16000040")) return QStringLiteral("VGA mode");
    if (normalized == QStringLiteral("16000041")) return QStringLiteral("Boot log");
    if (normalized == QStringLiteral("16000042")) return QStringLiteral("Advanced options");
    if (normalized == QStringLiteral("16000043")) return QStringLiteral("Options edit");
    if (normalized == QStringLiteral("16000044")) return QStringLiteral("FVE boot");
    if (normalized == QStringLiteral("16000045")) return QStringLiteral("Legacy APIC mode");
    if (normalized == QStringLiteral("16000050")) return QStringLiteral("Disable crash auto-reboot");
    if (normalized == QStringLiteral("16000048")) return QStringLiteral("No integrity checks");
    if (normalized == QStringLiteral("16000049")) return QStringLiteral("Test signing");
    if (normalized == QStringLiteral("16000060")) return QStringLiteral("Isolated execution context");
    if (normalized == QStringLiteral("1600007E")) return QStringLiteral("Flight signing");
    if (normalized == QStringLiteral("16000009")) return QStringLiteral("Recovery enabled");
    if (normalized == QStringLiteral("17000077")) return QStringLiteral("Allowed in-memory settings");
    if (normalized == QStringLiteral("21000001")) return QStringLiteral("Device");
    if (normalized == QStringLiteral("21000002")) return QStringLiteral("OS device");
    if (normalized == QStringLiteral("22000002")) {
        return objectType == 0x10200004u
            ? QStringLiteral("Resume file path")
            : QStringLiteral("System root");
    }
    if (normalized == QStringLiteral("23000003")) return QStringLiteral("Default boot entry");
    if (normalized == QStringLiteral("23000006")) return QStringLiteral("Resume object");
    if (normalized == QStringLiteral("24000001")) return QStringLiteral("Display order");
    if (normalized == QStringLiteral("24000002")) return QStringLiteral("One-time boot sequence");
    if (normalized == QStringLiteral("24000010")) return QStringLiteral("Tools display order");
    if (normalized == QStringLiteral("25000020")) return QStringLiteral("NX policy");
    if (normalized == QStringLiteral("25000004")) return QStringLiteral("Boot menu timeout");
    if (normalized == QStringLiteral("25000008") || normalized == QStringLiteral("250000C2"))
        return QStringLiteral("Boot menu policy");
    if (normalized == QStringLiteral("250000F0")) return QStringLiteral("Hypervisor launch type");
    if (normalized == QStringLiteral("26000006") && objectType == 0x10200004u)
        return QStringLiteral("Debug option enabled");
    if (normalized == QStringLiteral("26000022") && objectType == 0x10200003u)
        return QStringLiteral("WinPE mode");
    if (normalized == QStringLiteral("31000003")) return QStringLiteral("Ramdisk SDI device");
    if (normalized == QStringLiteral("32000004")) return QStringLiteral("Ramdisk SDI path");
    if (normalized == QStringLiteral("46000010")) return QStringLiteral("Recovery OS");
    if (normalized == QStringLiteral("1600000B")) return QStringLiteral("Bad memory access");
    return QStringLiteral("Other BCD setting");
}

static bool is_bcd_boolean_element(const QString &id) {
    const QString typeFamily = id.left(2).toUpper();
    return typeFamily == QStringLiteral("16") ||
        typeFamily == QStringLiteral("26") ||
        typeFamily == QStringLiteral("36") ||
        typeFamily == QStringLiteral("46");
}

static QString bcd_boolean_value_text(hive_h *hive, hive_value_h value) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData)
        return QStringLiteral("(empty)");

    const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
    QString result;
    if (type == hive_t_REG_BINARY && length == 1 && (bytes[0] == 0 || bytes[0] == 1))
        result = bytes[0] == 0 ? QStringLiteral("No") : QStringLiteral("Yes");
    else
        result = QStringLiteral("Invalid Boolean data (%1)")
            .arg(QString::fromLatin1(QByteArray(rawData, static_cast<int>(length)).toHex(' ')));
    free(rawData);
    return result;
}

static QString bcd_policy_value_text(hive_h *hive, hive_value_h value, const QString &id) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData || type != hive_t_REG_BINARY || length < sizeof(quint32)) {
        free(rawData);
        return QStringLiteral("Unknown policy value");
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
    const quint32 number = static_cast<quint32>(bytes[0]) |
        (static_cast<quint32>(bytes[1]) << 8) |
        (static_cast<quint32>(bytes[2]) << 16) |
        (static_cast<quint32>(bytes[3]) << 24);
    QString result;
    if (id == QStringLiteral("25000008") || id == QStringLiteral("250000C2")) {
        if (number == 0)
            result = QStringLiteral("Legacy");
        else if (number == 1)
            result = QStringLiteral("Standard");
    } else if (id == QStringLiteral("25000020")) {
        if (number == 0)
            result = QStringLiteral("OptIn");
        else if (number == 1)
            result = QStringLiteral("OptOut");
        else if (number == 2)
            result = QStringLiteral("AlwaysOn");
        else if (number == 3)
            result = QStringLiteral("AlwaysOff");
    } else if (id == QStringLiteral("250000F0")) {
        if (number == 0)
            result = QStringLiteral("Off");
        else if (number == 1)
            result = QStringLiteral("Auto");
    }
    free(rawData);
    return result.isEmpty() ? QStringLiteral("Unknown policy value") : result;
}

static QString bcd_display_message_value_text(hive_h *hive, hive_value_h value) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData || type != hive_t_REG_BINARY || length < sizeof(quint32)) {
        free(rawData);
        return QStringLiteral("Unknown display message value");
    }
    const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
    const quint32 number = static_cast<quint32>(bytes[0]) |
        (static_cast<quint32>(bytes[1]) << 8) |
        (static_cast<quint32>(bytes[2]) << 16) |
        (static_cast<quint32>(bytes[3]) << 24);
    free(rawData);
    switch (number) {
    case 0: return QStringLiteral("None");
    case 1: return QStringLiteral("Recovery");
    case 2: return QStringLiteral("Startup Repair");
    case 3: return QStringLiteral("Recovery and Startup Repair");
    default: return QStringLiteral("Unknown display message value");
    }
}

static QString bcd_numeric_value_text(hive_h *hive, hive_value_h value) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData || type != hive_t_REG_BINARY || length == 0) {
        free(rawData);
        return QStringLiteral("Unknown numeric value");
    }
    quint64 number = 0;
    const int valueBytes = qMin(length, sizeof(quint64));
    const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
    for (int index = 0; index < valueBytes; ++index)
        number |= static_cast<quint64>(bytes[index]) << (index * 8);
    free(rawData);
    return QString::number(number);
}

static QStringList bcd_reference_values(hive_h *hive, hive_value_h value) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData || length < 2) {
        free(rawData);
        return {};
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
    QStringList references;
    QString currentReference;
    for (size_t index = 0; index + 1 < length; index += 2) {
        const ushort codeUnit = static_cast<ushort>(bytes[index] | (bytes[index + 1] << 8));
        if (codeUnit == 0) {
            if (!currentReference.isEmpty()) {
                references.append(currentReference);
                currentReference.clear();
            } else if (!references.isEmpty()) {
                break;
            }
        } else {
            currentReference.append(QChar(codeUnit));
        }
    }
    free(rawData);
    if (!currentReference.isEmpty())
        references.append(currentReference);
    return references;
}

static QString bcd_reference_value_text(hive_h *hive, hive_value_h value) {
    const QStringList references = bcd_reference_values(hive, value);
    return references.isEmpty() ? QStringLiteral("Not configured")
                                : references.join(QStringLiteral(", "));
}

static QString bcdedit_option_name(const QString &id, quint32 objectType) {
    const QString normalized = id.toUpper();
    if (normalized == QStringLiteral("11000001") || normalized == QStringLiteral("21000001"))
        return QStringLiteral("device");
    if (normalized == QStringLiteral("12000002")) return QStringLiteral("path");
    if (normalized == QStringLiteral("12000004")) return QStringLiteral("description");
    if (normalized == QStringLiteral("12000005")) return QStringLiteral("locale");
    if (normalized == QStringLiteral("14000006")) return QStringLiteral("inherit");
    if (normalized == QStringLiteral("14000008")) return QStringLiteral("recoverysequence");
    if (normalized == QStringLiteral("15000065")) return QStringLiteral("displaymessage");
    if (normalized == QStringLiteral("15000066")) return QStringLiteral("displaymessageoverride");
    if (normalized == QStringLiteral("16000010")) return QStringLiteral("bootdebug");
    if (normalized == QStringLiteral("16000011")) return QStringLiteral("debug");
    if (normalized == QStringLiteral("16000020")) return QStringLiteral("ems");
    if (normalized == QStringLiteral("16000040")) return QStringLiteral("vga");
    if (normalized == QStringLiteral("16000041")) return QStringLiteral("bootlog");
    if (normalized == QStringLiteral("16000042")) return QStringLiteral("advancedoptions");
    if (normalized == QStringLiteral("16000043")) return QStringLiteral("optionsedit");
    if (normalized == QStringLiteral("16000044")) return QStringLiteral("fveboot");
    if (normalized == QStringLiteral("16000045")) return QStringLiteral("uselegacyapicmode");
    if (normalized == QStringLiteral("16000050")) return QStringLiteral("disablecrashautoreboot");
    if (normalized == QStringLiteral("16000048")) return QStringLiteral("nointegritychecks");
    if (normalized == QStringLiteral("16000049")) return QStringLiteral("testsigning");
    if (normalized == QStringLiteral("16000060")) return QStringLiteral("isolatedcontext");
    if (normalized == QStringLiteral("1600007E")) return QStringLiteral("flightsigning");
    if (normalized == QStringLiteral("16000009")) return QStringLiteral("recoveryenabled");
    if (normalized == QStringLiteral("17000077")) return QStringLiteral("allowedinmemorysettings");
    if (normalized == QStringLiteral("21000002")) return QStringLiteral("osdevice");
    if (normalized == QStringLiteral("22000002")) {
        return objectType == 0x10200004u
            ? QStringLiteral("filepath")
            : QStringLiteral("systemroot");
    }
    if (normalized == QStringLiteral("23000003")) return QStringLiteral("default");
    if (normalized == QStringLiteral("23000006")) return QStringLiteral("resumeobject");
    if (normalized == QStringLiteral("24000001")) return QStringLiteral("displayorder");
    if (normalized == QStringLiteral("24000002")) return QStringLiteral("bootsequence");
    if (normalized == QStringLiteral("24000010")) return QStringLiteral("toolsdisplayorder");
    if (normalized == QStringLiteral("25000020")) return QStringLiteral("nx");
    if (normalized == QStringLiteral("25000004")) return QStringLiteral("timeout");
    if (normalized == QStringLiteral("25000008") || normalized == QStringLiteral("250000C2"))
        return QStringLiteral("bootmenupolicy");
    if (normalized == QStringLiteral("250000F0")) return QStringLiteral("hypervisorlaunchtype");
    if (normalized == QStringLiteral("26000006") && objectType == 0x10200004u)
        return QStringLiteral("debugoptionenabled");
    if (normalized == QStringLiteral("26000022") && objectType == 0x10200003u)
        return QStringLiteral("winpe");
    if (normalized == QStringLiteral("31000003")) return QStringLiteral("ramdisksdidevice");
    if (normalized == QStringLiteral("32000004")) return QStringLiteral("ramdisksdipath");
    if (normalized == QStringLiteral("46000010")) return QStringLiteral("recoveryos");
    if (normalized == QStringLiteral("1600000B")) return QStringLiteral("badmemoryaccess");
    return QString();
}

static QString value_data_text(hive_h *hive, hive_value_h value) {
    hive_type type;
    size_t length = 0;
    char *rawData = hivex_value_value(hive, value, &type, &length);
    if (!rawData)
        return QStringLiteral("(empty)");

    QString result;
    if (type == hive_t_REG_SZ || type == hive_t_REG_EXPAND_SZ) {
        char *text = hivex_value_string(hive, value);
        result = text ? QString::fromUtf8(text) : QStringLiteral("(invalid string)");
        free(text);
    } else if (type == hive_t_REG_DWORD) {
        result = QString::number(static_cast<quint32>(hivex_value_dword(hive, value)));
    } else if (type == hive_t_REG_QWORD) {
        result = QString::number(static_cast<quint64>(hivex_value_qword(hive, value)));
    } else if (type == hive_t_REG_DWORD_BIG_ENDIAN && length >= sizeof(quint32)) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(rawData);
        const quint32 number = (static_cast<quint32>(bytes[0]) << 24) |
            (static_cast<quint32>(bytes[1]) << 16) |
            (static_cast<quint32>(bytes[2]) << 8) |
            static_cast<quint32>(bytes[3]);
        result = QString::number(number);
    } else {
        result = QString::fromLatin1(QByteArray(rawData, static_cast<int>(length)).toHex(' '));
    }
    free(rawData);
    return result;
}