#include "link2railclient.h"

#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkCookie>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QXmlStreamReader>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSslSocket>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {

void stampProvider(Link2RailWagonRecord &rec, TelematicsProviderId id)
{
    const TelematicsProviderInfo pi = Link2RailClient::providerInfo(id);
    rec.providerId = id;
    rec.providerKey = pi.key;
    rec.providerName = pi.shortName;
    rec.providerColor = pi.badgeColor;
}

QString firstString(const QJsonObject &obj, const QStringList &keys)
{
    for (const QString &k : keys) {
        const QJsonValue v = obj.value(k);
        if (v.isString()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) return s;
        } else if (v.isDouble()) {
            const double d = v.toDouble();
            if (std::isfinite(d)) {
                return QString::number(static_cast<qint64>(std::llround(d)));
            }
        }
    }
    return QString();
}

bool firstDouble(const QJsonObject &obj, const QStringList &keys, double &outVal)
{
    for (const QString &k : keys) {
        const QJsonValue v = obj.value(k);
        if (v.isDouble()) {
            const double d = v.toDouble();
            if (std::isfinite(d)) {
                outVal = d;
                return true;
            }
        } else if (v.isString()) {
            bool ok = false;
            QString s = v.toString().trimmed();
            s.replace(',', '.');
            const double d = s.toDouble(&ok);
            if (ok && std::isfinite(d)) {
                outVal = d;
                return true;
            }
        }
    }
    return false;
}

bool firstInt(const QJsonObject &obj, const QStringList &keys, int &outVal)
{
    double d = 0.0;
    if (firstDouble(obj, keys, d)) {
        outVal = static_cast<int>(std::llround(d));
        return true;
    }
    return false;
}

bool extractLatLonFromObject(const QJsonObject &obj, double &lat, double &lon)
{
    const QStringList latKeys = {
        QStringLiteral("latitude"), QStringLiteral("lat"),
        QStringLiteral("gpsLat"), QStringLiteral("gpsLatitude"),
        QStringLiteral("lastLat"), QStringLiteral("lastLatitude"),
        QStringLiteral("snappedTrackLat"), QStringLiteral("rawGnssLat"),
        QStringLiteral("geoLat"), QStringLiteral("y")
    };
    const QStringList lonKeys = {
        QStringLiteral("longitude"), QStringLiteral("lon"), QStringLiteral("lng"),
        QStringLiteral("gpsLon"), QStringLiteral("gpsLongitude"),
        QStringLiteral("lastLon"), QStringLiteral("lastLng"), QStringLiteral("lastLongitude"),
        QStringLiteral("snappedTrackLon"), QStringLiteral("rawGnssLon"),
        QStringLiteral("geoLon"), QStringLiteral("x")
    };

    double la = 0.0, lo = 0.0;
    if (firstDouble(obj, latKeys, la) && firstDouble(obj, lonKeys, lo)) {
        if (std::abs(la) > 0.001 && std::abs(lo) > 0.001 && std::abs(la) <= 90.0 && std::abs(lo) <= 180.0) {
            lat = la;
            lon = lo;
            return true;
        }
    }

    // GeoJSON geometry: { "type": "Point", "coordinates": [lon, lat] }
    if (obj.value(QStringLiteral("coordinates")).isArray()) {
        const QJsonArray coords = obj.value(QStringLiteral("coordinates")).toArray();
        if (coords.size() >= 2 && coords[0].isDouble() && coords[1].isDouble()) {
            const double c0 = coords[0].toDouble();
            const double c1 = coords[1].toDouble();
            if (std::abs(c1) <= 90.0 && std::abs(c0) <= 180.0 && (std::abs(c0) > 0.001 || std::abs(c1) > 0.001)) {
                lon = c0;
                lat = c1;
                return true;
            }
        }
    }

    const QStringList nestedKeys = {
        QStringLiteral("position"), QStringLiteral("geoPosition"), QStringLiteral("gps"),
        QStringLiteral("gpsPosition"), QStringLiteral("location"), QStringLiteral("coordinates"),
        QStringLiteral("geometry"), QStringLiteral("lastPosition"), QStringLiteral("currentPosition"),
        QStringLiteral("lastEvent"), QStringLiteral("latestEvent"), QStringLiteral("telematics"),
        QStringLiteral("gnss"), QStringLiteral("lastKnownPosition")
    };
    for (const QString &nk : nestedKeys) {
        if (obj.value(nk).isObject()) {
            if (extractLatLonFromObject(obj.value(nk).toObject(), lat, lon)) {
                return true;
            }
        }
    }

    return false;
}

void flattenObjectToMap(const QJsonObject &obj, const QString &prefix, QVariantMap &outMap)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString fullKey = prefix.isEmpty() ? it.key() : (prefix + QStringLiteral(".") + it.key());
        const QJsonValue v = it.value();
        if (v.isObject()) {
            flattenObjectToMap(v.toObject(), fullKey, outMap);
        } else if (v.isArray()) {
            const QJsonArray arr = v.toArray();
            if (arr.size() <= 6) {
                QStringList parts;
                for (const QJsonValue &av : arr) {
                    if (av.isString()) parts << av.toString();
                    else if (av.isDouble()) parts << QString::number(av.toDouble());
                    else if (av.isBool()) parts << (av.isBool() ? QStringLiteral("true") : QStringLiteral("false"));
                }
                if (!parts.isEmpty()) {
                    outMap.insert(fullKey, parts.join(QStringLiteral(", ")));
                }
            }
        } else if (!v.isNull() && !v.isUndefined()) {
            outMap.insert(fullKey, v.toVariant());
        }
    }
}

Link2RailWagonRecord parseSingleWagonObject(const QJsonObject &obj,
                                            const QString &parentTrainId,
                                            const QString &parentOrigin,
                                            const QString &parentDest,
                                            const QString &parentStatus,
                                            const QString &sourceTag,
                                            TelematicsProviderId providerId)
{
    Link2RailWagonRecord rec;
    stampProvider(rec, providerId);
    rec.sourceEndpoint = sourceTag;

    QJsonObject props = obj;
    if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("Feature") &&
        obj.value(QStringLiteral("properties")).isObject()) {
        props = obj.value(QStringLiteral("properties")).toObject();
    }

    rec.evn = firstString(props, {
        QStringLiteral("wagonNumber"), QStringLiteral("evn"), QStringLiteral("EVN"),
        QStringLiteral("uic"), QStringLiteral("uicNumber"), QStringLiteral("equipmentNumber"),
        QStringLiteral("vehicleNumber"), QStringLiteral("wagonId"), QStringLiteral("assetId"),
        QStringLiteral("assetName"), QStringLiteral("unitId"), QStringLiteral("unitName"),
        QStringLiteral("freightCarNumber"), QStringLiteral("name"), QStringLiteral("id")
    });

    rec.trainNumber = firstString(props, {
        QStringLiteral("trainNumber"), QStringLiteral("consistId"), QStringLiteral("trainId"),
        QStringLiteral("trainChainId"), QStringLiteral("groupId"), QStringLiteral("fleetName")
    });
    if (rec.trainNumber.isEmpty()) {
        rec.trainNumber = parentTrainId;
    }

    rec.shipmentId = firstString(props, {
        QStringLiteral("shipmentNumber"), QStringLiteral("shipmentId"), QStringLiteral("consignmentId"),
        QStringLiteral("consignmentNumber"), QStringLiteral("orderNumber"), QStringLiteral("orderId")
    });

    rec.wagonTypeCode = firstString(props, {
        QStringLiteral("wagonClass"), QStringLiteral("wagonType"), QStringLiteral("equipmentType"),
        QStringLiteral("vehicleType"), QStringLiteral("assetType"), QStringLiteral("letterMarking"),
        QStringLiteral("gattung"), QStringLiteral("typeCode"), QStringLiteral("type")
    });

    firstInt(props, {QStringLiteral("axles"), QStringLiteral("axleCount"), QStringLiteral("numberOfAxles")}, rec.axleCount);
    firstDouble(props, {QStringLiteral("lengthOverBuffersMeters"), QStringLiteral("lengthMeters"), QStringLiteral("length")}, rec.lengthMeters);

    double lat = 0.0, lon = 0.0;
    if (extractLatLonFromObject(obj, lat, lon) || extractLatLonFromObject(props, lat, lon)) {
        rec.hasValidGps = true;
        rec.rawLat = lat;
        rec.rawLon = lon;
    }

    firstDouble(props, {QStringLiteral("speedKmh"), QStringLiteral("speed"), QStringLiteral("velocity")}, rec.speedKmh);
    firstDouble(props, {QStringLiteral("headingDeg"), QStringLiteral("heading"), QStringLiteral("course"), QStringLiteral("bearing")}, rec.headingDeg);
    firstDouble(props, {QStringLiteral("gnssAccuracyMeters"), QStringLiteral("accuracy"), QStringLiteral("gpsAccuracy")}, rec.gpsAccuracyMeters);

    const QJsonObject lastEv = props.value(QStringLiteral("lastEvent")).isObject()
                                   ? props.value(QStringLiteral("lastEvent")).toObject()
                                   : props.value(QStringLiteral("latestEvent")).toObject();
    const QJsonObject geoObj = props.value(QStringLiteral("geofenceInfo")).isObject()
                                   ? props.value(QStringLiteral("geofenceInfo")).toObject()
                                   : props.value(QStringLiteral("geofence")).toObject();
    const QJsonObject locObj = props.value(QStringLiteral("location")).isObject()
                                   ? props.value(QStringLiteral("location")).toObject()
                                   : lastEv.value(QStringLiteral("location")).toObject();

    rec.statusText = firstString(props, {
        QStringLiteral("currentStatus"), QStringLiteral("status"), QStringLiteral("operationState"),
        QStringLiteral("movementStatus"), QStringLiteral("state"), QStringLiteral("transportStatus"),
        QStringLiteral("motionState")
    });
    if (rec.statusText.isEmpty()) rec.statusText = parentStatus;

    rec.lastEventType = firstString(lastEv, {QStringLiteral("eventType"), QStringLiteral("type"), QStringLiteral("code"), QStringLiteral("description")});
    if (rec.lastEventType.isEmpty()) {
        rec.lastEventType = firstString(props, {QStringLiteral("lastEventType"), QStringLiteral("eventType")});
    }

    rec.locationName = firstString(locObj, {QStringLiteral("name"), QStringLiteral("locationName"), QStringLiteral("stationName"), QStringLiteral("city"), QStringLiteral("address")});
    if (rec.locationName.isEmpty()) {
        rec.locationName = firstString(lastEv, {QStringLiteral("locationName"), QStringLiteral("stationName"), QStringLiteral("station")});
    }
    if (rec.locationName.isEmpty()) {
        rec.locationName = firstString(props, {QStringLiteral("locationName"), QStringLiteral("stationName"), QStringLiteral("operatingPointDS100"), QStringLiteral("currentLocation"), QStringLiteral("address")});
    }

    rec.locationCode = firstString(locObj, {QStringLiteral("code"), QStringLiteral("locationCode"), QStringLiteral("ds100"), QStringLiteral("plc")});
    if (rec.locationCode.isEmpty()) {
        rec.locationCode = firstString(lastEv, {QStringLiteral("locationCode"), QStringLiteral("ds100"), QStringLiteral("plc")});
    }
    if (rec.locationCode.isEmpty()) {
        rec.locationCode = firstString(props, {QStringLiteral("locationCode"), QStringLiteral("operatingPointDS100"), QStringLiteral("ds100")});
    }

    rec.geofenceSite = firstString(geoObj, {QStringLiteral("currentSite"), QStringLiteral("siteName"), QStringLiteral("name"), QStringLiteral("geofenceName")});
    if (rec.geofenceSite.isEmpty()) {
        rec.geofenceSite = firstString(props, {QStringLiteral("geofenceName"), QStringLiteral("currentSite"), QStringLiteral("siteName"), QStringLiteral("poiName")});
    }

    if (!firstInt(geoObj, {QStringLiteral("standingTimeMinutes"), QStringLiteral("dwellTimeMinutes"), QStringLiteral("idleTimeMinutes")}, rec.standingTimeMinutes)) {
        firstInt(props, {QStringLiteral("standingTimeMinutes"), QStringLiteral("dwellTimeMinutes"), QStringLiteral("idleTimeMinutes"), QStringLiteral("standingMinutes")}, rec.standingTimeMinutes);
    }

    rec.timestampIso = firstString(props, {
        QStringLiteral("gpsTimestamp"), QStringLiteral("positionTimestamp"), QStringLiteral("timestamp"),
        QStringLiteral("lastUpdate"), QStringLiteral("updatedAt"), QStringLiteral("eventTimestamp"),
        QStringLiteral("lastCommunication"), QStringLiteral("recordedAt")
    });
    if (rec.timestampIso.isEmpty()) {
        rec.timestampIso = firstString(lastEv, {QStringLiteral("timestamp"), QStringLiteral("eventTimestamp"), QStringLiteral("dateTime")});
    }

    rec.cargoDescription = firstString(props, {
        QStringLiteral("product"), QStringLiteral("cargo"), QStringLiteral("cargoDescription"),
        QStringLiteral("commodity"), QStringLiteral("nhmDescription"), QStringLiteral("goods")
    });
    firstDouble(props, {
        QStringLiteral("tons"), QStringLiteral("weightTons"),
        QStringLiteral("netWeightTons"), QStringLiteral("grossWeightTons"),
        QStringLiteral("netWeight"), QStringLiteral("grossWeight"),
        QStringLiteral("netWeightKg"), QStringLiteral("grossWeightKg"),
        QStringLiteral("weight")
    }, rec.weightTons);
    if (rec.weightTons > 500.0) {
        rec.weightTons /= 1000.0;
    }

    if (props.contains(QStringLiteral("isLoaded")) && props.value(QStringLiteral("isLoaded")).isBool()) {
        rec.isLoadedKnown = true;
        rec.isLoaded = props.value(QStringLiteral("isLoaded")).toBool();
    } else if (props.contains(QStringLiteral("loaded")) && props.value(QStringLiteral("loaded")).isBool()) {
        rec.isLoadedKnown = true;
        rec.isLoaded = props.value(QStringLiteral("loaded")).toBool();
    } else {
        const QString loadState = firstString(props, {QStringLiteral("loadState"), QStringLiteral("loadingStatus"), QStringLiteral("loadedEmpty")}).toLower();
        if (loadState.contains(QStringLiteral("empty")) || loadState.contains(QStringLiteral("leer"))) {
            rec.isLoadedKnown = true;
            rec.isLoaded = false;
        } else if (loadState.contains(QStringLiteral("load")) || loadState.contains(QStringLiteral("belad")) || loadState.contains(QStringLiteral("full"))) {
            rec.isLoadedKnown = true;
            rec.isLoaded = true;
        }
    }

    rec.originName = firstString(props, {QStringLiteral("origin"), QStringLiteral("originName"), QStringLiteral("departureStation"), QStringLiteral("sourceNode"), QStringLiteral("sender")});
    if (rec.originName.isEmpty()) rec.originName = parentOrigin;

    rec.destinationName = firstString(props, {QStringLiteral("destination"), QStringLiteral("destinationName"), QStringLiteral("arrivalStation"), QStringLiteral("sinkNode"), QStringLiteral("consignee")});
    if (rec.destinationName.isEmpty()) rec.destinationName = parentDest;

    rec.etaIso = firstString(props, {QStringLiteral("eta"), QStringLiteral("estimatedArrival"), QStringLiteral("estimatedTimeOfArrival")});
    rec.telematicsBoxId = firstString(props, {QStringLiteral("telematicsBoxId"), QStringLiteral("deviceId"), QStringLiteral("trackerId"), QStringLiteral("sensorId"), QStringLiteral("imei")});
    firstDouble(props, {QStringLiteral("torpedoHotMetalTempC"), QStringLiteral("temperatureC"), QStringLiteral("temperature")}, rec.temperatureC);
    firstInt(props, {QStringLiteral("batteryPercent"), QStringLiteral("batteryLevel"), QStringLiteral("battery")}, rec.batteryPercent);

    flattenObjectToMap(obj, QString(), rec.rawAttributes);
    return rec;
}

