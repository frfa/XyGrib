/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "EcmwfProvider.h"
#include "Util.h"

EcmwfProvider::EcmwfProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      currentStepIndex(0),
      currentReply(nullptr)
{
}

EcmwfProvider::~EcmwfProvider()
{
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }
}

void EcmwfProvider::stop()
{
    AbstractGribProvider::stop();
    if (currentReply) {
        currentReply->abort();
    }
}

void EcmwfProvider::abort()
{
    stop();
}

void EcmwfProvider::startDownload(const GribRequestParams &params)
{
    requestParams = params;
    isAborted = false;
    accumulatedGribData.clear();
    currentStepIndex = 0;

    QDateTime now = QDateTime::currentDateTimeUtc();
    int hourUtc = now.time().hour();
    if (hourUtc >= 18) cycleHour = "12";
    else cycleHour = "00";
    cycleDate = now.toString("yyyyMMdd");

    if (params.cycle != "" && params.cycle.toLower() != "last") {
        cycleHour = params.cycle;
        if (cycleHour.length() == 1) cycleHour = "0" + cycleHour;
    }

    forecastHours.clear();
    int maxHours = params.days * 24;
    int stepInt = params.interval > 0 ? params.interval : 6;
    for (int h = 0; h <= maxHours; h += stepInt) {
        forecastHours.append(h);
    }

    emit signalGribSendMessage(tr("Starting ECMWF Open Data download (%1z)...").arg(cycleHour));
    emit signalGribStartLoadData();

    processNextStep();
}

void EcmwfProvider::processNextStep()
{
    if (isAborted) return;

    if (currentStepIndex >= forecastHours.size()) {
        if (accumulatedGribData.isEmpty()) {
            emit signalGribLoadError(tr("No ECMWF data could be downloaded."));
            return;
        }

        QString outFileName = QString("ecmwf_%1_%2z.grb2").arg(cycleDate).arg(cycleHour);
        emit signalGribSendMessage(tr("Finished downloading %1 KB").arg(accumulatedGribData.size() / 1024));
        emit signalGribDataReceived(&accumulatedGribData, outFileName);
        return;
    }

    int hour = forecastHours[currentStepIndex];
    QString resStr = (requestParams.resolution <= 0.25f) ? "0p25" : "0p4";

    // https://data.ecmwf.int/forecasts/YYYYMMDD/HHz/ifs/0p25/oper/YYYYMMDDHH0000-FFFh-oper-fc.grib2
    QString urlStr = QString("https://data.ecmwf.int/forecasts/%1/%2z/ifs/%3/oper/%1%20000-%4h-oper-fc.grib2")
                         .arg(cycleDate)
                         .arg(cycleHour)
                         .arg(resStr)
                         .arg(hour);

    emit signalGribSendMessage(tr("Fetching ECMWF step +%1h...").arg(hour));
    emit signalGribReadProgress(1, currentStepIndex + 1, forecastHours.size());

    QNetworkRequest request = Util::makeNetworkRequest(urlStr);
    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(slotFileFinished()));
}

void EcmwfProvider::slotFileFinished()
{
    if (isAborted) return;

    if (currentReply && currentReply->error() == QNetworkReply::NoError) {
        QByteArray chunk = currentReply->readAll();
        accumulatedGribData.append(chunk);
    }

    currentReply->deleteLater();
    currentReply = nullptr;

    currentStepIndex++;
    processNextStep();
}
