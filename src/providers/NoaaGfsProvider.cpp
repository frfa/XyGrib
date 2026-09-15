/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "NoaaGfsProvider.h"
#include "Util.h"
#include <QDebug>
#include <QEventLoop>

NoaaGfsProvider::NoaaGfsProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      currentStepIndex(0),
      usingNomadsFilter(true),
      usingFallback(false),
      totalBytesDownloaded(0),
      currentReply(nullptr),
      currentRangeIndex(0),
      currentHour(0)
{
}

NoaaGfsProvider::~NoaaGfsProvider()
{
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }
}

void NoaaGfsProvider::stop()
{
    AbstractGribProvider::stop();
    if (currentReply) {
        currentReply->abort();
    }
}

void NoaaGfsProvider::abort()
{
    stop();
}

void NoaaGfsProvider::startDownload(const GribRequestParams &params)
{
    requestParams = params;
    isAborted = false;
    accumulatedGribData.clear();
    totalBytesDownloaded = 0;
    currentStepIndex = 0;
    usingNomadsFilter = true;
    usingFallback = false;

    // Build forecast hours list
    forecastHours.clear();
    int maxHours = params.days * 24;
    int stepInt = params.interval > 0 ? params.interval : 3;
    for (int h = 0; h <= maxHours; h += stepInt) {
        forecastHours.append(h);
    }

    emit signalGribSendMessage(tr("Checking latest NOAA cycle..."));
    emit signalGribStartLoadData();

    resolveCycleAndStart();
}

void NoaaGfsProvider::resolveCycleAndStart()
{
    if (isAborted) return;

    if (requestParams.cycle != "" && requestParams.cycle != "last") {
        cycleHour = requestParams.cycle;
        if (cycleHour.length() == 1) cycleHour = "0" + cycleHour;
        cycleDate = QDateTime::currentDateTimeUtc().toString("yyyyMMdd");
        processNextStep();
        return;
    }

    QDateTime now = QDateTime::currentDateTimeUtc();
    int hourUtc = now.time().hour();

    // NOAA GFS runs (00, 06, 12, 18) are published ~4.5 hours after run time
    if (hourUtc >= 22) cycleHour = "18";
    else if (hourUtc >= 16) cycleHour = "12";
    else if (hourUtc >= 10) cycleHour = "06";
    else if (hourUtc >= 4) cycleHour = "00";
    else {
        // Earlier than 04:00 UTC -> yesterday's 18z run
        cycleHour = "18";
        now = now.addDays(-1);
    }
    cycleDate = now.toString("yyyyMMdd");

    processNextStep();
}

void NoaaGfsProvider::processNextStep()
{
    if (isAborted) return;

    if (currentStepIndex >= forecastHours.size()) {
        // Download complete
        if (accumulatedGribData.isEmpty()) {
            emit signalGribLoadError(tr("No data downloaded from NOAA provider."));
            return;
        }

        QString outFileName = QString("gfs_%1_%2z.grb2").arg(cycleDate).arg(cycleHour);
        emit signalGribSendMessage(tr("Finished downloading %1 KB").arg(accumulatedGribData.size() / 1024));
        emit signalGribDataReceived(&accumulatedGribData, outFileName);
        return;
    }

    currentHour = forecastHours[currentStepIndex];
    int percent = (currentStepIndex * 100) / forecastHours.size();
    
    if (usingNomadsFilter) {
        emit signalGribSendMessage(tr("Fetching NOAA NOMADS GRIB filter (+%1h, bounded area)...").arg(currentHour));
        emit signalGribReadProgress(1, percent, 100);
        fetchNomadsForStep(currentHour);
    } else {
        emit signalGribSendMessage(tr("Fetching NOAA AWS S3 index (+%1h)...").arg(currentHour));
        emit signalGribReadProgress(1, percent, 100);
        fetchIdxForStep(currentHour);
    }
}