void collectRecordsRecursive(const QJsonValue &val,
                             const QString &parentTrainId,
                             const QString &parentOrigin,
                             const QString &parentDest,
                             const QString &parentStatus,
                             const QString &sourceTag,
                             TelematicsProviderId providerId,
                             QVector<Link2RailWagonRecord> &out)
{
    if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        for (const QJsonValue &item : arr) {
            collectRecordsRecursive(item, parentTrainId, parentOrigin, parentDest, parentStatus, sourceTag, providerId, out);
        }
        return;
    }

    if (!val.isObject()) {
        return;
    }

    const QJsonObject obj = val.toObject();

    QString ctxTrain = firstString(obj, {
        QStringLiteral("consistId"), QStringLiteral("trainNumber"), QStringLiteral("trainId"),
        QStringLiteral("trainChainId"), QStringLiteral("shipmentNumber"), QStringLiteral("shipmentId")
    });
    if (ctxTrain.isEmpty()) ctxTrain = parentTrainId;

    QString ctxOrig = firstString(obj, {QStringLiteral("sourceNode"), QStringLiteral("origin"), QStringLiteral("originName"), QStringLiteral("departureStation")});
    if (ctxOrig.isEmpty()) ctxOrig = parentOrigin;

    QString ctxDest = firstString(obj, {QStringLiteral("sinkNode"), QStringLiteral("destination"), QStringLiteral("destinationName"), QStringLiteral("arrivalStation")});
    if (ctxDest.isEmpty()) ctxDest = parentDest;

    QString ctxStatus = firstString(obj, {QStringLiteral("operationState"), QStringLiteral("currentStatus"), QStringLiteral("status")});
    if (ctxStatus.isEmpty()) ctxStatus = parentStatus;

    const QStringList containerKeys = {
        QStringLiteral("wagons"), QStringLiteral("equipments"), QStringLiteral("vehicles"),
        QStringLiteral("consists"), QStringLiteral("trains"), QStringLiteral("shipments"),
        QStringLiteral("consignments"), QStringLiteral("positions"), QStringLiteral("features"),
        QStringLiteral("items"), QStringLiteral("data"), QStringLiteral("results"),
        QStringLiteral("content"), QStringLiteral("geofences"), QStringLiteral("assets"),
        QStringLiteral("units"), QStringLiteral("devices"), QStringLiteral("fleet"),
        QStringLiteral("trackingHistory"), QStringLiteral("tracking-history")
    };

    bool hadSubCollection = false;
    for (const QString &ck : containerKeys) {
        if (obj.value(ck).isArray()) {
            const QJsonArray subArr = obj.value(ck).toArray();
            if (!subArr.isEmpty()) {
                hadSubCollection = true;
                collectRecordsRecursive(subArr, ctxTrain, ctxOrig, ctxDest, ctxStatus, sourceTag, providerId, out);
            }
        } else if (obj.value(ck).isObject()) {
            hadSubCollection = true;
            collectRecordsRecursive(obj.value(ck), ctxTrain, ctxOrig, ctxDest, ctxStatus, sourceTag, providerId, out);
        }
    }

    if (obj.value(QStringLiteral("wagonData")).isObject()) {
        hadSubCollection = true;
        collectRecordsRecursive(obj.value(QStringLiteral("wagonData")), ctxTrain, ctxOrig, ctxDest, ctxStatus, sourceTag, providerId, out);
    }

    if (hadSubCollection) {
        return;
    }

    Link2RailWagonRecord rec = parseSingleWagonObject(obj, ctxTrain, ctxOrig, ctxDest, ctxStatus, sourceTag, providerId);
    if (rec.hasValidGps || !rec.evn.isEmpty() || !rec.trainNumber.isEmpty()) {
        if (rec.evn.isEmpty()) {
            const QString pKey = Link2RailClient::providerInfo(providerId).key.toUpper();
            rec.evn = !rec.trainNumber.isEmpty()
                          ? QString("%1-%2-%3").arg(pKey, rec.trainNumber).arg(out.size() + 1)
                          : QString("%1-WG-%2").arg(pKey).arg(out.size() + 1);
        }
        out.append(rec);
    }
}

QString normalizeEvnKey(const QString &evn)
{
    QString k = evn.trimmed().toUpper();
    k.remove(' ');
    k.remove('-');
    return k;
}

} // namespace

// =============================================================================
// PROVIDER-KATALOG (ALLE 6 DIENSTE)
// =============================================================================

QVector<TelematicsProviderInfo> Link2RailClient::allProviders()
{
    return {
        {
            TelematicsProviderId::Link2Rail,
            QStringLiteral("link2rail"),
            QStringLiteral("DB link2rail"),
            QStringLiteral("link2rail"),
            QStringLiteral("DB Cargo link2rail (Track & Trace / Geofence Control)"),
            QStringLiteral("https://link2rail.dbcargo.com/"),
            QStringLiteral("DB Cargo Kundenportal & API Marketplace (CIDP Login API V3, Geofence Control, Track & Trace)."),
            QColor("#0D9488")
        },
        {
            TelematicsProviderId::IntermodalTelematics,
            QStringLiteral("imt"),
            QStringLiteral("Intermodal (IMT)"),
            QStringLiteral("IMT"),
            QStringLiteral("Intermodal Telematics (IMT Portal & Telematik-API)"),
            QStringLiteral("https://portal.intermodaltelematics.com/"),
            QStringLiteral("Intermodal Telematics GPS-, Temperatur-, Druck- & Flotten-Telematik (portal.intermodaltelematics.com)."),
            QColor("#2563EB")
        },
        {
            TelematicsProviderId::DotTelematik,
            QStringLiteral("dot"),
            QStringLiteral("DOT Telematik"),
            QStringLiteral("DOT"),
            QStringLiteral("DOT Telematik (DOT-Link & DOT Unified API)"),
            QStringLiteral("https://portal.dot-telematik.com/login"),
            QStringLiteral("DOT Telematik X-Rayl® GPS-Ortung, Geofences & Laufleistung (portal.dot-telematik.com)."),
            QColor("#7C3AED")
        },
        {
            TelematicsProviderId::Transwaggon,
            QStringLiteral("transwaggon"),
            QStringLiteral("TRANSWAGGON"),
            QStringLiteral("TRANSWAGGON"),
            QStringLiteral("TRANSWAGGON WOPS / CIS & Freight Buddy Telematik"),
            QStringLiteral("https://portal.transwaggon.de/csp/wops/index.html?err=1&langID=DE"),
            QStringLiteral("TRANSWAGGON WOPS-Portal & GPS-Waggonverfolgung (JSON / XML / CSV, portal.transwaggon.de)."),
            QColor("#D97706")
        },
        {
            TelematicsProviderId::Innofreight,
            QStringLiteral("innofreight"),
            QStringLiteral("Innofreight OS"),
            QStringLiteral("Innofreight"),
            QStringLiteral("Innofreight OS (InnoWaggon / InnoTainer Telematik)"),
            QStringLiteral("https://os.innofreight.com/login"),
            QStringLiteral("Innofreight Operating System – GPS-Ortung & Flottenstatus (os.innofreight.com)."),
            QColor("#16A34A")
        },
        {
            TelematicsProviderId::Wascosa,
            QStringLiteral("wascosa"),
            QStringLiteral("Wascosa"),
            QStringLiteral("Wascosa"),
            QStringLiteral("Wascosa Telematik (SAVVY Synergy / Nexxiot / Wascosa Digital)"),
            QStringLiteral("https://www.wascosa.com/"),
            QStringLiteral("Wascosa Güterwagen-Telematik inkl. Vorlagen für SAVVY® Synergy & Nexxiot Connect Cloud."),
            QColor("#E11D48")
        }
    };
}

TelematicsProviderInfo Link2RailClient::providerInfo(TelematicsProviderId id)
{
    const auto all = allProviders();
    for (const auto &p : all) {
        if (p.id == id) return p;
    }
    return all.first();
}

QStringList Link2RailClient::presetNamesForProvider(TelematicsProviderId id)
{
    switch (id) {
    case TelematicsProviderId::Link2Rail:
        return {
            QStringLiteral("DB API Marketplace – Login API V3 (Produktion)"),
            QStringLiteral("DB API Marketplace – Login API V3 (DEMO / Test)"),
            QStringLiteral("link2rail Webportal – Browser-Session / Bearer-Token / Cookie"),
            QStringLiteral("OAuth2 / OIDC Token-Endpunkt (Benutzerdefiniert)")
        };
    case TelematicsProviderId::IntermodalTelematics:
        return {
            QStringLiteral("IMT Teno Portal-Login (userId + Passwort + Account-Alias -> x-session-token)"),
            QStringLiteral("IMT Browser-Session / x-session-token (Microsoft SSO / Entra)"),
            QStringLiteral("IMT REST API – OAuth2 Token-Endpunkt (Client-ID / Secret)")
        };
    case TelematicsProviderId::DotTelematik:
        return {
            QStringLiteral("DOT Unified API – Direkter API-Key / Bearer-Token / Browser-Session"),
            QStringLiteral("DOT-Link Portal – JSON Login-POST (Benutzer + Passwort)"),
            QStringLiteral("DOT Unified API – HTTP Basic Auth")
        };
    case TelematicsProviderId::Transwaggon:
        return {
            QStringLiteral("TRANSWAGGON WOPS – Formular-Login (twa.portal.Home.cls + Cookie)"),
            QStringLiteral("TRANSWAGGON CIS / Freight Buddy – HTTP Basic Auth (JSON/XML)"),
            QStringLiteral("TRANSWAGGON Browser-Session / Cookie / API-Key")
        };
    case TelematicsProviderId::Innofreight:
        return {
            QStringLiteral("Innofreight OS – Portal-Login (JSON-POST)"),
            QStringLiteral("Innofreight OS – OAuth2 Password Grant"),
            QStringLiteral("Innofreight OS – Browser-Session / Bearer-Token / Cookie")
        };
    case TelematicsProviderId::Wascosa:
        return {
            QStringLiteral("Wascosa / Nexxiot Connect Cloud – API Login (cloud.nexxiot.net)"),
            QStringLiteral("Wascosa / SAVVY® Synergy – Portal Login (www.savvy-synergy.com)"),
            QStringLiteral("Wascosa Digital – Browser-Session / Bearer-Token / API-Key")
        };
    }
    return {QStringLiteral("Standard")};
}

