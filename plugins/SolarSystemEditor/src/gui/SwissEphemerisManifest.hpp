/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SWISS_EPHEMERIS_MANIFEST_HPP
#define SWISS_EPHEMERIS_MANIFEST_HPP
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

//! Download metadata checkpoints, independent of widgets and network requests.
//! Data files are authoritative; this record is supplementary provenance.
class SwissEphemerisManifest
{
public:
    static constexpr int checkpointSize = 64;
    bool load(const QString& fileName, QString& error)
    {
        if (dirty) { error=QStringLiteral("Unsaved download metadata"); return false; }
        QJsonObject loaded;
        if (QFileInfo::exists(fileName))
        {
            QFile file(fileName);
            if (!file.open(QIODevice::ReadOnly)) { error=file.errorString(); return false; }
            const auto bytes=file.readAll();
            if (file.error()!=QFile::NoError) { error=file.errorString(); return false; }
            QJsonParseError parse;
            const auto document=QJsonDocument::fromJson(bytes,&parse);
            if (parse.error!=QJsonParseError::NoError || !document.isObject())
            { error=QStringLiteral("Invalid download manifest: ")+(parse.error==QJsonParseError::NoError ? QStringLiteral("expected a JSON object") : parse.errorString()); return false; }
            loaded=document.object();
        }
        path=fileName; entries=loaded; changesSinceAttempt=0;
        return true;
    }
    bool contains(const QString& name) const { return entries.contains(name); }
    void insert(const QString& name,const QJsonObject& value)
    {
        if (entries.value(name)==QJsonValue(value)) return;
        entries.insert(name,value);
        dirty=true;
        ++changesSinceAttempt;
    }
    bool hasPending() const { return dirty; }
    bool checkpointDue() const { return changesSinceAttempt>=checkpointSize; }
    bool flush(QString& error)
    {
        if (!dirty) return true;
        // Failed attempts must not trigger another full rewrite on every file.
        changesSinceAttempt=0;
        QSaveFile file(path);
        const auto bytes=QJsonDocument(entries).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        { error=file.errorString(); return false; }
        dirty=false;
        return true;
    }
private:
    QString path;
    QJsonObject entries;
    int changesSinceAttempt=0;
    bool dirty=false;
};
#endif
