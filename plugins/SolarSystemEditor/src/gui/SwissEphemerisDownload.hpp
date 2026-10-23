/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SWISS_EPHEMERIS_DOWNLOAD_HPP
#define SWISS_EPHEMERIS_DOWNLOAD_HPP
#include <QWidget>
#include <QQueue>
#include <QPointer>
#include "SwissEphemerisManifest.hpp"
class QTimer;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QNetworkAccessManager;
class QNetworkReply;

//! Optional prototype downloader. Installs only data, never program code.
class SwissEphemerisDownload : public QWidget
{
public:
    explicit SwissEphemerisDownload(QWidget* parent = nullptr);
    ~SwissEphemerisDownload() override;
private:
    struct Job { QString path; bool shortFallback = false; };
    bool saveManifest();
    void start();
    void next();
    void finish(bool cancelled = false);
    void reportFile(const QString& path, const QByteArray& data);
    QString directory;
    QLineEdit* numbers;
    QSpinBox* firstYear;
    QSpinBox* lastYear;
    QPushButton* download;
    QPushButton* cancel;
    QPushButton* installed;
    QProgressBar* progress;
    QPlainTextEdit* log;
    QNetworkAccessManager* network;
    QPointer<QNetworkReply> reply;
    QQueue<Job> jobs;
    SwissEphemerisManifest manifest;
    QTimer* manifestTimer;
    bool manifestFailureReported = false;
    int total = 0;
    int completed = 0;
    int failures = 0;
    bool stopping = false;
};
#endif