Link2RailConfig Link2RailClient::presetConfig(TelematicsProviderId id, int presetIndex)
{
    Link2RailConfig cfg;
    cfg.providerId = id;
    const QStringList names = presetNamesForProvider(id);
    cfg.environmentName = (presetIndex >= 0 && presetIndex < names.size()) ? names[presetIndex] : names.first();

    switch (id) {
    case TelematicsProviderId::Link2Rail:
        if (presetIndex == 1) {
            cfg.authMode = Link2RailAuthMode::PortalLoginCidP;
            cfg.loginUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/login-demo/v3/api/v1/authenticate");
            cfg.gpsTelemetryUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/geofence-control-demo/v1/wagons");
            cfg.trackTraceUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/track-and-trace/v1/wagons");
        } else if (presetIndex == 2) {
            cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
            cfg.loginUrl = QStringLiteral("https://link2rail.dbcargo.com/");
            cfg.gpsTelemetryUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/geofence-control/v1/wagons");
            cfg.trackTraceUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/track-and-trace/v1/wagons");
            cfg.trainMgmtUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/train-management/v1/trains");
        } else if (presetIndex == 3) {
            cfg.authMode = Link2RailAuthMode::OAuth2PasswordGrant;
            cfg.loginUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/oauth2/token");
            cfg.gpsTelemetryUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/geofence-control/v1/wagons");
            cfg.trackTraceUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/track-and-trace/v1/wagons");
        } else {
            cfg.authMode = Link2RailAuthMode::PortalLoginCidP;
            cfg.loginUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/login/v3/api/v1/authenticate");
            cfg.gpsTelemetryUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/geofence-control/v1/wagons");
            cfg.trackTraceUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/track-and-trace/v1/wagons");
            cfg.trainMgmtUrl = QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/train-management/v1/trains");
            cfg.enableTrackTraceEndpoint = true;
        }
        break;

    case TelematicsProviderId::IntermodalTelematics:
        cfg.loginUrl = QStringLiteral("https://portal.intermodaltelematics.com/authorization/v1/authentication");
        cfg.gpsTelemetryUrl = QStringLiteral("https://portal.intermodaltelematics.com/user/v1/assets");
        cfg.trackTraceUrl = QStringLiteral("https://portal.intermodaltelematics.com/common/v1/tokenstatus");
        cfg.sessionTokenHeaderName = QStringLiteral("x-session-token");
        if (presetIndex == 1) cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
        else if (presetIndex == 2) cfg.authMode = Link2RailAuthMode::OAuth2PasswordGrant;
        else cfg.authMode = Link2RailAuthMode::ImtTenoAuth;
        break;

    case TelematicsProviderId::DotTelematik:
        cfg.loginUrl = QStringLiteral("https://portal.dot-telematik.com/api/auth/login");
        cfg.gpsTelemetryUrl = QStringLiteral("https://api.dot-telematik.com/v1/assets/positions");
        cfg.trackTraceUrl = QStringLiteral("https://api.dot-telematik.com/v1/geofences/assets");
        if (presetIndex == 1) {
            cfg.authMode = Link2RailAuthMode::PortalLoginCidP;
            cfg.gpsTelemetryUrl = QStringLiteral("https://portal.dot-telematik.com/api/v1/assets/positions");
        } else if (presetIndex == 2) {
            cfg.authMode = Link2RailAuthMode::HttpBasicAuth;
        } else {
            cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
        }
        break;

    case TelematicsProviderId::Transwaggon:
        cfg.loginUrl = QStringLiteral("https://portal.transwaggon.de/csp/wops/twa.portal.Home.cls");
        cfg.gpsTelemetryUrl = QStringLiteral("https://portal.transwaggon.de/csp/wops/api/wagons/positions");
        cfg.trackTraceUrl = QStringLiteral("https://portal.transwaggon.de/csp/wops/api/wagons/tracking");
        if (presetIndex == 1) cfg.authMode = Link2RailAuthMode::HttpBasicAuth;
        else if (presetIndex == 2) cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
        else cfg.authMode = Link2RailAuthMode::HtmlFormPost;
        break;

    case TelematicsProviderId::Innofreight:
        cfg.loginUrl = QStringLiteral("https://os.innofreight.com/api/auth/login");
        cfg.gpsTelemetryUrl = QStringLiteral("https://os.innofreight.com/api/v1/wagons/positions");
        cfg.trackTraceUrl = QStringLiteral("https://os.innofreight.com/api/v1/shipments");
        if (presetIndex == 1) {
            cfg.authMode = Link2RailAuthMode::OAuth2PasswordGrant;
            cfg.loginUrl = QStringLiteral("https://os.innofreight.com/oauth/token");
        } else if (presetIndex == 2) {
            cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
        } else {
            cfg.authMode = Link2RailAuthMode::PortalLoginCidP;
        }
        break;

    case TelematicsProviderId::Wascosa:
        if (presetIndex == 1) {
            cfg.authMode = Link2RailAuthMode::PortalLoginCidP;
            cfg.loginUrl = QStringLiteral("https://www.savvy-synergy.com/api/auth/login");
            cfg.gpsTelemetryUrl = QStringLiteral("https://www.savvy-synergy.com/api/v1/assets/positions");
            cfg.trackTraceUrl = QStringLiteral("https://www.savvy-synergy.com/api/v1/assets");
        } else if (presetIndex == 2) {
            cfg.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
            cfg.loginUrl = QStringLiteral("https://cloud.nexxiot.net/oauth/token");
            cfg.gpsTelemetryUrl = QStringLiteral("https://cloud.nexxiot.net/api/v1/assets/positions");
            cfg.trackTraceUrl = QStringLiteral("https://cloud.nexxiot.net/api/v1/assets");
        } else {
            cfg.authMode = Link2RailAuthMode::OAuth2PasswordGrant;
            cfg.loginUrl = QStringLiteral("https://cloud.nexxiot.net/oauth/token");
            cfg.gpsTelemetryUrl = QStringLiteral("https://cloud.nexxiot.net/api/v1/assets/positions");
            cfg.trackTraceUrl = QStringLiteral("https://cloud.nexxiot.net/api/v1/assets");
        }
        break;
    }

    cfg.enableGpsEndpoint = true;
    return cfg;
}

Link2RailClient::Link2RailClient(TelematicsProviderId providerId, QObject *parent)
    : QObject(parent)
    , m_providerId(providerId)
{
    m_netManager = new QNetworkAccessManager(this);
    m_cookieJar = new QNetworkCookieJar(this);
    m_netManager->setCookieJar(m_cookieJar);

    m_config = loadSavedConfig(providerId);

    m_tokenTimer = new QTimer(this);
    m_tokenTimer->setInterval(30000);
    connect(m_tokenTimer, &QTimer::timeout, this, &Link2RailClient::onTokenRefreshTimer);
    m_tokenTimer->start();
}

Link2RailClient::~Link2RailClient() = default;

Link2RailConfig Link2RailClient::loadSavedConfig(TelematicsProviderId id)
{
    Link2RailConfig cfg = presetConfig(id, 0);
    const QString group = QStringLiteral("Telematics_") + providerInfo(id).key;
    QSettings st(QStringLiteral("SHS_Logistics"), group);
    if (!st.contains(QStringLiteral("loginUrl"))) {
        return cfg;
    }

    cfg.providerId = id;
    cfg.authMode = static_cast<Link2RailAuthMode>(
        st.value(QStringLiteral("authMode"), static_cast<int>(cfg.authMode)).toInt());
    cfg.environmentName = st.value(QStringLiteral("environmentName"), cfg.environmentName).toString();
    cfg.username = st.value(QStringLiteral("username")).toString();
    cfg.rememberSettings = st.value(QStringLiteral("rememberSettings"), true).toBool();
    cfg.rememberPassword = st.value(QStringLiteral("rememberPassword"), false).toBool();
    if (cfg.rememberPassword) {
        cfg.password = QString::fromUtf8(QByteArray::fromBase64(st.value(QStringLiteral("pwdB64")).toByteArray()));
        cfg.clientSecret = QString::fromUtf8(QByteArray::fromBase64(st.value(QStringLiteral("secB64")).toByteArray()));
        cfg.apiKey = QString::fromUtf8(QByteArray::fromBase64(st.value(QStringLiteral("keyB64")).toByteArray()));
        cfg.manualBearerToken = QString::fromUtf8(QByteArray::fromBase64(st.value(QStringLiteral("tokB64")).toByteArray()));
        cfg.manualCookieHeader = QString::fromUtf8(QByteArray::fromBase64(st.value(QStringLiteral("ckB64")).toByteArray()));
    }
    cfg.clientId = st.value(QStringLiteral("clientId")).toString();
    cfg.sessionTokenHeaderName = st.value(QStringLiteral("sessionTokenHeaderName"), cfg.sessionTokenHeaderName).toString();
    cfg.loginUrl = st.value(QStringLiteral("loginUrl"), cfg.loginUrl).toString();
    cfg.gpsTelemetryUrl = st.value(QStringLiteral("gpsTelemetryUrl"), cfg.gpsTelemetryUrl).toString();
    cfg.trackTraceUrl = st.value(QStringLiteral("trackTraceUrl"), cfg.trackTraceUrl).toString();
    cfg.trainMgmtUrl = st.value(QStringLiteral("trainMgmtUrl"), cfg.trainMgmtUrl).toString();
    cfg.enableGpsEndpoint = st.value(QStringLiteral("enableGpsEndpoint"), true).toBool();
    cfg.enableTrackTraceEndpoint = st.value(QStringLiteral("enableTrackTraceEndpoint"), false).toBool();
    cfg.enableTrainMgmtEndpoint = st.value(QStringLiteral("enableTrainMgmtEndpoint"), false).toBool();
    cfg.wagonFilterEvns = st.value(QStringLiteral("wagonFilterEvns")).toString();
    cfg.extraHeadersRaw = st.value(QStringLiteral("extraHeadersRaw")).toString();
    cfg.pollIntervalSeconds = st.value(QStringLiteral("pollIntervalSeconds"), 60).toInt();
    cfg.autoPollEnabled = st.value(QStringLiteral("autoPollEnabled"), true).toBool();

    // Automatische Migration veralteter/ungültiger Standard-URLs aus v0.9 auf geprüfte Protokoll-Endpunkte
    bool migrated = false;
    const Link2RailConfig fresh = presetConfig(id, 0);
    if (id == TelematicsProviderId::Link2Rail &&
        cfg.loginUrl == QStringLiteral("https://link2rail.dbcargo.com/api/v1/authenticate")) {
        cfg.loginUrl = fresh.loginUrl;
        migrated = true;
    } else if (id == TelematicsProviderId::IntermodalTelematics &&
               cfg.loginUrl == QStringLiteral("https://portal.intermodaltelematics.com/api/auth/login")) {
        cfg.authMode = fresh.authMode;
        cfg.loginUrl = fresh.loginUrl;
        cfg.gpsTelemetryUrl = fresh.gpsTelemetryUrl;
        cfg.trackTraceUrl = fresh.trackTraceUrl;
        cfg.sessionTokenHeaderName = fresh.sessionTokenHeaderName;
        migrated = true;
    } else if (id == TelematicsProviderId::Transwaggon &&
               cfg.loginUrl == QStringLiteral("https://portal.transwaggon.de/csp/wops/api/login")) {
        cfg.authMode = fresh.authMode;
        cfg.loginUrl = fresh.loginUrl;
        migrated = true;
    } else if (id == TelematicsProviderId::Wascosa &&
               cfg.loginUrl.contains(QStringLiteral("portal.savvy-synergy.com"), Qt::CaseInsensitive)) {
        cfg.authMode = fresh.authMode;
        cfg.loginUrl = fresh.loginUrl;
        cfg.gpsTelemetryUrl = fresh.gpsTelemetryUrl;
        cfg.trackTraceUrl = fresh.trackTraceUrl;
        migrated = true;
    }
    if (migrated) {
        saveConfig(cfg);
    }

    return cfg;
}

