/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SWISS_EPHEMERIS_FILES_HPP
#define SWISS_EPHEMERIS_FILES_HPP
#include <QBuffer>
#include <QDataStream>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace SwissEphemerisFiles
{
// Independent of the optional Swiss library; also used by offline tests.
inline QList<int> parseNumbers(const QString& input)
{
    QSet<int> values;
    const QRegularExpression syntax("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$");
    for (const auto& part : input.split(','))
    {
        const auto match = syntax.match(part);
        bool ok1=false, ok2=true;
        const qlonglong first = match.captured(1).toLongLong(&ok1);
        const qlonglong last = match.captured(2).isEmpty() ? first : match.captured(2).toLongLong(&ok2);
        if (!match.hasMatch() || !ok1 || !ok2 || first<1 || last<first || last>2147473647 || last-first>10000)
            return {};
        for (qlonglong n=first; n<=last; ++n)
        {
            values.insert(int(n));
            if (values.size()>10000) return {};
        }
    }
    auto result = values.values();
    std::sort(result.begin(), result.end());
    return result;
}

inline QString asteroidPath(int number, bool shortFile = false)
{
    const QString stem = number <= 99999 ? QString("se%1").arg(number,5,10,QLatin1Char('0'))
                                       : QString("s%1").arg(number,6,10,QLatin1Char('0'));
    return QString("ast%1/%2%3.se1").arg(number/1000).arg(stem, shortFile ? "s" : "");
}

//! Read the documented header layout used by Swiss read_const().
//! The library itself validates the remaining binary structure when used.
inline bool coverage(const QByteArray& data, const QString& path, double& first, double& last, qint64 fileSize = -1)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    if (!buffer.readLine(1024).startsWith("SWISSEPH")) return false;
    if (buffer.readLine(1024).trimmed() != QFileInfo(path).fileName().toLatin1()) return false;
    if (!buffer.readLine(1024).endsWith("\r\n")) return false;
    if (path.startsWith("ast") && !buffer.readLine(1024).endsWith("\r\n")) return false;
    const QByteArray endian = buffer.read(4);
    QDataStream stream(&buffer);
    if (endian == QByteArray::fromHex("63626100")) stream.setByteOrder(QDataStream::LittleEndian);
    else if (endian == QByteArray::fromHex("00616263")) stream.setByteOrder(QDataStream::BigEndian);
    else return false;
    quint32 length=0, de=0;
    stream >> length >> de >> first >> last;
    return stream.status()==QDataStream::Ok && qint64(length)==(fileSize<0 ? data.size() : fileSize) && de>0 &&
        std::isfinite(first) && std::isfinite(last) && first<last;
}
}
#endif
