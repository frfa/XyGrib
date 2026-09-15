/**********************************************************************
XyGrib: meteorological GRIB file viewer
***********************************************************************/

#ifndef ABSTRACT_GRIB_PROVIDER_H
#define ABSTRACT_GRIB_PROVIDER_H

#include <QObject>
#include <QtNetwork>
#include <QString>
#include <QByteArray>
#include <QDateTime>

struct GribRequestParams {
    QString atmModel;
    float x0, y0, x1, y1;
    float resolution;
    int interval;
    int days;
    QString cycle;

    bool wind;
    bool pressure;
    bool rain;
    bool cloud;
    bool temp;
    bool humid;
    bool isotherm0;
    bool snowDepth;
    bool snowCateg;
    bool frzRainCateg;
    bool CAPEsfc;
    bool CINsfc;
    bool reflectivity;
    bool GUSTsfc;

    bool altitudeData200;
    bool altitudeData300;
    bool altitudeData400;
    bool altitudeData500;
    bool altitudeData600;
    bool altitudeData700;
    bool altitudeData850;
    bool altitudeData925;
    bool skewTData;

    QString wvModel;
    bool sgwh;
    bool swell;
    bool wwav;
};

class AbstractGribProvider : public QObject
{
    Q_OBJECT
public:
    explicit AbstractGribProvider(QNetworkAccessManager *manager, QObject *parent = nullptr)
        : QObject(parent), networkManager(manager), isAborted(false) {}
    virtual ~AbstractGribProvider() {}

    virtual void startDownload(const GribRequestParams &params) = 0;
    virtual void stop() { isAborted = true; }
    virtual void abort() { isAborted = true; }

signals:
    void signalGribDataReceived(QByteArray *content, QString fileName);
    void signalGribReadProgress(int step, int done, int total);
    void signalGribSendMessage(QString msg);
    void signalGribLoadError(const QString& error);
    void signalGribStartLoadData();

protected:
    QNetworkAccessManager *networkManager;
    bool isAborted;
};

#endif // ABSTRACT_GRIB_PROVIDER_H
