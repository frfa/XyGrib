/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "DwdIconProvider.h"
#include "Util.h"
#include <bzlib.h>
#include <QDebug>

DwdIconProvider::DwdIconProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      currentStepIndex(0),
      currentParamIndex(0),
      currentReply(nullptr)
{
}

DwdIconProvider::~DwdIconProvider()
{
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }
}

void DwdIconProvider::stop()
{
    AbstractGribProvider::stop();
    if (currentReply) {
        currentReply->abort();
    }
}

void DwdIconProvider::abort()
{
    stop();
}

QStringList DwdIconProvider::buildParamList()
{
    QStringList params;
    if (requestParams.pressure) params.append("pmsl");
    if (requestParams.wind) {
        params.append("u_10m");
        params.append("v_10m");
    }
    if (requestParams.temp) params.append("t_2m");
    if (requestParams.humid) params.append("relhum_2m");
    if (requestParams.rain) params.append("tot_prec");
    if (requestParams.cloud) params.append("clct");
    if (requestParams.GUSTsfc) params.append("vmax_10m");
    if (requestParams.snowDepth) params.append("h_snow");

    if (params.isEmpty()) {
        params.append("pmsl");
        params.append("u_10m");
        params.append("v_10m");
    }
    return params;
}

void DwdIconProvider::startDownload(const GribRequestParams &params)
{
    requestParams = params;
    isAborted = false;
    accumulatedGribData.clear();
    currentStepIndex = 0;
    currentParamIndex = 0;

    QDateTime now = QDateTime::currentDateTimeUtc();
    int hourUtc = now.time().hour();
    if (hourUtc >= 21) cycleHour = "18";
    else if (hourUtc >= 15) cycleHour = "12";
    else if (hourUtc >= 9) cycleHour = "06";
    else cycleHour = "00";
    cycleDate = now.toString("yyyyMMdd");

    if (params.cycle != "" && params.cycle != "last") {
        cycleHour = params.cycle;
        if (cycleHour.length() == 1) cycleHour = "0" + cycleHour;
    }

    forecastHours.clear();
    int maxHours = params.days * 24;
    int stepInt = params.interval > 0 ? params.interval : 3;
    for (int h = 0; h <= maxHours; h += stepInt) {
        forecastHours.append(h);
    }

    requestedParams = buildParamList();

    emit signalGribSendMessage(tr("Starting DWD ICON download (%1z cycle)...").arg(cycleHour));
    emit signalGribStartLoadData();

    processNextFile();
}

void DwdIconProvider::processNextFile()
{
    if (isAborted) return;

    if (currentStepIndex >= forecastHours.size()) {
        if (accumulatedGribData.isEmpty()) {
            emit signalGribLoadError(tr("No DWD ICON data could be downloaded."));
            return;
        }

        QString outFileName = QString("icon_%1_%2z.grb2").arg(cycleDate).arg(cycleHour);
        emit signalGribSendMessage(tr("Finished downloading %1 KB").arg(accumulatedGribData.size() / 1024));
        emit signalGribDataReceived(&accumulatedGribData, outFileName);
        return;
    }

    if (currentParamIndex >= requestedParams.size()) {
        currentParamIndex = 0;
        currentStepIndex++;
        processNextFile();
        return;
    }

    int hour = forecastHours[currentStepIndex];
    QString param = requestedParams[currentParamIndex];

    bool isEu = (requestParams.atmModel == "ICON-EU");
    QString modelStr = isEu ? "icon-eu" : "icon";
    QString domainStr = isEu ? "europe_regular-lat-lon" : "global_regular-lat-lon";
    QString levelType = "single-level";
    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString paramUpper = param.toUpper();

    // DWD URL pattern:
    // https://opendata.dwd.de/weather/nwp/icon-eu/grib/HH/param/icon-eu_europe_regular-lat-lon_single-level_YYYYMMDDHH_FFF_PARAM.grib2.bz2
    QString urlStr = QString("https://opendata.dwd.de/weather/nwp/%1/grib/%2/%3/%4_%5_%6_%7%2_%8_%9.grib2.bz2")
                         .arg(modelStr)
                         .arg(cycleHour)
                         .arg(param)
                         .arg(modelStr)
                         .arg(domainStr)
                         .arg(levelType)
                         .arg(cycleDate)
                         .arg(hourStr)
                         .arg(paramUpper);

    int totalFiles = forecastHours.size() * requestedParams.size();
    int currentFileNum = currentStepIndex * requestedParams.size() + currentParamIndex + 1;
    emit signalGribSendMessage(tr("Fetching DWD ICON [%1/%2] +%3h %4...")
                                  .arg(currentFileNum)
                                  .arg(totalFiles)
                                  .arg(hour)
                                  .arg(paramUpper));
    emit signalGribReadProgress(1, currentFileNum, totalFiles);

    QNetworkRequest request = Util::makeNetworkRequest(urlStr);
    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(slotFileFinished()));
}

void DwdIconProvider::slotFileFinished()
{
    if (isAborted) return;

    if (currentReply && currentReply->error() == QNetworkReply::NoError) {
        QByteArray bz2Data = currentReply->readAll();
        QByteArray decompressed = decompressBz2(bz2Data);
        if (!decompressed.isEmpty()) {
            accumulatedGribData.append(decompressed);
        }
    }

    currentReply->deleteLater();
    currentReply = nullptr;

    currentParamIndex++;
    processNextFile();
}

QByteArray DwdIconProvider::decompressBz2(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) return QByteArray();

    unsigned int uncompressedLen = compressedData.size() * 10 + 1024;
    QByteArray uncompressed;
    uncompressed.resize(uncompressedLen);

    int ret = BZ2_bzBuffToBuffDecompress(uncompressed.data(), &uncompressedLen,
                                         const_cast<char*>(compressedData.constData()),
                                         compressedData.size(), 0, 0);

    if (ret == BZ_OUTBUFF_FULL) {
        uncompressedLen = compressedData.size() * 30;
        uncompressed.resize(uncompressedLen);
        ret = BZ2_bzBuffToBuffDecompress(uncompressed.data(), &uncompressedLen,
                                         const_cast<char*>(compressedData.constData()),
                                         compressedData.size(), 0, 0);
    }

    if (ret == BZ_OK) {
        uncompressed.resize(uncompressedLen);
        return uncompressed;
    }

    return QByteArray();
}