QUrl NoaaGfsProvider::buildNomadsFilterUrl(int hour)
{
    QString resScript = "filter_gfs_0p25.pl";
    QString resStr = "0p25";
    if (requestParams.resolution == 0.50f) {
        resScript = "filter_gfs_0p50.pl";
        resStr = "0p50";
    } else if (requestParams.resolution == 1.0f) {
        resScript = "filter_gfs_1p00.pl";
        resStr = "1p00";
    }

    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString fileName = QString("gfs.t%1z.pgrb2.%2.f%3").arg(cycleHour).arg(resStr).arg(hourStr);
    QString dirPath = QString("/gfs.%1/%2/atmos").arg(cycleDate).arg(cycleHour);

    QUrlQuery query;
    query.addQueryItem("file", fileName);
    query.addQueryItem("dir", dirPath);

    // Subregion bounds clipping
    query.addQueryItem("subregion", "on");
    query.addQueryItem("toplat", QString::number(requestParams.y1, 'f', 2));
    query.addQueryItem("bottomlat", QString::number(requestParams.y0, 'f', 2));
    query.addQueryItem("leftlon", QString::number(requestParams.x0, 'f', 2));
    query.addQueryItem("rightlon", QString::number(requestParams.x1, 'f', 2));

    // Variable & Level filters
    if (requestParams.pressure) {
        query.addQueryItem("var_PRMSL", "on");
        query.addQueryItem("lev_mean_sea_level", "on");
    }
    if (requestParams.wind) {
        query.addQueryItem("var_UGRD", "on");
        query.addQueryItem("var_VGRD", "on");
        query.addQueryItem("lev_10_m_above_ground", "on");
    }
    if (requestParams.temp) {
        query.addQueryItem("var_TMP", "on");
        query.addQueryItem("lev_2_m_above_ground", "on");
    }
    if (requestParams.humid) {
        query.addQueryItem("var_RH", "on");
        query.addQueryItem("lev_2_m_above_ground", "on");
    }
    if (requestParams.rain) {
        query.addQueryItem("var_APCP", "on");
        query.addQueryItem("lev_surface", "on");
    }
    if (requestParams.cloud) {
        query.addQueryItem("var_TCDC", "on");
        query.addQueryItem("lev_entire_atmosphere", "on");
    }
    if (requestParams.isotherm0) {
        query.addQueryItem("var_HGT", "on");
        query.addQueryItem("lev_0C_isotherm", "on");
    }
    if (requestParams.GUSTsfc) {
        query.addQueryItem("var_GUST", "on");
        query.addQueryItem("lev_surface", "on");
    }
    if (requestParams.CAPEsfc) {
        query.addQueryItem("var_CAPE", "on");
        query.addQueryItem("lev_surface", "on");
    }
    if (requestParams.CINsfc) {
        query.addQueryItem("var_CIN", "on");
        query.addQueryItem("lev_surface", "on");
    }
    if (requestParams.reflectivity) {
        query.addQueryItem("var_REFC", "on");
        query.addQueryItem("lev_entire_atmosphere", "on");
    }
    if (requestParams.snowDepth) {
        query.addQueryItem("var_SNOD", "on");
        query.addQueryItem("lev_surface", "on");
    }

    // Isobaric Levels
    bool hasIsobaric = false;
    if (requestParams.altitudeData200) { query.addQueryItem("lev_200_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData300) { query.addQueryItem("lev_300_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData400) { query.addQueryItem("lev_400_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData500) { query.addQueryItem("lev_500_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData600) { query.addQueryItem("lev_600_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData700) { query.addQueryItem("lev_700_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData850) { query.addQueryItem("lev_850_mb", "on"); hasIsobaric = true; }
    if (requestParams.altitudeData925) { query.addQueryItem("lev_925_mb", "on"); hasIsobaric = true; }

    if (hasIsobaric) {
        query.addQueryItem("var_HGT", "on");
        query.addQueryItem("var_TMP", "on");
        query.addQueryItem("var_UGRD", "on");
        query.addQueryItem("var_VGRD", "on");
        query.addQueryItem("var_RH", "on");
    }

    QUrl url(QString("https://nomads.ncep.noaa.gov/cgi-bin/%1").arg(resScript));
    url.setQuery(query);
    return url;
}

void NoaaGfsProvider::fetchNomadsForStep(int hour)
{
    QUrl nomadsUrl = buildNomadsFilterUrl(hour);

    QNetworkRequest request = Util::makeNetworkRequest(nomadsUrl.toString());
    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(slotNomadsStepFinished()));
}

void NoaaGfsProvider::slotNomadsStepFinished()
{
    if (isAborted) return;

    if (currentReply && currentReply->error() == QNetworkReply::NoError) {
        QByteArray chunk = currentReply->readAll();
        
        // Verify valid GRIB stream (starts with "GRIB")
        if (chunk.size() >= 4 && chunk.startsWith("GRIB")) {
            accumulatedGribData.append(chunk);
            totalBytesDownloaded += chunk.size();

            emit signalGribReadProgress(2, accumulatedGribData.size(), accumulatedGribData.size() + 1024);
            emit signalGribSendMessage(tr("Downloaded %1 KB (step %2/%3)...")
                                          .arg(totalBytesDownloaded / 1024)
                                          .arg(currentStepIndex + 1)
                                          .arg(forecastHours.size()));

            currentReply->deleteLater();
            currentReply = nullptr;

            currentStepIndex++;
            processNextStep();
            return;
        }
    }

    // If NOMADS CGI filter failed or returned non-GRIB error message, switch to S3 Range requests
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }

    usingNomadsFilter = false;
    processNextStep();
}

void NoaaGfsProvider::fetchIdxForStep(int hour)
{
    QString host = "https://noaa-gfs-bdp-pds.s3.amazonaws.com";
    QString resStr = "0p25";
    if (requestParams.resolution == 0.50f) resStr = "0p50";
    else if (requestParams.resolution == 1.0f) resStr = "1p00";

    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString urlStr = QString("%1/gfs.%2/%3/atmos/gfs.t%3z.pgrb2.%4.f%5.idx")
                         .arg(host)
                         .arg(cycleDate)
                         .arg(cycleHour)
                         .arg(resStr)
                         .arg(hourStr);

    QNetworkRequest request = Util::makeNetworkRequest(urlStr);
    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(slotIdxFinished()));
}

