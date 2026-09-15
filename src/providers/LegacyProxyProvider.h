/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef LEGACY_PROXY_PROVIDER_H
#define LEGACY_PROXY_PROVIDER_H

#include "AbstractGribProvider.h"

class LegacyProxyProvider : public AbstractGribProvider
{
    Q_OBJECT
public:
    explicit LegacyProxyProvider(QNetworkAccessManager *manager, QObject *parent = nullptr);
    ~LegacyProxyProvider();

    void startDownload(const GribRequestParams &params) override;
    void stop() override;
    void abort() override;

private slots:
    void downloadProgress(qint64 done, qint64 total);
    void slotNetworkError(QNetworkReply::NetworkError err);
    void slotFinished_step1();
    void slotFinished_step2();

private:
    QString fileName;
    QString checkSumSHA1;
    int fileSize;
    int step;
    bool downloadError;

    QNetworkReply *reply_step1;
    QNetworkReply *reply_step2;
    QByteArray arrayContent;
};

#endif // LEGACY_PROXY_PROVIDER_H
