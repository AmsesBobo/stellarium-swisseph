/* SPDX-License-Identifier: GPL-2.0-or-later */
// Run the real application initialization in an isolated temporary profile.
// This integration test needs the same OpenGL/display support as Stellarium.
#include "SwissEphemeris.hpp"
#include "StelApp.hpp"
#include "StelFileMgr.hpp"
#include "StelModuleMgr.hpp"
#include "SolarSystem.hpp"
#include "Planet.hpp"
#include "StelCore.hpp"
#include "../../plugins/SolarSystemEditor/src/gui/SwissEphemerisDownload.hpp"
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QDebug>
#include <stdexcept>

#define main stellariumApplicationMain
#include "../main.cpp"
#undef main

namespace {
bool passed=false;
bool completed=false;
bool running=false;
QString backup;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void same(const Vec3d& a,const Vec3d& b,const char* message) {check((a-b).norm()<1e-12,message);}
#ifdef TEST_SWISS_DOWNLOADER
void runManifestRegression()
{
    const QString path=StelFileMgr::getUserDir()+"/ephemeris/swiss/downloads.json";
    const auto readManifest=[&] {
        QFile file(path); check(file.open(QIODevice::ReadOnly),"Manifest checkpoint missing");
        return QJsonDocument::fromJson(file.readAll()).object();
    };
    const auto configure=[](SwissEphemerisDownload& widget) {
        widget.findChild<QLineEdit*>("swissNumbers")->setText("1-4");
        widget.findChild<QSpinBox*>("swissFirstYear")->setValue(1800);
        widget.findChild<QSpinBox*>("swissLastYear")->setValue(2399);
    };
    const auto drain=[](SwissEphemerisDownload& widget) {
        QElapsedTimer timeout; timeout.start();
        while(widget.findChild<QPushButton*>("swissCancel")->isEnabled() && timeout.elapsed()<2000)
            QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        check(!widget.findChild<QPushButton*>("swissCancel")->isEnabled(),"Offline download queue did not finish");
    };
    {
        SwissEphemerisDownload widget; configure(widget);
        widget.findChild<QPushButton*>("swissDownload")->click();
        check(!QFile::exists(path),"Manifest written for every individual file");
        bool timerFound=false;
        for(auto* timer:widget.findChildren<QTimer*>())
            if(timer->interval()==5000)
            {
                timerFound=true;
                check(QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection),"Cannot exercise timer checkpoint");
            }
        check(timerFound && readManifest().size()==1,"Timer did not save partial batch");
        widget.findChild<QPushButton*>("swissCancel")->click(); drain(widget);
        check(readManifest().size()==1,"Cancellation discarded completed metadata");
        widget.findChild<QPushButton*>("swissDownload")->click(); drain(widget);
        check(readManifest().size()==3,"Resume did not recover installed files");
    }
    check(QFile::remove(path),"Cannot reset temporary manifest");
    {
        SwissEphemerisDownload widget; configure(widget);
        widget.findChild<QPushButton*>("swissDownload")->click();
        widget.findChild<QPushButton*>("swissCancel")->click(); drain(widget);
        check(readManifest().size()==1,"Cancellation did not flush unsaved partial batch");
    }
    check(QFile::remove(path),"Cannot reset temporary manifest");
    {
        SwissEphemerisDownload widget; configure(widget);
        widget.findChild<QPushButton*>("swissDownload")->click();
        check(!QFile::exists(path),"Unexpected immediate checkpoint");
    }
    const auto recovered=readManifest().value("sepl_18.se1").toObject();
    check(recovered.contains("verified_utc") && !recovered.contains("retrieved_utc"),"Recovery fabricated retrieval provenance");
    qInfo("PASS: downloader timer, cancellation, resume, recovery and destruction checkpoints using installed files");
}
#endif
void runRegression()
{
    auto* solar=GETSTELMODULE(SolarSystem);
    auto asteroid=solar->searchByEnglishName("(5) Astraea");
    check(!asteroid.isNull(),"Astraea missing from catalogue");
    const double currentJD=2451545.125, orbitJD=currentJD+10.;
    // Populate both Planet caches, including the current-date shortcut.
    asteroid->computePosition(nullptr,currentJD,Vec3d(0.));
    solar->setFlagHideAsteroidsWithoutEphemeris(true);
    check(solar->isBodyVisibleWithEphemerisFilter(asteroid.data()),"Covered asteroid hidden");
    check(solar->isBodyVisibleWithEphemerisFilter(solar->searchByEnglishName("Moon").data()),"Moon hidden by asteroid filter");
#ifdef TEST_SWISS_DOWNLOADER
    {
        SwissEphemerisDownload widget;
        auto* toggle=widget.findChild<QCheckBox*>("swissHideMissing");
        check(toggle && toggle->isChecked(),"Filter checkbox does not reflect setting");
        toggle->setChecked(false);
        check(!solar->getFlagHideAsteroidsWithoutEphemeris(),"Checkbox did not update filter");
        solar->setFlagHideAsteroidsWithoutEphemeris(true);
        check(toggle->isChecked(),"Checkbox did not follow property change");
    }
#endif
    const Vec3d original=asteroid->getEclipticPos(currentJD);
    const Vec3d originalOrbit=asteroid->getEclipticPos(orbitJD);
    Vec3d direct,velocity;
    asteroid->computePosition(currentJD,direct,velocity);
    same(original,direct,"Initial current position mismatch");
    check(SwissEphemeris::removeFiles({"ast0/se00005.se1"}).empty(),"Temporary ephemeris deletion failed");
    check(!solar->isBodyVisibleWithEphemerisFilter(asteroid.data()),"Deleted ephemeris still visible before refresh");
    asteroid->computePosition(currentJD,direct,velocity); // uncached Kepler result
    check((original-direct).norm()>1e-9,"Fixture does not distinguish Swiss and Kepler");
    same(asteroid->getEclipticPos(currentJD),direct,"Stale current-date shortcut after deletion");
    asteroid->computePosition(nullptr,currentJD,Vec3d(0.)); // SAME date: must bypass deltaJDE
    same(asteroid->getHeliocentricEclipticPos(),direct,"Stored current position not refreshed");
    check(!solar->isBodyVisibleWithEphemerisFilter(asteroid.data()),"Kepler fallback not hidden");
    solar->setFlagPlanets(true);
    const auto* pickingCore=StelApp::getInstance().getCore();
    Vec3d direction=asteroid->getJ2000EquatorialPos(pickingCore);
    direction.normalize();
    check(!solar->searchAround(direction,180.,pickingCore).contains(asteroid),"Hidden asteroid can be picked");
    solar->setFlagHideAsteroidsWithoutEphemeris(false);
    check(solar->isBodyVisibleWithEphemerisFilter(asteroid.data()),"Disabled filter hides asteroid");
    check(solar->searchAround(direction,180.,pickingCore).contains(asteroid),"Disabled filter excludes asteroid from picking");
    solar->setFlagHideAsteroidsWithoutEphemeris(true);
    same(asteroid->getEclipticVelocity(),velocity,"Stored current velocity not refreshed");
    asteroid->computePosition(orbitJD,direct,velocity);
    check((originalOrbit-direct).norm()>1e-9,"Orbit fixture does not distinguish sources");
    same(asteroid->getEclipticPos(orbitJD),direct,"Stale orbit samples after deletion");

    const QString destination=StelFileMgr::getUserDir()+"/ephemeris/swiss/ast0/se00005.se1";
    check(QFile::copy(backup,destination),"Cannot reinstall temporary ephemeris");
    SwissEphemeris::reload(); // same publication path used after GUI downloads
    same(asteroid->getEclipticPos(currentJD),original,"Stale current-date shortcut after installation");
    same(asteroid->getEclipticPos(orbitJD),originalOrbit,"Stale orbit samples after installation");
    asteroid->computePosition(nullptr,currentJD,Vec3d(0.));
    same(asteroid->getHeliocentricEclipticPos(),original,"Stored current position not refreshed after installation");
    check(solar->isBodyVisibleWithEphemerisFilter(asteroid.data()),"Reinstalled asteroid remains hidden");
    asteroid->computePosition(currentJD,direct,velocity);
    same(asteroid->getEclipticVelocity(),velocity,"Stored current velocity not refreshed after installation");
    // Use the real SolarSystem light-time iterations at both coverage edges.
    asteroid=solar->searchByEnglishName("(1) Ceres");
    check(!asteroid.isNull(),"Ceres missing from catalogue");
    auto* core=StelApp::getInstance().getCore();
    SwissEphemeris::State coverage;
    check(SwissEphemeris::calculate(1,currentJD,coverage),"Cannot read fixture coverage");
    for (bool startEdge : {true,false})
    {
        double edge=coverage.startJDE;
        // Ceres uses the 1800-2399 seas file. Swiss selects seas_24 for
        // a fresh lookup in 2400, before the overlapping raw file end.
        if (!startEdge) check(StelUtils::getJDFromDate(&edge,2400,1,1,0,0,0),"Invalid boundary date");
        const double observingJD=edge+1e-6;
        core->setJDE(observingJD);
        solar->setFlagLightTravelTime(false);
        // The base info map computes rise/set times on a date change, temporarily
        // updating the whole simulation. Warm that unrelated cache first, then
        // clear Swiss state to make the missing next file deterministic.
        asteroid->getInfoMap(core);
        SwissEphemeris::reload();
        solar->computePositions(core,observingJD,core->getCurrentPlanet());
        auto info=asteroid->getInfoMap(core);
        check(info.value("position-source").toString()==(startEdge ? "Swiss Ephemeris" : "Kepler elements"),
              "Incorrect source without light-time correction at boundary");
        solar->setFlagLightTravelTime(true);
        solar->computePositions(core,observingJD,core->getCurrentPlanet());
        info=asteroid->getInfoMap(core);
        const QString expected=startEdge ? "Kepler elements" : "Swiss Ephemeris";
        qInfo().noquote()<<QString("Boundary start=%1 edge=%2 observation=%3 position=%4 source=%5").arg(startEdge).arg(edge,0,'f',9).arg(observingJD,0,'f',9).arg(info.value("position-source-jde").toDouble(),0,'f',9).arg(info.value("position-source").toString());
        check(info.value("position-source").toString()==expected,"Source follows observing date rather than retarded position");
        check(solar->isBodyVisibleWithEphemerisFilter(asteroid.data()) == !startEdge,
              "Visibility ignores light-time-corrected source at coverage boundary");
        const double positionJD=info.value("position-source-jde").toDouble();
        check(positionJD<edge && observingJD-positionJD<1.,"Fixture did not cross coverage edge through light time");
        check(info.value("position-source-revision").toULongLong()==SwissEphemeris::dataRevision(),"Wrong position revision");
        if (!startEdge)
            check(info.value("ephemeris-file").toString()==QString::fromStdString(coverage.filePath),"Wrong retained source file");
        else
            check(!info.contains("ephemeris-file"),"Kepler fallback retains Swiss metadata");
        asteroid->computePosition(startEdge ? currentJD : coverage.endJDE+1.,direct,velocity);
        asteroid->getEclipticPos(startEdge ? currentJD : coverage.endJDE+1.);
        asteroid->hasValidPositionalData(currentJD,Planet::Position);
        const QString html=asteroid->getInfoString(core,StelObject::Extra);
        check(html.contains(startEdge ? "Kepler" : "Swiss Ephemeris (DE"),"Displayed source disagrees with stored position");
        const auto after=asteroid->getInfoMap(core);
        check(after.value("position-source")==info.value("position-source") &&
              after.value("position-source-jde")==info.value("position-source-jde"),"Off-date query overwrote position provenance");
    }
    // Reload alone does not change the stored position or its provenance.
    const auto beforeReload=asteroid->getInfoMap(core);
    SwissEphemeris::reload();
    const auto afterReload=asteroid->getInfoMap(core);
    check(afterReload.value("position-source-revision")==beforeReload.value("position-source-revision"),
          "Info relabelled an old position with a new data revision");
    asteroid->computePosition(nullptr,beforeReload.value("position-source-jde").toDouble(),Vec3d(0.));
    check(asteroid->getInfoMap(core).value("position-source-revision").toULongLong()==SwissEphemeris::dataRevision(),
          "Position provenance did not refresh after reload");
    qInfo("PASS: stored position provenance, both light-time coverage boundaries, off-date isolation and reload revision");
#ifdef TEST_SWISS_DOWNLOADER
    runManifestRegression();
#else
    qInfo("SKIP: downloader lifecycle checks require SolarSystemEditor");
#endif
    solar->setFlagHideAsteroidsWithoutEphemeris(false);
    qInfo("PASS: ephemeris visibility filter, GUI binding, missing files, reload, picking and light-time boundaries");
    passed=true;
    qInfo("PASS: real Planet current state and orbit caches refresh after deletion and reinstallation at identical dates");
}
void installRegressionTimer()
{
    auto* timer=new QTimer(QCoreApplication::instance());
    QObject::connect(timer,&QTimer::timeout,timer,[timer]{
        // Startup pumps events before app.exec(); an early quit alone is lost.
        // Keep requesting exit until the application's main loop is running.
        if(completed) { QCoreApplication::quit(); return; }
        if(running || !StelApp::isInitialized()) return;
        running=true;
        try {runRegression();} catch(const std::exception& error) {qCritical()<<error.what();}
        completed=true;
        QCoreApplication::quit();
    });
    timer->start(100);
}
}
Q_COREAPP_STARTUP_FUNCTION(installRegressionTimer)

