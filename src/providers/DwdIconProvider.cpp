/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "DwdIconProvider.h"
#include "Util.h"
#include <bzlib.h>
#include <cstdio>
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
    else if (hourUtc >= 3) cycleHour = "00";
    else {
        cycleHour = "18";
        now = now.addDays(-1);
    }
    cycleDate = now.toString("yyyyMMdd");

    if (params.cycle != "" && params.cycle.toLower() != "last") {
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
            emit signalGribLoadError(tr("No DWD ICON data could be downloaded. Check selected cycle/date."));
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
    QString modelFolder = isEu ? "icon-eu" : "icon";
    QString filePrefix = isEu ? "icon-eu_europe_regular-lat-lon" : "icon_global_icosahedral";
    QString levelType = "single-level";
    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString paramUpper = param.toUpper();

    // Explicit two-stage URL formatting to prevent QString::arg() placeholder collisions
    QString fileNameStr = QString("%1_%2_%3%4_%5_%6.grib2.bz2")
                              .arg(filePrefix)
                              .arg(levelType)
                              .arg(cycleDate)
                              .arg(cycleHour)
                              .arg(hourStr)
                              .arg(paramUpper);

    QString urlStr = QString("https://opendata.dwd.de/weather/nwp/%1/grib/%2/%3/%4")
                         .arg(modelFolder)
                         .arg(cycleHour)
                         .arg(param)
                         .arg(fileNameStr);

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

    if (currentReply) {
        if (currentReply->error() == QNetworkReply::NoError) {
            QByteArray bz2Data = currentReply->readAll();
            QByteArray decompressed = decompressBz2(bz2Data);
            if (!decompressed.isEmpty()) {
                accumulatedGribData.append(decompressed);
                emit signalGribReadProgress(2, accumulatedGribData.size(), accumulatedGribData.size() + 1024);
            }
        } else {
            fprintf(stderr, "[DWD ICON Warning] %s returned HTTP %d (%s)\n",
                    qPrintable(currentReply->url().toString()),
                    currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                    qPrintable(currentReply->errorString()));
        }
        currentReply->deleteLater();
        currentReply = nullptr;
    }

    currentParamIndex++;
    processNextFile();
}

QByteArray DwdIconProvider::decompressBz2(const QByteArray &compressedData)
{
    if (compressedData.isEmpty()) return QByteArray();

    unsigned int uncompressedLen = compressedData.size() * 15 + 100000;
    QByteArray uncompressed;
    uncompressed.resize(uncompressedLen);

    int ret = BZ2_bzBuffToBuffDecompress(uncompressed.data(), &uncompressedLen,
                                         const_cast<char*>(compressedData.constData()),
                                         compressedData.size(), 0, 0);

    if (ret == BZ_OUTBUFF_FULL) {
        uncompressedLen = compressedData.size() * 50 + 500000;
        uncompressed.resize(uncompressedLen);
        ret = BZ2_bzBuffToBuffDecompress(uncompressed.data(), &uncompressedLen,
                                         const_cast<char*>(compressedData.constData()),
                                         compressedData.size(), 0, 0);
    }

    if (ret == BZ_OK) {
        uncompressed.resize(uncompressedLen);
        return uncompressed;
    }

    fprintf(stderr, "[DWD ICON Error] bzip2 decompression failed, error code %d\n", ret);
    return QByteArray();
}
