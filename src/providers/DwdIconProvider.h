/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef DWD_ICON_PROVIDER_H
#define DWD_ICON_PROVIDER_H

#include "AbstractGribProvider.h"
#include <QVector>
#include <QStringList>
#include <QPair>

class DwdIconProvider : public AbstractGribProvider
{
    Q_OBJECT
public:
    explicit DwdIconProvider(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~DwdIconProvider();

    void startDownload(const GribRequestParams &params) override;
    void stop() override;
    void abort() override;

private slots:
    void slotFileFinished();
    void slotProbeFinished();

private:
    void processNextFile();
    QStringList buildParamList();
    QByteArray decompressBz2(const QByteArray &compressedData);

    void probeForLatestCycle();
    void probeNextCandidate();
    void buildForecastHoursAndStart();

    GribRequestParams requestParams;
    QString cycleDate; // YYYYMMDD
    QString cycleHour; // 00, 03, 06, 09, 12, 15, 18, 21
    int currentStepIndex;
    int currentParamIndex;
    QVector<int> forecastHours;
    QStringList requestedParams;

    QByteArray accumulatedGribData;
    QNetworkReply *currentReply;
    QNetworkReply *probeReply;

    // For probing latest available cycle
    QList<QPair<QString,QString>> probeCandidates; // (date, hour)
};

#endif // DWD_ICON_PROVIDER_H
