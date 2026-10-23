/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SWISS_EPHEMERIS_INVENTORY_HPP
#define SWISS_EPHEMERIS_INVENTORY_HPP
#include <QDialog>
#include <QJsonObject>
#include <QStringList>
#include <memory>
class QDirIterator;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class SwissEphemerisInventory : public QDialog
{
public:
    explicit SwissEphemerisInventory(const QString& directory,QWidget* parent=nullptr);
    ~SwissEphemerisInventory() override;
    void reject() override;
private:
    void scan();
    void scanBatch();
    void updateSelection();
    void selectRange();
    void removeSelected();
    void removeBatch();
    QString directory;
    QTreeWidget* tree;
    QTreeWidgetItem* individual;
    QTreeWidgetItem* shared;
    QLabel* status;
    QLineEdit* range;
    QPushButton* select;
    QPushButton* remove;
    QPushButton* refresh;
    std::unique_ptr<QDirIterator> iterator;
    QStringList pending;
    QStringList deletionErrors;
    QJsonObject manifest;
    int deleted=0, failed=0, scanned=0;
    qint64 totalBytes=0, deletedBytes=0;
    bool deleting=false;
};
#endif