int main(int argc,char** argv)
{
    if(argc!=2) {qCritical("Provide a fixture ephemeris directory");return 2;}
    QTemporaryDir profile;
    if(!profile.isValid()) return 2;
    const QString fixtures=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString data=profile.path()+"/ephemeris/swiss";
    QDir().mkpath(data+"/ast0");
    for(const QString file:{"sepl_18.se1","semo_18.se1","seas_18.se1","ast0/se00005.se1"})
        if(!QFile::copy(fixtures+"/"+file,data+"/"+file)) return 2;
    backup=profile.path()+"/asteroid-backup.se1";
    if(!QFile::copy(data+"/ast0/se00005.se1",backup)) return 2;
    // Never use or write the normal user profile, even during early startup.
    qputenv("STEL_USERDIR",QFile::encodeName(profile.path()));
    qunsetenv("SWEPH_PATH"); qunsetenv("SE_EPHE_PATH"); qunsetenv("SWEPH_EXACT");
    QSettings settings(profile.path()+"/config.ini",QSettings::IniFormat);
    settings.setValue("video/fullscreen",false);
    settings.setValue("video/screen_w",800); settings.setValue("video/screen_h",600);
    settings.setValue("main/check_requirements",false); settings.sync();
    QByteArray program=QFile::encodeName(QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath());
    char* arguments[]={program.data(),nullptr};
    stellariumApplicationMain(1,arguments);
    return passed ? 0 : 1;
}
