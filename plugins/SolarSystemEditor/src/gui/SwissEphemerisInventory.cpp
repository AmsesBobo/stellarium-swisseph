/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemerisInventory.hpp"
#include "SwissEphemerisFiles.hpp"
#include "SwissEphemeris.hpp"
#include "StelUtils.hpp"
#include "StelTranslator.hpp"
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDirIterator>
#include <QTimer>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QLocale>

namespace {
class FileItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column=treeWidget()->sortColumn();
        if(column==0 && data(1,Qt::UserRole).toInt()>0 && other.data(1,Qt::UserRole).toInt()>0)
            return data(1,Qt::UserRole).toInt()<other.data(1,Qt::UserRole).toInt();
        if(column==3) return data(3,Qt::UserRole).toLongLong()<other.data(3,Qt::UserRole).toLongLong();
        return QTreeWidgetItem::operator<(other);
    }
};
QString sizeText(qint64 bytes) { return QLocale().formattedDataSize(bytes); }
QString dateText(double jd) {
    int y,m,d; StelUtils::getDateFromJulianDay(jd,&y,&m,&d);
    return QString("%1-%2-%3").arg(y).arg(m,2,10,QLatin1Char('0')).arg(d,2,10,QLatin1Char('0'));
}
}
SwissEphemerisInventory::SwissEphemerisInventory(const QString& path,QWidget* parent)
    : QDialog(parent), directory(path)
{
    setWindowTitle(q_("Installed Swiss ephemerides"));
    resize(850,620);
    auto* layout=new QVBoxLayout(this);
    auto* help=new QLabel(q_("Select asteroid files by number or range, for example 1001-10000. Both long and short files are selected. Objects remain in the catalogue; without data they use Kepler elements. Asteroids 1–4 share seas files in the separate shared-data group."),this);
    help->setWordWrap(true); layout->addWidget(help);
    auto* line=new QHBoxLayout;
    range=new QLineEdit(this); range->setPlaceholderText("1001-10000"); range->setObjectName("swissDeleteRange");
    select=new QPushButton(q_("Select numbers"),this); select->setObjectName("swissSelectNumbers");
    refresh=new QPushButton(q_("Refresh"),this);
    line->addWidget(range); line->addWidget(select); line->addWidget(refresh); layout->addLayout(line);
    tree=new QTreeWidget(this); tree->setObjectName("swissInventory");
    tree->setColumnCount(5);
    tree->setHeaderLabels({q_("Number / data type"),q_("Name"),q_("Coverage (TT)"),q_("Size"),q_("File")});
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setUniformRowHeights(true);
    tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree->setColumnWidth(0,155); tree->setColumnWidth(1,130); tree->setColumnWidth(2,235);
    layout->addWidget(tree);
    status=new QLabel(this); status->setWordWrap(true); status->setObjectName("swissInventoryStatus"); layout->addWidget(status);
    remove=new QPushButton(q_("Delete selected files…"),this); remove->setObjectName("swissDeleteSelected");
    layout->addWidget(remove);
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,this); layout->addWidget(close);
    connect(close,&QDialogButtonBox::rejected,this,&SwissEphemerisInventory::reject);
    connect(select,&QPushButton::clicked,this,[this]{selectRange();});
    connect(range,&QLineEdit::returnPressed,this,[this]{if(select->isEnabled()) selectRange();});
    connect(refresh,&QPushButton::clicked,this,[this]{scan();});
    connect(remove,&QPushButton::clicked,this,[this]{removeSelected();});
    connect(tree,&QTreeWidget::itemSelectionChanged,this,[this]{updateSelection();});
    scan();
}
SwissEphemerisInventory::~SwissEphemerisInventory() = default;
void SwissEphemerisInventory::reject() { if (!deleting) QDialog::reject(); }
void SwissEphemerisInventory::scan()
{
    select->setEnabled(false); remove->setEnabled(false); refresh->setEnabled(false);
    tree->setSortingEnabled(false); tree->clear(); totalBytes=0; scanned=0;
    individual=new QTreeWidgetItem(tree,{q_("Individual asteroids")});
    shared=new QTreeWidgetItem(tree,{q_("Shared data — affects multiple objects")});
    for(auto* group:{individual,shared}) {group->setFlags(group->flags() & ~Qt::ItemIsSelectable); group->setExpanded(true); group->setFirstColumnSpanned(true);}
    iterator.reset(new QDirIterator(directory,{"*.se1"},QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories));
    status->setText(q_("Reading installed file headers…"));
    QTimer::singleShot(0,this,[this]{scanBatch();});
}
void SwissEphemerisInventory::scanBatch()
{
    const QRegularExpression asteroid("^ast([0-9]+)/(?:se([0-9]{5})|s([0-9]{6,}))(s?)\\.se1$");
    const QRegularExpression common("^(sepl|semo|seas)[_m][0-9]{2}\\.se1$");
    for(int i=0;i<100 && iterator->hasNext();++i)
    {
        const QString path=iterator->next();
        const QFileInfo info=iterator->fileInfo();
        const QString relative=QDir(directory).relativeFilePath(path);
        const auto match=asteroid.match(relative);
        const auto base=common.match(relative);
        if (!match.hasMatch() && !base.hasMatch()) continue;
        if (!info.canonicalFilePath().startsWith(QFileInfo(directory).canonicalFilePath()+"/")) continue;
        QFile file(path); const bool readable=file.open(QIODevice::ReadOnly);
        const QByteArray header=readable ? file.read(4096) : QByteArray();
        double first=0,last=0;
        const bool valid=SwissEphemerisFiles::coverage(header,relative,first,last,info.size());
        QString number,name;
        int id=0;
        if(match.hasMatch())
        {
            id=(match.captured(2).isEmpty()?match.captured(3):match.captured(2)).toInt();
            number=QString::number(id);
            const auto lines=header.split('\n');
            if(valid && lines.size()>3)
            {
                const QString elements=QString::fromLatin1(lines[3]);
                const auto prefix=QRegularExpression("^\\s*[0-9]+ ").match(elements);
                if(prefix.hasMatch()) name=elements.mid(prefix.capturedLength(),19).trimmed();
            }
        }
        else number=base.captured(1)=="seas" ? q_("Asteroids 1–4") : base.captured(1)=="sepl" ? q_("Planets") : q_("Moon");
        auto* item=new FileItem(id?individual:shared,{number,name,valid?dateText(first)+" … "+dateText(last):q_("Unreadable / invalid header"),sizeText(info.size()),relative});
        item->setData(0,Qt::UserRole,relative);
        item->setData(1,Qt::UserRole,id);
        item->setData(3,Qt::UserRole,info.size());
        totalBytes+=info.size(); ++scanned;
    }
    if(iterator->hasNext()) { status->setText(q_("Reading installed file headers… %1 files (%2)").arg(scanned).arg(sizeText(totalBytes))); QTimer::singleShot(0,this,[this]{scanBatch();}); return; }
    iterator.reset(); tree->setSortingEnabled(true); tree->sortItems(0,Qt::AscendingOrder); select->setEnabled(true); refresh->setEnabled(true);
    updateSelection();
}
void SwissEphemerisInventory::updateSelection()
{
    if(iterator || deleting) return;
    qint64 bytes=0; const auto items=tree->selectedItems();
    for(auto* item:items) bytes+=item->data(3,Qt::UserRole).toLongLong();
    status->setText(q_("Installed: %1 files, %2. Selected: %3 files, %4.").arg(scanned).arg(sizeText(totalBytes)).arg(items.size()).arg(sizeText(bytes)));
    remove->setEnabled(!items.isEmpty());
}
void SwissEphemerisInventory::selectRange()
{
    const auto values=SwissEphemerisFiles::parseNumbers(range->text());
    if(values.isEmpty()) { status->setText(q_("Enter numbers or ranges such as 1001-10000 (up to 10000 numbers per selection).")); return; }
    const QSet<int> selected(values.begin(),values.end());
    tree->blockSignals(true); tree->clearSelection();
    for(int i=0;i<individual->childCount();++i)
    {
        auto* item=individual->child(i);
        item->setSelected(selected.contains(item->data(1,Qt::UserRole).toInt()));
    }
    tree->blockSignals(false); updateSelection();
}
void SwissEphemerisInventory::removeSelected()
{
    pending.clear(); qint64 bytes=0; bool hasShared=false;
    for(auto* item:tree->selectedItems())
    {
        pending.append(item->data(0,Qt::UserRole).toString());
        bytes+=item->data(3,Qt::UserRole).toLongLong();
        hasShared|=item->parent()==shared;
    }
    if(pending.isEmpty()) return;
    QString message=q_("Permanently delete %1 selected files (%2)? Objects stay in the catalogue. Missing ephemerides fall back to Kepler elements. Files can be downloaded again.").arg(pending.size()).arg(sizeText(bytes));
    if(hasShared) message+="\n\n"+q_("Your selection includes shared data. Removing planetary or lunar files can disable Swiss positions for many retained asteroids. Removing seas files affects asteroids 1–4.");
    if(QMessageBox::question(this,q_("Delete ephemerides"),message,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) {pending.clear(); return;}
    deleting=true; deleted=failed=0; deletedBytes=0; deletionErrors.clear();
    tree->setEnabled(false); select->setEnabled(false); refresh->setEnabled(false); remove->setEnabled(false);
    QFile file(directory+"/downloads.json");
    manifest=file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object():QJsonObject();
    removeBatch();
}
void SwissEphemerisInventory::removeBatch()
{
    std::vector<std::string> batch; QMap<QString,qint64> sizes;
    for(int i=0;i<64 && !pending.isEmpty();++i)
    {
        const QString path=pending.takeFirst(); batch.push_back(path.toUtf8().toStdString());
        sizes.insert(path,QFileInfo(QDir(directory).filePath(path)).size());
    }
    const auto errors=SwissEphemeris::removeFiles(batch);
    for(const auto& name:batch)
    {
        if(std::find(errors.begin(),errors.end(),name)!=errors.end()) {++failed; if(deletionErrors.size()<10) deletionErrors.append(QString::fromUtf8(name.c_str())); continue;}
        const QString path=QString::fromUtf8(name.c_str());
        ++deleted; deletedBytes+=sizes.value(path); manifest.remove(path);
    }
    status->setText(q_("Deleted %1 files (%2). Failed: %3. Remaining: %4.").arg(deleted).arg(sizeText(deletedBytes)).arg(failed).arg(pending.size()));
    if(!pending.isEmpty()) {QTimer::singleShot(0,this,[this]{removeBatch();}); return;}
    QSaveFile file(directory+"/downloads.json"); const auto json=QJsonDocument(manifest).toJson();
    const bool saved=file.open(QIODevice::WriteOnly) && file.write(json)==json.size() && file.commit();
    deleting=false; tree->setEnabled(true);
    QString result=q_("Deleted %1 files; released %2. Failed: %3. Positions will refresh automatically.").arg(deleted).arg(sizeText(deletedBytes)).arg(failed);
    if(!deletionErrors.isEmpty()) result+="\n\n"+q_("Files that could not be deleted (first 10):")+"\n"+deletionErrors.join("\n");
    if(!saved) result+="\n"+q_("Could not update download provenance.");
    QMessageBox::information(this,q_("Ephemeris deletion"),result);
    scan();
}
