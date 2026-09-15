/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef ECMWF_PROVIDER_H
#define ECMWF_PROVIDER_H

#include "AbstractGribProvider.h"
#include <QVector>

class EcmwfProvider : public AbstractGribProvider
{
    Q_OBJECT
public:
    explicit EcmwfProvider(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~EcmwfProvider();

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

#endif // ECMWF_PROVIDER_H
