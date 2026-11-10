/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef NOAA_GFS_PROVIDER_H
#define NOAA_GFS_PROVIDER_H

#include <QElapsedTimer>
#include <QTimer>

#include "AbstractGribProvider.h"
#include <QVector>
#include <QPair>
#include <QUrlQuery>

struct ByteRange {
    qint64 start;
    qint64 end; // -1 if to EOF
};

class NoaaGfsProvider : public AbstractGribProvider
{
    Q_OBJECT
public:
    explicit NoaaGfsProvider(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~NoaaGfsProvider();

    void startDownload(const GribRequestParams &params) override;
    void stop() override;
    void abort() override;

private slots:
    void slotNomadsStepFinished();
    void slotIdxFinished();
    void signalGribByteChunkFinished();
    void slotWaveStepFinished();

private:
    void resolveCycleAndStart();
    void processNextStep();
    void fetchNomadsForStep(int hour);
    void fetchIdxForStep(int hour);
    void fetchByteRangesForStep(int hour, const QVector<ByteRange> &ranges);
    bool isVariableRequested(const QString &var, const QString &level);
    QUrl buildNomadsFilterUrl(int hour);

    // GFS-Wave (WAVEWATCH III driven by GFS) through the NOMADS wave filter.
    // Runs after the atmospheric steps, on the same cycle, and appends its
    // GRIB2 messages to the same buffer (single file for XyGrib).
    void processNextWaveStep();
    QUrl buildWaveFilterUrl(int hour);
    void finishDownload();

    // NOMADS pacing: >= 10 s between the END of one response and the start of the next
    // (requested by the official GRIB filter page). Applies only to nomads.ncep.noaa.gov;
    // S3 requests (.idx / byte ranges) do not go through here.
    static const int kNomadsPauseMs = 10000;
    QElapsedTimer lastNomadsEnd;   // invalid until the first response
    int downloadGeneration = 0;    // +1 per startDownload: invalidates pending waits
    void nomadsGet(const QUrl &url, void (NoaaGfsProvider::*slot)(), const QString &fetchMsg);
    void nomadsTick(const QUrl &url, void (NoaaGfsProvider::*slot)(), int gen, const QString &fetchMsg);

    GribRequestParams requestParams;
    QString cycleDate; // YYYYMMDD
    QString cycleHour; // 00, 06, 12, 18
    int currentStepIndex;
    QVector<int> forecastHours;

    bool usingNomadsFilter;
    bool usingFallback;

    QByteArray accumulatedGribData;
    qint64 totalBytesDownloaded;

    QNetworkReply *currentReply;
    QVector<ByteRange> pendingRanges;
    int currentRangeIndex;
    int currentHour;

    bool atmRequested;   // an atmospheric model was selected
    bool waveRequested;  // GFS-Wave data was selected
    bool inWavePhase;
    int waveStepIndex;
    int waveFilesOk;     // wave steps actually received
    QString waveNote;    // why wave data is missing or incomplete (shown to the user)
};

#endif // NOAA_GFS_PROVIDER_H
