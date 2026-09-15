/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef METEO_FRANCE_PROVIDER_H
#define METEO_FRANCE_PROVIDER_H

#include "AbstractGribProvider.h"
#include <QVector>

class MeteoFranceProvider : public AbstractGribProvider
{
    Q_OBJECT
public:
    explicit MeteoFranceProvider(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~MeteoFranceProvider();

    void startDownload(const GribRequestParams &params) override;
    void stop() override;
    void abort() override;

private slots:
    void slotFileFinished();

private:
    void processNextStep();

    GribRequestParams requestParams;
    QString cycleDate;
    QString cycleHour;
    int currentStepIndex;
    QVector<int> forecastHours;

    QByteArray accumulatedGribData;
    QNetworkReply *currentReply;
};

#endif // METEO_FRANCE_PROVIDER_H