void Link2RailClient::saveConfig(const Link2RailConfig &cfg)
{
    const QString group = QStringLiteral("Telematics_") + providerInfo(cfg.providerId).key;
    QSettings st(QStringLiteral("SHS_Logistics"), group);
    if (!cfg.rememberSettings) {
        st.clear();
        return;
    }
    st.setValue(QStringLiteral("authMode"), static_cast<int>(cfg.authMode));
    st.setValue(QStringLiteral("environmentName"), cfg.environmentName);
    st.setValue(QStringLiteral("username"), cfg.username);
    st.setValue(QStringLiteral("clientId"), cfg.clientId);
    st.setValue(QStringLiteral("sessionTokenHeaderName"), cfg.sessionTokenHeaderName);
    st.setValue(QStringLiteral("rememberSettings"), cfg.rememberSettings);
    st.setValue(QStringLiteral("rememberPassword"), cfg.rememberPassword);
    if (cfg.rememberPassword) {
        st.setValue(QStringLiteral("pwdB64"), cfg.password.toUtf8().toBase64());
        st.setValue(QStringLiteral("secB64"), cfg.clientSecret.toUtf8().toBase64());
        st.setValue(QStringLiteral("keyB64"), cfg.apiKey.toUtf8().toBase64());
        st.setValue(QStringLiteral("tokB64"), cfg.manualBearerToken.toUtf8().toBase64());
        st.setValue(QStringLiteral("ckB64"), cfg.manualCookieHeader.toUtf8().toBase64());
    } else {
        st.remove(QStringLiteral("pwdB64"));
        st.remove(QStringLiteral("secB64"));
        st.remove(QStringLiteral("keyB64"));
        st.remove(QStringLiteral("tokB64"));
        st.remove(QStringLiteral("ckB64"));
    }
    st.setValue(QStringLiteral("loginUrl"), cfg.loginUrl);
    st.setValue(QStringLiteral("gpsTelemetryUrl"), cfg.gpsTelemetryUrl);
    st.setValue(QStringLiteral("trackTraceUrl"), cfg.trackTraceUrl);
    st.setValue(QStringLiteral("trainMgmtUrl"), cfg.trainMgmtUrl);
    st.setValue(QStringLiteral("enableGpsEndpoint"), cfg.enableGpsEndpoint);
    st.setValue(QStringLiteral("enableTrackTraceEndpoint"), cfg.enableTrackTraceEndpoint);
    st.setValue(QStringLiteral("enableTrainMgmtEndpoint"), cfg.enableTrainMgmtEndpoint);
    st.setValue(QStringLiteral("wagonFilterEvns"), cfg.wagonFilterEvns);
    st.setValue(QStringLiteral("extraHeadersRaw"), cfg.extraHeadersRaw);
    st.setValue(QStringLiteral("pollIntervalSeconds"), cfg.pollIntervalSeconds);
    st.setValue(QStringLiteral("autoPollEnabled"), cfg.autoPollEnabled);
}

void Link2RailClient::setConfig(const Link2RailConfig &cfg)
{
    m_config = cfg;
    m_config.providerId = m_providerId;
    saveConfig(m_config);
}

void Link2RailClient::appendLog(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    emit protocolLogAppended(QString("[%1] [%2] %3").arg(ts, info().shortName, line));
}

bool Link2RailClient::isTokenExpiringSoon() const
{
    if (!m_authenticated || !m_tokenExpiresAt.isValid()) {
        return false;
    }
    return QDateTime::currentDateTimeUtc().secsTo(m_tokenExpiresAt) < 120;
}

void Link2RailClient::onTokenRefreshTimer()
{
    if (m_authenticated && isTokenExpiringSoon() &&
        m_config.authMode != Link2RailAuthMode::DirectApiKeyOrToken &&
        m_config.authMode != Link2RailAuthMode::HttpBasicAuth &&
        !m_config.loginUrl.trimmed().isEmpty()) {
        appendLog(QStringLiteral("Sitzungstoken läuft bald ab – automatische Erneuerung ..."));
        m_fetchAfterLogin = false;
        login(m_config);
    }
}

bool Link2RailClient::parseCurlOrTokenText(const QString &inputText,
                                           Link2RailConfig &inOutConfig,
                                           QString *outSummary)
{
    QString raw = inputText.trimmed();
    if (raw.isEmpty()) {
        if (outSummary) *outSummary = QStringLiteral("Eingabe ist leer.");
        return false;
    }

    // Zeilenfortsetzungen aus Bash (\) und Windows CMD (^) zusammenführen
    raw.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    raw.replace(QRegularExpression(QStringLiteral("\\\\\\s*\\n\\s*")), QStringLiteral(" "));
    raw.replace(QRegularExpression(QStringLiteral("\\^\\s*\\n\\s*")), QStringLiteral(" "));
    // Windows CMD Escapes auflösen (^" -> ", ^& -> &, ^= -> =)
    if (raw.contains(QStringLiteral("^\"")) || raw.contains(QStringLiteral("^&"))) {
        raw.replace(QStringLiteral("^\""), QStringLiteral("\""));
        raw.replace(QStringLiteral("^&"), QStringLiteral("&"));
        raw.replace(QStringLiteral("^|"), QStringLiteral("|"));
        raw.replace(QStringLiteral("^%"), QStringLiteral("%"));
        raw.replace(QStringLiteral("^^"), QStringLiteral("^"));
    }

    QStringList extractedSummary;
    bool extractedAny = false;
    QStringList keptExtraHeaders;

    static const QSet<QString> ignoredHeaders = {
        QStringLiteral("accept"), QStringLiteral("accept-encoding"), QStringLiteral("accept-language"),
        QStringLiteral("cache-control"), QStringLiteral("connection"), QStringLiteral("content-length"),
        QStringLiteral("dnt"), QStringLiteral("host"), QStringLiteral("pragma"), QStringLiteral("priority"),
        QStringLiteral("te"), QStringLiteral("upgrade-insecure-requests"), QStringLiteral("user-agent")
    };

    auto applyHeaderLine = [&](const QString &headerLine) {
        const int sep = headerLine.indexOf(':');
        if (sep <= 0) return;
        const QString name = headerLine.left(sep).trimmed();
        const QString lower = name.toLower();
        const QString val = headerLine.mid(sep + 1).trimmed();
        if (val.isEmpty()) return;

        if (lower == QStringLiteral("authorization")) {
            if (val.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)) {
                inOutConfig.manualBearerToken = val.mid(7).trimmed();
                inOutConfig.sessionTokenHeaderName.clear();
                extractedSummary << QStringLiteral("Bearer-Token");
            } else {
                keptExtraHeaders << (name + QStringLiteral(": ") + val);
                extractedSummary << QStringLiteral("Authorization-Header");
            }
            extractedAny = true;
        } else if (lower == QStringLiteral("x-session-token") || lower == QStringLiteral("x-auth-token")) {
            inOutConfig.manualBearerToken = val;
            inOutConfig.sessionTokenHeaderName = name;
            extractedSummary << name;
            extractedAny = true;
        } else if (lower == QStringLiteral("cookie")) {
            inOutConfig.manualCookieHeader = val;
            extractedSummary << QStringLiteral("Browser-Cookie");
            extractedAny = true;
        } else if (lower == QStringLiteral("x-api-key") || lower == QStringLiteral("db-api-key") ||
                   lower == QStringLiteral("apikey") || lower == QStringLiteral("ocp-apim-subscription-key")) {
            if (lower == QStringLiteral("ocp-apim-subscription-key")) {
                keptExtraHeaders << (name + QStringLiteral(": ") + val);
            } else {
                inOutConfig.apiKey = val;
            }
            extractedSummary << name;
            extractedAny = true;
        } else if (lower == QStringLiteral("db-client-id") || lower == QStringLiteral("x-client-id")) {
            inOutConfig.clientId = val;
            extractedSummary << name;
            extractedAny = true;
        } else if (!ignoredHeaders.contains(lower) && !lower.startsWith(QStringLiteral("sec-"))) {
            keptExtraHeaders << (name + QStringLiteral(": ") + val);
        }
    };

    if (raw.startsWith(QStringLiteral("curl "), Qt::CaseInsensitive) ||
        raw.startsWith(QStringLiteral("curl.exe "), Qt::CaseInsensitive)) {
        // Shell-Tokenizer (unterstützt '...' und "...")
        QStringList tokens;
        QString cur;
        QChar quote;
        bool inToken = false;
        for (int i = 0; i < raw.size(); ++i) {
            const QChar ch = raw[i];
            if (!quote.isNull()) {
                if (ch == '\\' && quote == '"' && i + 1 < raw.size()) {
                    cur.append(raw[++i]);
                } else if (ch == quote) {
                    quote = QChar();
                } else {
                    cur.append(ch);
                }
            } else {
                if (ch == '\'' || ch == '"') {
                    quote = ch;
                    inToken = true;
                } else if (ch.isSpace()) {
                    if (inToken) {
                        tokens << cur;
                        cur.clear();
                        inToken = false;
                    }
                } else {
                    cur.append(ch);
                    inToken = true;
                }
            }
        }
        if (inToken) tokens << cur;

        for (int i = 1; i < tokens.size(); ++i) {
            const QString &tok = tokens[i];
            if ((tok == QStringLiteral("-H") || tok == QStringLiteral("--header")) && i + 1 < tokens.size()) {
                applyHeaderLine(tokens[++i]);
            } else if ((tok == QStringLiteral("-b") || tok == QStringLiteral("--cookie")) && i + 1 < tokens.size()) {
                inOutConfig.manualCookieHeader = tokens[++i].trimmed();
                extractedSummary << QStringLiteral("Browser-Cookie");
                extractedAny = true;
            } else if ((tok == QStringLiteral("--url")) && i + 1 < tokens.size()) {
                const QString u = tokens[++i].trimmed();
                if (u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                    u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                    inOutConfig.gpsTelemetryUrl = u;
                    inOutConfig.enableGpsEndpoint = true;
                    extractedSummary << QStringLiteral("API-URL");
                    extractedAny = true;
                }
            } else if (tok.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                       tok.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                if (inOutConfig.gpsTelemetryUrl.isEmpty() || !extractedSummary.contains(QStringLiteral("API-URL"))) {
                    inOutConfig.gpsTelemetryUrl = tok.trimmed();
                    inOutConfig.enableGpsEndpoint = true;
                    extractedSummary << QStringLiteral("API-URL");
                    extractedAny = true;
                }
            }
        }
    } else {
        // Direkte Eingabe: einzelne Header-Zeilen, Bearer-Token, JWT oder Cookie-String
        const QStringList lines = raw.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.contains(':') && !t.startsWith(QStringLiteral("http://")) && !t.startsWith(QStringLiteral("https://"))) {
                applyHeaderLine(t);
            } else if (t.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)) {
                inOutConfig.manualBearerToken = t.mid(7).trimmed();
                inOutConfig.sessionTokenHeaderName.clear();
                extractedSummary << QStringLiteral("Bearer-Token");
                extractedAny = true;
            } else if (t.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                       t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                inOutConfig.gpsTelemetryUrl = t;
                inOutConfig.enableGpsEndpoint = true;
                extractedSummary << QStringLiteral("API-URL");
                extractedAny = true;
            } else if (t.contains('=') && t.contains(';')) {
                inOutConfig.manualCookieHeader = t;
                extractedSummary << QStringLiteral("Browser-Cookie");
                extractedAny = true;
            } else if (t.size() >= 12 && !t.contains(' ')) {
                inOutConfig.manualBearerToken = t;
                if (inOutConfig.providerId == TelematicsProviderId::IntermodalTelematics && !t.startsWith(QStringLiteral("eyJ"))) {
                    inOutConfig.sessionTokenHeaderName = QStringLiteral("x-session-token");
                    extractedSummary << QStringLiteral("x-session-token");
                } else {
                    extractedSummary << QStringLiteral("Token");
                }
                extractedAny = true;
            }
        }
    }

    if (!extractedAny) {
        if (outSummary) {
            *outSummary = QStringLiteral("Kein cURL-Befehl, Token, Cookie oder API-Header erkannt.");
        }
        return false;
    }

    inOutConfig.authMode = Link2RailAuthMode::DirectApiKeyOrToken;
    if (!keptExtraHeaders.isEmpty()) {
        inOutConfig.extraHeadersRaw = keptExtraHeaders.join('\n');
    }

    extractedSummary.removeDuplicates();
    if (outSummary) {
        *outSummary = QStringLiteral("Übernommen: %1").arg(extractedSummary.join(QStringLiteral(" + ")));
    }
    return true;
}

