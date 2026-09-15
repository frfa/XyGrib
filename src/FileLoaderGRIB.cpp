/**********************************************************************
XyGrib: meteorological GRIB file viewer
Copyright (C) 2008-2012 - Jacques Zaninetti - http://www.zygrib.org

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/

#include "FileLoaderGRIB.h"
#include "providers/NoaaGfsProvider.h"
#include "providers/DwdIconProvider.h"
#include "providers/EcmwfProvider.h"
#include "providers/MeteoFranceProvider.h"
#include "providers/LegacyProxyProvider.h"
#include "Util.h"

FileLoaderGRIB::FileLoaderGRIB (QNetworkAccessManager *manager, QWidget *parent)
			: FileLoader (manager), activeProvider(nullptr)
{
	this->parent = parent;
}

FileLoaderGRIB::~FileLoaderGRIB () 
{
	if (activeProvider) {
		activeProvider->deleteLater();
		activeProvider = nullptr;
	}
}

void FileLoaderGRIB::stop () 
{
	if (activeProvider) {
		activeProvider->stop();
	}
}

void FileLoaderGRIB::abort ()
{
	if (activeProvider) {
		activeProvider->abort();
	}
}

void FileLoaderGRIB::getGribFile(
        const QString& atmModel,
        float x0, float x1, float y0, float y1,
        float resolution, int interval, int days,
        const QString& cycle,
        bool wind, bool pressure, bool rain,
        bool cloud, bool temp, bool humid, bool isotherm0,
        bool snowDepth,
		bool snowCateg, bool frzRainCateg,
        bool CAPEsfc, bool CINsfc, bool reflectivity,
		bool altitudeData200,
		bool altitudeData300,
		bool altitudeData400,
		bool altitudeData500,
		bool altitudeData600,
		bool altitudeData700,
		bool altitudeData850,
		bool altitudeData925,
		bool skewTData,
		bool GUSTsfc,
        const QString& wvModel,
        bool sgwh,
        bool swell,
        bool wwav
	)
{
    GribRequestParams params;
    params.atmModel = atmModel;
    params.x0 = x0; params.x1 = x1;
    params.y0 = y0; params.y1 = y1;
    params.resolution = resolution;
    params.interval = interval;
    params.days = days;
    params.cycle = cycle;
    params.wind = wind;
    params.pressure = pressure;
    params.rain = rain;
    params.cloud = cloud;
    params.temp = temp;
    params.humid = humid;
    params.isotherm0 = isotherm0;
    params.snowDepth = snowDepth;
    params.snowCateg = snowCateg;
    params.frzRainCateg = frzRainCateg;
    params.CAPEsfc = CAPEsfc;
    params.CINsfc = CINsfc;
    params.reflectivity = reflectivity;
    params.altitudeData200 = altitudeData200;
    params.altitudeData300 = altitudeData300;
    params.altitudeData400 = altitudeData400;
    params.altitudeData500 = altitudeData500;
    params.altitudeData600 = altitudeData600;
    params.altitudeData700 = altitudeData700;
    params.altitudeData850 = altitudeData850;
    params.altitudeData925 = altitudeData925;
    params.skewTData = skewTData;
    params.GUSTsfc = GUSTsfc;
    params.wvModel = wvModel;
    params.sgwh = sgwh;
    params.swell = swell;
    params.wwav = wwav;

    if (activeProvider) {
        activeProvider->deleteLater();
        activeProvider = nullptr;
    }

    bool useProxy = Util::getSetting("useLegacyProxyServer", false).toBool();

    if (useProxy) {
        activeProvider = new LegacyProxyProvider(networkManager, this);
    } else if (atmModel.contains("ICON")) {
        activeProvider = new DwdIconProvider(networkManager, this);
    } else if (atmModel == "ECMWF") {
        activeProvider = new EcmwfProvider(networkManager, this);
    } else if (atmModel.contains("Arpege") || atmModel.contains("Arome")) {
        activeProvider = new MeteoFranceProvider(networkManager, this);
    } else {
        // GFS, NAM, or default
        activeProvider = new NoaaGfsProvider(networkManager, this);
    }

    connect(activeProvider, &AbstractGribProvider::signalGribDataReceived,
            this, &FileLoaderGRIB::signalGribDataReceived);
    connect(activeProvider, &AbstractGribProvider::signalGribReadProgress,
            this, &FileLoaderGRIB::signalGribReadProgress);
    connect(activeProvider, &AbstractGribProvider::signalGribSendMessage,
            this, &FileLoaderGRIB::signalGribSendMessage);
    connect(activeProvider, &AbstractGribProvider::signalGribStartLoadData,
            this, &FileLoaderGRIB::signalGribStartLoadData);
    connect(activeProvider, &AbstractGribProvider::signalGribLoadError,
            this, &FileLoaderGRIB::signalGribLoadError);

    activeProvider->startDownload(params);
}