void NoaaGfsProvider::slotIdxFinished()
{
    if (isAborted) return;

    if (!currentReply || currentReply->error() != QNetworkReply::NoError) {
        if (currentStepIndex == 0) {
            emit signalGribLoadError(tr("Failed to fetch GFS index file from NOAA server: ") + 
                                     (currentReply ? currentReply->errorString() : "Unknown error"));
            return;
        }
        currentStepIndex++;
        processNextStep();
        return;
    }

    QByteArray idxData = currentReply->readAll();
    currentReply->deleteLater();
    currentReply = nullptr;

    QString idxText = QString::fromUtf8(idxData);
    QStringList lines = idxText.split('\n');

    QVector<ByteRange> ranges;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        QStringList parts = lines[i].split(':');
        if (parts.size() < 5) continue;

        qint64 startByte = parts[1].toLongLong();
        qint64 endByte = -1;
        if (i + 1 < lines.size()) {
            QStringList nextParts = lines[i + 1].split(':');
            if (nextParts.size() >= 2) {
                endByte = nextParts[1].toLongLong() - 1;
            }
        }

        QString varName = parts[3];
        QString levelStr = parts[4];

        if (isVariableRequested(varName, levelStr)) {
            ByteRange br;
            br.start = startByte;
            br.end = endByte;
            ranges.append(br);
        }
    }

    if (ranges.isEmpty()) {
        ByteRange fullRange = {0, -1};
        ranges.append(fullRange);
    }

    pendingRanges = ranges;
    currentRangeIndex = 0;
    fetchByteRangesForStep(currentHour, pendingRanges);
}