bool Link2RailClient::applyFromCurlOrToken(const QString &inputText, QString *outSummary)
{
    Link2RailConfig cfg = m_config;
    QString summary;
    if (!parseCurlOrTokenText(inputText, cfg, &summary)) {
        if (outSummary) *outSummary = summary;
        appendLog(QStringLiteral("Browser-Session/cURL-Import abgelehnt: ") + summary);
        return false;
    }

    if (outSummary) *outSummary = summary;
    appendLog(QStringLiteral("Browser-Session / cURL erkannt (%1). Aktiviere Direkt-Sitzung ...").arg(summary));
    login(cfg);
    return true;
}

void Link2RailClient::probeConnection(const Link2RailConfig &cfgIn)
{
    const QString pName = info().shortName;
    QString targetStr = cfgIn.loginUrl.trimmed();
    if (targetStr.isEmpty() || cfgIn.authMode == Link2RailAuthMode::DirectApiKeyOrToken ||
        cfgIn.authMode == Link2RailAuthMode::HttpBasicAuth) {
        targetStr = cfgIn.gpsTelemetryUrl.trimmed();
    }
    if (targetStr.isEmpty()) {
        targetStr = info().portalUrl;
    }

    const QUrl url(targetStr);
    if (!url.isValid() || url.scheme().isEmpty()) {
        const QString msg = QStringLiteral("[%1] Verbindungstest: Ungültige URL '%2'.").arg(pName, targetStr);
        appendLog(msg);
        emit connectionTestFinished(false, msg);
        return;
    }

    QNetworkRequest req(url);
    req.setTransferTimeout(12000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json, application/xml, text/html, */*");
    req.setRawHeader("User-Agent", "SHS-Logistics-Telematics-Client/1.0 (Qt6)");

    appendLog(QStringLiteral("🔍 Verbindungstest -> GET %1 (TLS/SSL: %2) ...")
                  .arg(url.toString(), QSslSocket::supportsSsl() ? QStringLiteral("OK") : QStringLiteral("FEHLT")));

    QNetworkReply *reply = m_netManager->get(req);
    reply->setProperty("probeTargetUrl", url.toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onProbeReplyFinished(reply);
    });
}

void Link2RailClient::onProbeReplyFinished(QNetworkReply *reply)
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QUrl finalUrl = reply->url();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString errStr = reply->errorString();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    const QString pName = info().shortName;
    if (httpStatus == 0 && netErr != QNetworkReply::NoError) {
        const QString report = QStringLiteral(
            "🔴 %1 Server nicht erreichbar: %2\n"
            "• Prüfe Domain/URL, Internetverbindung, Proxy oder Firewall.")
            .arg(pName, errStr);
        appendLog(report);
        emit connectionTestFinished(false, report);
        return;
    }

    const QString bodyLow = QString::fromUtf8(raw.left(600)).trimmed().toLower();
    const bool isHtml = ct.contains(QStringLiteral("text/html"), Qt::CaseInsensitive) ||
                        bodyLow.startsWith(QStringLiteral("<!doctype html")) ||
                        bodyLow.startsWith(QStringLiteral("<html"));

    QString diagnosis;
    if (httpStatus == 401 || httpStatus == 403 || httpStatus == 405) {
        diagnosis = QStringLiteral("API-Endpunkt ist erreichbar und erwartet Authentifizierung / POST-Login (sehr gutes Zeichen!).");
    } else if (httpStatus >= 200 && httpStatus < 300 && isHtml) {
        diagnosis = QStringLiteral("Server erreichbar, liefert aber eine HTML-Webseite (Webportal / SSO). "
                                   "Für Web-SSO nutze '📋 Browser-Session / cURL' oder trage die REST-API-URL ein.");
    } else if (httpStatus >= 200 && httpStatus < 300) {
        diagnosis = QStringLiteral("Server erreichbar und liefert API-Daten (%1).").arg(ct.isEmpty() ? QStringLiteral("OK") : ct);
    } else if (httpStatus == 404) {
        diagnosis = QStringLiteral("Host erreichbar, aber Pfad existiert nicht (HTTP 404). Bitte Login-/API-URL prüfen oder Vorlage wechseln.");
    } else {
        diagnosis = QStringLiteral("HTTP %1 (%2).").arg(httpStatus).arg(errStr);
    }

    const QString report = QStringLiteral(
        "🟢 %1 Verbindungstest: HTTP %2 | Ziel: %3 | Typ: %4\n• Diagnose: %5")
        .arg(pName).arg(httpStatus).arg(finalUrl.toString(), ct.isEmpty() ? QStringLiteral("-") : ct, diagnosis);
    appendLog(report);
    emit connectionTestFinished(true, report);
}

void Link2RailClient::login(const Link2RailConfig &cfgIn)
{
    m_config = cfgIn;
    m_config.providerId = m_providerId;
    saveConfig(m_config);
    const QString pName = info().shortName;

    // Direkte Modi ohne separaten Login-POST: DirectApiKeyOrToken oder HttpBasicAuth
    if (m_config.authMode == Link2RailAuthMode::DirectApiKeyOrToken) {
        const bool hasToken = !m_config.manualBearerToken.trimmed().isEmpty();
        const bool hasKey   = !m_config.apiKey.trimmed().isEmpty();
        const bool hasCookie = !m_config.manualCookieHeader.trimmed().isEmpty();
        const bool hasExtra = !m_config.extraHeadersRaw.trimmed().isEmpty();
        if (!hasToken && !hasKey && !hasCookie && !hasExtra) {
            const QString err = QStringLiteral(
                "[%1] Bitte ein Bearer-/Session-Token, einen API-Key, ein Browser-Cookie oder über '📋 Browser-Session / cURL' eine Sitzung angeben.")
                .arg(pName);
            appendLog(QStringLiteral("FEHLER: ") + err);
            emit loginFailed(err);
            return;
        }
        m_accessToken = m_config.manualBearerToken.trimmed();
        if (m_accessToken.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)) {
            m_accessToken = m_accessToken.mid(7).trimmed();
        }
        m_sessionId = (m_config.sessionTokenHeaderName.compare(QStringLiteral("x-session-token"), Qt::CaseInsensitive) == 0)
                          ? m_accessToken : QString();
        m_tokenType = QStringLiteral("Bearer");
        m_authenticated = true;
        m_authenticatedUser = !m_config.username.trimmed().isEmpty()
                                  ? m_config.username.trimmed()
                                  : (!m_config.clientId.trimmed().isEmpty()
                                         ? m_config.clientId.trimmed()
                                         : (hasCookie ? QStringLiteral("Browser-Session") : QStringLiteral("Token/API-Key")));
        m_tokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(7200);

        QStringList arts;
        if (hasToken) arts << (m_config.sessionTokenHeaderName.isEmpty() ? QStringLiteral("Bearer-Token") : m_config.sessionTokenHeaderName);
        if (hasCookie) arts << QStringLiteral("Cookie");
        if (hasKey) arts << QStringLiteral("API-Key");
        const QString msg = QStringLiteral("%1: Direkt-Authentifizierung aktiv (%2 | %3).")
                                .arg(pName, m_authenticatedUser, arts.join(QStringLiteral(" + ")));
        appendLog(msg);
        emit authenticationChanged(true, msg);
        emit loginSucceeded(m_authenticatedUser, msg);
        fetchAllLiveData();
        return;
    }

    if (m_config.authMode == Link2RailAuthMode::HttpBasicAuth) {
        if (m_config.username.trimmed().isEmpty() || m_config.password.isEmpty()) {
            const QString err = QStringLiteral("[%1] Bitte Benutzername und Passwort für HTTP Basic Auth eingeben.").arg(pName);
            appendLog(QStringLiteral("FEHLER: ") + err);
            emit loginFailed(err);
            return;
        }
        m_accessToken.clear();
        m_sessionId.clear();
        m_authenticated = true;
        m_authenticatedUser = m_config.username.trimmed();
        m_tokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(86400);

        const QString msg = QStringLiteral("%1: HTTP Basic Auth aktiviert (%2).").arg(pName, m_authenticatedUser);
        appendLog(msg);
        emit authenticationChanged(true, msg);
        emit loginSucceeded(m_authenticatedUser, msg);
        fetchAllLiveData();
        return;
    }

    const QUrl url(m_config.loginUrl.trimmed());
    if (!url.isValid() || url.scheme().isEmpty()) {
        const QString err = QStringLiteral("[%1] Ungültige Login-URL: '%2'").arg(pName, m_config.loginUrl);
        appendLog(QStringLiteral("FEHLER: ") + err);
        emit loginFailed(err);
        return;
    }

    if ((m_config.authMode == Link2RailAuthMode::PortalLoginCidP ||
         m_config.authMode == Link2RailAuthMode::OAuth2PasswordGrant ||
         m_config.authMode == Link2RailAuthMode::ImtTenoAuth ||
         m_config.authMode == Link2RailAuthMode::HtmlFormPost) &&
        (m_config.username.trimmed().isEmpty() || m_config.password.isEmpty())) {
        const QString err = QStringLiteral("[%1] Bitte Benutzername (E-Mail) und Passwort eingeben.").arg(pName);
        appendLog(QStringLiteral("FEHLER: ") + err);
        emit loginFailed(err);
        return;
    }

    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json, application/xml, text/html, text/plain, */*");
    req.setRawHeader("User-Agent", "SHS-Logistics-Telematics-Client/1.0 (Qt6)");

    if (!m_config.clientId.trimmed().isEmpty() && m_config.authMode != Link2RailAuthMode::ImtTenoAuth) {
        req.setRawHeader("DB-Client-Id", m_config.clientId.trimmed().toUtf8());
        req.setRawHeader("X-Client-Id", m_config.clientId.trimmed().toUtf8());
    }
    if (!m_config.apiKey.trimmed().isEmpty()) {
        req.setRawHeader("DB-Api-Key", m_config.apiKey.trimmed().toUtf8());
        req.setRawHeader("X-Api-Key", m_config.apiKey.trimmed().toUtf8());
    }

    QByteArray body;
    if (m_config.authMode == Link2RailAuthMode::ImtTenoAuth) {
        // Intermodal Telematics (IMT) TenoSessionService:
        // POST /authorization/v1/authentication -> { userId, password, accountAlias, consumer, consumerVersion }
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QJsonObject payload;
        payload[QStringLiteral("userId")] = m_config.username.trimmed();
        payload[QStringLiteral("password")] = m_config.password;
        if (!m_config.clientId.trimmed().isEmpty()) {
            payload[QStringLiteral("accountAlias")] = m_config.clientId.trimmed();
        }
        payload[QStringLiteral("consumer")] = QStringLiteral("web-portal");
        payload[QStringLiteral("consumerVersion")] = QStringLiteral("1.0");
        body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    } else if (m_config.authMode == Link2RailAuthMode::HtmlFormPost) {
        // HTML-Formular-POST (z. B. TRANSWAGGON WOPS: twa.portal.Home.cls)
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery q;
        if (m_providerId == TelematicsProviderId::Transwaggon ||
            url.host().contains(QStringLiteral("transwaggon"), Qt::CaseInsensitive)) {
            q.addQueryItem(QStringLiteral("dummyChar"), QString::fromUtf8("\xE2\x98\xA0"));
            q.addQueryItem(QStringLiteral("langID"), QStringLiteral("DE"));
            q.addQueryItem(QStringLiteral("key"), QString());
            q.addQueryItem(QStringLiteral("PRTG"), QStringLiteral("PRTG-10"));
            q.addQueryItem(QStringLiteral("login"), m_config.username.trimmed());
            q.addQueryItem(QStringLiteral("password"), m_config.password);
        } else {
            q.addQueryItem(QStringLiteral("username"), m_config.username.trimmed());
            q.addQueryItem(QStringLiteral("login"), m_config.username.trimmed());
            q.addQueryItem(QStringLiteral("password"), m_config.password);
        }
        body = q.toString(QUrl::FullyEncoded).toUtf8();
    } else if (m_config.authMode == Link2RailAuthMode::PortalLoginCidP) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QJsonObject payload;
        payload[QStringLiteral("username")] = m_config.username.trimmed();
        payload[QStringLiteral("email")] = m_config.username.trimmed();
        payload[QStringLiteral("password")] = m_config.password;
        if (!m_config.clientId.trimmed().isEmpty()) {
            payload[QStringLiteral("clientId")] = m_config.clientId.trimmed();
        }
        if (!m_config.clientSecret.trimmed().isEmpty()) {
            payload[QStringLiteral("clientSecret")] = m_config.clientSecret;
        }
        body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    } else if (m_config.authMode == Link2RailAuthMode::OAuth2PasswordGrant) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("password"));
        q.addQueryItem(QStringLiteral("username"), m_config.username.trimmed());
        q.addQueryItem(QStringLiteral("password"), m_config.password);
        if (!m_config.clientId.trimmed().isEmpty()) {
            q.addQueryItem(QStringLiteral("client_id"), m_config.clientId.trimmed());
        }
        if (!m_config.clientSecret.trimmed().isEmpty()) {
            q.addQueryItem(QStringLiteral("client_secret"), m_config.clientSecret);
        }
        body = q.toString(QUrl::FullyEncoded).toUtf8();
    } else if (m_config.authMode == Link2RailAuthMode::OAuth2ClientCreds) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
        if (!m_config.clientId.trimmed().isEmpty()) {
            q.addQueryItem(QStringLiteral("client_id"), m_config.clientId.trimmed());
        }
        if (!m_config.clientSecret.trimmed().isEmpty()) {
            q.addQueryItem(QStringLiteral("client_secret"), m_config.clientSecret);
        }
        body = q.toString(QUrl::FullyEncoded).toUtf8();
    }

    appendLog(QStringLiteral("POST %1 (Benutzer: %2) ...")
                  .arg(url.toString(),
                       m_config.username.trimmed().isEmpty() ? m_config.clientId.trimmed() : m_config.username.trimmed()));

    m_fetchAfterLogin = true;
    m_pendingRequests++;
    QNetworkReply *reply = m_netManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginReplyFinished(reply);
    });
}

