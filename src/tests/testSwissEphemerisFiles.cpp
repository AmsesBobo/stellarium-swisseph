/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../plugins/SolarSystemEditor/src/gui/SwissEphemerisFiles.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <cstdlib>
static void require(bool value) { if (!value) { qCritical("Swiss download validation failed"); std::exit(1); } }
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    using namespace SwissEphemerisFiles;
    require(parseNumbers("1-1000").size()==1000);
    require(parseNumbers("5, 433, 5")==QList<int>({5,433}));
    require(parseNumbers("136199")==QList<int>({136199}));
    for (const QString invalid : {"", "0", "5-1", "1,", "1-10001", "999999999999999999999", "1;2"})
        require(parseNumbers(invalid).isEmpty());
    require(asteroidPath(136199)=="ast136/s136199.se1");
    require(asteroidPath(433,true)=="ast0/se00433s.se1");
    double first=0,last=0;
    require(!coverage("<html>error</html>","seas_18.se1",first,last));
    if (argc>1)
    {
        for (const QString path : {"seas_18.se1","sepl_18.se1","semo_18.se1","ast0/se00005.se1","ast0/se00433s.se1"})
        {
            QFile file(QString::fromLocal8Bit(argv[1])+"/"+path);
            require(file.open(QIODevice::ReadOnly));
            const auto data=file.readAll();
            require(coverage(data,path,first,last));
            require(coverage(data.left(4096),path,first,last,data.size()));
            require(!coverage(data.left(4096),path,first,last,data.size()+1));
            require(first<last);
            require(!coverage(data.left(data.size()-1),path,first,last));
            require(!coverage(data,path+"wrong",first,last));
        }
    }
    qInfo("Swiss download validation passed");
}