bool NoaaGfsProvider::isVariableRequested(const QString &var, const QString &level)
{
    if (requestParams.pressure && (var == "PRMSL" || var == "MSLET")) return true;
    if (requestParams.wind && (var == "UGRD" || var == "VGRD") && level.contains("10 m above ground")) return true;
    if (requestParams.temp && var == "TMP" && level.contains("2 m above ground")) return true;
    if (requestParams.humid && var == "RH" && level.contains("2 m above ground")) return true;
    if (requestParams.rain && (var == "APCP" || var == "PRATE")) return true;
    if (requestParams.cloud && (var == "TCDC" || var == "LCDC" || var == "MCDC" || var == "HCDC")) return true;
    if (requestParams.isotherm0 && var == "HGT" && level.contains("0C isotherm")) return true;
    if (requestParams.snowDepth && var == "SNOD") return true;
    if (requestParams.snowCateg && var == "CSNOW") return true;
    if (requestParams.frzRainCateg && var == "CFRZR") return true;
    if (requestParams.CAPEsfc && var == "CAPE" && level.contains("surface")) return true;
    if (requestParams.CINsfc && var == "CIN" && level.contains("surface")) return true;
    if (requestParams.reflectivity && (var == "REFC" || var == "Reflectivity")) return true;
    if (requestParams.GUSTsfc && var == "GUST" && level.contains("surface")) return true;

    if (level.contains("mb") || level.contains("hPa")) {
        bool levelMatched = false;
        if (requestParams.altitudeData200 && level.contains("200")) levelMatched = true;
        if (requestParams.altitudeData300 && level.contains("300")) levelMatched = true;
        if (requestParams.altitudeData400 && level.contains("400")) levelMatched = true;
        if (requestParams.altitudeData500 && level.contains("500")) levelMatched = true;
        if (requestParams.altitudeData600 && level.contains("600")) levelMatched = true;
        if (requestParams.altitudeData700 && level.contains("700")) levelMatched = true;
        if (requestParams.altitudeData850 && level.contains("850")) levelMatched = true;
        if (requestParams.altitudeData925 && level.contains("925")) levelMatched = true;

        if (levelMatched && (var == "HGT" || var == "TMP" || var == "UGRD" || var == "VGRD" || var == "RH" || var == "VVEL")) {
            return true;
        }
    }

    return false;
}

void NoaaGfsProvider::fetchByteRangesForStep(int hour, const QVector<ByteRange> &ranges)
{
    if (isAborted) return;

    if (currentRangeIndex >= ranges.size()) {
        currentStepIndex++;
        processNextStep();
        return;
    }

    ByteRange range = ranges[currentRangeIndex];

    QString host = "https://noaa-gfs-bdp-pds.s3.amazonaws.com";
    QString resStr = "0p25";
    if (requestParams.resolution == 0.50f) resStr = "0p50";
    else if (requestParams.resolution == 1.0f) resStr = "1p00";

    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString urlStr = QString("%1/gfs.%2/%3/atmos/gfs.t%3z.pgrb2.%4.f%5")
                         .arg(host)
                         .arg(cycleDate)
                         .arg(cycleHour)
                         .arg(resStr)
                         .arg(hourStr);

    QNetworkRequest request = Util::makeNetworkRequest(urlStr);
    if (range.end >= 0) {
        QString rangeHeader = QString("bytes=%1-%2").arg(range.start).arg(range.end);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    } else if (range.start > 0) {
        QString rangeHeader = QString("bytes=%1-").arg(range.start);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }

    if (currentReply) {
        currentReply->deleteLater();
    }
    currentReply = networkManager->get(request);
    connect(currentReply, SIGNAL(finished()), this, SLOT(signalGribByteChunkFinished()));
}

void NoaaGfsProvider::signalGribByteChunkFinished()
{
    if (isAborted) return;

    if (currentReply && (currentReply->error() == QNetworkReply::NoError || 
                         currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206)) {
        QByteArray chunk = currentReply->readAll();
        accumulatedGribData.append(chunk);
        totalBytesDownloaded += chunk.size();

        emit signalGribReadProgress(2, accumulatedGribData.size(), accumulatedGribData.size() + 1024);
        emit signalGribSendMessage(tr("Downloaded %1 KB (step %2/%3)...")
                                      .arg(totalBytesDownloaded / 1024)
                                      .arg(currentStepIndex + 1)
                                      .arg(forecastHours.size()));
    }

    currentReply->deleteLater();
    currentReply = nullptr;

    currentRangeIndex++;
    fetchByteRangesForStep(currentHour, pendingRanges);
}
