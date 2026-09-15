/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef DWD_ICON_PROVIDER_H
#define DWD_ICON_PROVIDER_H

#include "AbstractGribProvider.h"
#include <QVector>
#include <QStringList>

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

private:
    void processNextFile();
    QStringList buildParamList();
    QByteArray decompressBz2(const QByteArray &compressedData);

    GribRequestParams requestParams;
    QString cycleDate; // YYYYMMDD
    QString cycleHour; // 00, 06, 12, 18
    int currentStepIndex;
    int currentParamIndex;
    QVector<int> forecastHours;
    QStringList requestedParams;

    QByteArray accumulatedGribData;
    QNetworkReply *currentReply;
};

#endif // DWD_ICON_PROVIDER_H
