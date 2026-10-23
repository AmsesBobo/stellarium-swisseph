/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "SwissEphemeris.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <cstdlib>

static void require(bool ok, const char* message)
{
    if (!ok) { qCritical("%s", message); std::exit(1); }
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    require(argc == 2, "Provide fixture directory");
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create temporary profile");
    const QString source = QString::fromLocal8Bit(argv[1]);
    const auto copyData = [&](const QString& root) {
        require(QDir().mkpath(root+"/ast0"), "Cannot create data directory");
        for (const QString name : {"sepl_18.se1", "semo_18.se1", "seas_18.se1", "ast0/se00005.se1"})
            require(QFile::copy(source+"/"+name, root+"/"+name), "Cannot copy fixture");
    };
    const QString root = temporary.path()+QString::fromUtf8("/rymd åäö");
    copyData(root);
    const QByteArray missing = QFile::encodeName(temporary.path()+"/missing");
    const QByteArray nativeRoot = QFile::encodeName(root);
    const auto configure = [](const QByteArray& path) {
        SwissEphemeris::setDataDirectory(path.toStdString());
        SwissEphemeris::reload();
    };
    qunsetenv("SWEPH_PATH");
    qputenv("SE_EPHE_PATH", missing);
    configure(nativeRoot);
    SwissEphemeris::State state;
    for (int number : {1, 5})
    {
        require(SwissEphemeris::calculate(number, 2451545., state), "Native environment overrode explicit directory");
        require(QFileInfo(QString::fromLocal8Bit(state.filePath.c_str())).canonicalFilePath().startsWith(QFileInfo(root).canonicalFilePath()+"/"), "Wrong data source");
    }
    require(qgetenv("SE_EPHE_PATH") == missing, "Adapter changed process environment");
    qputenv("SE_EPHE_PATH", nativeRoot);
    configure(missing);
    state.position[0] = 123.;
    require(!SwissEphemeris::calculate(1, 2451545., state) && state.position[0] == 123., "Used native environment as fallback");
    qputenv("SWEPH_PATH", nativeRoot);
    SwissEphemeris::reload();
    require(SwissEphemeris::calculate(5, 2451545., state), "Explicit developer override broken");
    qunsetenv("SWEPH_PATH");

    // Exact native-byte boundary accepted by the pinned swe_set_ephe_path.
    QByteArray boundary = QFile::encodeName(temporary.path())+"/";
    boundary += QByteArray(242-boundary.size(), 'x');
    copyData(QFile::decodeName(boundary));
    configure(boundary);
    require(SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::Ready, "242-byte path rejected");
    require(SwissEphemeris::calculate(1, 2451545., state), "242-byte main-file path failed");
    configure(boundary+"x");
    require(SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::TooLong, "243-byte path accepted");
    state.position[0] = 123.;
    require(!SwissEphemeris::calculateInterpolated(1, 2451545., state) && state.position[0] == 123., "Long path silently fell back");
    configure(nativeRoot+";elsewhere");
    require(SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::Invalid, "Single directory treated as search list");
    require(!SwissEphemeris::calculate(1, 2451545., state), "Invalid path used");
    configure(QByteArray("bad\0path", 8));
    require(SwissEphemeris::pathStatus() == SwissEphemeris::PathStatus::Invalid, "Embedded NUL accepted");
    configure({});
    require(!SwissEphemeris::isEnabled() && !SwissEphemeris::calculate(1, 2451545., state), "Empty path fell back to environment");
    qInfo("PASS: explicit paths, unchanged native environment, Unicode/spaces, developer override and path limits");
}
