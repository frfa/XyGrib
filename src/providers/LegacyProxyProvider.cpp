/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "LegacyProxyProvider.h"
#include "Util.h"
#include "Version.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

LegacyProxyProvider::LegacyProxyProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      fileSize(0),
      step(0),
      downloadError(false),
      reply_step1(nullptr),
      reply_step2(nullptr)
{
}

LegacyProxyProvider::~LegacyProxyProvider()
{
    if (reply_step1) reply_step1->deleteLater();
    if (reply_step2) reply_step2->deleteLater();
}

void LegacyProxyProvider::stop()
{
    AbstractGribProvider::stop();
    downloadError = true;
    if (reply_step1) reply_step1->close();
    if (reply_step2) reply_step2->close();
}

void LegacyProxyProvider::abort()
{
    AbstractGribProvider::abort();
    downloadError = true;
    if (reply_step1) reply_step1->abort();
    if (reply_step2) reply_step2->abort();
}

void LegacyProxyProvider::startDownload(const GribRequestParams &params)
{
    step = 1;
    downloadError = false;
    emit signalGribSendMessage(tr("Preparing file on proxy server... Please wait..."));
    emit signalGribStartLoadData();
    emit signalGribReadProgress(step, 0, 0);

    QString parameters = "";
    if (params.wind) parameters += "W;";
    if (params.pressure) parameters += "P;";
    if (params.rain) parameters += "R;";
    if (params.cloud) parameters += "C;";
    if (params.temp) parameters += "T;";
    if (params.humid) parameters += "H;";
    if (params.isotherm0) parameters += "I;";
    if (params.snowDepth) parameters += "S;";
    if (params.snowCateg) parameters += "s;";
    if (params.frzRainCateg) parameters += "Z;";
    if (params.CAPEsfc) parameters += "c;";
    if (params.CINsfc) parameters += "i;";
    if (params.reflectivity) parameters += "r;";
    if (params.GUSTsfc) parameters += "G;";

    if (params.altitudeData200) parameters += "2;";
    if (params.altitudeData300) parameters += "3;";
    if (params.altitudeData400) parameters += "4;";
    if (params.altitudeData500) parameters += "5;";
    if (params.altitudeData600) parameters += "6;";
    if (params.altitudeData700) parameters += "7;";
    if (params.altitudeData850) parameters += "8;";
    if (params.altitudeData925) parameters += "9;";
    if (params.skewTData) parameters += "skewt;";

    QString waveParams = "";
    if (params.sgwh) waveParams += "s;";
    if (params.swell) waveParams += "H;D;P;";
    if (params.wwav) waveParams += "h;d;p;";

    QString amod = "gfs_p25_";
    if (params.atmModel == "ICON") amod = "icon_p25_";
    else if (params.atmModel == "Arpege") amod = "arpege_p50_";
    else if (params.atmModel == "ECMWF") amod = "ecmwf_p50_";

    QString wmod = "none";
    if (params.wvModel == "WW3") wmod = "ww3_p50_";
    else if (params.wvModel == "GWAM") wmod = "gwam_p25_";
    else if (params.wvModel == "EWAM") wmod = "ewam_p05_";

    QString page;
    QTextStream(&page) << "/getmygribs2.php?"
                       << "osys=" << QSysInfo::productType()
                       << "&ver=" << Version::getVersion()
                       << "&model=" << amod
                       << "&la1=" << params.y0
                       << "&la2=" << params.y1
                       << "&lo1=" << params.x0
                       << "&lo2=" << params.x1
                       << "&intv=" << params.interval
                       << "&days=" << params.days
                       << "&cyc=" << params.cycle
                       << "&par=" << parameters
                       << "&wmdl=" << wmod
                       << "&wpar=" << waveParams;

    QNetworkRequest request = Util::makeNetworkRequest("http://" + Util::getServerName() + page);
    reply_step1 = networkManager->get(request);
    connect(reply_step1, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));
    connect(reply_step1, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(slotNetworkError(QNetworkReply::NetworkError)));
    connect(reply_step1, SIGNAL(finished()), this, SLOT(slotFinished_step1()));
}

void LegacyProxyProvider::slotNetworkError(QNetworkReply::NetworkError /*err*/)
{
    if (!downloadError) {
        downloadError = true;
        if (sender() == reply_step1) emit signalGribLoadError(reply_step1->errorString());
        else if (sender() == reply_step2) emit signalGribLoadError(reply_step2->errorString());
    }
}

void LegacyProxyProvider::downloadProgress(qint64 done, qint64 total)
{
    if (downloadError) return;
    if (sender() == reply_step2) {
        emit signalGribReadProgress(step, done, total);
    }
}

void LegacyProxyProvider::slotFinished_step1()
{
    if (!downloadError && reply_step1) {
        QByteArray data = reply_step1->readAll();
        QJsonDocument jsondoc = QJsonDocument::fromJson(data);
        QJsonObject jsondata = jsondoc.object();

        bool status = jsondata["status"].toBool();
        if (status) {
            QJsonObject msg = jsondata["message"].toObject();
            fileName = msg["url"].toString();
            fileSize = msg["size"].toInt();
            checkSumSHA1 = msg["sha1"].toString();

            step = 2;
            emit signalGribSendMessage(tr("Total size : %1 KB").arg(fileSize / 1024));
            QNetworkRequest request = Util::makeNetworkRequest(fileName);
            reply_step2 = networkManager->get(request);
            connect(reply_step2, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(downloadProgress(qint64,qint64)));
            connect(reply_step2, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(slotNetworkError(QNetworkReply::NetworkError)));
            connect(reply_step2, SIGNAL(finished()), this, SLOT(slotFinished_step2()));
        } else {
            QString m = jsondata["message"].toString();
            if (m.isEmpty()) m = tr("Proxy server returned an invalid response.");
            emit signalGribLoadError(m);
            downloadError = true;
        }
    }
}

void LegacyProxyProvider::slotFinished_step2()
{
    if (!downloadError && reply_step2) {
        arrayContent = reply_step2->readAll();
        if (arrayContent.size() < 80) {
            emit signalGribLoadError(tr("Empty file received from proxy."));
            return;
        }

        if (Util::sha1(arrayContent) == checkSumSHA1 || checkSumSHA1.isEmpty()) {
            emit signalGribDataReceived(&arrayContent, fileName.replace("%20", ".grb"));
        } else {
            emit signalGribLoadError(tr("Bad checksum from proxy server."));
        }
    }
}
