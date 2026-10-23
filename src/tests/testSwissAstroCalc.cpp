/* SPDX-License-Identifier: GPL-2.0-or-later */
// Full-application test: operate the real AstroCalc controls, not a replica loop.
#include "SwissEphemeris.hpp"
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelModuleMgr.hpp"
#include "SolarSystem.hpp"
#include "AstroCalcDialog.hpp"
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QListWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QCryptographicHash>
#include <QSignalBlocker>
#include <QDebug>
#include <stdexcept>
#define main stellariumApplicationMain
#include "../main.cpp"
#undef main

namespace {
bool passed=false, completed=false, running=false;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
class TestDialog : public AstroCalcDialog
{
public:
    TestDialog() : AstroCalcDialog(nullptr) {}
    template<class T> T* control(const char* name)
    {
        auto* widget=dialog->findChild<T*>(name);
        check(widget,"Missing AstroCalc control");
        return widget;
    }
    void spin(const char* name,int value) { auto* w=control<QSpinBox>(name); QSignalBlocker block(w); w->setValue(value); }
    void combo(const char* name,const QVariant& value)
    {
        auto* w=control<QComboBox>(name); QSignalBlocker block(w);
        const int index=w->findData(value); check(index>=0,"Missing combo choice"); w->setCurrentIndex(index);
    }
    void toggle(const char* name,bool value) { auto* w=control<QCheckBox>(name); QSignalBlocker block(w); w->setChecked(value); }
};
void checkDateScrolling(TestDialog& dialog,StelCore* core)
{
    // Changing dates after generating markers must not run the hidden WUT
    // catalogue calculation (which temporarily updates every solar-system body).
    auto* pages=dialog.control<QListWidget>("stackListWidget");
    auto* wut=dialog.control<QTreeWidget>("wutMatchingObjectsTreeWidget");
    auto* categories=dialog.control<QListWidget>("wutCategoryListWidget");
    const auto planetCategories=categories->findItems(q_("Planets"),Qt::MatchExactly);
    check(planetCategories.size()==1,"Missing WUT planets category");
    categories->setCurrentItem(planetCategories.first());
    pages->setCurrentRow(1); // Ephemeris page, with generated markers retained.
    double ancientJD=0.;
    StelUtils::getJDFromDate(&ancientJD,8,1,1,0,0,0.);
    core->setJD(ancientJD);
    const QString sentinel="hidden-WUT-must-not-refresh";
    auto* today=dialog.control<QLabel>("labelToday");
    bool untouched=true;
    for (bool visible : {true,false})
    {
        dialog.setVisible(visible);
        wut->clear();
        new QTreeWidgetItem(wut,QStringList{sentinel});
        today->setText(sentinel);
        QElapsedTimer elapsed; elapsed.start();
        for(int step=0;step<10;++step) core->addSolarDays(1.);
        const bool retained=!wut->findItems(sentinel,Qt::MatchExactly).isEmpty();
        qInfo()<<"WUT date-scroll probe"<<"dialog visible"<<visible
               <<"elapsed ms"<<elapsed.elapsed()<<"hidden table unchanged"<<retained;
        const bool almanacRetained=today->text()==sentinel;
        qInfo()<<"Hidden almanac unchanged"<<almanacRetained;
        untouched=untouched && retained && almanacRetained;
    }
    check(untouched,"Date scrolling recalculated a hidden AstroCalc page");
    dialog.setVisible(true);
    pages->setCurrentRow(5); // Entering WUT must refresh deferred data.
    check(wut->findItems(sentinel,Qt::MatchExactly).isEmpty(),"WUT did not refresh on entry");
    wut->clear(); new QTreeWidgetItem(wut,QStringList{sentinel});
    core->addSolarDays(1.);
    check(wut->findItems(sentinel,Qt::MatchExactly).isEmpty(),"Visible WUT did not refresh on date change");
    dialog.setVisible(false);
    wut->clear(); new QTreeWidgetItem(wut,QStringList{sentinel});
    core->addSolarDays(1.);
    check(!wut->findItems(sentinel,Qt::MatchExactly).isEmpty(),"Closed WUT recalculated on date change");
    dialog.setVisible(true);
    check(wut->findItems(sentinel,Qt::MatchExactly).isEmpty(),"WUT did not refresh on reopening");
    pages->setCurrentRow(8); // Almanac must catch up after hidden date changes.
    check(today->text()!=sentinel,"Almanac did not refresh on entry");
    today->setText(sentinel);
    core->addSolarDays(1.);
    check(today->text()!=sentinel,"Visible almanac did not refresh on date change");
    dialog.setVisible(false);
    today->setText(sentinel);
    core->addSolarDays(1.);
    check(today->text()==sentinel,"Closed almanac recalculated on date change");
    dialog.setVisible(true);
    check(today->text()!=sentinel,"Almanac did not refresh on reopening");
}
void checkRestrictedUpdates(StelCore* core,SolarSystem* solar)
{
    auto ceres=solar->searchByEnglishName("(1) Ceres");
    auto pallas=solar->searchByEnglishName("(2) Pallas");
    check(ceres && pallas,"Missing scope regression asteroids");
    QList<const Planet*> all;
    for(const auto& body:solar->getAllPlanets()) all.append(body.data());
    const double originalJD=core->getJD();
    for(bool lightTime:{false,true})
    {
        solar->setFlagLightTravelTime(lightTime);
        core->setJD(2378132.5); core->update(0);
        const auto untouched=pallas->getHeliocentricEclipticPos();
        Vec3d limited;
        {
            SolarSystem::ScopedPositionUpdate scope(*solar,{ceres.data(),core->getCurrentPlanet().data()});
            check(!scope.bodies().contains(pallas),"Unrelated asteroid retained in subset");
            core->setJD(2378133.5); core->update(0);
            limited=ceres->getJ2000EquatorialPos(core);
            check((untouched-pallas->getHeliocentricEclipticPos()).norm()==0.,"Subset updated unrelated asteroid");
            {
                SolarSystem::ScopedPositionUpdate inner(*solar,{pallas.data()});
                check(inner.bodies().contains(ceres) && inner.bodies().contains(pallas),"Nested scope lost targets");
            }
            core->setJD(2378132.5); core->update(0);
        }
        core->setJD(2378133.5); core->update(0);
        check((limited-ceres->getJ2000EquatorialPos(core)).norm()<1e-11,"Subset changed asteroid coordinates");
        check((untouched-pallas->getHeliocentricEclipticPos()).norm()>1e-8,"Full updates not restored on scope exit");
        for(const QString name:{"Moon","Mars","(1) Ceres"})
        {
            auto body=solar->searchByEnglishName(name);
            // Reset the native ephemeris interpolation windows as well as the
            // stored Planet positions before each independent RTS calculation.
            core->setJD(2378033.5); core->update(0);
            core->setJD(2378133.5); core->update(0);
            const auto other=pallas->getHeliocentricEclipticPos();
            const Vec4d limitedRTS=body->getRTSTime(core);
            check(std::abs(core->getJD()-2378133.5)<1e-10,"RTS failed to restore date");
            check((other-pallas->getHeliocentricEclipticPos()).norm()==0.,"RTS touched unrelated asteroid");
            core->setJD(2378033.5); core->update(0);
            core->setJD(2378133.5); core->update(0);
            Vec4d fullRTS;
            {
                SolarSystem::ScopedPositionUpdate full(*solar,all);
                fullRTS=body->getRTSTime(core);
            }
            for(int k=0;k<4;++k)
            {
                if (!(std::abs(fullRTS[k]-limitedRTS[k])<1e-8))
                    qCritical()<<"RTS comparison"<<name<<"light-time"<<lightTime<<"component"<<k
                               <<QString::number(limitedRTS[k],'g',17)<<QString::number(fullRTS[k],'g',17);
                check(std::abs(fullRTS[k]-limitedRTS[k])<1e-8,"Subset changed rise/transit/set result");
            }
        }
    }
    core->setJD(originalJD); core->update(0);
    qInfo()<<"PASS restricted/full updates and RTS; light-time on/off, nesting, restoration";
}
void runRegression()
{
    auto* core=StelApp::getInstance().getCore();
    auto* solar=GETSTELMODULE(SolarSystem);
    auto ceres=solar->searchByEnglishName("(1) Ceres");
    check(!ceres.isNull(),"Ceres missing");
    core->setTimeRate(0.);
    core->setCurrentTimeZone("UTC");
    TestDialog dialog;
    dialog.setVisible(true);
    // Keeping every body in an outer scope supplies a full-catalogue reference
    // for the same code paths, without a production runtime switch.
    std::unique_ptr<SolarSystem::ScopedPositionUpdate> fullCatalogue;
    if(qEnvironmentVariableIsSet("SWISS_WORKLOAD_FULL"))
    {
        QList<const Planet*> all;
        for(const auto& body:solar->getAllPlanets()) all.append(body.data());
        fullCatalogue=std::make_unique<SolarSystem::ScopedPositionUpdate>(*solar,all);
    }
    // Optional local reproduction with a copied catalogue and read-only data.
    if (qEnvironmentVariableIsSet("SWISS_SCROLL_PROFILE"))
    {
        auto ganymed=solar->searchByEnglishName("(1036) Ganymed");
        check(!ganymed.isNull(),"Ganymed missing from supplied catalogue");
        dialog.combo("celestialBodyComboBox",ganymed->getEnglishName());
        dialog.combo("secondaryCelestialBodyComboBox","none");
        dialog.toggle("allNakedEyePlanetsCheckBox",false);
        dialog.toggle("ephemerisIgnoreDateTestCheckBox",true);
        const bool workload=qEnvironmentVariableIsSet("SWISS_WORKLOAD");
        dialog.spin("dateFromYearSpinBox",workload ? -1999 : 8);
        dialog.spin("dateFromMonthSpinBox",1);
        dialog.spin("dateFromDaySpinBox",1);
        dialog.spin("dateFromHourSpinBox",0);
        dialog.spin("dateFromMinuteSpinBox",0);
        dialog.combo("dateToUnitsComboBox",3); // days
        dialog.spin("dateToDurationSpinBox",workload ? 365 : 30);
        dialog.combo("ephemerisStepComboBox",workload ? 6 : 14); // daily / 10 days
        QElapsedTimer workloadTimer; workloadTimer.start();
        dialog.control<QPushButton>("ephemerisPushButton")->click();
        qInfo()<<"WORKLOAD ephemeris ms"<<workloadTimer.elapsed();
        check(AstroCalcDialog::EphemerisList.size()==(workload ? 366 : 4),"Missing Ganymed markers");
        if(workload)
        {
            QByteArray coordinates;
            for(const auto& point:AstroCalcDialog::EphemerisList)
            {
                coordinates+=QByteArray::number(point.objDate,'g',17)+';';
                for(int k=0;k<3;++k) coordinates+=QByteArray::number(point.coord[k],'g',17)+';';
            }
            qInfo()<<"WORKLOAD marker SHA256"<<QCryptographicHash::hash(coordinates,QCryptographicHash::Sha256).toHex();
            double jd; StelUtils::getJDFromDate(&jd,-1999,1,1,12,0,0.);
            core->setJD(jd); core->update(0);
            for(const QString name:{"Moon","Mars"})
            {
                auto body=solar->searchByEnglishName(name);
                workloadTimer.restart();
                const auto times=body->getRTSTime(core);
                qInfo()<<"WORKLOAD RTS"<<name<<"ms"<<workloadTimer.elapsed()
                       <<QString::number(times[0],'g',17)<<QString::number(times[1],'g',17)
                       <<QString::number(times[2],'g',17)<<times[3];
            }
            dialog.setVisible(false); passed=true; return;
        }
        qInfo()<<"Scroll reproduction solar-system objects"<<solar->getAllPlanets().size();
        checkDateScrolling(dialog,core);
        dialog.setVisible(false);
        passed=true;
        return;
    }
    checkRestrictedUpdates(core,solar);
    dialog.combo("celestialBodyComboBox",ceres->getEnglishName());
    dialog.combo("secondaryCelestialBodyComboBox","none");
    dialog.toggle("allNakedEyePlanetsCheckBox",false);
    dialog.toggle("ephemerisIgnoreDateTestCheckBox",false);
    dialog.spin("dateFromHourSpinBox",0);
    dialog.spin("dateFromMinuteSpinBox",0);
    double largestAngle=0., largestStateError=0.;
    SwissEphemeris::State oldFile;
    SwissEphemeris::reload();
    check(SwissEphemeris::calculate(1,2378131.5,oldFile),"Cannot inspect 1799 file");
    for (int window : {0,1,2})
    for (bool lightTime : {false,true})
    for (bool horizontal : {false,true})
    {
        const bool dense=window!=0;
        solar->setFlagLightTravelTime(lightTime);
        // Block UI auto-regeneration while setting up the next complete case.
        dialog.toggle("ephemerisHorizontalCoordinatesCheckBox",horizontal);
        check(solar->setProperty("ephemerisHorizontalCoordinates",horizontal),"Cannot set rendering coordinate frame");
        int year=1799,month=dense ? 12 : 1,day=dense ? 31 : 1;
        if(window==2) StelUtils::getDateFromJulianDay(oldFile.asteroidEndJDE-1.,&year,&month,&day);
        dialog.spin("dateFromYearSpinBox",year);
        dialog.spin("dateFromMonthSpinBox",month);
        dialog.spin("dateFromDaySpinBox",day);
        dialog.combo("dateToUnitsComboBox",dense ? 3 : 6); // days / Julian years
        dialog.spin("dateToDurationSpinBox",dense ? 2 : 4);
        dialog.combo("ephemerisStepComboBox",dense ? 1 : 14); // 10 minutes / 10 days
        SwissEphemeris::reload();
        dialog.control<QPushButton>("ephemerisPushButton")->click();
        const auto points=AstroCalcDialog::EphemerisList;
        check(points.size()==(dense ? 289 : 147),"Missing AstroCalc points across file boundary");
        check(dialog.control<QTreeWidget>("ephemerisTreeWidget")->topLevelItemCount()==points.size(),"Table and drawing disagree");
        QSet<QString> files;
        // Reverse traversal catches stale state after crossing back into an older file.
        for (int i=points.size()-1;i>=0;--i)
        {
            const auto& point=points[i];
            check(point.sso==ceres,"Drawing point has wrong object");
            core->setJD(point.objDate);
            core->update(0);
            const Vec3d live=horizontal ? ceres->getAltAzPosAuto(core) : ceres->getJ2000EquatorialPos(core);
            const double angle=point.coord.angle(live);
            largestAngle=std::max(largestAngle,angle);
            // Horizontal transforms also use Stellarium's day/hour precession
            // and nutation caches, whose epochs depend on traversal order.
            const double tolerance=horizontal ? M_PI/(180.*3600.) : 1e-7;
            if (!(std::isfinite(angle) && angle<tolerance))
                qCritical()<<"Point mismatch"<<window<<lightTime<<horizontal<<i<<"arcsec"<<angle*180./M_PI*3600.;
            check(std::isfinite(angle) && angle<tolerance,"AstroCalc point differs from live sky beyond tolerance");
            // Sample exact raw-library states, including every dense boundary point.
            if (dense || i%10==0 || i==points.size()-1)
            {
                // The marker/live comparison above retains traversal history.
                // For the separate raw-output check, warm the info panel's RTS
                // cache first, then start a fresh position calculation. Otherwise
                // RTS can leave different overlapping files open while stored
                // coordinates still legitimately use a previously cached fit.
                ceres->getInfoMap(core);
                SwissEphemeris::reload();
                core->update(0);
                const auto info=ceres->getInfoMap(core);
                check(info.value("position-source").toString()=="Swiss Ephemeris","Unexpected Kepler fallback in installed interval");
                SwissEphemeris::State exact;
                // Compare interpolation with exact output while retaining the
                // library's open-file selection. Adjacent asteroid AND supporting
                // files overlap: a cold restart can select different valid fits.
                check(SwissEphemeris::calculate(1,info.value("position-source-jde").toDouble(),exact),"Exact reference failed");
                check(QString::fromStdString(exact.filePath)==info.value("ephemeris-file").toString(),
                      "Raw reference uses a different asteroid file");
                const Vec3d reference=StelCore::matJ2000ToVsop87*Vec3d(exact.position.data());
                const double error=(reference-ceres->getHeliocentricEclipticPos()).norm();
                largestStateError=std::max(largestStateError,error);
                if(error>=1e-8)
                    qCritical()<<"Raw mismatch"<<window<<lightTime<<horizontal<<i<<"error AU"<<error
                               <<"date"<<QString::number(core->getJDE(),'g',17)
                               <<"state date"<<QString::number(info.value("position-source-jde").toDouble(),'g',17)
                               <<"stored"<<ceres->getHeliocentricEclipticPos()<<"reference"<<reference
                               <<"files"<<info.value("ephemeris-file")<<QString::fromStdString(exact.filePath);
                check(error<1e-8,"Stored position disagrees with raw Swiss reference");
                files.insert(QFileInfo(QString::fromStdString(exact.filePath)).fileName());
                SwissEphemeris::reload(); // Next live sample starts independently of this traversal/cache.
            }
        }
        if(window==0) check(files.contains("seas_12.se1") && files.contains("seas_18.se1"),"Did not exercise both asteroid files");
        qInfo()<<"PASS AstroCalc"<<"window"<<window
               <<"lightTime"<<lightTime<<"horizontal"<<horizontal<<"points"<<points.size();
    }
    checkDateScrolling(dialog,core);
    qInfo()<<"PASS: AstroCalc drawing inputs; maximum angular difference (arcsec)"<<largestAngle*180./M_PI*3600.
           <<"maximum raw Swiss state difference (AU)"<<largestStateError;
    dialog.setVisible(false);
    passed=true;
}
void installTimer()
{
    auto* timer=new QTimer(QCoreApplication::instance());
    QObject::connect(timer,&QTimer::timeout,timer,[]{
        if(completed) { QCoreApplication::quit(); return; }
        if(running || !StelApp::isInitialized()) return;
        running=true;
        auto* core=StelApp::getInstance().getCore();
        const double initialJD=core->getJD();
        try { runRegression(); } catch(const std::exception& error) { qCritical()<<error.what(); }
        core->setJD(initialJD); // Do not initialize remaining plugins at an ancient test date.
        completed=true; QCoreApplication::quit();
    });
    timer->start(100);
}
}
Q_COREAPP_STARTUP_FUNCTION(installTimer)
int main(int argc,char** argv)
{
    if(argc!=2) return 2;
    QTemporaryDir profile;
    if(!profile.isValid()) return 2;
    const QString fixtures=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString data=profile.path()+"/ephemeris/swiss";
    QDir().mkpath(data);
    for(const QString block:{"12","18"})
        for(const QString kind:{"sepl","semo","seas"})
        {
            const QString file=kind+"_"+block+".se1";
            if(!QFile::copy(fixtures+"/"+file,data+"/"+file)) return 2;
        }
    qputenv("STEL_USERDIR",QFile::encodeName(profile.path()));
    qunsetenv("SWEPH_PATH"); qunsetenv("SE_EPHE_PATH"); qunsetenv("SWEPH_EXACT");
    const QString scrollProfile=qEnvironmentVariable("SWISS_SCROLL_PROFILE");
    if(!scrollProfile.isEmpty())
    {
        QDir().mkpath(profile.path()+"/data");
        if(!QFile::copy(scrollProfile+"/data/ssystem_minor.ini",profile.path()+"/data/ssystem_minor.ini")) return 2;
        qputenv("SWEPH_PATH",QFile::encodeName(scrollProfile+"/ephemeris/swiss"));
    }
    QSettings settings(profile.path()+"/config.ini",QSettings::IniFormat);
    settings.setValue("plugins_load_at_startup/Satellites",false);
    settings.setValue("video/fullscreen",false);
    settings.setValue("video/screen_w",1000); settings.setValue("video/screen_h",700);
    settings.setValue("main/check_requirements",false); settings.sync();
    QByteArray program=QFile::encodeName(QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath());
    char* arguments[]={program.data(),nullptr};
    stellariumApplicationMain(1,arguments);
    return passed ? 0 : 1;
}
