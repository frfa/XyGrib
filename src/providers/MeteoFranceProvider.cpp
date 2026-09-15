/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "MeteoFranceProvider.h"
#include "Util.h"

MeteoFranceProvider::MeteoFranceProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      currentStepIndex(0),
      currentReply(nullptr)
{
}

MeteoFranceProvider::~MeteoFranceProvider()
{
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }
}

void MeteoFranceProvider::stop()
{
    AbstractGribProvider::stop();
    if (currentReply) {
        currentReply->abort();
    }
}

void MeteoFranceProvider::abort()
{
    stop();
}

void MeteoFranceProvider::startDownload(const GribRequestParams &params)
{
    requestParams = params;
    isAborted = false;
    accumulatedGribData.clear();
    currentStepIndex = 0;

    QDateTime now = QDateTime::currentDateTimeUtc();
    int hourUtc = now.time().hour();
    if (hourUtc >= 18) cycleHour = "12";
    else if (hourUtc >= 12) cycleHour = "06";
    else if (hourUtc >= 6) cycleHour = "00";
    else cycleHour = "18";
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

    emit signalGribSendMessage(tr("Starting Météo-France %1 download (%2z)...").arg(params.atmModel).arg(cycleHour));
    emit signalGribStartLoadData();

    processNextStep();
}

void MeteoFranceProvider::processNextStep()
{
    if (isAborted) return;

    if (currentStepIndex >= forecastHours.size()) {
        if (accumulatedGribData.isEmpty()) {
            emit signalGribLoadError(tr("No Météo-France data could be downloaded."));
            return;
        }

        QString outFileName = QString("mf_%1_%2_%3z.grb2").arg(requestParams.atmModel.toLower().remove(' ')).arg(cycleDate).arg(cycleHour);
        emit signalGribSendMessage(tr("Finished downloading %1 KB").arg(accumulatedGribData.size() / 1024));
        emit signalGribDataReceived(&accumulatedGribData, outFileName);
        return;
    }

    int hour = forecastHours[currentStepIndex];
    QString modelCode = "arpege";
    if (requestParams.atmModel.contains("Arome")) modelCode = "arome";
    else if (requestParams.atmModel.contains("Arpege-EU")) modelCode = "arpege-eu";

    // Météo-France open data endpoint pattern
    QString urlStr = QString("https://donneespubliques.meteofrance.fr/donnees_libre/GRIB/%1_%2_%3z_%4h.grib2")
                         .arg(modelCode)
                         .arg(cycleDate)
                         .arg(cycleHour)
                         .arg(hour);

    emit signalGribSendMessage(tr("Fetching Météo-France %1 step +%2h...").arg(modelCode).arg(hour));
    emit signalGribReadProgress(1, currentStepIndex + 1, forecastHours.size());

    QNetworkRequest request = Util::makeNetworkRequest(urlStr);
    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(slotFileFinished()));
}

void MeteoFranceProvider::slotFileFinished()
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
