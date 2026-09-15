/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#include "DwdIconProvider.h"
#include "Util.h"
#include <bzlib.h>
#include <cstdio>
#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

DwdIconProvider::DwdIconProvider(QNetworkAccessManager *manager, QObject *parent)
    : AbstractGribProvider(manager, parent),
      currentStepIndex(0),
      currentParamIndex(0),
      currentReply(nullptr),
      probeReply(nullptr)
{
}

DwdIconProvider::~DwdIconProvider()
{
    if (currentReply) {
        currentReply->deleteLater();
        currentReply = nullptr;
    }
    if (probeReply) {
        probeReply->deleteLater();
        probeReply = nullptr;
    }
}

void DwdIconProvider::stop()
{
    AbstractGribProvider::stop();
    if (currentReply) {
        currentReply->abort();
    }
    if (probeReply) {
        probeReply->abort();
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

    // ICON Global uses an icosahedral unstructured grid with CCSDS compression
    // (DRS Template 5.42) which is not supported by XyGrib's GRIB reader.
    // Only ICON-EU is provided on a regular lat-lon grid compatible with XyGrib.
    if (requestParams.atmModel == "ICON") {
        emit signalGribLoadError(tr(
            "DWD ICON Global uses an icosahedral unstructured grid and CCSDS "
            "compression (DRS Template 5.42) which cannot be displayed by XyGrib.\n\n"
            "Please select 'ICON-EU' instead for European coverage on a regular "
            "lat-lon grid."));
        return;
    }

    // ICON-EU runs every 3 hours: 00, 03, 06, 09, 12, 15, 18, 21 UTC
    // Files become available approximately 2-2.5 hours after the run time.
    if (params.cycle != "" && params.cycle.toLower() != "last") {
        cycleHour = params.cycle;
        if (cycleHour.length() == 1) cycleHour = "0" + cycleHour;
        // Determine date based on requested cycle
        QDateTime now = QDateTime::currentDateTimeUtc();
        cycleDate = now.toString("yyyyMMdd");
        // If cycle hour > current hour, use previous day
        if (cycleHour.toInt() > now.time().hour()) {
            cycleDate = now.addDays(-1).toString("yyyyMMdd");
        }
        buildForecastHoursAndStart();
    } else {
        // Auto-detect: probe the server for the latest available run
        emit signalGribSendMessage(tr("Detecting latest DWD ICON-EU cycle..."));
        probeForLatestCycle();
    }
}

void DwdIconProvider::probeForLatestCycle()
{
    // ICON-EU is published approximately 2.5 hours after the run time.
    // Build a list of candidate (date, cycleHour) pairs, most recent first,
    // going back up to 2 days to cover all likely scenarios.
    QDateTime now = QDateTime::currentDateTimeUtc();

    probeCandidates.clear();
    for (int i = 0; i < 20; i++) {
        // Step back i * 3 hours from now
        QDateTime t = now.addSecs((qint64)(-i) * 3 * 3600);
        // Round down to nearest 3h boundary
        int ch = (t.time().hour() / 3) * 3;
        QDateTime cycleTime = QDateTime(t.date(), QTime(ch, 0, 0), Qt::UTC);
        QString cDate = cycleTime.toString("yyyyMMdd");
        QString cHour = QString("%1").arg(ch, 2, 10, QChar('0'));
        QPair<QString,QString> candidate(cDate, cHour);
        if (!probeCandidates.contains(candidate)) {
            probeCandidates.append(candidate);
        }
    }

    probeNextCandidate();
}

void DwdIconProvider::probeNextCandidate()
{
    if (isAborted) return;
    if (probeCandidates.isEmpty()) {
        emit signalGribLoadError(tr("Could not determine latest DWD ICON-EU cycle. "
                                    "The DWD server may be updating. Please try again in a few minutes."));
        return;
    }

    auto candidate = probeCandidates.first();
    probeCandidates.removeFirst();

    QString probeDate = candidate.first;
    QString probeHour = candidate.second;

    // Probe for PMSL hour 000 of this cycle
    QString probeUrl = QString("https://opendata.dwd.de/weather/nwp/icon-eu/grib/%1/pmsl/"
                               "icon-eu_europe_regular-lat-lon_single-level_%2%3_000_PMSL.grib2.bz2")
                           .arg(probeHour)
                           .arg(probeDate)
                           .arg(probeHour);

    emit signalGribSendMessage(tr("Probing DWD ICON-EU %1z %2...").arg(probeHour).arg(probeDate));

    QNetworkRequest request = Util::makeNetworkRequest(probeUrl);
    request.setAttribute(QNetworkRequest::User, QVariant::fromValue(QPair<QString,QString>(probeDate, probeHour)));
    // Use HEAD request to avoid downloading the full file
    if (probeReply) probeReply->deleteLater();
    probeReply = networkManager->head(request);
    connect(probeReply, SIGNAL(finished()), this, SLOT(slotProbeFinished()));
}

void DwdIconProvider::slotProbeFinished()
{
    if (isAborted) return;

    if (!probeReply) return;

    bool found = (probeReply->error() == QNetworkReply::NoError);
    // Extract the candidate info from the request URL
    QString urlStr = probeReply->url().toString();
    // Parse date and hour from URL
    QRegularExpression re("single-level_(\\d{8})(\\d{2})_000");
    QRegularExpressionMatch m = re.match(urlStr);

    probeReply->deleteLater();
    probeReply = nullptr;

    if (found && m.hasMatch()) {
        cycleDate = m.captured(1);
        cycleHour = m.captured(2);
        buildForecastHoursAndStart();
    } else {
        probeNextCandidate();
    }
}

void DwdIconProvider::buildForecastHoursAndStart()
{
    forecastHours.clear();
    int maxHours = requestParams.days * 24;
    // ICON-EU provides hourly steps up to +78h, 3-hourly beyond
    int stepInt = requestParams.interval > 0 ? requestParams.interval : 1;
    for (int h = 0; h <= maxHours && h <= 78; h += stepInt) {
        forecastHours.append(h);
    }
    // If interval allows and we want beyond 78h, ICON-EU only goes to 120h at 3h steps
    if (maxHours > 78 && stepInt <= 3) {
        for (int h = 81; h <= qMin(maxHours, 120); h += 3) {
            if (!forecastHours.contains(h)) forecastHours.append(h);
        }
    }

    requestedParams = buildParamList();

    emit signalGribSendMessage(tr("Starting DWD ICON-EU download (%1z cycle, %2)...")
                                   .arg(cycleHour).arg(cycleDate));
    emit signalGribStartLoadData();

    processNextFile();
}

void DwdIconProvider::processNextFile()
{
    if (isAborted) return;

    if (currentStepIndex >= forecastHours.size()) {
        if (accumulatedGribData.isEmpty()) {
            emit signalGribLoadError(tr("No DWD ICON-EU data could be downloaded. "
                                        "Check selected cycle/date or try again later."));
            return;
        }

        QString outFileName = QString("icon-eu_%1_%2z.grb2").arg(cycleDate).arg(cycleHour);
        emit signalGribSendMessage(tr("ICON-EU download complete: %1 KB").arg(accumulatedGribData.size() / 1024));
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

    // ICON-EU: always regular-lat-lon
    QString hourStr = QString("%1").arg(hour, 3, 10, QChar('0'));
    QString paramUpper = param.toUpper();

    QString fileNameStr = QString("icon-eu_europe_regular-lat-lon_single-level_%1%2_%3_%4.grib2.bz2")
                              .arg(cycleDate)
                              .arg(cycleHour)
                              .arg(hourStr)
                              .arg(paramUpper);

    QString urlStr = QString("https://opendata.dwd.de/weather/nwp/icon-eu/grib/%1/%2/%3")
                         .arg(cycleHour)
                         .arg(param)
                         .arg(fileNameStr);

    int totalFiles = forecastHours.size() * requestedParams.size();
    int currentFileNum = currentStepIndex * requestedParams.size() + currentParamIndex + 1;
    emit signalGribSendMessage(tr("Fetching DWD ICON-EU [%1/%2] +%3h %4...")
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
            int httpCode = currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // 404 at hour=0 is unusual; for later hours it can mean the step doesn't exist
            if (httpCode != 404) {
                fprintf(stderr, "[DWD ICON-EU Warning] %s returned HTTP %d (%s)\n",
                        qPrintable(currentReply->url().toString()),
                        httpCode,
                        qPrintable(currentReply->errorString()));
            }
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

    fprintf(stderr, "[DWD ICON-EU Error] bzip2 decompression failed, error code %d\n", ret);
    return QByteArray();
}
