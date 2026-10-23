/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../plugins/SolarSystemEditor/src/gui/SwissEphemerisManifest.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QDebug>
#include <cstdlib>
static void check(bool ok,const char* message) {if(!ok){qCritical("%s",message);std::exit(1);}}
static QJsonObject read(const QString& path)
{
    QFile f(path); check(f.open(QIODevice::ReadOnly),"Cannot read checkpoint");
    return QJsonDocument::fromJson(f.readAll()).object();
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir temporary;
    check(temporary.isValid(),"Temporary directory failed");
    const QString path=temporary.path()+"/downloads.json";
    SwissEphemerisManifest manifest;
    QString error;
    check(manifest.load(path,error),"Cannot start new manifest");
    int checkpoints=0;
    for(int i=0;i<10000;++i)
    {
        manifest.insert(QString::number(i),QJsonObject{{"sha256",QString::number(i)},{"bytes",123}});
        if(manifest.checkpointDue()) {check(manifest.flush(error),"Batch checkpoint failed");++checkpoints;}
    }
    check(read(path).size()==9984,"Unexpected last periodic checkpoint");
    check(manifest.hasPending(),"Final partial batch lost");
    check(manifest.flush(error),"Final checkpoint failed"); ++checkpoints;
    check(checkpoints==157 && read(path).size()==10000,"Batching did not reduce writes or lost records");
    SwissEphemerisManifest resumed;
    check(resumed.load(path,error) && resumed.contains("9999"),"Resume lost existing records");
    resumed.insert("9999",QJsonObject{{"sha256","9999"},{"bytes",123}});
    check(!resumed.hasPending(),"Unchanged entry unnecessarily marked dirty");
    resumed.insert("recovered",QJsonObject{{"verified_utc","2026-10-17T00:00:00Z"}});
    // Force a write failure without relying on Unix permissions or running UID.
    check(QFile::rename(path,path+".saved") && QDir().mkdir(path),"Cannot simulate failed output");
    check(!resumed.flush(error) && resumed.hasPending(),"Failure lost pending data");
    check(!resumed.checkpointDue(),"Failure would retry on every downloaded file");
    check(read(path+".saved").size()==10000,"Failed write damaged previous checkpoint");
    check(!resumed.load(path+".saved",error),"Load discarded unsaved data");
    check(QDir().rmdir(path) && QFile::rename(path+".saved",path),"Cannot restore output");
    check(resumed.flush(error) && read(path).size()==10001,"Retry failed to preserve metadata");
    // No-op flush must not attempt any writes, even if the target is now blocked.
    check(QFile::rename(path,path+".saved") && QDir().mkdir(path),"Cannot block clean output");
    check(resumed.flush(error),"Clean manifest performed a write");
    check(QDir().rmdir(path),"Cannot remove test directory");
    QFile corrupt(path); check(corrupt.open(QIODevice::WriteOnly),"Cannot create invalid manifest");
    corrupt.write("{broken"); corrupt.close();
    check(!resumed.load(path,error),"Invalid JSON was silently discarded");
    check(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll()=="{broken","Invalid manifest overwritten");
    qInfo()<<"PASS: 10000 entries in"<<checkpoints<<"checkpoints; resume, no-op, failure retention and retry";
}
