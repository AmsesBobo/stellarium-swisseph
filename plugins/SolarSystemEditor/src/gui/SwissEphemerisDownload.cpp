/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemerisDownload.hpp"
#include "SwissEphemerisFiles.hpp"
#include "SwissEphemerisInventory.hpp"
#include "SwissEphemeris.hpp"
#include "StelFileMgr.hpp"
#include "StelApp.hpp"
#include "StelModuleMgr.hpp"
#include "SolarSystem.hpp"
#include <QCheckBox>
#include "StelUtils.hpp"
#include "StelTranslator.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QTimer>

namespace
{
const QString mainBase = "https://raw.githubusercontent.com/aloistr/swisseph/aacf962d19d79f8bc921dbcdaacf306d85be1917/ephe/";
const QString asteroidBase = "https://ephe.scryr.io/ephe/";
constexpr qint64 maximumFileSize = 32*1024*1024;
}

SwissEphemerisDownload::SwissEphemerisDownload(QWidget* parent) : QWidget(parent)
{
    directory = StelFileMgr::getUserDir()+"/ephemeris/swiss";
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(q_("Download ephemerides for numbered asteroids. Objects must also exist in the Solar System catalogue. Outside available coverage, Stellarium uses Kepler elements."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* solar = GETSTELMODULE(SolarSystem);
    auto* hideMissing = new QCheckBox(q_("Hide asteroids without usable ephemerides"), this);
    hideMissing->setObjectName("swissHideMissing");
    hideMissing->setToolTip(q_("Hide asteroids whose current position uses Kepler elements, including dates outside installed coverage. Catalogue searches and calculated AstroCalc ephemeris markers remain available."));
    hideMissing->setChecked(solar->getFlagHideAsteroidsWithoutEphemeris());
    hideMissing->setEnabled(SwissEphemeris::isEnabled());
    connect(hideMissing, &QCheckBox::toggled, solar, &SolarSystem::setFlagHideAsteroidsWithoutEphemeris);
    connect(solar, &SolarSystem::flagHideAsteroidsWithoutEphemerisChanged, hideMissing, &QCheckBox::setChecked);
    layout->addWidget(hideMissing);
    auto* location = new QLineEdit(QDir::toNativeSeparators(directory), this);
    location->setReadOnly(true);
    auto* form = new QFormLayout;
    form->addRow(q_("Data directory:"), location);
    numbers = new QLineEdit("1-5,433", this);
    numbers->setObjectName("swissNumbers");
    form->addRow(q_("Asteroid numbers:"), numbers);
    firstYear = new QSpinBox(this);
    lastYear = new QSpinBox(this);
    firstYear->setObjectName("swissFirstYear");
    lastYear->setObjectName("swissLastYear");
    for (auto* year : {firstYear,lastYear}) year->setRange(-2999,2999);
    firstYear->setValue(1500);
    lastYear->setValue(2100);
    form->addRow(q_("From year (0 = 1 BCE):"), firstYear);
    form->addRow(q_("Through year:"), lastYear);
    layout->addLayout(form);
    auto* note = new QLabel(q_("Whole files are downloaded. Supporting data use 600-year blocks. Individual files prefer long coverage, with short files as fallback. Actual coverage is reported below; unavailable periods remain on Kepler elements."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QHBoxLayout;
    download = new QPushButton(q_("Download missing files"), this);
    download->setObjectName("swissDownload");
    cancel = new QPushButton(q_("Cancel"), this);
    cancel->setObjectName("swissCancel");
    cancel->setEnabled(false);
    buttons->addWidget(download);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    installed = new QPushButton(q_("Installed ephemerides…"),this);
    installed->setObjectName("swissInstalled");
    layout->addWidget(installed);
    connect(installed,&QPushButton::clicked,this,[this]{
        SwissEphemerisInventory inventory(directory,this);
        inventory.exec();
    });
    progress = new QProgressBar(this);
    layout->addWidget(progress);
    log = new QPlainTextEdit(this);
    log->setObjectName("swissLog");
    log->setReadOnly(true);
    log->setMaximumBlockCount(20000);
    layout->addWidget(log);
    manifestTimer = new QTimer(this);
    manifestTimer->setInterval(5000);
    connect(manifestTimer,&QTimer::timeout,this,[this]{ saveManifest(); });
    network = new QNetworkAccessManager(this);
    connect(download,&QPushButton::clicked,this,[this]{ start(); });
    connect(cancel,&QPushButton::clicked,this,[this]{
        stopping=true;
        if (reply) reply->abort();
        // If checking local files, the already queued next() observes stopping.
    });
    if (SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::TooLong)
    {
        log->appendPlainText(q_("The ephemeris directory path is too long for Swiss Ephemeris (maximum 242 bytes). Choose a shorter Stellarium user directory. Positions use Kepler elements until this is corrected."));
        download->setEnabled(false);
    }
    else if (SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::Invalid)
    {
        log->appendPlainText(q_("The ephemeris directory contains a character that Swiss Ephemeris treats as a search-path separator. Choose another Stellarium user directory. Positions use Kepler elements until this is corrected."));
        download->setEnabled(false);
    }
    if (!qgetenv("SE_EPHE_PATH").isEmpty())
        log->appendPlainText(q_("SE_EPHE_PATH is ignored: Stellarium uses its own ephemeris directory."));
    if (!qgetenv("SWEPH_PATH").isEmpty())
    {
        log->appendPlainText(q_("SWEPH_PATH overrides the user directory. Remove that environment override and restart Stellarium before using this downloader."));
        download->setEnabled(false);
        installed->setEnabled(false);
    }
}

SwissEphemerisDownload::~SwissEphemerisDownload()
{
    manifestTimer->stop();
    if (reply)
    {
        disconnect(reply,nullptr,this,nullptr);
        reply->abort();
    }
    QString error;
    if (!manifest.flush(error)) qWarning()<<"Swiss download metadata could not be saved on shutdown:"<<error;
}

bool SwissEphemerisDownload::saveManifest()
{
    QString error;
    if (manifest.flush(error))
    {
        manifestFailureReported=false;
        return true;
    }
    if (!manifestFailureReported)
        log->appendPlainText(q_("Could not save download metadata. Completed ephemeris files are retained; saving will be retried.")+" "+error);
    manifestFailureReported=true;
    return false;
}

void SwissEphemerisDownload::start()
{
    const auto selected = SwissEphemerisFiles::parseNumbers(numbers->text());
    if (selected.isEmpty() || firstYear->value()>lastYear->value())
    {
        log->appendPlainText(q_("Enter positive asteroid numbers or ranges (e.g. 1-1000,433), at most 10000 per batch, and a valid year interval."));
        return;
    }
    if (!QDir().mkpath(directory))
    {
        log->appendPlainText(q_("Cannot create data directory: ")+directory);
        return;
    }
    // Retry a failed final checkpoint before loading disk state or starting work.
    if (!saveManifest()) return;
    QString manifestError;
    if (!manifest.load(directory+"/downloads.json",manifestError))
    {
        log->appendPlainText(q_("Cannot read download metadata: ")+manifestError);
        return;
    }
    jobs.clear();
    // A selected year means the whole year. Blocks begin at multiples of 600.
    const int firstBlock = int(std::floor(firstYear->value()/600.))*6;
    const int lastBlock = int(std::floor(lastYear->value()/600.))*6;
    for (int century=firstBlock; century<=lastBlock; century+=6)
    {
        const QString suffix = QString("%1%2.se1").arg(century<0 ? "m" : "_").arg(std::abs(century),2,10,QLatin1Char('0'));
        for (const auto& kind : {QString("sepl"),QString("semo"),QString("seas")})
            if (kind!="seas" || selected.first()<=4) jobs.enqueue({kind+suffix,false});
    }
    for (int number : selected)
        if (number>4 && number!=134340) jobs.enqueue({SwissEphemerisFiles::asteroidPath(number),true});
    if (selected.contains(134340))
        log->appendPlainText(q_("134340 Pluto uses Stellarium's planetary ephemeris and is skipped."));
    total=jobs.size(); completed=0; failures=0; stopping=false;
    progress->setRange(0,total); progress->setValue(0);
    download->setEnabled(false); installed->setEnabled(false); cancel->setEnabled(true);
    numbers->setEnabled(false); firstYear->setEnabled(false); lastYear->setEnabled(false);
    log->appendPlainText(q_("Checking %1 files; valid installed files are reused.").arg(total));
    manifestTimer->start();
    next();
}

void SwissEphemerisDownload::reportFile(const QString& path, const QByteArray& data)
{
    double first=0, last=0;
    SwissEphemerisFiles::coverage(data,path,first,last);
    int y1,m1,d1,y2,m2,d2;
    StelUtils::getDateFromJulianDay(first,&y1,&m1,&d1);
    StelUtils::getDateFromJulianDay(last,&y2,&m2,&d2);
    log->appendPlainText(QString("%1: %2 KiB; TT %3-%4-%5 … %6-%7-%8")
        .arg(path).arg(data.size()/1024.).arg(y1).arg(m1,2,10,QLatin1Char('0')).arg(d1,2,10,QLatin1Char('0'))
        .arg(y2).arg(m2,2,10,QLatin1Char('0')).arg(d2,2,10,QLatin1Char('0')));
    double requestedFirst=0, requestedLast=0;
    StelUtils::getJDFromDate(&requestedFirst,firstYear->value(),1,1,0,0,0);
    StelUtils::getJDFromDate(&requestedLast,lastYear->value()+1,1,1,0,0,0);
    if (path.startsWith("ast") && (first>requestedFirst || last<requestedLast))
        log->appendPlainText(q_("Coverage warning: this asteroid file does not cover the entire requested interval."));
}

void SwissEphemerisDownload::next()
{
    if (stopping || jobs.isEmpty()) { finish(stopping); return; }
    const Job job = jobs.dequeue();
    QFile installed(directory+"/"+job.path);
    double first=0,last=0;
    if (installed.size()<=maximumFileSize && installed.open(QIODevice::ReadOnly))
    {
        const auto data=installed.readAll();
        if (SwissEphemerisFiles::coverage(data,job.path,first,last))
        {
            reportFile(job.path,data);
            if (!manifest.contains(job.path))
            {
                // Recovery after a crash between data commit and checkpoint.
                // The original retrieval time/HTTP headers cannot be reconstructed.
                manifest.insert(job.path,QJsonObject{{"bytes",double(data.size())},
                    {"sha256",QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())},
                    {"verified_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {"start_jde",first},{"end_jde",last}});
                if (manifest.checkpointDue()) saveManifest();
            }
            progress->setValue(++completed);
            QTimer::singleShot(0,this,[this]{ next(); });
            return;
        }
    }
    const QUrl url((job.path.startsWith("ast") ? asteroidBase : mainBase)+job.path);
    log->appendPlainText(q_("Downloading: ")+job.path);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    reply=network->get(request);
    // Bound both duration and memory, including servers that omit Content-Length.
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,reply,&QNetworkReply::abort);
    timeout->start(90000);
    connect(reply,&QNetworkReply::downloadProgress,this,[this](qint64 received,qint64 size){
        progress->setFormat(QString("%1/%2 — %3 / %4 KiB").arg(completed).arg(total).arg(received/1024).arg(size<0 ? "?" : QString::number(size/1024)));
        if (received>maximumFileSize || size>maximumFileSize) reply->abort();
    });
    connect(reply,&QIODevice::readyRead,this,[this]{
        if (reply->bytesAvailable()>maximumFileSize) reply->abort();
    });
    connect(reply,&QNetworkReply::finished,this,[this,job,url]{
        auto* response=reply.data();
        reply=nullptr;
        const int status=response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error=response->error();
        const QString reason=response->errorString();
        const auto data=response->isOpen() ? response->readAll() : QByteArray();
        const QString modified=QString::fromLatin1(response->rawHeader("Last-Modified"));
        response->deleteLater();
        if (stopping) { finish(true); return; }
        if (status==404 && job.shortFallback)
        {
            QString shortPath=job.path;
            shortPath.insert(shortPath.size()-4,'s');
            jobs.prepend({shortPath,false});
            next();
            return;
        }
        double first=0,last=0;
        if (error!=QNetworkReply::NoError || status!=200 || data.size()>maximumFileSize ||
            !SwissEphemerisFiles::coverage(data,job.path,first,last))
        {
            ++failures;
            log->appendPlainText(q_("Not installed: %1 — %2. Kepler fallback remains available.").arg(job.path,
                status==404 ? q_("file not available") : (error!=QNetworkReply::NoError ? reason : q_("invalid ephemeris header or length"))));
        }
        else
        {
            const QString target=directory+"/"+job.path;
            QSaveFile file(target);
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())
            {
                ++failures;
                log->appendPlainText(q_("Cannot save file: ")+target);
            }
            else
            {
                reportFile(job.path,data);
                manifest.insert(job.path,QJsonObject{{"url",url.toString()},{"bytes",double(data.size())},
                    {"sha256",QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())},
                    {"retrieved_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {"last_modified",modified},{"start_jde",first},{"end_jde",last}});
                if (manifest.checkpointDue()) saveManifest();
            }
        }
        progress->setValue(++completed);
        QTimer::singleShot(0,this,[this]{ next(); });
    });
}

void SwissEphemerisDownload::finish(bool cancelled)
{
    jobs.clear();
    manifestTimer->stop();
    const bool metadataSaved=saveManifest();
    if (!metadataSaved)
        log->appendPlainText(q_("Download metadata are still unsaved. Start the download again to retry saving before managing installed files."));
    SwissEphemeris::reload();
    progress->setFormat("%v/%m");
    download->setEnabled(true); installed->setEnabled(metadataSaved); cancel->setEnabled(false);
    numbers->setEnabled(true); firstYear->setEnabled(true); lastYear->setEnabled(true);
    log->appendPlainText(cancelled ? q_("Cancelled. Completed files have been retained.") : q_("Finished. %1 files unavailable or failed.").arg(failures));
    log->appendPlainText(q_("Installed data are ready. Positions will refresh automatically."));
}
