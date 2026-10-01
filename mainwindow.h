#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QPushButton>
#include <QLineEdit>
#include <QString>
#include <hivex.h>

class QComboBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QTabWidget;
class QKeySequence;
class QKeySequenceEdit;
class QShortcut;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openBcdFile();
    void openBcdPath();
    void createBcdStore();
    void saveBcdFileAs();
    void editSelectedValue();
    void showBootObjectValues(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void showBootTreeContextMenu(const QPoint &position);
    void showBootTableContextMenu(const QPoint &position);

private:
    struct HiveValueSnapshot {
        QString name;
        hive_type type = hive_t_REG_NONE;
        QByteArray data;
    };

    struct HiveNodeSnapshot {
        QString name;
        QVector<HiveValueSnapshot> values;
        QVector<HiveNodeSnapshot> children;
    };

    struct PartitionMountLocation {
        QStringList mountPoints;
        QString devicePath;
        QString type;

        QString displayText() const;
        int matchPriority() const;
    };

    void setupUI();
    void populateBootObjects(hive_node_h root);
    bool captureNode(hive_node_h node, HiveNodeSnapshot &snapshot) const;
    hive_node_h cloneNode(const HiveNodeSnapshot &snapshot, hive_node_h parent,
                          const QString &overrideName = QString());
    void refreshBootTree(const QString &selectGuid = QString());
    void createBootEntry();
    void copySelectedBootEntry();
    void cutSelectedBootEntry();
    void pasteBootEntry();
    void deleteSelectedBootEntry();
    void cutSelectedElement();
    void pasteElement();
    void createNewField();
    void deleteSelectedElement();
    void setupHotkeysTab();
    bool applyHotkeys(bool saveSettings);
    QKeySequence hotkeySequence(const QString &id) const;
    void refreshPartitionMounts();
    QString deviceValueText(hive_value_h value) const;
    hive_node_h findChildNode(hive_node_h parent, const QString &name) const;
    bool loadBcdFile(const QString &filePath);
    bool confirmDiscardChanges();
    void updateActions();

    QComboBox *pathComboBox;
    QPushButton *openButton;
    QPushButton *saveButton;
    QPushButton *createStoreButton;
    QTabWidget *modeTabs;
    QTreeWidget *bootTree;
    QTableWidget *bootTable;
    QLabel *statusLabel;
    hive_h *hive = nullptr;
    QString hivePath;
    QHash<QByteArray, PartitionMountLocation> partitionMounts;
    QHash<QString, QString> hotkeys;
    QHash<QString, QKeySequenceEdit *> hotkeyEditors;
    QVector<QShortcut *> hotkeyShortcuts;
    HiveNodeSnapshot objectClipboard;
    HiveNodeSnapshot elementClipboard;
    bool hasObjectClipboard = false;
    bool hasElementClipboard = false;
    bool modified = false;
};