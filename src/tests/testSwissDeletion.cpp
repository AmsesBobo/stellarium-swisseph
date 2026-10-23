/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemeris.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <cstdlib>
static void require(bool ok,const char* message) {if(!ok){qCritical("%s",message);std::exit(1);}}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    require(argc==2,"Provide fixture directory");
    QTemporaryDir temporary;
    require(temporary.isValid(),"Cannot create test directory");
    const QString root=temporary.path()+"/ephemeris";
    QDir().mkpath(root+"/ast0");
    for(const QString path:{"sepl_18.se1","semo_18.se1","seas_18.se1","ast0/se00005.se1","ast0/se00433s.se1"})
        require(QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+path,root+"/"+path),"Cannot copy fixture");
    qunsetenv("SWEPH_PATH");
    SwissEphemeris::setDataDirectory(QFile::encodeName(root).toStdString());
    SwissEphemeris::State s;
    require(SwissEphemeris::calculateInterpolated(5,2451545.,s),"Cannot prime cache");
    const auto revision=SwissEphemeris::dataRevision();
    require(SwissEphemeris::removeFiles({"ast0/se00005.se1"}).empty(),"Deletion failed");
    require(SwissEphemeris::dataRevision()>revision,"Deletion did not advance data revision");
    s.position[0]=123.;
    require(!SwissEphemeris::calculateInterpolated(5,2451545.,s) && s.position[0]==123.,"Deleted data still served from cache");
    require(SwissEphemeris::calculateInterpolated(433,2451545.,s),"Unselected asteroid affected");
    require(QFile::exists(root+"/sepl_18.se1"),"Unselected shared file removed");
    QFile outside(temporary.path()+"/outside.se1"); require(outside.open(QIODevice::WriteOnly),"Cannot create outside fixture"); outside.write("sentinel");outside.close();
    require(SwissEphemeris::removeFiles({"../outside.se1","downloads.json","ast0/missing.se1"}).size()==3,"Unsafe paths accepted");
    require(QFile::exists(outside.fileName()),"Traversal deleted outside file");
#ifndef Q_OS_WIN
    require(QFile::link(outside.fileName(),root+"/ast0/se00006.se1"),"Cannot create test symlink");
    require(SwissEphemeris::removeFiles({"ast0/se00006.se1"}).size()==1,"Symlink accepted");
    require(QFile::exists(outside.fileName()),"Symlink target deleted");
#endif
    qputenv("SWEPH_PATH",QFile::encodeName(root));
    require(SwissEphemeris::removeFiles({"ast0/se00433s.se1"}).size()==1,"Environment override ignored");
    qunsetenv("SWEPH_PATH");
    require(SwissEphemeris::removeFiles({"sepl_18.se1"}).empty(),"Shared file deletion failed");
    require(!SwissEphemeris::calculateInterpolated(433,2451545.,s),"Removed supporting data still cached");
    qInfo("PASS: selective deletion, immediate cache invalidation, shared data, traversal and override guards");
}