void Link2RailClient::onLoginReplyFinished(QNetworkReply *reply)
{
    m_pendingRequests = std::max(0, m_pendingRequests - 1);
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QByteArray raw = reply->readAll();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrStr = reply->errorString();
    const QByteArray hdrAuth = reply->rawHeader("Authorization");
    const QByteArray hdrSession = reply->rawHeader("X-Session-Id");
    const QByteArray hdrSessionToken = reply->rawHeader("x-session-token");
    const QByteArray hdrToken = reply->rawHeader("X-Auth-Token");
    const QUrl reqUrl = reply->url();
    reply->deleteLater();

    const QString pName = info().shortName;
    if (netErr != QNetworkReply::NoError || (httpStatus >= 400 && httpStatus < 600)) {
        m_authenticated = false;
        const QString detail = QString::fromUtf8(raw).left(400).trimmed();
        QString hint;
        if (httpStatus == 401) {
            hint = QStringLiteral(" (Zugangsdaten abgelehnt – Benutzername, Passwort, Account-Alias oder API-Key prüfen)");
        } else if (httpStatus == 403) {
            hint = QStringLiteral(" (Zugriff verweigert – API-Berechtigung / API-Key / Account-Alias prüfen)");
        } else if (httpStatus == 404 || httpStatus == 405) {
            hint = QStringLiteral(" (Login-Endpunkt existiert nicht oder erwartet Browser-SSO -> '📋 Browser-Session / cURL' nutzen oder Vorlage wechseln)");
        } else if (httpStatus == 0) {
            hint = QStringLiteral(" (Server nicht erreichbar – Domain, Internetverbindung, Proxy oder Firewall prüfen)");
        }

        const QString errMsg = QStringLiteral("%1 Anmeldung fehlgeschlagen [HTTP %2]: %3%4%5")
                                   .arg(pName).arg(httpStatus)
                                   .arg(netErrStr, hint, detail.isEmpty() ? QString() : (QStringLiteral("\nServer-Antwort: ") + detail));
        appendLog(errMsg);
        emit authenticationChanged(false, errMsg);
        emit loginFailed(errMsg);
        return;
    }

    const QString bodyStr = QString::fromUtf8(raw);
    const QString bodyLow = bodyStr.left(4000).trimmed().toLower();
    const bool isHtmlResponse = contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive) ||
                                bodyLow.startsWith(QStringLiteral("<!doctype html")) ||
                                bodyLow.startsWith(QStringLiteral("<html"));

    // HTML-Formular-Login (z. B. TRANSWAGGON WOPS)
    if (m_config.authMode == Link2RailAuthMode::HtmlFormPost) {
        const bool bouncedToLogin = reqUrl.toString().contains(QStringLiteral("err=1"), Qt::CaseInsensitive) ||
                                    bodyLow.contains(QStringLiteral("name=\"loginform\"")) ||
                                    bodyLow.contains(QStringLiteral("type=\"password\"")) ||
                                    bodyLow.contains(QStringLiteral("ungültige anmeldedaten")) ||
                                    bodyLow.contains(QStringLiteral("invalid login"));
        const int cookieCount = m_cookieJar ? m_cookieJar->cookiesForUrl(reqUrl).size() : 0;
        if (bouncedToLogin) {
            m_authenticated = false;
            const QString err = QStringLiteral(
                "%1 Formular-Anmeldung abgelehnt: Portal hat erneut die Login-Maske geliefert (%2).\n"
                "Bitte Benutzername/Passwort prüfen oder über '📋 Browser-Session / cURL' die aktive Browser-Sitzung übernehmen.")
                .arg(pName, reqUrl.toString());
            appendLog(err);
            emit authenticationChanged(false, err);
            emit loginFailed(err);
            return;
        }
        m_accessToken.clear();
        m_sessionId = (cookieCount > 0) ? QStringLiteral("HTML-COOKIE-SESSION") : QStringLiteral("HTML-FORM-OK");
        m_authenticated = true;
        m_authenticatedUser = m_config.username.trimmed();
        m_tokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
        const QString okMsg = QStringLiteral("%1 Formular-Anmeldung erfolgreich (%2 | %3 Cookie(s)).")
                                  .arg(pName, m_authenticatedUser).arg(cookieCount);
        appendLog(okMsg);
        emit authenticationChanged(true, okMsg);
        emit loginSucceeded(m_authenticatedUser, okMsg);
        if (m_fetchAfterLogin) {
            m_fetchAfterLogin = false;
            fetchAllLiveData();
        }
        return;
    }

    // Bei JSON-/OAuth-Modi: Wenn der Server HTML (SPA/SSO-Webseite) statt JSON liefert -> klare Diagnose!
    if (isHtmlResponse) {
        m_authenticated = false;
        const QString warn = QStringLiteral(
            "%1: Der Server hat eine HTML-Webseite statt einer JSON-API-Antwort geliefert (HTTP %2, Ziel: %3).\n"
            "Viele Telematik-Portale verwenden Web-SSO / 2FA (Salesforce, Entra, Flutter/Angular).\n"
            "Empfehlung: Klicke auf '📋 Browser-Session / cURL', melde dich im Browser an und füge den API-Request (Als cURL kopieren) oder dein Token ein.")
            .arg(pName).arg(httpStatus).arg(reqUrl.toString());
        appendLog(warn);
        emit authenticationChanged(false, warn);
        emit loginFailed(warn);
        return;
    }

    m_accessToken.clear();
    m_refreshToken.clear();
    m_sessionId.clear();
    m_tokenType = QStringLiteral("Bearer");
    int expiresInSec = 3600;

    const QStringList tokenKeys = {
        QStringLiteral("sessionToken"), QStringLiteral("session_token"), QStringLiteral("x-session-token"),
        QStringLiteral("access_token"), QStringLiteral("accessToken"),
        QStringLiteral("token"), QStringLiteral("jwt"), QStringLiteral("id_token"),
        QStringLiteral("idToken"), QStringLiteral("authToken"), QStringLiteral("apiToken")
    };
    const QStringList sessionKeys = {
        QStringLiteral("sessionToken"), QStringLiteral("sessionId"),
        QStringLiteral("sessionID"), QStringLiteral("session_id")
    };

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        m_accessToken = firstString(obj, tokenKeys);
        m_sessionId = firstString(obj, sessionKeys);
        for (const QString &subKey : {QStringLiteral("data"), QStringLiteral("session"),
                                      QStringLiteral("result"), QStringLiteral("authentication")}) {
            if (obj.value(subKey).isObject()) {
                const QJsonObject sub = obj.value(subKey).toObject();
                if (m_accessToken.isEmpty()) m_accessToken = firstString(sub, tokenKeys);
                if (m_sessionId.isEmpty()) m_sessionId = firstString(sub, sessionKeys);
                if (m_refreshToken.isEmpty()) m_refreshToken = firstString(sub, {QStringLiteral("refresh_token"), QStringLiteral("refreshToken")});
                firstInt(sub, {QStringLiteral("expires_in"), QStringLiteral("expiresIn")}, expiresInSec);
            }
        }
        if (m_refreshToken.isEmpty()) {
            m_refreshToken = firstString(obj, {QStringLiteral("refresh_token"), QStringLiteral("refreshToken")});
        }
        const QString tt = firstString(obj, {QStringLiteral("token_type"), QStringLiteral("tokenType")});
        if (!tt.isEmpty()) m_tokenType = tt;
        firstInt(obj, {QStringLiteral("expires_in"), QStringLiteral("expiresIn")}, expiresInSec);
    }

    if (m_accessToken.isEmpty() && !hdrSessionToken.isEmpty()) {
        m_accessToken = QString::fromUtf8(hdrSessionToken).trimmed();
        m_sessionId = m_accessToken;
    }
    if (m_accessToken.isEmpty() && !hdrAuth.isEmpty()) {
        QString a = QString::fromUtf8(hdrAuth).trimmed();
        if (a.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)) a = a.mid(7).trimmed();
        m_accessToken = a;
    }
    if (m_accessToken.isEmpty() && !hdrToken.isEmpty()) {
        m_accessToken = QString::fromUtf8(hdrToken).trimmed();
    }
    if (m_sessionId.isEmpty() && !hdrSession.isEmpty()) {
        m_sessionId = QString::fromUtf8(hdrSession).trimmed();
    }
    if (m_config.authMode == Link2RailAuthMode::ImtTenoAuth && m_sessionId.isEmpty() && !m_accessToken.isEmpty()) {
        m_sessionId = m_accessToken;
    }

    const int cookieCount = m_cookieJar ? m_cookieJar->cookiesForUrl(reqUrl).size() : 0;
    if (m_accessToken.isEmpty() && m_sessionId.isEmpty() && cookieCount == 0) {
        const QString warn = QStringLiteral(
            "%1: HTTP %2 erhalten, aber weder Access-Token, x-session-token, Session-ID noch Cookie gefunden.\nAntwort: %3")
            .arg(pName).arg(httpStatus).arg(bodyStr.left(300));
        appendLog(warn);
        emit loginFailed(warn);
        return;
    }

    m_authenticated = true;
    m_authenticatedUser = !m_config.username.trimmed().isEmpty()
                              ? m_config.username.trimmed()
                              : (!m_config.clientId.trimmed().isEmpty() ? m_config.clientId.trimmed() : pName);
    m_tokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(std::max(180, expiresInSec));

    QStringList authArtifacts;
    if (m_config.authMode == Link2RailAuthMode::ImtTenoAuth) authArtifacts << QStringLiteral("x-session-token");
    else if (!m_accessToken.isEmpty()) authArtifacts << QStringLiteral("JWT/Bearer");
    if (!m_sessionId.isEmpty() && m_config.authMode != Link2RailAuthMode::ImtTenoAuth) authArtifacts << QStringLiteral("SessionID");
    if (cookieCount > 0) authArtifacts << QString("%1 Cookie(s)").arg(cookieCount);

    const QString okMsg = QStringLiteral("%1 angemeldet (%2 | %3 | bis %4).")
                              .arg(pName, m_authenticatedUser,
                                   authArtifacts.join(QStringLiteral(" + ")),
                                   m_tokenExpiresAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")));
    appendLog(okMsg);
    emit authenticationChanged(true, okMsg);
    emit loginSucceeded(m_authenticatedUser, okMsg);

    if (m_fetchAfterLogin) {
        m_fetchAfterLogin = false;
        fetchAllLiveData();
    }
}

void Link2RailClient::logout()
{
    m_authenticated = false;
    m_authenticatedUser.clear();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_sessionId.clear();
    m_records.clear();
    m_tokenExpiresAt = QDateTime();
    if (m_cookieJar) {
        m_cookieJar->deleteLater();
        m_cookieJar = new QNetworkCookieJar(this);
        m_netManager->setCookieJar(m_cookieJar);
    }
    appendLog(QStringLiteral("Abgemeldet. Sitzungstoken, Cookies & Waggondaten dieses Dienstes zurückgesetzt."));
    emit authenticationChanged(false, QStringLiteral("Nicht angemeldet"));
    emit liveDataUpdated(m_records, QStringLiteral("%1 abgemeldet.").arg(info().shortName));
}

void Link2RailClient::applyAuthHeaders(QNetworkRequest &req) const
{
    req.setTransferTimeout(25000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json, application/xml, text/csv, */*");
    req.setRawHeader("User-Agent", "SHS-Logistics-Telematics-Client/1.0 (Qt6)");

    if (m_config.authMode == Link2RailAuthMode::ImtTenoAuth) {
        const QString tok = !m_sessionId.isEmpty() ? m_sessionId : m_accessToken;
        if (!tok.isEmpty()) {
            req.setRawHeader("x-session-token", tok.toUtf8());
        }
    } else if (!m_config.sessionTokenHeaderName.trimmed().isEmpty() && !m_accessToken.isEmpty()) {
        req.setRawHeader(m_config.sessionTokenHeaderName.trimmed().toUtf8(), m_accessToken.toUtf8());
    } else if (m_config.authMode == Link2RailAuthMode::HttpBasicAuth && !m_config.username.trimmed().isEmpty()) {
        const QByteArray creds = (m_config.username.trimmed() + QLatin1Char(':') + m_config.password).toUtf8().toBase64();
        req.setRawHeader("Authorization", "Basic " + creds);
    } else if (!m_accessToken.isEmpty()) {
        req.setRawHeader("Authorization", (m_tokenType + QStringLiteral(" ") + m_accessToken).toUtf8());
    }

    if (!m_sessionId.isEmpty() && m_config.authMode != Link2RailAuthMode::ImtTenoAuth &&
        m_config.authMode != Link2RailAuthMode::HtmlFormPost) {
        req.setRawHeader("sessionId", m_sessionId.toUtf8());
        req.setRawHeader("X-Session-Id", m_sessionId.toUtf8());
    }
    if (!m_config.manualCookieHeader.trimmed().isEmpty()) {
        req.setRawHeader("Cookie", m_config.manualCookieHeader.trimmed().toUtf8());
    }
    if (!m_config.clientId.trimmed().isEmpty() && m_config.authMode != Link2RailAuthMode::ImtTenoAuth) {
        req.setRawHeader("DB-Client-Id", m_config.clientId.trimmed().toUtf8());
        req.setRawHeader("X-Client-Id", m_config.clientId.trimmed().toUtf8());
    }
    if (!m_config.apiKey.trimmed().isEmpty()) {
        req.setRawHeader("DB-Api-Key", m_config.apiKey.trimmed().toUtf8());
        req.setRawHeader("X-Api-Key", m_config.apiKey.trimmed().toUtf8());
    }

    const QStringList lines = m_config.extraHeadersRaw.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int sep = line.indexOf(':');
        if (sep > 0) {
            const QByteArray k = line.left(sep).trimmed().toUtf8();
            const QByteArray v = line.mid(sep + 1).trimmed().toUtf8();
            if (!k.isEmpty()) {
                req.setRawHeader(k, v);
            }
        }
    }
}

void Link2RailClient::fetchAllLiveData()
{
    if (!m_authenticated) {
        const QString err = QStringLiteral("[%1] Abruf nicht möglich: Bitte zuerst anmelden.").arg(info().shortName);
        appendLog(err);
        emit apiError(err);
        return;
    }

    if (isTokenExpiringSoon() &&
        m_config.authMode != Link2RailAuthMode::DirectApiKeyOrToken &&
        m_config.authMode != Link2RailAuthMode::HttpBasicAuth) {
        appendLog(QStringLiteral("Token erneuern vor Datenabruf ..."));
        m_fetchAfterLogin = true;
        login(m_config);
        return;
    }

    m_pendingBatch.clear();
    int dispatched = 0;

    if (m_config.enableGpsEndpoint && !m_config.gpsTelemetryUrl.trimmed().isEmpty()) {
        querySingleEndpoint(m_config.gpsTelemetryUrl.trimmed(), info().shortName + QStringLiteral(" GPS"));
        dispatched++;
    }
    if (m_config.enableTrackTraceEndpoint && !m_config.trackTraceUrl.trimmed().isEmpty()) {
        querySingleEndpoint(m_config.trackTraceUrl.trimmed(), info().shortName + QStringLiteral(" T&T"));
        dispatched++;
    }
    if (m_config.enableTrainMgmtEndpoint && !m_config.trainMgmtUrl.trimmed().isEmpty()) {
        querySingleEndpoint(m_config.trainMgmtUrl.trimmed(), info().shortName + QStringLiteral(" Zug"));
        dispatched++;
    }

    if (dispatched == 0) {
        const QString err = QStringLiteral("[%1] Kein aktiver Daten-Endpunkt konfiguriert.").arg(info().shortName);
        appendLog(err);
        emit apiError(err);
    }
}

void Link2RailClient::querySingleEndpoint(const QString &urlTemplate, const QString &sourceTag)
{
    QUrl url(urlTemplate);
    if (!url.isValid() || url.scheme().isEmpty()) {
        appendLog(QStringLiteral("Ungültige URL übersprungen: %1").arg(urlTemplate));
        return;
    }

    const QString evnFilter = m_config.wagonFilterEvns.trimmed();
    if (!evnFilter.isEmpty()) {
        QStringList evns = evnFilter.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        if (!evns.isEmpty()) {
            QUrlQuery q(url);
            if (!q.hasQueryItem(QStringLiteral("wagonNumber")) && !q.hasQueryItem(QStringLiteral("wagonNumbers"))) {
                q.addQueryItem(evns.size() == 1 ? QStringLiteral("wagonNumber") : QStringLiteral("wagonNumbers"),
                               evns.join(','));
                url.setQuery(q);
            }
        }
    }

    QNetworkRequest req(url);
    applyAuthHeaders(req);

    appendLog(QStringLiteral("GET %1").arg(url.toString()));
    m_pendingRequests++;
    QNetworkReply *reply = m_netManager->get(req);
    reply->setProperty("sourceTag", sourceTag);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onDataReplyFinished(reply);
    });
}

void Link2RailClient::mergeBatchRecords(const QVector<Link2RailWagonRecord> &incoming)
{
    for (const Link2RailWagonRecord &inc : incoming) {
        const QString key = normalizeEvnKey(inc.evn);
        int existingIdx = -1;
        for (int i = 0; i < m_pendingBatch.size(); ++i) {
            if (normalizeEvnKey(m_pendingBatch[i].evn) == key && !key.isEmpty()) {
                existingIdx = i;
                break;
            }
        }
        if (existingIdx < 0) {
            m_pendingBatch.append(inc);
        } else {
            Link2RailWagonRecord &dst = m_pendingBatch[existingIdx];
            if (!dst.hasValidGps && inc.hasValidGps) {
                dst.hasValidGps = true;
                dst.rawLat = inc.rawLat;
                dst.rawLon = inc.rawLon;
            }
            if (dst.trainNumber.isEmpty() && !inc.trainNumber.isEmpty()) dst.trainNumber = inc.trainNumber;
            if (dst.shipmentId.isEmpty() && !inc.shipmentId.isEmpty()) dst.shipmentId = inc.shipmentId;
            if (dst.wagonTypeCode.isEmpty() && !inc.wagonTypeCode.isEmpty()) dst.wagonTypeCode = inc.wagonTypeCode;
            if (dst.axleCount <= 0 && inc.axleCount > 0) dst.axleCount = inc.axleCount;
            if (dst.statusText.isEmpty() && !inc.statusText.isEmpty()) dst.statusText = inc.statusText;
            if (dst.lastEventType.isEmpty() && !inc.lastEventType.isEmpty()) dst.lastEventType = inc.lastEventType;
            if (dst.locationName.isEmpty() && !inc.locationName.isEmpty()) dst.locationName = inc.locationName;
            if (dst.locationCode.isEmpty() && !inc.locationCode.isEmpty()) dst.locationCode = inc.locationCode;
            if (dst.geofenceSite.isEmpty() && !inc.geofenceSite.isEmpty()) dst.geofenceSite = inc.geofenceSite;
            if (dst.standingTimeMinutes <= 0 && inc.standingTimeMinutes > 0) dst.standingTimeMinutes = inc.standingTimeMinutes;
            if (dst.cargoDescription.isEmpty() && !inc.cargoDescription.isEmpty()) dst.cargoDescription = inc.cargoDescription;
            if (dst.weightTons <= 0.1 && inc.weightTons > 0.1) dst.weightTons = inc.weightTons;
            if (dst.originName.isEmpty() && !inc.originName.isEmpty()) dst.originName = inc.originName;
            if (dst.destinationName.isEmpty() && !inc.destinationName.isEmpty()) dst.destinationName = inc.destinationName;
            if (dst.etaIso.isEmpty() && !inc.etaIso.isEmpty()) dst.etaIso = inc.etaIso;
            if (dst.timestampIso.isEmpty() && !inc.timestampIso.isEmpty()) dst.timestampIso = inc.timestampIso;
            for (auto it = inc.rawAttributes.begin(); it != inc.rawAttributes.end(); ++it) {
                dst.rawAttributes.insert(it.key(), it.value());
            }
            if (!dst.sourceEndpoint.contains(inc.sourceEndpoint)) {
                dst.sourceEndpoint += QStringLiteral(" + ") + inc.sourceEndpoint;
            }
        }
    }
}

void Link2RailClient::onDataReplyFinished(QNetworkReply *reply)
{
    m_pendingRequests = std::max(0, m_pendingRequests - 1);
    const QString sourceTag = reply->property("sourceTag").toString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QUrl finalUrl = reply->url();
    const QByteArray raw = reply->readAll();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrStr = reply->errorString();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus >= 400 && httpStatus < 600)) {
        const QString errMsg = QStringLiteral("[%1] API-Fehler HTTP %2: %3 %4")
                                   .arg(sourceTag).arg(httpStatus).arg(netErrStr, QString::fromUtf8(raw).left(300).trimmed());
        appendLog(errMsg);
        emit apiError(errMsg);
        if (httpStatus == 401) {
            m_authenticated = false;
            emit authenticationChanged(false, QStringLiteral("Sitzung abgelaufen (HTTP 401) – bitte neu anmelden."));
        }
        return;
    }

    m_lastRawResponse = QString::fromUtf8(raw);
    const QByteArray trimmed = raw.trimmed();
    const QByteArray lowHead = trimmed.left(200).toLower();
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive) ||
        lowHead.startsWith("<!doctype html") || lowHead.startsWith("<html")) {
        const QString htmlMsg = QStringLiteral(
            "[%1] Warnung: Endpunkt (%2) lieferte HTML statt JSON/XML/CSV (ggf. falsche Daten-URL oder Web-Login abgelaufen). "
            "Tipp: Per '📋 Browser-Session / cURL' die echte JSON-/XML-Abfrage aus dem Browser übernehmen oder CSV/JSON über '📂 Datei laden' importieren.")
            .arg(sourceTag, finalUrl.toString());
        appendLog(htmlMsg);
        emit apiError(htmlMsg);
        return;
    }

    QString parseSummary;
    QVector<Link2RailWagonRecord> parsed;
    if (trimmed.startsWith('<')) {
        parsed = parseXmlPayload(raw, sourceTag, &parseSummary, m_providerId);
    } else if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
        parsed = parseJsonPayload(raw, sourceTag, &parseSummary, m_providerId);
    } else {
        parsed = parseCsvPayload(raw, sourceTag, &parseSummary, m_providerId);
    }

    appendLog(QStringLiteral("HTTP %1 (%2 Bytes) -> %3").arg(httpStatus).arg(raw.size()).arg(parseSummary));
    mergeBatchRecords(parsed);

    if (m_pendingRequests == 0) {
        m_records = m_pendingBatch;
        m_lastSyncTime = QDateTime::currentDateTime();
        int gpsCount = 0;
        for (const auto &r : m_records) {
            if (r.hasValidGps) gpsCount++;
        }
        const QString finalSummary = QStringLiteral("%1 Live-Sync: %2 Waggons (%3 mit GPS) um %4.")
                                         .arg(info().shortName).arg(m_records.size()).arg(gpsCount)
                                         .arg(m_lastSyncTime.toString(QStringLiteral("HH:mm:ss")));
        appendLog(finalSummary);
        emit liveDataUpdated(m_records, finalSummary);
    }
}

QVector<Link2RailWagonRecord> Link2RailClient::parseJsonPayload(const QByteArray &rawJson,
                                                                const QString &sourceTag,
                                                                QString *outSummary,
                                                                TelematicsProviderId providerId)
{
    QVector<Link2RailWagonRecord> result;
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(rawJson, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || doc.isNull()) {
        if (outSummary) {
            *outSummary = QStringLiteral("JSON-Parse-Fehler: %1").arg(parseErr.errorString());
        }
        return result;
    }

    if (doc.isArray()) {
        collectRecordsRecursive(doc.array(), QString(), QString(), QString(), QString(), sourceTag, providerId, result);
    } else if (doc.isObject()) {
        collectRecordsRecursive(doc.object(), QString(), QString(), QString(), QString(), sourceTag, providerId, result);
    }

    if (outSummary) {
        int withGps = 0;
        for (const auto &r : result) {
            if (r.hasValidGps) withGps++;
        }
        *outSummary = QStringLiteral("%1 Waggons erkannt (%2 mit GPS)").arg(result.size()).arg(withGps);
    }
    return result;
}

QVector<Link2RailWagonRecord> Link2RailClient::parseXmlPayload(const QByteArray &rawXml,
                                                               const QString &sourceTag,
                                                               QString *outSummary,
                                                               TelematicsProviderId providerId)
{
    QVector<Link2RailWagonRecord> result;
    QXmlStreamReader xml(rawXml);

    const QSet<QString> itemTags = {
        QStringLiteral("wagon"), QStringLiteral("waggon"), QStringLiteral("equipment"),
        QStringLiteral("asset"), QStringLiteral("vehicle"), QStringLiteral("unit"),
        QStringLiteral("item"), QStringLiteral("record"), QStringLiteral("position"),
        QStringLiteral("row")
    };

    bool inItem = false;
    QString currentItemTag;
    int itemDepth = 0;
    int depth = 0;
    QJsonObject currentObj;

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            depth++;
            const QString tag = xml.name().toString();
            const QString tagLower = tag.toLower();

            if (!inItem && itemTags.contains(tagLower)) {
                inItem = true;
                currentItemTag = tagLower;
                itemDepth = depth;
                currentObj = QJsonObject();
                for (const auto &attr : xml.attributes()) {
                    currentObj.insert(attr.name().toString(), attr.value().toString());
                }
            } else if (inItem) {
                for (const auto &attr : xml.attributes()) {
                    currentObj.insert(attr.name().toString(), attr.value().toString());
                }
                const QString txt = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                depth--; // readElementText konsumiert das schließende Tag
                if (!txt.isEmpty()) {
                    currentObj.insert(tag, txt);
                }
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            const QString tagLower = xml.name().toString().toLower();
            if (inItem && depth == itemDepth && tagLower == currentItemTag) {
                Link2RailWagonRecord rec = parseSingleWagonObject(
                    currentObj, QString(), QString(), QString(), QString(), sourceTag, providerId);
                if (rec.hasValidGps || !rec.evn.isEmpty()) {
                    if (rec.evn.isEmpty()) {
                        rec.evn = QString("%1-XML-%2").arg(providerInfo(providerId).key.toUpper()).arg(result.size() + 1);
                    }
                    result.append(rec);
                }
                inItem = false;
            }
            depth--;
        }
    }

    if (outSummary) {
        int withGps = 0;
        for (const auto &r : result) if (r.hasValidGps) withGps++;
        *outSummary = QStringLiteral("%1 XML-Waggons erkannt (%2 mit GPS)").arg(result.size()).arg(withGps);
    }
    return result;
}

QVector<Link2RailWagonRecord> Link2RailClient::parseCsvPayload(const QByteArray &rawCsv,
                                                               const QString &sourceTag,
                                                               QString *outSummary,
                                                               TelematicsProviderId providerId)
{
    QVector<Link2RailWagonRecord> result;
    QString text = QString::fromUtf8(rawCsv);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 2) {
        if (outSummary) *outSummary = QStringLiteral("CSV enthält keine Datenzeilen.");
        return result;
    }

    const QChar sep = (lines[0].count(';') >= lines[0].count(',')) ? QChar(';') : QChar(',');
    const QStringList headers = lines[0].split(sep);
    QStringList normHeaders;
    for (QString h : headers) {
        h = h.trimmed().remove('"').toLower();
        normHeaders << h;
    }

    auto colVal = [&](const QStringList &cols, const QStringList &candidates) -> QString {
        for (const QString &cand : candidates) {
            for (int i = 0; i < normHeaders.size() && i < cols.size(); ++i) {
                if (normHeaders[i] == cand || normHeaders[i].contains(cand)) {
                    QString v = cols[i].trimmed();
                    if (v.startsWith('"') && v.endsWith('"') && v.size() >= 2) {
                        v = v.mid(1, v.size() - 2);
                    }
                    if (!v.isEmpty()) return v;
                }
            }
        }
        return QString();
    };

    for (int row = 1; row < lines.size(); ++row) {
        const QStringList cols = lines[row].split(sep);
        if (cols.isEmpty()) continue;

        Link2RailWagonRecord rec;
        stampProvider(rec, providerId);
        rec.sourceEndpoint = sourceTag;
        rec.evn = colVal(cols, {QStringLiteral("wagonnumber"), QStringLiteral("wagon"), QStringLiteral("wagennummer"), QStringLiteral("evn"), QStringLiteral("uic"), QStringLiteral("asset")});
        rec.trainNumber = colVal(cols, {QStringLiteral("trainnumber"), QStringLiteral("zugnummer"), QStringLiteral("train"), QStringLiteral("consist")});
        rec.shipmentId = colVal(cols, {QStringLiteral("shipment"), QStringLiteral("sendung"), QStringLiteral("order")});
        rec.wagonTypeCode = colVal(cols, {QStringLiteral("gattung"), QStringLiteral("type"), QStringLiteral("bauart"), QStringLiteral("class")});
        rec.statusText = colVal(cols, {QStringLiteral("status"), QStringLiteral("state")});
        rec.locationName = colVal(cols, {QStringLiteral("location"), QStringLiteral("ort"), QStringLiteral("station"), QStringLiteral("bahnhof")});
        rec.geofenceSite = colVal(cols, {QStringLiteral("geofence"), QStringLiteral("site"), QStringLiteral("werk")});
        rec.timestampIso = colVal(cols, {QStringLiteral("timestamp"), QStringLiteral("zeit"), QStringLiteral("date")});
        rec.cargoDescription = colVal(cols, {QStringLiteral("cargo"), QStringLiteral("gut"), QStringLiteral("product"), QStringLiteral("ladung")});
        rec.originName = colVal(cols, {QStringLiteral("origin"), QStringLiteral("versand"), QStringLiteral("quelle"), QStringLiteral("from")});
        rec.destinationName = colVal(cols, {QStringLiteral("destination"), QStringLiteral("empfang"), QStringLiteral("ziel"), QStringLiteral("to")});

        QString latS = colVal(cols, {QStringLiteral("latitude"), QStringLiteral("lat"), QStringLiteral("breite")}).replace(',', '.');
        QString lonS = colVal(cols, {QStringLiteral("longitude"), QStringLiteral("lon"), QStringLiteral("lng"), QStringLiteral("laenge"), QStringLiteral("länge")}).replace(',', '.');
        bool okLa = false, okLo = false;
        const double la = latS.toDouble(&okLa);
        const double lo = lonS.toDouble(&okLo);
        if (okLa && okLo && std::abs(la) > 0.001 && std::abs(lo) > 0.001) {
            rec.hasValidGps = true;
            rec.rawLat = la;
            rec.rawLon = lo;
        }

        const QString spdS = colVal(cols, {QStringLiteral("speed"), QStringLiteral("kmh")}).replace(',', '.');
        bool okSp = false;
        const double sp = spdS.toDouble(&okSp);
        if (okSp) rec.speedKmh = sp;

        const QString standS = colVal(cols, {QStringLiteral("standing"), QStringLiteral("standzeit"), QStringLiteral("dwell")}).replace(',', '.');
        bool okSt = false;
        const double st = standS.toDouble(&okSt);
        if (okSt) rec.standingTimeMinutes = static_cast<int>(std::llround(st));

        for (int i = 0; i < normHeaders.size() && i < cols.size(); ++i) {
            rec.rawAttributes.insert(headers[i].trimmed().remove('"'), cols[i].trimmed().remove('"'));
        }

        if (!rec.evn.isEmpty() || rec.hasValidGps) {
            if (rec.evn.isEmpty()) rec.evn = QString("%1-CSV-%2").arg(providerInfo(providerId).key.toUpper()).arg(row);
            result.append(rec);
        }
    }

    if (outSummary) {
        int withGps = 0;
        for (const auto &r : result) if (r.hasValidGps) withGps++;
        *outSummary = QStringLiteral("%1 CSV-Waggons importiert (%2 mit GPS)").arg(result.size()).arg(withGps);
    }
    return result;
}

bool Link2RailClient::importFromFile(const QString &filePath, QString *outError)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (outError) *outError = QStringLiteral("Datei konnte nicht geöffnet werden: %1").arg(f.errorString());
        return false;
    }
    const QByteArray raw = f.readAll();
    const QString ext = QFileInfo(filePath).suffix().toLower();
    const QString tag = info().shortName + QStringLiteral(" Import");
    QString summary;
    QVector<Link2RailWagonRecord> parsed;
    if (ext == QStringLiteral("xml")) {
        parsed = parseXmlPayload(raw, tag, &summary, m_providerId);
    } else if (ext == QStringLiteral("csv") || ext == QStringLiteral("txt")) {
        parsed = parseCsvPayload(raw, tag, &summary, m_providerId);
    } else {
        parsed = parseJsonPayload(raw, tag, &summary, m_providerId);
    }

    if (parsed.isEmpty()) {
        if (outError) *outError = QStringLiteral("Keine Waggon-/GPS-Datensätze gefunden (%1).").arg(summary);
        return false;
    }

    m_records = parsed;
    m_lastSyncTime = QDateTime::currentDateTime();
    m_lastRawResponse = QString::fromUtf8(raw);
    const QString msg = QStringLiteral("Datei '%1' geladen: %2").arg(QFileInfo(filePath).fileName(), summary);
    appendLog(msg);
    emit liveDataUpdated(m_records, msg);
    return true;
}

bool Link2RailClient::exportRecordsToJsonFile(const QVector<Link2RailWagonRecord> &records,
                                              const QString &filePath,
                                              QString *outError)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (outError) *outError = f.errorString();
        return false;
    }
    QJsonArray arr;
    for (const auto &r : records) {
        QJsonObject o;
        o[QStringLiteral("provider")] = r.providerName;
        o[QStringLiteral("wagonNumber")] = r.evn;
        o[QStringLiteral("trainNumber")] = r.trainNumber;
        o[QStringLiteral("shipmentId")] = r.shipmentId;
        o[QStringLiteral("wagonType")] = r.wagonTypeCode;
        o[QStringLiteral("hasValidGps")] = r.hasValidGps;
        o[QStringLiteral("latitude")] = r.rawLat;
        o[QStringLiteral("longitude")] = r.rawLon;
        o[QStringLiteral("speedKmh")] = r.speedKmh;
        o[QStringLiteral("status")] = r.statusText;
        o[QStringLiteral("lastEventType")] = r.lastEventType;
        o[QStringLiteral("locationName")] = r.locationName;
        o[QStringLiteral("locationCode")] = r.locationCode;
        o[QStringLiteral("geofenceSite")] = r.geofenceSite;
        o[QStringLiteral("standingTimeMinutes")] = r.standingTimeMinutes;
        o[QStringLiteral("timestamp")] = r.timestampIso;
        o[QStringLiteral("cargo")] = r.cargoDescription;
        o[QStringLiteral("weightTons")] = r.weightTons;
        o[QStringLiteral("origin")] = r.originName;
        o[QStringLiteral("destination")] = r.destinationName;
        o[QStringLiteral("eta")] = r.etaIso;
        o[QStringLiteral("sourceEndpoint")] = r.sourceEndpoint;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("exportedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root[QStringLiteral("system")] = QStringLiteral("SHS Logistics GmbH – Multi-Telematik Live-GPS Leitsystem");
    root[QStringLiteral("count")] = records.size();
    root[QStringLiteral("wagons")] = arr;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool Link2RailClient::exportRecordsToCsvFile(const QVector<Link2RailWagonRecord> &records,
                                             const QString &filePath,
                                             QString *outError)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (outError) *outError = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts << "Provider;EVN;TrainNumber;ShipmentId;WagonType;Latitude;Longitude;SpeedKmh;Status;LastEvent;Location;DS100;Geofence;StandingMin;Timestamp;Cargo;WeightTons;Origin;Destination;ETA\n";
    auto esc = [](QString s) {
        s.replace(';', ',');
        s.replace('\n', ' ');
        return s;
    };
    for (const auto &r : records) {
        ts << esc(r.providerName) << ';'
           << esc(r.evn) << ';'
           << esc(r.trainNumber) << ';'
           << esc(r.shipmentId) << ';'
           << esc(r.wagonTypeCode) << ';'
           << QString::number(r.rawLat, 'f', 6) << ';'
           << QString::number(r.rawLon, 'f', 6) << ';'
           << QString::number(r.speedKmh, 'f', 1) << ';'
           << esc(r.statusText) << ';'
           << esc(r.lastEventType) << ';'
           << esc(r.locationName) << ';'
           << esc(r.locationCode) << ';'
           << esc(r.geofenceSite) << ';'
           << r.standingTimeMinutes << ';'
           << esc(r.timestampIso) << ';'
           << esc(r.cargoDescription) << ';'
           << QString::number(r.weightTons, 'f', 1) << ';'
           << esc(r.originName) << ';'
           << esc(r.destinationName) << ';'
           << esc(r.etaIso) << '\n';
    }
    return true;
}
