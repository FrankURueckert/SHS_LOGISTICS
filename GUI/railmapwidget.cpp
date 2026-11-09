#include "railmapwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QtMath>
#include <algorithm>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusMeters = 6378137.0;

double degToRad(double deg) { return deg * kPi / 180.0; }
double radToDeg(double rad) { return rad * 180.0 / kPi; }

double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = degToRad(lat2 - lat1);
    const double dLon = degToRad(lon2 - lon1);
    const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                     std::cos(degToRad(lat1)) * std::cos(degToRad(lat2)) *
                     std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return kEarthRadiusMeters * c;
}

double bearingDegrees(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = degToRad(lat1);
    const double phi2 = degToRad(lat2);
    const double dLambda = degToRad(lon2 - lon1);
    const double y = std::sin(dLambda) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2) -
                     std::sin(phi1) * std::cos(phi2) * std::cos(dLambda);
    double brng = radToDeg(std::atan2(y, x));
    return std::fmod(brng + 360.0, 360.0);
}

double lonToMercX(double lon)
{
    return (lon + 180.0) / 360.0;
}

double latToMercY(double lat)
{
    const double clamped = std::clamp(lat, -85.05112878, 85.05112878);
    const double rad = degToRad(clamped);
    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / kPi) / 2.0;
}

double mercXToLon(double x)
{
    return x * 360.0 - 180.0;
}

double mercYToLat(double y)
{
    const double n = kPi - 2.0 * kPi * y;
    return radToDeg(std::atan(0.5 * (std::exp(n) - std::exp(-n))));
}
} // namespace

RailMapWidget::RailMapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(640, 460);
    setFocusPolicy(Qt::StrongFocus);

    m_netManager = new QNetworkAccessManager(this);
    connect(m_netManager, &QNetworkAccessManager::finished,
            this, &RailMapWidget::onTileReplyFinished);

    initGeographyAndTracks();
    initSourcesAndSinks();
    initDefaultTrainsAndCargo();
    updateAllTrainGeometries();

    m_simTimer = new QTimer(this);
    m_simTimer->setInterval(33);
    connect(m_simTimer, &QTimer::timeout, this, &RailMapWidget::onSimTimerTick);
    m_simTimer->start();

    m_cameraAnimTimer = new QTimer(this);
    m_cameraAnimTimer->setInterval(16);
    connect(m_cameraAnimTimer, &QTimer::timeout, this, &RailMapWidget::onCameraAnimTick);
}

RailMapWidget::~RailMapWidget() = default;

QColor RailMapWidget::colorForWagon(WagonCategory cat, WagonColorMode mode)
{
    if (mode == WagonColorMode::StrictByAxleCount) {
        const int axles = axleCountForCategory(cat);
        return (axles == 6) ? QColor("#EA580C") : QColor("#0284C7");
    }
    if (mode == WagonColorMode::HighlightSammsVsRnsz) {
        switch (cat) {
        case WagonCategory::Samms_6Axle:    return QColor("#F97316");
        case WagonCategory::Rnsz_4Axle:     return QColor("#06B6D4");
        case WagonCategory::Standard_4Axle: return QColor("#64748B");
        case WagonCategory::Heavy_6Axle:    return QColor("#94A3B8");
        }
    }
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return QColor("#EF7D00"); // SHS-Orange (Samms 6-Achser)
    case WagonCategory::Rnsz_4Axle:     return QColor("#0284C7"); // SHS-Stahlblau (Rns-z 4-Achser)
    case WagonCategory::Standard_4Axle: return QColor("#10B981"); // Smaragdgrün (4-Achser Shimmns Coilwagen)
    case WagonCategory::Heavy_6Axle:    return QColor("#E11D48"); // Karmesinrot (6-Achser Torpedo/Faals)
    }
    return QColor("#EF7D00");
}

QString RailMapWidget::typeNameForWagon(WagonCategory cat)
{
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return QStringLiteral("Samms 489/709 (6-Achser Schwerlast)");
    case WagonCategory::Rnsz_4Axle:     return QStringLiteral("Rns-z 655 (4-Achser Rungenwagen)");
    case WagonCategory::Standard_4Axle: return QStringLiteral("Shimmns / 4-Achser (Coil-/Haubenwagen)");
    case WagonCategory::Heavy_6Axle:    return QStringLiteral("Torpedowagen (6-Achser Flüssiges Eisen 1420°C)");
    }
    return QStringLiteral("Samms (6-Achser)");
}

QString RailMapWidget::shortCodeForWagon(WagonCategory cat)
{
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return QStringLiteral("Samms [6A]");
    case WagonCategory::Rnsz_4Axle:     return QStringLiteral("Rns-z [4A]");
    case WagonCategory::Standard_4Axle: return QStringLiteral("4-Achser [4A]");
    case WagonCategory::Heavy_6Axle:    return QStringLiteral("Torpedo [6A]");
    }
    return QStringLiteral("Samms");
}

int RailMapWidget::axleCountForCategory(WagonCategory cat)
{
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return 6;
    case WagonCategory::Rnsz_4Axle:     return 4;
    case WagonCategory::Standard_4Axle: return 4;
    case WagonCategory::Heavy_6Axle:    return 6;
    }
    return 4;
}

double RailMapWidget::lengthForCategory(WagonCategory cat)
{
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return 16.4;
    case WagonCategory::Rnsz_4Axle:     return 19.9;
    case WagonCategory::Standard_4Axle: return 14.2;
    case WagonCategory::Heavy_6Axle:    return 18.5;
    }
    return 16.4;
}

double RailMapWidget::maxPayloadTonsForCategory(WagonCategory cat)
{
    switch (cat) {
    case WagonCategory::Samms_6Axle:    return 90.0; // 6-Achser Schwerlast bis ~92 t
    case WagonCategory::Rnsz_4Axle:     return 62.0; // 4-Achser Rungenwagen bis ~64 t
    case WagonCategory::Standard_4Axle: return 56.0; // 4-Achser Shimmns Coilwagen bis ~58 t
    case WagonCategory::Heavy_6Axle:    return 96.0; // 6-Achser Torpedopfannenwagen (Flüssigeisen)
    }
    return 85.0;
}

QString RailMapWidget::productName(CargoProductType prod)
{
    switch (prod) {
    case CargoProductType::SteelBillets:     return QStringLiteral("Stahlknüppel 180x180 mm (Halbzeug)");
    case CargoProductType::HeavyPlates:      return QStringLiteral("Dillinger Grobbleche (Schwerblech)");
    case CargoProductType::SteelSlabs:       return QStringLiteral("Stahlbrammen / Slabs (Vorprodukt)");
    case CargoProductType::WireRodCoils:     return QStringLiteral("Aufgerollter Walzdraht (Drahtcoils)");
    case CargoProductType::SteelBlooms:      return QStringLiteral("Strangguss-Vorblöcke (Blooms)");
    case CargoProductType::BarSteelProfiles: return QStringLiteral("Stabstahl & Edelbaustahl-Profile");
    case CargoProductType::LiquidPigIron:    return QStringLiteral("Flüssiges Eisen / Roheisen 1420°C (Torpedo)");
    case CargoProductType::ForgedParts:      return QStringLiteral("Freiform-Schmiedestücke (Wellen)");
    case CargoProductType::EmptyWagon:       return QStringLiteral("Leerwagen (ohne Beladung)");
    }
    return QStringLiteral("Stahlknüppel");
}

QString RailMapWidget::productShortName(CargoProductType prod)
{
    switch (prod) {
    case CargoProductType::SteelBillets:     return QStringLiteral("Knüppel");
    case CargoProductType::HeavyPlates:      return QStringLiteral("Grobbleche");
    case CargoProductType::SteelSlabs:       return QStringLiteral("Brammen");
    case CargoProductType::WireRodCoils:     return QStringLiteral("Draht-Coils");
    case CargoProductType::SteelBlooms:      return QStringLiteral("Vorblöcke");
    case CargoProductType::BarSteelProfiles: return QStringLiteral("Stabstahl");
    case CargoProductType::LiquidPigIron:    return QStringLiteral("🔥Flüssigeisen");
    case CargoProductType::ForgedParts:      return QStringLiteral("Schmiedeteile");
    case CargoProductType::EmptyWagon:       return QStringLiteral("Leer");
    }
    return QStringLiteral("Stahl");
}

WagonCategory RailMapWidget::recommendedWagonForProduct(CargoProductType prod)
{
    switch (prod) {
    case CargoProductType::SteelBillets:     return WagonCategory::Samms_6Axle;
    case CargoProductType::HeavyPlates:      return WagonCategory::Rnsz_4Axle;
    case CargoProductType::SteelSlabs:       return WagonCategory::Samms_6Axle;
    case CargoProductType::WireRodCoils:     return WagonCategory::Standard_4Axle;
    case CargoProductType::SteelBlooms:      return WagonCategory::Samms_6Axle;
    case CargoProductType::BarSteelProfiles: return WagonCategory::Rnsz_4Axle;
    case CargoProductType::LiquidPigIron:    return WagonCategory::Heavy_6Axle;
    case CargoProductType::ForgedParts:      return WagonCategory::Samms_6Axle;
    case CargoProductType::EmptyWagon:       return WagonCategory::Rnsz_4Axle;
    }
    return WagonCategory::Samms_6Axle;
}

QString RailMapWidget::defaultSourceForProduct(CargoProductType prod)
{
    switch (prod) {
    case CargoProductType::LiquidPigIron:    return QStringLiteral("Q-DL-ROGESA");
    case CargoProductType::SteelSlabs:       return QStringLiteral("Q-DL-STRANG");
    case CargoProductType::HeavyPlates:      return QStringLiteral("QS-DL-BLECH");
    case CargoProductType::SteelBillets:     return QStringLiteral("QS-VK-LD");
    case CargoProductType::SteelBlooms:      return QStringLiteral("QS-VK-LD");
    case CargoProductType::ForgedParts:      return QStringLiteral("QS-VK-SCHMIEDE");
    case CargoProductType::WireRodCoils:     return QStringLiteral("QS-BB-DRAHT");
    case CargoProductType::BarSteelProfiles: return QStringLiteral("QS-NK-WALZ");
    case CargoProductType::EmptyWagon:       return QStringLiteral("P-VK-ABSTELL");
    }
    return QStringLiteral("QS-VK-LD");
}

QString RailMapWidget::defaultSinkForProduct(CargoProductType prod)
{
    switch (prod) {
    case CargoProductType::LiquidPigIron:    return QStringLiteral("QS-VK-LD");
    case CargoProductType::SteelSlabs:       return QStringLiteral("QS-DL-BLECH");
    case CargoProductType::HeavyPlates:      return QStringLiteral("S-DL-HAFEN");
    case CargoProductType::SteelBillets:     return QStringLiteral("QS-BB-DRAHT");
    case CargoProductType::SteelBlooms:      return QStringLiteral("QS-NK-WALZ");
    case CargoProductType::ForgedParts:      return QStringLiteral("P-VK-ABSTELL");
    case CargoProductType::WireRodCoils:     return QStringLiteral("QS-VK-WALZ");
    case CargoProductType::BarSteelProfiles: return QStringLiteral("P-VK-ABSTELL");
    case CargoProductType::EmptyWagon:       return QStringLiteral("QS-VK-LD");
    }
    return QStringLiteral("QS-BB-DRAHT");
}

void RailMapWidget::initGeographyAndTracks()
{
    m_roads.clear();
    m_buildings.clear();
    m_tracks.clear();

    // =========================================================================
    // 1. FLÜSSE (Exakter Verlauf der Saar & Rossel nach Google Maps)
    // =========================================================================
    {
        SimplifiedRoad saar;
        saar.name = QStringLiteral("Saar (Wasserstraße)");
        saar.roadClass = QStringLiteral("river");
        saar.path = {
            {49.3680, 6.7210}, {49.3520, 6.7310}, {49.3320, 6.7440},
            {49.3080, 6.7660}, {49.2820, 6.7920}, {49.2660, 6.8170},
            {49.2575, 6.8325}, {49.2505, 6.8410}, {49.2462, 6.8475},
            {49.2422, 6.8545}, {49.2400, 6.8635}, {49.2395, 6.8725},
            {49.2428, 6.8835}, {49.2448, 6.8960}, {49.2412, 6.9180},
            {49.2375, 6.9450}, {49.2345, 6.9780}
        };
        m_roads.push_back(saar);

        SimplifiedRoad rossel;
        rossel.name = QStringLiteral("Rossel");
        rossel.roadClass = QStringLiteral("river");
        rossel.path = {
            {49.2220, 6.8420}, {49.2310, 6.8505}, {49.2375, 6.8528}, {49.2422, 6.8545}
        };
        m_roads.push_back(rossel);
    }

    // =========================================================================
    // 2. STRASSENNETZ (Exakt auf den Google-Maps-Verlauf A620, B51, Werksstraßen abgestimmt)
    // =========================================================================
    {
        SimplifiedRoad a620;
        a620.name = QStringLiteral("BAB A620 (Saarlouis – Völklingen – Saarbrücken)");
        a620.roadClass = QStringLiteral("highway");
        a620.path = {
            {49.3420, 6.7320}, {49.3040, 6.7630}, {49.2720, 6.8080},
            {49.2565, 6.8310}, {49.2495, 6.8400}, {49.2445, 6.8475},
            {49.2412, 6.8555}, {49.2392, 6.8655}, {49.2388, 6.8745},
            {49.2420, 6.8855}, {49.2440, 6.8970}, {49.2370, 6.9460}
        };
        m_roads.push_back(a620);

        SimplifiedRoad b51;
        b51.name = QStringLiteral("B51 (Bismarckstraße / Saarbrücker Str.)");
        b51.roadClass = QStringLiteral("primary");
        b51.path = {
            {49.2610, 6.8300}, {49.2545, 6.8415}, {49.2512, 6.8495},
            {49.2485, 6.8585}, {49.2465, 6.8695}, {49.2458, 6.8810},
            {49.2472, 6.8950}, {49.2425, 6.9480}
        };
        m_roads.push_back(b51);

        SimplifiedRoad rathausStr;
        rathausStr.name = QStringLiteral("Rathausstraße / Stadionstraße");
        rathausStr.roadClass = QStringLiteral("primary");
        rathausStr.path = {
            {49.2435, 6.8495}, {49.2482, 6.8565}, {49.2528, 6.8645}
        };
        m_roads.push_back(rathausStr);

        SimplifiedRoad werksStrVK;
        werksStrVK.name = QStringLiteral("Saarstahl Werksstraße (Torhaus 1 – LD-Stahlwerk – Walzwerk)");
        werksStrVK.roadClass = QStringLiteral("plant");
        werksStrVK.path = {
            {49.2508, 6.8445}, {49.2478, 6.8510}, {49.2452, 6.8558},
            {49.2442, 6.8635}, {49.2448, 6.8725}
        };
        m_roads.push_back(werksStrVK);
    }

    // =========================================================================
    // 3. WERKSGELÄNDE & WERKHALLEN (Exakt auf die realen Google-Maps-Koordinaten gelegt!)
    // =========================================================================
    {
        // --- WERK VÖLKLINGEN (SAARSTAHL AG) ---
        PlantBuilding vkPerimeter;
        vkPerimeter.name = QStringLiteral("SAARSTAHL AG – WERK VÖLKLINGEN");
        vkPerimeter.subtitle = QStringLiteral("Gesamtes Hütten- & Walzwerksgelände Völklingen");
        vkPerimeter.siteId = QStringLiteral("Voelklingen");
        vkPerimeter.fillColor = QColor(72, 85, 95, 55);
        vkPerimeter.borderColor = QColor("#EF7D00");
        vkPerimeter.footprint = {
            {49.2545, 6.8375}, {49.2518, 6.8475}, {49.2488, 6.8575},
            {49.2468, 6.8695}, {49.2455, 6.8795}, {49.2422, 6.8775},
            {49.2408, 6.8650}, {49.2412, 6.8525}, {49.2462, 6.8455},
            {49.2512, 6.8385}
        };
        m_buildings.push_back(vkPerimeter);

        // Weltkulturerbe Völklinger Hütte (Exakt bei 49.2510, 6.8430)
        PlantBuilding wkeHuette;
        wkeHuette.name = QStringLiteral("WKE Völklinger Hütte & Übergabebahnhof");
        wkeHuette.subtitle = QStringLiteral("Westkopf / DB-Übergabe");
        wkeHuette.siteId = QStringLiteral("Voelklingen");
        wkeHuette.fillColor = QColor(120, 113, 108, 165);
        wkeHuette.borderColor = QColor("#F59E0B");
        wkeHuette.footprint = {
            {49.2525, 6.8405}, {49.2528, 6.8448}, {49.2498, 6.8462}, {49.2495, 6.8418}
        };
        m_buildings.push_back(wkeHuette);

        // Saarschmiede GmbH Freiformschmiede (49.2488, 6.8490)
        PlantBuilding saarschmiede;
        saarschmiede.name = QStringLiteral("Saarschmiede GmbH (Freiformschmiede)");
        saarschmiede.subtitle = QStringLiteral("Schmiedeblöcke -> Turbinen- & Generatorwellen");
        saarschmiede.siteId = QStringLiteral("Voelklingen");
        saarschmiede.fillColor = QColor(37, 99, 235, 165);
        saarschmiede.borderColor = QColor("#93C5FD");
        saarschmiede.footprint = {
            {49.2504, 6.8468}, {49.2492, 6.8542}, {49.2470, 6.8530}, {49.2482, 6.8458}
        };
        m_buildings.push_back(saarschmiede);

        // Saarstahl LD-Stahlwerk & Strangguss (Exakt auf Google Maps "Saarstahl LD Stahlwerk" bei 49.2432, 6.8555)
        PlantBuilding ldStahlwerk;
        ldStahlwerk.name = QStringLiteral("Saarstahl LD-Stahlwerk & Strangguss (SGA 1–4)");
        ldStahlwerk.subtitle = QStringLiteral("Senke: Flüssigroheisen | Quelle: Knüppel, Vorblöcke, Brammen");
        ldStahlwerk.siteId = QStringLiteral("Voelklingen");
        ldStahlwerk.fillColor = QColor(185, 28, 28, 175);
        ldStahlwerk.borderColor = QColor("#FCA5A5");
        ldStahlwerk.footprint = {
            {49.2462, 6.8518}, {49.2455, 6.8615}, {49.2416, 6.8602}, {49.2422, 6.8508}
        };
        m_buildings.push_back(ldStahlwerk);

        // Walzwerk Nauweiler Gewann & Ladestelle 1 (49.2445, 6.8685)
        PlantBuilding walzwerkVK;
        walzwerkVK.name = QStringLiteral("Walzwerk Nauweiler Gewann & Ladestelle 1");
        walzwerkVK.subtitle = QStringLiteral("Senke: Knüppel | Quelle: Stabstahl, Profile & Walzdraht-Coils");
        walzwerkVK.siteId = QStringLiteral("Voelklingen");
        walzwerkVK.fillColor = QColor(14, 116, 144, 175);
        walzwerkVK.borderColor = QColor("#67E8F9");
        walzwerkVK.footprint = {
            {49.2462, 6.8625}, {49.2455, 6.8768}, {49.2426, 6.8755}, {49.2432, 6.8618}
        };
        m_buildings.push_back(walzwerkVK);

        // --- DILLINGER HÜTTE & ROGESA (DILLINGEN/SAAR) ---
        PlantBuilding dillingenPerimeter;
        dillingenPerimeter.name = QStringLiteral("DILLINGER HÜTTE & ROGESA");
        dillingenPerimeter.subtitle = QStringLiteral("Hochöfen, Stahlwerk, Grobblechwalzwerk & SHS Logistics Zentrale");
        dillingenPerimeter.siteId = QStringLiteral("Dillingen");
        dillingenPerimeter.fillColor = QColor(185, 28, 28, 60);
        dillingenPerimeter.borderColor = QColor("#EF7D00");
        dillingenPerimeter.footprint = {
            {49.3655, 6.7330}, {49.3650, 6.7625}, {49.3525, 6.7605}, {49.3530, 6.7310}
        };
        m_buildings.push_back(dillingenPerimeter);

        PlantBuilding rogesaHochoefen;
        rogesaHochoefen.name = QStringLiteral("ROGESA Hochöfen 4 & 5 + Strangguss Dillingen");
        rogesaHochoefen.subtitle = QStringLiteral("Quelle: Flüssigroheisen (1420°C) & schwere Stahlbrammen");
        rogesaHochoefen.siteId = QStringLiteral("Dillingen");
        rogesaHochoefen.fillColor = QColor(220, 38, 38, 180);
        rogesaHochoefen.borderColor = QColor("#FDE047");
        rogesaHochoefen.footprint = {
            {49.3622, 6.7380}, {49.3618, 6.7485}, {49.3578, 6.7480}, {49.3582, 6.7375}
        };
        m_buildings.push_back(rogesaHochoefen);

        PlantBuilding grobblechDL;
        grobblechDL.name = QStringLiteral("Dillinger Grobblechwalzwerk & Blechversand");
        grobblechDL.subtitle = QStringLiteral("Senke: Brammen | Quelle: Dillinger Grobbleche (auf Samms & Rns-z)");
        grobblechDL.siteId = QStringLiteral("Dillingen");
        grobblechDL.fillColor = QColor(2, 132, 199, 175);
        grobblechDL.borderColor = QColor("#7DD3FC");
        grobblechDL.footprint = {
            {49.3615, 6.7495}, {49.3608, 6.7605}, {49.3558, 6.7595}, {49.3562, 6.7488}
        };
        m_buildings.push_back(grobblechDL);

        // --- WERK BURBACH (SAARBRÜCKEN-BURBACH) ---
        PlantBuilding burbachWerk;
        burbachWerk.name = QStringLiteral("SAARSTAHL AG – WERK BURBACH (Drahtwerk)");
        burbachWerk.subtitle = QStringLiteral("Senke: Knüppel aus Völklingen | Quelle: Aufgerollter Walzdraht (Coils)");
        burbachWerk.siteId = QStringLiteral("Burbach");
        burbachWerk.fillColor = QColor(14, 116, 144, 175);
        burbachWerk.borderColor = QColor("#FBD50E");
        burbachWerk.footprint = {
            {49.2422, 6.9405}, {49.2415, 6.9565}, {49.2382, 6.9555}, {49.2388, 6.9395}
        };
        m_buildings.push_back(burbachWerk);

        // --- WERK NEUNKIRCHEN (NEUNKIRCHEN/SAAR) ---
        PlantBuilding neunkirchenWerk;
        neunkirchenWerk.name = QStringLiteral("SAARSTAHL AG – WERK NEUNKIRCHEN");
        neunkirchenWerk.subtitle = QStringLiteral("Senke: Vorblöcke | Quelle: Edelbaustahl, Stabstahl & Drahtcoils");
        neunkirchenWerk.siteId = QStringLiteral("Neunkirchen");
        neunkirchenWerk.fillColor = QColor(16, 185, 129, 170);
        neunkirchenWerk.borderColor = QColor("#FBD50E");
        neunkirchenWerk.footprint = {
            {49.3468, 7.1590}, {49.3464, 7.1745}, {49.3420, 7.1735}, {49.3424, 7.1585}
        };
        m_buildings.push_back(neunkirchenWerk);
    }

    // =========================================================================
    // 4. REALITÄTSGETREUE BAHNGLEISE (Exakt auf die Google-Maps-Bahnlinien gelegt!)
    // =========================================================================
    auto addTrack = [this](const QString &id, const QString &name, const QString &siteId,
                           bool isLoop, bool isMainline, bool isStorageSiding, const QColor &col,
                           const QVector<GeoPoint> &pts) {
        RailTrack t;
        t.id = id;
        t.name = name;
        t.siteId = siteId;
        t.isLoop = isLoop;
        t.isMainline = isMainline;
        t.isStorageSiding = isStorageSiding;
        t.trackColor = col;
        t.waypoints = pts;
        computeTrackMetrics(t);
        m_tracks.push_back(t);
    };

    // Gleis 1: Werksringbahn Völklingen (Folgt exakt dem realen Südbogen am Saar-Ufer & Nordgleis an der B51)
    addTrack(
        QStringLiteral("VK-RING"),
        QStringLiteral("Werksringbahn Völklingen (Übergabebhf – LD-Stahlwerk – Walzwerk – Saarschmiede)"),
        QStringLiteral("Voelklingen"),
        true, false, false, QColor("#FBD50E"),
        {
            {49.2532, 6.8395}, // Westkopf Wehrden / Völklingen
            {49.2502, 6.8432}, // Völklingen Hbf Südseite
            {49.2472, 6.8478}, // Westzufahrt LD-Stahlwerk
            {49.2442, 6.8535}, // LD-Stahlwerk & Konverterhalle
            {49.2430, 6.8595}, // Stranggießanlagen (SGA 1–4)
            {49.2432, 6.8665}, // Walzwerk Nauweiler Gewann Süd
            {49.2438, 6.8745}, // Ostkopf Nauweiler Gewann / Fenne
            {49.2452, 6.8752}, // Wendeschleife Ost
            {49.2456, 6.8675}, // Walzwerk Ladestelle 1 Nordgleis
            {49.2468, 6.8590}, // Torhaus 1 / Zentrale Versandgleise
            {49.2486, 6.8515}, // Saarschmiede Schwerlastgleis
            {49.2512, 6.8452}, // Völklinger Hütte Nordgleis
            {49.2532, 6.8395}  // Ringschluss Westkopf
        }
    );

    // Gleis 2: Durchfahrgleis LD-Stahlwerk -> Strangguss -> Walzwerk Ladestelle 1
    addTrack(
        QStringLiteral("VK-LADESTELLE1"),
        QStringLiteral("Werksgleis Völklingen (LD-Stahlwerk – Strangguss – Ladestelle 1)"),
        QStringLiteral("Voelklingen"),
        false, false, false, QColor("#EF7D00"),
        {
            {49.2518, 6.8415},
            {49.2485, 6.8462},
            {49.2452, 6.8525},
            {49.2436, 6.8575},
            {49.2438, 6.8645},
            {49.2445, 6.8725},
            {49.2455, 6.8785}
        }
    );

    // Gleis 3: Saarschmiede & Vorblock-Gleis Völklingen
    addTrack(
        QStringLiteral("VK-SCHMIEDE"),
        QStringLiteral("Schmiedegleis Völklingen (Übergabebhf – Saarschmiede – Vorblocklager)"),
        QStringLiteral("Voelklingen"),
        false, false, false, QColor("#38BDF8"),
        {
            {49.2538, 6.8390},
            {49.2514, 6.8438},
            {49.2492, 6.8485},
            {49.2478, 6.8542},
            {49.2464, 6.8612},
            {49.2455, 6.8685}
        }
    );

    // Gleis 4: Puffer- & Abstellgleis Völklingen (für beladene Waggons ohne freie Lokomotive!)
    addTrack(
        QStringLiteral("VK-ABSTELL"),
        QStringLiteral("Puffer- & Abstellgleis Völklingen (Zwischenlager für Waggons ohne Lok)"),
        QStringLiteral("Voelklingen"),
        false, false, true, QColor("#A855F7"),
        {
            {49.2552, 6.8370},
            {49.2528, 6.8412},
            {49.2504, 6.8452},
            {49.2482, 6.8492},
            {49.2466, 6.8538}
        }
    );

    // Gleis 5: Roheisen- & Brammen-Hauptstrecke Dillingen <-> Saarlouis <-> Bous <-> Völklingen (DB 3230)
    addTrack(
        QStringLiteral("KORRIDOR-DL-VK"),
        QStringLiteral("Saarstrecke DB 3230 (Dillinger Hütte ↔ Saarlouis ↔ Ensdorf ↔ Bous ↔ Völklingen)"),
        QStringLiteral("Corridor"),
        false, true, false, QColor("#F97316"),
        {
            {49.3605, 6.7420}, // Dillinger Hütte ROGESA Hochofen
            {49.3568, 6.7475}, // Übergabebahnhof Dillingen-Hütte
            {49.3485, 6.7435}, // Dillingen (Saar) Bf
            {49.3320, 6.7485}, // Saarlouis Hbf
            {49.3180, 6.7620}, // Fraulautern / Ensdorf Nord
            {49.3045, 6.7735}, // Ensdorf (Saar) Bf
            {49.2895, 6.7875}, // Bous Nord
            {49.2765, 6.7995}, // Bous (Saar) Bf
            {49.2665, 6.8195}, // Wadgassen / Wehrden Nord
            {49.2575, 6.8342}, // Völklingen-Wehrden
            {49.2512, 6.8425}, // Völklingen Hbf
            {49.2470, 6.8485}, // Einfahrt Saarstahl Werk Völklingen
            {49.2438, 6.8555}  // Saarstahl LD-Stahlwerk Völklingen
        }
    );

    // Gleis 6: Knüppel- & Draht-Hauptstrecke Völklingen <-> Luisenthal <-> Werk Burbach (DB 3230 Ost)
    addTrack(
        QStringLiteral("KORRIDOR-VK-BB"),
        QStringLiteral("Saarstrecke DB 3230 Ost (Werk Völklingen ↔ Luisenthal ↔ Altenkessel ↔ Werk Burbach)"),
        QStringLiteral("Corridor"),
        false, true, false, QColor("#0EA5E9"),
        {
            {49.2436, 6.8595}, // Saarstahl Strangguss Völklingen
            {49.2435, 6.8685}, // Walzwerk Nauweiler Gewann
            {49.2442, 6.8775}, // Fenne / Kraftwerk
            {49.2465, 6.8865}, // Luisenthal West
            {49.2468, 6.8965}, // Luisenthal (Saar) Bf
            {49.2445, 6.9125}, // Rockershausen
            {49.2418, 6.9275}, // Burbach West
            {49.2400, 6.9425}, // Einfahrt Saarstahl Werk Burbach
            {49.2395, 6.9540}  // Werk Burbach Drahtstraße & Coil-Verladung
        }
    );

    // Gleis 7: Vorblock- & Stabstahl-Strecke Völklingen -> Burbach -> Saarbrücken -> Sulzbach -> Werk Neunkirchen
    addTrack(
        QStringLiteral("KORRIDOR-VK-NK"),
        QStringLiteral("Sulzbachtalbahn DB 3511 (Völklingen ↔ Burbach ↔ Saarbrücken ↔ Sulzbach ↔ Werk Neunkirchen)"),
        QStringLiteral("Corridor"),
        false, true, false, QColor("#10B981"),
        {
            {49.2445, 6.8725}, // Völklingen Ost / Nauweiler Gewann
            {49.2468, 6.8965}, // Luisenthal
            {49.2398, 6.9485}, // Burbach
            {49.2412, 6.9750}, // Saarbrücken-Malstatt
            {49.2428, 6.9920}, // Saarbrücken Hbf / Rbf
            {49.2585, 7.0150}, // Jägersfreude
            {49.2765, 7.0360}, // Dudweiler
            {49.3015, 7.0620}, // Sulzbach (Saar)
            {49.3235, 7.0975}, // Friedrichsthal (Saar)
            {49.3385, 7.1290}, // Landsweiler-Reden
            {49.3442, 7.1595}, // Neunkirchen (Saar) West
            {49.3446, 7.1725}  // Saarstahl Werk Neunkirchen Walzwerk
        }
    );

    // Gleis 8: Werks- & Abstellgleis Dillinger Hütte (Hafen -> ROGESA -> Grobblechwalzwerk)
    addTrack(
        QStringLiteral("DL-WERKSGLEIS"),
        QStringLiteral("Werks- & Abstellgleis Dillinger Hütte (Hafen – ROGESA – Grobblechwalzwerk)"),
        QStringLiteral("Dillingen"),
        false, false, true, QColor("#F43F5E"),
        {
            {49.3545, 6.7335}, // Katzenschwanzhafen Dillingen
            {49.3585, 6.7405}, // ROGESA Hochofen 4 & 5
            {49.3595, 6.7495}, // Brammenlager & Stahlwerk Dillingen
            {49.3585, 6.7595}  // Grobblechwalzwerk & Blechversand
        }
    );
}

void RailMapWidget::computeTrackMetrics(RailTrack &track)
{
    track.cumulativeMeters.clear();
    track.totalLengthMeters = 0.0;
    if (track.waypoints.isEmpty()) {
        return;
    }
    track.cumulativeMeters.push_back(0.0);
    for (int i = 1; i < track.waypoints.size(); ++i) {
        const double seg = haversineMeters(
            track.waypoints[i - 1].lat, track.waypoints[i - 1].lon,
            track.waypoints[i].lat, track.waypoints[i].lon);
        track.totalLengthMeters += seg;
        track.cumulativeMeters.push_back(track.totalLengthMeters);
    }
}

void RailMapWidget::initSourcesAndSinks()
{
    m_sourceSinkNodes.clear();

    auto addNode = [this](const QString &id, const QString &name, const QString &siteId,
                          const QString &role, const QString &trackId, double distM,
                          double lat, double lon, const QString &prod, const QString &cons,
                          double stock, double cap, double loadedT, double unloadedT,
                          const QColor &col) {
        SourceSinkNode n;
        n.id = id;
        n.name = name;
        n.siteId = siteId;
        n.role = role;
        n.trackId = trackId;
        n.trackDistMeters = distM;
        n.lat = lat;
        n.lon = lon;
        n.producedGoods = prod;
        n.consumedGoods = cons;
        n.currentStockTons = stock;
        n.maxCapacityTons = cap;
        n.totalLoadedTons = loadedT;
        n.totalUnloadedTons = unloadedT;
        n.badgeColor = col;
        m_sourceSinkNodes.push_back(n);
    };

    // 1. DILLINGER HÜTTE & ROGESA
    addNode(QStringLiteral("Q-DL-ROGESA"),
            QStringLiteral("ROGESA Hochöfen 4 & 5 (Dillingen)"),
            QStringLiteral("Dillingen"), QStringLiteral("Quelle (Aufnahme)"),
            QStringLiteral("KORRIDOR-DL-VK"), 250.0, 49.3602, 6.7418,
            QStringLiteral("Flüssigroheisen (1420°C)"), QStringLiteral("Eisenerz, Koks"),
            820.0, 1500.0, 1420.0, 310.0, QColor("#DC2626"));

    addNode(QStringLiteral("Q-DL-STRANG"),
            QStringLiteral("Strangguss & Brammenhalle Dillingen"),
            QStringLiteral("Dillingen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("DL-WERKSGLEIS"), 1200.0, 49.3595, 6.7485,
            QStringLiteral("Stahlbrammen (Slabs)"), QStringLiteral("Flüssigroheisen"),
            640.0, 1200.0, 960.0, 880.0, QColor("#EA580C"));

    addNode(QStringLiteral("QS-DL-BLECH"),
            QStringLiteral("Dillinger Grobblechwalzwerk"),
            QStringLiteral("Dillingen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("DL-WERKSGLEIS"), 1850.0, 49.3585, 6.7565,
            QStringLiteral("Dillinger Grobbleche (Schwerblech)"), QStringLiteral("Stahlbrammen"),
            510.0, 1000.0, 780.0, 740.0, QColor("#0284C7"));

    addNode(QStringLiteral("S-DL-HAFEN"),
            QStringLiteral("Hafen Dillingen / DB-Fernversand"),
            QStringLiteral("Dillingen"), QStringLiteral("Senke (Abladung)"),
            QStringLiteral("DL-WERKSGLEIS"), 150.0, 49.3548, 6.7345,
            QStringLiteral("Erz/Kohle-Umschlag"), QStringLiteral("Grobbleche, Stabstahl (Export)"),
            290.0, 1500.0, 210.0, 1150.0, QColor("#0D9488"));

    // 2. WERK VÖLKLINGEN (SAARSTAHL AG)
    addNode(QStringLiteral("QS-VK-LD"),
            QStringLiteral("LD-Stahlwerk & Strangguss Völklingen"),
            QStringLiteral("Voelklingen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("VK-LADESTELLE1"), 1350.0, 49.2435, 6.8565,
            QStringLiteral("Stahlknüppel, Vorblöcke, Schmiedeblöcke"), QStringLiteral("Flüssigroheisen (aus Dillingen)"),
            740.0, 1400.0, 1680.0, 1240.0, QColor("#EF7D00"));

    addNode(QStringLiteral("QS-VK-SCHMIEDE"),
            QStringLiteral("Saarschmiede GmbH (Freiformschmiede)"),
            QStringLiteral("Voelklingen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("VK-SCHMIEDE"), 1150.0, 49.2485, 6.8502,
            QStringLiteral("Freiform-Schmiedestücke / Turbinenwellen"), QStringLiteral("Schmiederohblöcke"),
            310.0, 800.0, 540.0, 490.0, QColor("#2563EB"));

    addNode(QStringLiteral("QS-VK-WALZ"),
            QStringLiteral("Walzwerk Nauweiler Gewann & Ladestelle 1"),
            QStringLiteral("Voelklingen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("VK-LADESTELLE1"), 2350.0, 49.2442, 6.8685,
            QStringLiteral("Stabstahl, Schienenprofile, Walzdraht-Coils"), QStringLiteral("Stahlknüppel, Vorblöcke"),
            590.0, 1200.0, 1120.0, 1340.0, QColor("#0891B2"));

    addNode(QStringLiteral("P-VK-ABSTELL"),
            QStringLiteral("Übergabebahnhof & Abstellgleise Völklingen"),
            QStringLiteral("Voelklingen"), QStringLiteral("Senke & Pufferlager"),
            QStringLiteral("VK-ABSTELL"), 850.0, 49.2518, 6.8432,
            QStringLiteral("Zwischenlager (Waggons ohne Lok)"), QStringLiteral("Alle Stahlprodukte (DB-Übergabe)"),
            480.0, 1600.0, 420.0, 1490.0, QColor("#9333EA"));

    // 3. WERK BURBACH (SAARSTAHL DRAHTWERK)
    addNode(QStringLiteral("QS-BB-DRAHT"),
            QStringLiteral("Drahtwerk Burbach (Knüppellager & Drahtstraße)"),
            QStringLiteral("Burbach"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("KORRIDOR-VK-BB"), 6800.0, 49.2400, 6.9485,
            QStringLiteral("Aufgerollter Walzdraht (Drahtcoils)"), QStringLiteral("Stahlknüppel 180x180 (aus Völklingen)"),
            460.0, 1100.0, 890.0, 1020.0, QColor("#0284C7"));

    // 4. WERK NEUNKIRCHEN (SAARSTAHL WALZWERK)
    addNode(QStringLiteral("QS-NK-WALZ"),
            QStringLiteral("Walzwerk Neunkirchen (Edelbaustahl & Draht)"),
            QStringLiteral("Neunkirchen"), QStringLiteral("Quelle & Senke"),
            QStringLiteral("KORRIDOR-VK-NK"), 27200.0, 49.3445, 7.1665,
            QStringLiteral("Stabstahl, Edelbaustahl & Drahtcoils"), QStringLiteral("Vorblöcke & Knüppel (aus Völklingen)"),
            520.0, 1100.0, 760.0, 910.0, QColor("#10B981"));
}

void RailMapWidget::initDefaultTrainsAndCargo()
{
    m_trains.clear();
    m_cargoOrders.clear();
    m_nextUicSerial = 101;
    m_nextOrderSerial = 1001;
    m_totalLocosInPool = 6; // 6 Lokomotiven im Pool, aber 8 Waggonverbände -> 2 Verbände sind ohne Lok abgestellt!

    auto makeWagon = [this](int pos, WagonCategory cat, CargoProductType prod,
                            const QString &orderId, double tons, bool currentlyLoaded = true) {
        WagonData w;
        w.positionIndex = pos;
        w.category = cat;
        w.assignedProductType = prod;
        w.assignedWeightTons = tons;
        w.productType = currentlyLoaded ? prod : CargoProductType::EmptyWagon;
        w.weightTons = currentlyLoaded ? tons : 0.0;
        w.cargoOrderId = orderId;
        w.axleCount = axleCountForCategory(cat);
        w.lengthMeters = lengthForCategory(cat);
        w.cargoDescription = QString("%1 [%2]").arg(productName(w.productType), shortCodeForWagon(cat));

        const int serial = m_nextUicSerial++;
        const int checkDigit = (serial * 7) % 10;
        if (cat == WagonCategory::Samms_6Axle) {
            w.uicNumber = QString("31 80 4891 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit);
        } else if (cat == WagonCategory::Rnsz_4Axle) {
            w.uicNumber = QString("31 80 3552 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit);
        } else if (cat == WagonCategory::Standard_4Axle) {
            w.uicNumber = QString("31 80 4674 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit);
        } else {
            w.uicNumber = QString("83 80 9981 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit);
        }
        return w;
    };

    auto registerOrder = [this](const QString &orderId, CargoProductType prod, double tons,
                                const QString &src, const QString &dst, const QString &consistId,
                                int wCount, WagonCategory wCat, CargoOrderStatus st) {
        CargoOrder o;
        o.orderId = orderId;
        o.product = prod;
        o.weightTons = tons;
        o.sourceNodeId = src;
        o.sinkNodeId = dst;
        o.assignedConsistId = consistId;
        o.wagonCountUsed = wCount;
        o.preferredWagon = wCat;
        o.status = st;
        m_cargoOrders.push_back(o);
    };

    // 1. Zug: SHS-VK-101 (Mit Lok) auf der Werksringbahn Völklingen (Quelle: QS-VK-LD -> Senke: QS-VK-WALZ)
    // Fährt voll beladen von der Quelle QS-VK-LD zur Senke QS-VK-WALZ zum Abladen!
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("SHS-VK-101");
        t.name = QStringLiteral("Werksringzug Völklingen (Quelle: LD-Stahlwerk -> Senke: Walzwerk)");
        t.locoName = QStringLiteral("SHS Lok 12 (MaK G1206)");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("QS-VK-LD");
        t.sinkNodeId = QStringLiteral("QS-VK-WALZ");
        t.sourceTrackDistMeters = 1050.0;
        t.sinkTrackDistMeters = 2250.0;
        t.opState = TrainOperationState::MovingLoadedToSink;
        t.opProgress = 0.0;
        t.completedDeliveries = 3;
        t.trackId = QStringLiteral("VK-RING");
        t.headDistanceMeters = 1650.0;
        t.speedKmh = 30.0;
        t.direction = 1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Heavy_6Axle,    CargoProductType::LiquidPigIron,    ordId, 98.0, true),
            makeWagon(2, WagonCategory::Heavy_6Axle,    CargoProductType::LiquidPigIron,    ordId, 98.0, true),
            makeWagon(3, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets,     ordId, 89.5, true),
            makeWagon(4, WagonCategory::Samms_6Axle,    CargoProductType::SteelBlooms,      ordId, 91.0, true),
            makeWagon(5, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 62.0, true),
            makeWagon(6, WagonCategory::Samms_6Axle,    CargoProductType::SteelSlabs,       ordId, 92.0, true),
            makeWagon(7, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils,     ordId, 55.0, true),
            makeWagon(8, WagonCategory::Rnsz_4Axle,     CargoProductType::HeavyPlates,      ordId, 63.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::LiquidPigIron, 648.5, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Heavy_6Axle, CargoOrderStatus::InTransit);
    }

    // 2. Zug: SHS-VK-102 (Mit Lok) auf dem Werksgleis Völklingen (nimmt gerade Güter an Quelle QS-VK-LD auf!)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("SHS-VK-102");
        t.name = QStringLiteral("Walzstahl- & Drahtcoil-Zug (Quelle: LD-Stahlwerk -> Senke: Ladestelle 1)");
        t.locoName = QStringLiteral("SHS Rangierlok 05 (Henschel)");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("QS-VK-LD");
        t.sinkNodeId = QStringLiteral("QS-VK-WALZ");
        t.sourceTrackDistMeters = 1350.0;
        t.sinkTrackDistMeters = 2550.0;
        t.opState = TrainOperationState::LoadingAtSource;
        t.opProgress = 0.22; // Nimmt gerade an der Quelle QS-VK-LD Güter auf!
        t.completedDeliveries = 2;
        t.trackId = QStringLiteral("VK-LADESTELLE1");
        t.headDistanceMeters = 1350.0;
        t.speedKmh = 26.0;
        t.direction = 1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 60.0, true),
            makeWagon(2, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 61.5, false),
            makeWagon(3, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils,     ordId, 56.0, false),
            makeWagon(4, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils,     ordId, 55.0, false),
            makeWagon(5, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets,     ordId, 88.0, false)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::BarSteelProfiles, 320.5, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Rnsz_4Axle, CargoOrderStatus::LoadingAtSource);
    }

    // 3. Zug: SHS-VK-103 (Mit Lok) Saarschmiede Schwerlastgleis (lädt gerade an Senke P-VK-ABSTELL ab!)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("SHS-VK-103");
        t.name = QStringLiteral("Schwerlastzug Saarschmiede (Quelle: Schmiede -> Senke: Übergabebhf)");
        t.locoName = QStringLiteral("SHS Lok 08 (Voith Gravita)");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("QS-VK-SCHMIEDE");
        t.sinkNodeId = QStringLiteral("P-VK-ABSTELL");
        t.sourceTrackDistMeters = 1550.0;
        t.sinkTrackDistMeters = 650.0;
        t.opState = TrainOperationState::UnloadingAtSink;
        t.opProgress = 0.25; // Lädt gerade an der Senke ab!
        t.completedDeliveries = 1;
        t.trackId = QStringLiteral("VK-SCHMIEDE");
        t.headDistanceMeters = 650.0;
        t.speedKmh = 22.0;
        t.direction = -1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Samms_6Axle, CargoProductType::ForgedParts,  ordId, 94.0, false),
            makeWagon(2, WagonCategory::Heavy_6Axle, CargoProductType::ForgedParts,  ordId, 96.0, true),
            makeWagon(3, WagonCategory::Samms_6Axle, CargoProductType::SteelBlooms,  ordId, 90.0, true),
            makeWagon(4, WagonCategory::Rnsz_4Axle,  CargoProductType::HeavyPlates,  ordId, 62.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::ForgedParts, 342.0, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Samms_6Axle, CargoOrderStatus::UnloadingAtSink);
    }

    // 4. Zug: TORPEDO-DL-201 (Mit Lok) Torpedowagen-Ganzzug mit flüssigem Eisen 1420°C (Quelle: Q-DL-ROGESA -> Senke: QS-VK-LD)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("TORPEDO-DL-201");
        t.name = QStringLiteral("Torpedowagen-Zug Flüssigeisen 1420°C (Quelle: ROGESA Dillingen -> Senke: LD-Stahlwerk VK)");
        t.locoName = QStringLiteral("SaarRail Lok 74 (EuroDual)");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("Q-DL-ROGESA");
        t.sinkNodeId = QStringLiteral("QS-VK-LD");
        t.sourceTrackDistMeters = 350.0;
        t.sinkTrackDistMeters = 15250.0;
        t.opState = TrainOperationState::MovingLoadedToSink;
        t.opProgress = 0.0;
        t.completedDeliveries = 4;
        t.trackId = QStringLiteral("KORRIDOR-DL-VK");
        t.headDistanceMeters = 14850.0; // Fährt gerade mit 6 Torpedowagen in das LD-Stahlwerk Völklingen ein!
        t.speedKmh = 45.0;
        t.direction = 1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true),
            makeWagon(2, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true),
            makeWagon(3, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true),
            makeWagon(4, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true),
            makeWagon(5, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true),
            makeWagon(6, WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron, ordId, 98.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::LiquidPigIron, 588.0, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Heavy_6Axle, CargoOrderStatus::InTransit);
    }

    // 5. Zug: SHS-BB-301 (Mit Lok) Knüppel & Aufgerollter Walzdraht (Quelle: QS-VK-LD -> Senke: QS-BB-DRAHT)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("SHS-BB-301");
        t.name = QStringLiteral("Knüppel- & Drahtcoil-Pendelzug (Quelle: Völklingen -> Senke: Burbach)");
        t.locoName = QStringLiteral("DB Cargo / SHS Vectron 193");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("QS-VK-LD");
        t.sinkNodeId = QStringLiteral("QS-BB-DRAHT");
        t.sourceTrackDistMeters = 300.0;
        t.sinkTrackDistMeters = 7100.0;
        t.opState = TrainOperationState::MovingLoadedToSink;
        t.opProgress = 0.0;
        t.completedDeliveries = 3;
        t.trackId = QStringLiteral("KORRIDOR-VK-BB");
        t.headDistanceMeters = 3200.0;
        t.speedKmh = 50.0;
        t.direction = 1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets, ordId, 89.0, true),
            makeWagon(2, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets, ordId, 89.0, true),
            makeWagon(3, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets, ordId, 88.0, true),
            makeWagon(4, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils, ordId, 56.0, true),
            makeWagon(5, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils, ordId, 56.0, true),
            makeWagon(6, WagonCategory::Rnsz_4Axle,     CargoProductType::WireRodCoils, ordId, 59.5, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::SteelBillets, 437.5, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Samms_6Axle, CargoOrderStatus::InTransit);
    }

    // 6. Zug: SHS-NK-401 (Mit Lok) Vorblöcke & Stabstahl (Quelle: QS-VK-LD -> Senke: QS-NK-WALZ)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("SHS-NK-401");
        t.name = QStringLiteral("Vorblock- & Edelbaustahl-Zug (Quelle: Völklingen -> Senke: Neunkirchen)");
        t.locoName = QStringLiteral("SHS / SaarRail BR 185");
        t.hasLocomotive = true;
        t.sourceNodeId = QStringLiteral("QS-VK-LD");
        t.sinkNodeId = QStringLiteral("QS-NK-WALZ");
        t.sourceTrackDistMeters = 500.0;
        t.sinkTrackDistMeters = 28500.0;
        t.opState = TrainOperationState::MovingLoadedToSink;
        t.opProgress = 0.0;
        t.completedDeliveries = 2;
        t.trackId = QStringLiteral("KORRIDOR-VK-NK");
        t.headDistanceMeters = 14800.0;
        t.speedKmh = 60.0;
        t.direction = 1;
        t.active = true;
        t.wagons = {
            makeWagon(1, WagonCategory::Samms_6Axle,    CargoProductType::SteelBlooms,      ordId, 89.0, true),
            makeWagon(2, WagonCategory::Samms_6Axle,    CargoProductType::SteelBlooms,      ordId, 88.5, true),
            makeWagon(3, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 61.0, true),
            makeWagon(4, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 60.0, true),
            makeWagon(5, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils,     ordId, 55.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::SteelBlooms, 353.5, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Samms_6Axle, CargoOrderStatus::InTransit);
    }

    // =========================================================================
    // ABGESTELLTE / GELAGERTE WAGGONGRUPPEN OHNE LOKOMOTIVE (LOK-ENGPASS!)
    // =========================================================================
    // 7. Gelagerte Waggongruppe auf dem Abstellgleis Völklingen (KEINE LOK VERFÜGBAR!)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("LAGER-VK-P01");
        t.name = QStringLiteral("Abgestellte Waggongruppe Völklingen (Drahtcoils & Knüppel – WARTET AUF LOK)");
        t.locoName = QStringLiteral("KEINE LOK (Auf Abstellgleis gelagert)");
        t.hasLocomotive = false;
        t.sourceNodeId = QStringLiteral("QS-VK-WALZ");
        t.sinkNodeId = QStringLiteral("QS-BB-DRAHT");
        t.sourceTrackDistMeters = 250.0;
        t.sinkTrackDistMeters = 1350.0;
        t.opState = TrainOperationState::ParkedWithoutLoco;
        t.opProgress = 0.0;
        t.trackId = QStringLiteral("VK-ABSTELL");
        t.headDistanceMeters = 920.0;
        t.speedKmh = 0.0;
        t.direction = 1;
        t.active = false;
        t.wagons = {
            makeWagon(1, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils, ordId, 56.0, true),
            makeWagon(2, WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils, ordId, 56.0, true),
            makeWagon(3, WagonCategory::Samms_6Axle,    CargoProductType::SteelBillets, ordId, 90.0, true),
            makeWagon(4, WagonCategory::Rnsz_4Axle,     CargoProductType::BarSteelProfiles, ordId, 61.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::WireRodCoils, 263.0, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Standard_4Axle, CargoOrderStatus::StoredOnParkedWagons);
    }

    // 8. Gelagerte Waggongruppe Dillinger Hütte (Grobbleche & Brammen warten auf Lok!)
    {
        const QString ordId = QString("GUT-%1").arg(m_nextOrderSerial++);
        TrainConsist t;
        t.id = QStringLiteral("LAGER-DL-P02");
        t.name = QStringLiteral("Abgestellte Waggongruppe Dillingen (Grobbleche & Brammen – WARTET AUF LOK)");
        t.locoName = QStringLiteral("KEINE LOK (Im Werk Dillingen zwischengelagert)");
        t.hasLocomotive = false;
        t.sourceNodeId = QStringLiteral("QS-DL-BLECH");
        t.sinkNodeId = QStringLiteral("S-DL-HAFEN");
        t.sourceTrackDistMeters = 1850.0;
        t.sinkTrackDistMeters = 200.0;
        t.opState = TrainOperationState::ParkedWithoutLoco;
        t.opProgress = 0.0;
        t.trackId = QStringLiteral("DL-WERKSGLEIS");
        t.headDistanceMeters = 1350.0;
        t.speedKmh = 0.0;
        t.direction = -1;
        t.active = false;
        t.wagons = {
            makeWagon(1, WagonCategory::Rnsz_4Axle,  CargoProductType::HeavyPlates, ordId, 64.0, true),
            makeWagon(2, WagonCategory::Rnsz_4Axle,  CargoProductType::HeavyPlates, ordId, 64.0, true),
            makeWagon(3, WagonCategory::Samms_6Axle, CargoProductType::SteelSlabs,  ordId, 91.0, true),
            makeWagon(4, WagonCategory::Samms_6Axle, CargoProductType::SteelSlabs,  ordId, 91.0, true)
        };
        m_trains.push_back(t);
        registerOrder(ordId, CargoProductType::HeavyPlates, 310.0, t.sourceNodeId, t.sinkNodeId,
                      t.id, t.wagons.size(), WagonCategory::Rnsz_4Axle, CargoOrderStatus::StoredOnParkedWagons);
    }
}

int RailMapWidget::usedLocomotivesCount() const
{
    int count = 0;
    for (const TrainConsist &tr : m_trains) {
        if (tr.hasLocomotive) {
            ++count;
        }
    }
    return count;
}

int RailMapWidget::freeLocomotivesCount() const
{
    return std::max(0, m_totalLocosInPool - usedLocomotivesCount());
}

void RailMapWidget::setTotalLocomotivesInPool(int totalLocos)
{
    m_totalLocosInPool = std::clamp(totalLocos, 1, 25);
    // Falls weniger Loks im Pool sind als aktuell gebunden, kupple von hinteren Zügen Loks ab,
    // sodass deren Waggons mit Gütern auf den Gleisen zwischengelagert werden!
    for (int i = m_trains.size() - 1; i >= 0 && usedLocomotivesCount() > m_totalLocosInPool; --i) {
        if (m_trains[i].hasLocomotive) {
            uncoupleLocoFromConsist(i);
        }
    }
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    update();
}

bool RailMapWidget::coupleFreeLocoToConsist(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return false;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (tr.hasLocomotive) {
        emit statusMessage(QStringLiteral("Verband %1 besitzt bereits eine Lokomotive (%2).").arg(tr.id, tr.locoName));
        return true;
    }
    if (freeLocomotivesCount() <= 0) {
        emit statusMessage(
            QStringLiteral("⚠️ Lokomotiv-Engpass! Alle %1 Loks sind im Einsatz. Bitte erst Lok-Pool erhöhen oder bei einem anderen Zug die Lok abkuppeln.")
                .arg(m_totalLocosInPool));
        return false;
    }

    tr.hasLocomotive = true;
    tr.active = true;
    tr.opState = TrainOperationState::MovingLoadedToSink;
    tr.opProgress = 0.0;
    if (tr.speedKmh < 15.0) {
        tr.speedKmh = 28.0;
    }
    tr.locoName = QString("SHS Dispo-Lok %1 (G1206)").arg(usedLocomotivesCount() + 10);
    if (tr.name.contains(QStringLiteral("WARTET AUF LOK"))) {
        tr.name.replace(QStringLiteral(" – WARTET AUF LOK"), QStringLiteral(" – IN FAHRT"));
    }

    for (CargoOrder &ord : m_cargoOrders) {
        if (ord.assignedConsistId == tr.id) {
            ord.status = CargoOrderStatus::InTransit;
        }
    }

    updateAllTrainGeometries();
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    emit statusMessage(QStringLiteral("🚂 Freie Lokomotive an Waggongruppe %1 angekuppelt – Abfahrt Richtung Senke %2 (zum Abladen) gestartet!")
                           .arg(tr.id, tr.sinkNodeId));
    update();
    return true;
}

void RailMapWidget::uncoupleLocoFromConsist(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (!tr.hasLocomotive) {
        return;
    }
    tr.hasLocomotive = false;
    tr.active = false;
    tr.opState = TrainOperationState::ParkedWithoutLoco;
    tr.opProgress = 0.0;
    tr.locoName = QStringLiteral("KEINE LOK (Waggons mit Gütern auf Gleis gelagert)");

    for (CargoOrder &ord : m_cargoOrders) {
        if (ord.assignedConsistId == tr.id) {
            ord.status = CargoOrderStatus::StoredOnParkedWagons;
        }
    }

    updateAllTrainGeometries();
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    emit statusMessage(QStringLiteral("🅿️ Lokomotive von %1 abgekuppelt: %2 beladene Waggons werden auf Gleis [%3] zwischengelagert.")
                           .arg(tr.id).arg(tr.wagons.size()).arg(tr.trackId));
    update();
}

QString RailMapWidget::createAndDistributeCargoOrder(CargoProductType product,
                                                     double totalTons,
                                                     const QString &sourceId,
                                                     const QString &sinkId,
                                                     WagonCategory preferredWagon)
{
    totalTons = std::clamp(totalTons, 30.0, 2000.0);
    const double maxPerWagon = maxPayloadTonsForCategory(preferredWagon);
    const int numWagons = std::clamp(static_cast<int>(std::ceil(totalTons / maxPerWagon)), 1, 25);
    const double tonsPerWagon = totalTons / numWagons;

    // Finde passendes Gleis anhand von Quelle/Senke
    QString chosenTrackId = QStringLiteral("VK-LADESTELLE1");
    double startDist = 900.0;
    for (const SourceSinkNode &n : m_sourceSinkNodes) {
        if (n.id == sourceId) {
            chosenTrackId = n.trackId;
            startDist = n.trackDistMeters;
            break;
        }
    }

    // Wenn Quelle in Völklingen und Senke in Burbach/Neunkirchen/Dillingen liegt, wähle Korridor
    if (sinkId.contains(QStringLiteral("BB"))) {
        chosenTrackId = QStringLiteral("KORRIDOR-VK-BB");
        startDist = 350.0;
    } else if (sinkId.contains(QStringLiteral("NK"))) {
        chosenTrackId = QStringLiteral("KORRIDOR-VK-NK");
        startDist = 550.0;
    } else if (sourceId.contains(QStringLiteral("DL")) && sinkId.contains(QStringLiteral("VK"))) {
        chosenTrackId = QStringLiteral("KORRIDOR-DL-VK");
        startDist = 400.0;
    }

    const bool hasFreeLoco = (freeLocomotivesCount() > 0);
    const QString orderId = QString("GUT-%1").arg(m_nextOrderSerial++);
    const QString consistId = hasFreeLoco
                              ? QString("SHS-DISP-%1").arg(m_trains.size() + 101)
                              : QString("LAGER-P%1").arg(m_trains.size() + 1);

    // Falls keine Lok frei ist, auf Puffer-/Abstellgleis der Quelle lagern
    if (!hasFreeLoco && sourceId.contains(QStringLiteral("VK"))) {
        chosenTrackId = QStringLiteral("VK-ABSTELL");
    }

    double trackLen = 2800.0;
    for (const RailTrack &rt : m_tracks) {
        if (rt.id == chosenTrackId) {
            trackLen = rt.totalLengthMeters;
            break;
        }
    }

    TrainConsist t;
    t.id = consistId;
    t.sourceNodeId = sourceId;
    t.sinkNodeId = sinkId;
    t.trackId = chosenTrackId;
    t.sourceTrackDistMeters = std::clamp(startDist, 180.0, std::max(250.0, trackLen * 0.35));
    t.sinkTrackDistMeters = std::max(t.sourceTrackDistMeters + 450.0, trackLen - 180.0);
    t.headDistanceMeters = t.sourceTrackDistMeters;
    t.direction = 1;
    t.hasLocomotive = hasFreeLoco;
    t.active = hasFreeLoco;
    t.opState = hasFreeLoco ? TrainOperationState::LoadingAtSource : TrainOperationState::ParkedWithoutLoco;
    t.opProgress = 0.0;
    t.speedKmh = hasFreeLoco ? 36.0 : 0.0;
    t.locoName = hasFreeLoco
                 ? QString("SHS Dispo-Lok %1 (Vectron Dual)").arg(usedLocomotivesCount() + 11)
                 : QStringLiteral("KEINE LOK (Auf Gleis zwischengelagert)");
    t.name = hasFreeLoco
             ? QString("%1 (Quelle %2 -> Senke %3)").arg(productShortName(product), sourceId, sinkId)
             : QString("Gelagerte %1 (%2 -> %3 – WARTET AUF LOK)").arg(productShortName(product), sourceId, sinkId);

    for (int i = 0; i < numWagons; ++i) {
        WagonData w;
        w.positionIndex = i + 1;
        w.category = preferredWagon;
        w.assignedProductType = product;
        w.assignedWeightTons = tonsPerWagon;
        // Wenn mit Lok an der Quelle gestartet wird, nimmt der Zug die Güter sichtbar an der Quelle auf!
        w.productType = hasFreeLoco ? CargoProductType::EmptyWagon : product;
        w.weightTons = hasFreeLoco ? 0.0 : tonsPerWagon;
        w.cargoOrderId = orderId;
        w.axleCount = axleCountForCategory(preferredWagon);
        w.lengthMeters = lengthForCategory(preferredWagon);
        w.cargoDescription = QString("%1 [%2]").arg(productName(w.productType), shortCodeForWagon(preferredWagon));

        const int serial = m_nextUicSerial++;
        const int checkDigit = (serial * 7) % 10;
        w.uicNumber = (preferredWagon == WagonCategory::Samms_6Axle)
                      ? QString("31 80 4891 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit)
                      : QString("31 80 3552 %1-%2").arg(serial, 3, 10, QChar('0')).arg(checkDigit);
        t.wagons.push_back(w);
    }

    m_trains.push_back(t);

    CargoOrder ord;
    ord.orderId = orderId;
    ord.product = product;
    ord.weightTons = totalTons;
    ord.sourceNodeId = sourceId;
    ord.sinkNodeId = sinkId;
    ord.assignedConsistId = consistId;
    ord.wagonCountUsed = numWagons;
    ord.preferredWagon = preferredWagon;
    ord.status = hasFreeLoco ? CargoOrderStatus::LoadingAtSource : CargoOrderStatus::StoredOnParkedWagons;
    m_cargoOrders.push_back(ord);

    m_selectedTrainIdx = m_trains.size() - 1;
    m_selectedWagonIdx = 0;
    updateAllTrainGeometries();
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    emit selectionChanged(m_selectedTrainIdx, m_selectedWagonIdx);

    if (hasFreeLoco) {
        emit statusMessage(
            QStringLiteral("⬆️ Güterauftrag %1 (%2 t %3): Zug %4 steht an Quelle [%5] und nimmt die Güter auf %6x %7 auf!")
                .arg(orderId).arg(totalTons, 0, 'f', 0).arg(productShortName(product))
                .arg(t.id, sourceId).arg(numWagons).arg(shortCodeForWagon(preferredWagon)));
    } else {
        emit statusMessage(
            QStringLiteral("⚠️ Keine freie Lokomotive verfügbar! Güterauftrag %1 (%2 t %3 auf %4x %5) wurde auf Gleis [%6] als %7 zwischengelagert.")
                .arg(orderId).arg(totalTons, 0, 'f', 0).arg(productShortName(product))
                .arg(numWagons).arg(shortCodeForWagon(preferredWagon), chosenTrackId, t.id));
    }

    update();
    return orderId;
}

void RailMapWidget::loadTrainAtSourceNow(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    tr.headDistanceMeters = tr.sourceTrackDistMeters;
    tr.direction = 1;

    double loadedTotal = 0.0;
    for (WagonData &w : tr.wagons) {
        if (w.assignedProductType == CargoProductType::EmptyWagon) {
            w.assignedProductType = CargoProductType::SteelBillets;
        }
        if (w.assignedWeightTons <= 1.0) {
            w.assignedWeightTons = maxPayloadTonsForCategory(w.category) * 0.95;
        }
        w.productType = w.assignedProductType;
        w.weightTons = w.assignedWeightTons;
        w.cargoDescription = QString("%1 [%2]").arg(productName(w.productType), shortCodeForWagon(w.category));
        loadedTotal += w.weightTons;
    }

    for (SourceSinkNode &n : m_sourceSinkNodes) {
        if (n.id == tr.sourceNodeId) {
            n.currentStockTons = std::max(40.0, n.currentStockTons - loadedTotal * 0.35);
            n.totalLoadedTons += loadedTotal;
            break;
        }
    }

    for (CargoOrder &ord : m_cargoOrders) {
        if (ord.assignedConsistId == tr.id) {
            ord.status = tr.hasLocomotive ? CargoOrderStatus::InTransit : CargoOrderStatus::StoredOnParkedWagons;
        }
    }

    tr.opState = tr.hasLocomotive ? TrainOperationState::MovingLoadedToSink : TrainOperationState::ParkedWithoutLoco;
    tr.opProgress = 1.0;

    updateAllTrainGeometries();
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    emit statusMessage(
        QStringLiteral("⬆️ [QUELLE %1] Zug %2 hat %3 t Güter auf %4 Waggons aufgenommen und startet Richtung Senke [%5]!")
            .arg(tr.sourceNodeId, tr.id).arg(loadedTotal, 0, 'f', 0).arg(tr.wagons.size()).arg(tr.sinkNodeId));
    update();
}

void RailMapWidget::unloadTrainAtSinkNow(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    tr.headDistanceMeters = tr.sinkTrackDistMeters;
    tr.direction = -1;

    double unloadedTotal = 0.0;
    for (WagonData &w : tr.wagons) {
        if (w.productType != CargoProductType::EmptyWagon) {
            w.assignedProductType = w.productType;
            w.assignedWeightTons = std::max(w.weightTons, maxPayloadTonsForCategory(w.category) * 0.90);
            unloadedTotal += w.weightTons;
        } else {
            unloadedTotal += w.assignedWeightTons;
        }
        w.productType = CargoProductType::EmptyWagon;
        w.weightTons = 0.0;
        w.cargoDescription = QStringLiteral("Leerwagen (an Senke abgeladen)");
    }

    for (SourceSinkNode &n : m_sourceSinkNodes) {
        if (n.id == tr.sinkNodeId) {
            n.currentStockTons = std::min(n.maxCapacityTons, n.currentStockTons + unloadedTotal * 0.5);
            n.totalUnloadedTons += unloadedTotal;
            break;
        }
    }

    for (CargoOrder &ord : m_cargoOrders) {
        if (ord.assignedConsistId == tr.id) {
            ord.status = CargoOrderStatus::DeliveredToSink;
        }
    }

    tr.completedDeliveries++;
    tr.opState = tr.hasLocomotive ? TrainOperationState::MovingEmptyToSource : TrainOperationState::ParkedWithoutLoco;
    tr.opProgress = 1.0;

    updateAllTrainGeometries();
    emit trainsUpdated();
    emit cargoAndNodesUpdated();
    emit statusMessage(
        QStringLiteral("⬇️ [SENKE %1] Zug %2 hat %3 t Güter von %4 Waggons abgeladen -> Rückfahrt als Leerzug zur Quelle [%5]!")
            .arg(tr.sinkNodeId, tr.id).arg(unloadedTotal, 0, 'f', 0).arg(tr.wagons.size()).arg(tr.sourceNodeId));
    update();
}

QString RailMapWidget::operationStateText(const TrainConsist &train)
{
    if (!train.hasLocomotive) {
        return QStringLiteral("🅿️ Ohne Lok zwischengelagert");
    }
    switch (train.opState) {
    case TrainOperationState::LoadingAtSource:
        return QStringLiteral("⬆️ Nimmt Güter an Quelle [%1] auf (%2%)")
            .arg(train.sourceNodeId)
            .arg(static_cast<int>(std::round(train.opProgress * 100.0)));
    case TrainOperationState::MovingLoadedToSink:
        return QStringLiteral("🚛 Beladen: Quelle [%1] -> Senke [%2]")
            .arg(train.sourceNodeId, train.sinkNodeId);
    case TrainOperationState::UnloadingAtSink:
        return QStringLiteral("⬇️ Lädt Güter an Senke [%1] ab (%2%)")
            .arg(train.sinkNodeId)
            .arg(static_cast<int>(std::round(train.opProgress * 100.0)));
    case TrainOperationState::MovingEmptyToSource:
        return QStringLiteral("🚃 Leerzug: Senke [%1] -> Quelle [%2]")
            .arg(train.sinkNodeId, train.sourceNodeId);
    case TrainOperationState::ParkedWithoutLoco:
        return QStringLiteral("🅿️ Ohne Lok zwischengelagert");
    }
    return QStringLiteral("Aktiv");
}

void RailMapWidget::sampleTrackAtDistance(const RailTrack &track, double distMeters,
                                          double &outLat, double &outLon, double &outHeadingDeg) const
{
    if (track.waypoints.isEmpty()) {
        outLat = 49.2460;
        outLon = 6.8540;
        outHeadingDeg = 90.0;
        return;
    }
    if (track.waypoints.size() == 1 || track.totalLengthMeters <= 0.01) {
        outLat = track.waypoints[0].lat;
        outLon = track.waypoints[0].lon;
        outHeadingDeg = 90.0;
        return;
    }

    double d = distMeters;
    if (track.isLoop) {
        d = std::fmod(d, track.totalLengthMeters);
        if (d < 0.0) {
            d += track.totalLengthMeters;
        }
    } else {
        d = std::clamp(d, 0.0, track.totalLengthMeters);
    }

    int segIdx = 0;
    for (int i = 0; i < track.cumulativeMeters.size() - 1; ++i) {
        if (d >= track.cumulativeMeters[i] && d <= track.cumulativeMeters[i + 1]) {
            segIdx = i;
            break;
        }
    }

    const double segStart = track.cumulativeMeters[segIdx];
    const double segLen = std::max(0.001, track.cumulativeMeters[segIdx + 1] - segStart);
    const double t = std::clamp((d - segStart) / segLen, 0.0, 1.0);

    const GeoPoint &p1 = track.waypoints[segIdx];
    const GeoPoint &p2 = track.waypoints[segIdx + 1];
    outLat = p1.lat + t * (p2.lat - p1.lat);
    outLon = p1.lon + t * (p2.lon - p1.lon);
    outHeadingDeg = bearingDegrees(p1.lat, p1.lon, p2.lat, p2.lon);
}

double RailMapWidget::findNearestTrackDistance(const RailTrack &track, double lat, double lon) const
{
    if (track.waypoints.size() < 2) {
        return 0.0;
    }
    double bestDistSq = 1e18;
    double bestAlongMeters = 0.0;

    for (int i = 0; i < track.waypoints.size() - 1; ++i) {
        const GeoPoint &a = track.waypoints[i];
        const GeoPoint &b = track.waypoints[i + 1];
        const double dx = b.lon - a.lon;
        const double dy = b.lat - a.lat;
        const double lenSq = dx * dx + dy * dy;
        double t = 0.0;
        if (lenSq > 1e-12) {
            t = std::clamp(((lon - a.lon) * dx + (lat - a.lat) * dy) / lenSq, 0.0, 1.0);
        }
        const double projLat = a.lat + t * dy;
        const double projLon = a.lon + t * dx;
        const double dSq = (lat - projLat) * (lat - projLat) + (lon - projLon) * (lon - projLon);
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            const double segLen = track.cumulativeMeters[i + 1] - track.cumulativeMeters[i];
            bestAlongMeters = track.cumulativeMeters[i] + t * segLen;
        }
    }
    return bestAlongMeters;
}

void RailMapWidget::updateAllTrainGeometries()
{
    const double mpp = metersPerPixel();
    const double minVisualSpacingMeters = m_exaggerateWagonSize
                                          ? std::clamp(mpp * 27.0, 21.0, 310.0)
                                          : 21.0;

    for (TrainConsist &train : m_trains) {
        const RailTrack *trackPtr = nullptr;
        for (const RailTrack &tr : m_tracks) {
            if (tr.id == train.trackId) {
                trackPtr = &tr;
                break;
            }
        }
        if (!trackPtr && !m_tracks.isEmpty()) {
            trackPtr = &m_tracks.first();
        }
        if (!trackPtr) {
            continue;
        }

        sampleTrackAtDistance(*trackPtr, train.headDistanceMeters,
                              train.locoLat, train.locoLon, train.locoHeadingDeg);
        if (train.direction < 0) {
            train.locoHeadingDeg = std::fmod(train.locoHeadingDeg + 180.0, 360.0);
        }

        double offsetMeters = 0.0;
        for (int i = 0; i < train.wagons.size(); ++i) {
            WagonData &w = train.wagons[i];
            w.positionIndex = i + 1;
            const double stepMeters = std::max(w.lengthMeters + 4.2, minVisualSpacingMeters);
            offsetMeters += stepMeters;

            const double wagonDist = train.headDistanceMeters - (train.direction * offsetMeters);
            sampleTrackAtDistance(*trackPtr, wagonDist, w.currentLat, w.currentLon, w.headingDeg);
            if (train.direction < 0) {
                w.headingDeg = std::fmod(w.headingDeg + 180.0, 360.0);
            }
        }
    }
}

void RailMapWidget::onSimTimerTick()
{
    if (m_simRunning) {
        stepSimulation(0.033);
    }
}

void RailMapWidget::stepSimulation(double dtSeconds)
{
    bool stateOrStockChanged = false;

    // Kontinuierliche Stahl-Nachproduktion an den Quellen (Hochöfen, Strangguss, Walzwerke)
    for (SourceSinkNode &n : m_sourceSinkNodes) {
        if (n.role.contains(QStringLiteral("Quelle"))) {
            n.currentStockTons = std::min(n.maxCapacityTons, n.currentStockTons + 1.8 * dtSeconds * m_simSpeedFactor);
        }
    }

    for (TrainConsist &train : m_trains) {
        // Waggongruppen ohne Lokomotive bleiben auf ihrem Abstellgleis stehen!
        if (!train.active || !train.hasLocomotive) {
            train.opState = TrainOperationState::ParkedWithoutLoco;
            continue;
        }
        const RailTrack *trackPtr = nullptr;
        for (const RailTrack &tr : m_tracks) {
            if (tr.id == train.trackId) {
                trackPtr = &tr;
                break;
            }
        }
        if (!trackPtr || trackPtr->totalLengthMeters <= 1.0) {
            continue;
        }

        // 1. ZUSTAND: GÜTER-AUFNAHME AN DER QUELLE (BELADUNG DER WAGGONS #1..#N)
        if (train.opState == TrainOperationState::LoadingAtSource) {
            train.opProgress += dtSeconds * 0.12 * std::clamp(m_simSpeedFactor, 0.5, 3.0);
            const int nWags = std::max(1, static_cast<int>(train.wagons.size()));
            const int loadedCount = std::clamp(static_cast<int>( std::floor(train.opProgress * nWags) ), 0, nWags);

            for (int i = 0; i < loadedCount; ++i) {
                WagonData &w = train.wagons[i];
                if (w.productType == CargoProductType::EmptyWagon) {
                    if (w.assignedProductType == CargoProductType::EmptyWagon) {
                        w.assignedProductType = CargoProductType::SteelBillets;
                    }
                    if (w.assignedWeightTons <= 1.0) {
                        w.assignedWeightTons = maxPayloadTonsForCategory(w.category) * 0.95;
                    }
                    w.productType = w.assignedProductType;
                    w.weightTons = w.assignedWeightTons;
                    w.cargoDescription = QString("%1 [%2]").arg(productName(w.productType), shortCodeForWagon(w.category));

                    for (SourceSinkNode &node : m_sourceSinkNodes) {
                        if (node.id == train.sourceNodeId) {
                            node.currentStockTons = std::max(40.0, node.currentStockTons - w.weightTons * 0.35);
                            node.totalLoadedTons += w.weightTons;
                            break;
                        }
                    }
                    stateOrStockChanged = true;
                }
            }

            if (train.opProgress >= 1.0) {
                train.opProgress = 0.0;
                train.opState = TrainOperationState::MovingLoadedToSink;
                if (!trackPtr->isLoop) {
                    train.direction = (train.sinkTrackDistMeters >= train.sourceTrackDistMeters) ? 1 : -1;
                } else {
                    train.direction = 1;
                }
                for (CargoOrder &ord : m_cargoOrders) {
                    if (ord.assignedConsistId == train.id) {
                        ord.status = CargoOrderStatus::InTransit;
                    }
                }
                emit statusMessage(
                    QStringLiteral("⬆️ [QUELLE %1] Zug %2 hat alle %3 Waggons beladen und fährt nun zur Senke [%4].")
                        .arg(train.sourceNodeId, train.id).arg(train.wagons.size()).arg(train.sinkNodeId));
                stateOrStockChanged = true;
            }
            continue;
        }

        // 2. ZUSTAND: GÜTER-ABLADUNG AN DER SENKE (ENTLADUNG DER WAGGONS #1..#N -> LEERWAGEN)
        if (train.opState == TrainOperationState::UnloadingAtSink) {
            train.opProgress += dtSeconds * 0.12 * std::clamp(m_simSpeedFactor, 0.5, 3.0);
            const int nWags = std::max(1, static_cast<int>(train.wagons.size()));
            const int unloadedCount = std::clamp(static_cast<int>( std::floor(train.opProgress * nWags) ), 0, nWags);

            for (int i = 0; i < unloadedCount; ++i) {
                WagonData &w = train.wagons[i];
                if (w.productType != CargoProductType::EmptyWagon) {
                    w.assignedProductType = w.productType;
                    w.assignedWeightTons = std::max(w.weightTons, maxPayloadTonsForCategory(w.category) * 0.90);
                    const double droppedTons = w.weightTons;
                    w.productType = CargoProductType::EmptyWagon;
                    w.weightTons = 0.0;
                    w.cargoDescription = QStringLiteral("Leerwagen (an Senke abgeladen)");

                    for (SourceSinkNode &node : m_sourceSinkNodes) {
                        if (node.id == train.sinkNodeId) {
                            node.currentStockTons = std::min(node.maxCapacityTons, node.currentStockTons + droppedTons * 0.45);
                            node.totalUnloadedTons += droppedTons;
                            break;
                        }
                    }
                    stateOrStockChanged = true;
                }
            }

            if (train.opProgress >= 1.0) {
                train.opProgress = 0.0;
                train.completedDeliveries++;
                train.opState = TrainOperationState::MovingEmptyToSource;
                if (!trackPtr->isLoop) {
                    train.direction = (train.sourceTrackDistMeters <= train.sinkTrackDistMeters) ? -1 : 1;
                } else {
                    train.direction = 1;
                }
                for (CargoOrder &ord : m_cargoOrders) {
                    if (ord.assignedConsistId == train.id) {
                        ord.status = CargoOrderStatus::DeliveredToSink;
                    }
                }
                emit statusMessage(
                    QStringLiteral("⬇️ [SENKE %1] Zug %2 hat seine Güter vollständig abgeladen -> Rückfahrt als Leerzug zur Quelle [%3].")
                        .arg(train.sinkNodeId, train.id, train.sourceNodeId));
                stateOrStockChanged = true;
            }
            continue;
        }

        // 3. FAHRT ZUR SENKE (BELADEN) ODER ZURÜCK ZUR QUELLE (LEERZUG)
        const double corridorBoost = trackPtr->isMainline ? 3.2 : 1.0;
        const double speedMetersPerSec = (train.speedKmh / 3.6) * m_simSpeedFactor * corridorBoost;
        const double prevDist = train.headDistanceMeters;
        const double stepDist = speedMetersPerSec * dtSeconds;
        train.headDistanceMeters += train.direction * stepDist;

        if (trackPtr->isLoop) {
            train.headDistanceMeters = std::fmod(train.headDistanceMeters, trackPtr->totalLengthMeters);
            if (train.headDistanceMeters < 0.0) {
                train.headDistanceMeters += trackPtr->totalLengthMeters;
            }

            const double targetDist = (train.opState == TrainOperationState::MovingLoadedToSink)
                                      ? train.sinkTrackDistMeters
                                      : train.sourceTrackDistMeters;
            const bool crossedTarget = (prevDist <= targetDist && train.headDistanceMeters >= targetDist) ||
                                       (std::abs(train.headDistanceMeters - targetDist) <= stepDist * 1.25);
            if (crossedTarget) {
                train.headDistanceMeters = targetDist;
                train.opProgress = 0.0;
                if (train.opState == TrainOperationState::MovingLoadedToSink) {
                    train.opState = TrainOperationState::UnloadingAtSink;
                    for (CargoOrder &ord : m_cargoOrders) {
                        if (ord.assignedConsistId == train.id) {
                            ord.status = CargoOrderStatus::UnloadingAtSink;
                        }
                    }
                } else {
                    train.opState = TrainOperationState::LoadingAtSource;
                    for (CargoOrder &ord : m_cargoOrders) {
                        if (ord.assignedConsistId == train.id) {
                            ord.status = CargoOrderStatus::LoadingAtSource;
                        }
                    }
                }
                stateOrStockChanged = true;
            }
        } else {
            const double targetDist = (train.opState == TrainOperationState::MovingLoadedToSink)
                                      ? std::clamp(train.sinkTrackDistMeters, 140.0, trackPtr->totalLengthMeters - 140.0)
                                      : std::clamp(train.sourceTrackDistMeters, 140.0, trackPtr->totalLengthMeters - 140.0);

            const bool reachedTarget = (train.direction > 0 && train.headDistanceMeters >= targetDist) ||
                                       (train.direction < 0 && train.headDistanceMeters <= targetDist);
            if (reachedTarget) {
                train.headDistanceMeters = targetDist;
                train.opProgress = 0.0;
                if (train.opState == TrainOperationState::MovingLoadedToSink) {
                    train.opState = TrainOperationState::UnloadingAtSink;
                    for (CargoOrder &ord : m_cargoOrders) {
                        if (ord.assignedConsistId == train.id) {
                            ord.status = CargoOrderStatus::UnloadingAtSink;
                        }
                    }
                } else {
                    train.opState = TrainOperationState::LoadingAtSource;
                    for (CargoOrder &ord : m_cargoOrders) {
                        if (ord.assignedConsistId == train.id) {
                            ord.status = CargoOrderStatus::LoadingAtSource;
                        }
                    }
                }
                stateOrStockChanged = true;
            }
        }
    }

    updateAllTrainGeometries();
    if (stateOrStockChanged) {
        emit trainsUpdated();
        emit cargoAndNodesUpdated();
    }
    emit simulationStepped();
    update();
}

void RailMapWidget::focusSite(const QString &siteName, bool animate)
{
    double targetLat = 49.2465;
    double targetLon = 6.8555;
    double targetZoom = 15.2;

    if (siteName.contains(QStringLiteral("Dillingen"), Qt::CaseInsensitive)) {
        targetLat = 49.3590;
        targetLon = 6.7475;
        targetZoom = 14.8;
        m_activeSiteName = QStringLiteral("Dillinger Hütte & ROGESA (Quellen: Roheisen, Brammen, Grobbleche)");
    } else if (siteName.contains(QStringLiteral("Burbach"), Qt::CaseInsensitive)) {
        targetLat = 49.2402;
        targetLon = 6.9480;
        targetZoom = 15.1;
        m_activeSiteName = QStringLiteral("Saarstahl Werk Burbach (Senke: Knüppel | Quelle: Aufgerollter Walzdraht)");
    } else if (siteName.contains(QStringLiteral("Neunkirchen"), Qt::CaseInsensitive)) {
        targetLat = 49.3445;
        targetLon = 7.1665;
        targetZoom = 15.0;
        m_activeSiteName = QStringLiteral("Saarstahl Werk Neunkirchen (Senke: Vorblöcke | Quelle: Stabstahl & Draht)");
    } else if (siteName.contains(QStringLiteral("Gesamt"), Qt::CaseInsensitive) ||
               siteName.contains(QStringLiteral("Saarland"), Qt::CaseInsensitive) ||
               siteName.contains(QStringLiteral("Alle"), Qt::CaseInsensitive)) {
        targetLat = 49.3000;
        targetLon = 6.9450;
        targetZoom = 11.4;
        m_activeSiteName = QStringLiteral("Gesamtübersicht Saarland-Netz (Quellen, Senken & Züge)");
    } else {
        targetLat = 49.2465;
        targetLon = 6.8555;
        targetZoom = 15.2;
        m_activeSiteName = QStringLiteral("Saarstahl AG – Hauptwerk Völklingen (LD-Stahlwerk, Walzwerk, Schmiede)");
    }

    if (!animate) {
        m_centerLat = targetLat;
        m_centerLon = targetLon;
        m_zoom = targetZoom;
        updateAllTrainGeometries();
        update();
        return;
    }

    m_animStartLat = m_centerLat;
    m_animStartLon = m_centerLon;
    m_animStartZoom = m_zoom;
    m_animTargetLat = targetLat;
    m_animTargetLon = targetLon;
    m_animTargetZoom = targetZoom;
    m_animProgress = 0.0;
    m_cameraAnimTimer->start();

    emit statusMessage(QStringLiteral("Kamerafokus gewechselt auf: %1").arg(m_activeSiteName));
}

void RailMapWidget::onCameraAnimTick()
{
    m_animProgress += 0.08;
    if (m_animProgress >= 1.0) {
        m_animProgress = 1.0;
        m_cameraAnimTimer->stop();
    }
    const double t = m_animProgress * m_animProgress * (3.0 - 2.0 * m_animProgress);
    m_centerLat = m_animStartLat + t * (m_animTargetLat - m_animStartLat);
    m_centerLon = m_animStartLon + t * (m_animTargetLon - m_animStartLon);
    m_zoom = m_animStartZoom + t * (m_animTargetZoom - m_animStartZoom);
    updateAllTrainGeometries();
    update();
}

void RailMapWidget::zoomIn()  { setZoomLevel(m_zoom + 0.6); }
void RailMapWidget::zoomOut() { setZoomLevel(m_zoom - 0.6); }

void RailMapWidget::setZoomLevel(double zoom)
{
    m_zoom = std::clamp(zoom, 10.0, 18.5);
    updateAllTrainGeometries();
    update();
}

void RailMapWidget::setMapBackgroundMode(MapBackgroundMode mode) { m_bgMode = mode; update(); }
void RailMapWidget::setWagonColorMode(WagonColorMode mode)       { m_colorMode = mode; update(); }
void RailMapWidget::setShowSimplifiedPlant(bool show)            { m_showSimplifiedPlant = show; update(); }
void RailMapWidget::setShowRoadsAndRivers(bool show)             { m_showRoadsAndRivers = show; update(); }
void RailMapWidget::setShowTrackLabels(bool show)                { m_showTrackLabels = show; update(); }
void RailMapWidget::setShowWagonLabels(bool show)                { m_showWagonLabels = show; update(); }
void RailMapWidget::setShowSourcesAndSinks(bool show)            { m_showSourcesAndSinks = show; update(); }

void RailMapWidget::setExaggerateWagonSize(bool exaggerate)
{
    m_exaggerateWagonSize = exaggerate;
    updateAllTrainGeometries();
    update();
}

void RailMapWidget::setSimulationRunning(bool running) { m_simRunning = running; update(); }
void RailMapWidget::setSimulationSpeedMultiplier(double factor) { m_simSpeedFactor = std::clamp(factor, 0.1, 15.0); }

void RailMapWidget::selectTrain(int trainIdx, int wagonIdx, bool centerCamera)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    m_selectedTrainIdx = trainIdx;
    m_selectedWagonIdx = wagonIdx;

    if (centerCamera) {
        const TrainConsist &tr = m_trains[trainIdx];
        if (wagonIdx >= 0 && wagonIdx < tr.wagons.size()) {
            m_centerLat = tr.wagons[wagonIdx].currentLat;
            m_centerLon = tr.wagons[wagonIdx].currentLon;
        } else {
            m_centerLat = tr.locoLat;
            m_centerLon = tr.locoLon;
        }
    }
    emit selectionChanged(m_selectedTrainIdx, m_selectedWagonIdx);
    update();
}

void RailMapWidget::appendWagonToTrain(int trainIdx, WagonCategory category, CargoProductType product)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (tr.wagons.size() >= 30) {
        emit statusMessage(QStringLiteral("Maximale Zuglänge (30 Waggons) erreicht."));
        return;
    }

    WagonData w;
    w.positionIndex = tr.wagons.size() + 1;
    w.category = category;
    w.productType = product;
    w.assignedProductType = (product == CargoProductType::EmptyWagon) ? CargoProductType::SteelBillets : product;
    w.axleCount = axleCountForCategory(category);
    w.lengthMeters = lengthForCategory(category);
    w.assignedWeightTons = maxPayloadTonsForCategory(category) * 0.96;
    w.weightTons = (product == CargoProductType::EmptyWagon) ? 0.0 : w.assignedWeightTons;
    w.cargoDescription = QString("%1 [%2]").arg(productName(product), shortCodeForWagon(category));

    const int serial = m_nextUicSerial++;
    const int check = (serial * 7) % 10;
    if (category == WagonCategory::Samms_6Axle) {
        w.uicNumber = QString("31 80 4891 %1-%2").arg(serial, 3, 10, QChar('0')).arg(check);
    } else if (category == WagonCategory::Rnsz_4Axle) {
        w.uicNumber = QString("31 80 3552 %1-%2").arg(serial, 3, 10, QChar('0')).arg(check);
    } else if (category == WagonCategory::Standard_4Axle) {
        w.uicNumber = QString("31 80 4674 %1-%2").arg(serial, 3, 10, QChar('0')).arg(check);
    } else {
        w.uicNumber = QString("83 80 9981 %1-%2").arg(serial, 3, 10, QChar('0')).arg(check);
    }

    tr.wagons.push_back(w);
    m_selectedWagonIdx = tr.wagons.size() - 1;
    updateAllTrainGeometries();
    emit trainsUpdated();
    emit statusMessage(QStringLiteral("Zug %1 verlängert: Waggon #%2 (%3 mit %4) angehängt.")
                           .arg(tr.id).arg(w.positionIndex)
                           .arg(shortCodeForWagon(category), productShortName(product)));
    update();
}

void RailMapWidget::removeLastWagonFromTrain(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (tr.wagons.size() <= 1) {
        emit statusMessage(QStringLiteral("Mindestens 1 Waggon bleibt im Zugverband."));
        return;
    }
    const WagonData removed = tr.wagons.takeLast();
    if (m_selectedWagonIdx >= tr.wagons.size()) {
        m_selectedWagonIdx = tr.wagons.size() - 1;
    }
    updateAllTrainGeometries();
    emit trainsUpdated();
    emit statusMessage(QStringLiteral("Zug %1 verkürzt: Waggon #%2 (%3) abgehängt.")
                           .arg(tr.id).arg(removed.positionIndex).arg(shortCodeForWagon(removed.category)));
    update();
}

void RailMapWidget::setTrainWagonCount(int trainIdx, int targetCount, WagonCategory defaultCategoryForNew)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    targetCount = std::clamp(targetCount, 1, 30);
    while (m_trains[trainIdx].wagons.size() < targetCount) {
        CargoProductType defProd = CargoProductType::SteelBillets;
        if (!m_trains[trainIdx].wagons.isEmpty()) {
            defProd = m_trains[trainIdx].wagons.last().productType;
        }
        appendWagonToTrain(trainIdx, defaultCategoryForNew, defProd);
    }
    while (m_trains[trainIdx].wagons.size() > targetCount) {
        removeLastWagonFromTrain(trainIdx);
    }
}

void RailMapWidget::setWagonCategoryAt(int trainIdx, int wagonIdx, WagonCategory category)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (wagonIdx < 0 || wagonIdx >= tr.wagons.size()) {
        return;
    }
    WagonData &w = tr.wagons[wagonIdx];
    w.category = category;
    w.axleCount = axleCountForCategory(category);
    w.lengthMeters = lengthForCategory(category);
    w.weightTons = (w.productType == CargoProductType::EmptyWagon) ? 0.0 : maxPayloadTonsForCategory(category) * 0.95;
    w.cargoDescription = QString("%1 [%2]").arg(productName(w.productType), shortCodeForWagon(category));
    updateAllTrainGeometries();
    emit trainsUpdated();
    update();
}

void RailMapWidget::setWagonProductAt(int trainIdx, int wagonIdx, CargoProductType product)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    TrainConsist &tr = m_trains[trainIdx];
    if (wagonIdx < 0 || wagonIdx >= tr.wagons.size()) {
        return;
    }
    WagonData &w = tr.wagons[wagonIdx];
    w.productType = product;
    w.weightTons = (product == CargoProductType::EmptyWagon) ? 0.0 : maxPayloadTonsForCategory(w.category) * 0.95;
    w.cargoDescription = QString("%1 [%2]").arg(productName(product), shortCodeForWagon(w.category));
    emit trainsUpdated();
    update();
}

void RailMapWidget::reverseTrainDirection(int trainIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    m_trains[trainIdx].direction *= -1;
    updateAllTrainGeometries();
    emit trainsUpdated();
    update();
}

void RailMapWidget::assignTrainToTrack(int trainIdx, const QString &trackId)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    m_trains[trainIdx].trackId = trackId;
    m_trains[trainIdx].headDistanceMeters = 700.0;
    updateAllTrainGeometries();
    emit trainsUpdated();
    update();
}

void RailMapWidget::zoomToWagon(int trainIdx, int wagonIdx)
{
    if (trainIdx < 0 || trainIdx >= m_trains.size()) {
        return;
    }
    const TrainConsist &tr = m_trains[trainIdx];
    m_selectedTrainIdx = trainIdx;
    m_selectedWagonIdx = wagonIdx;

    double targetLat = tr.locoLat;
    double targetLon = tr.locoLon;
    if (wagonIdx >= 0 && wagonIdx < tr.wagons.size()) {
        targetLat = tr.wagons[wagonIdx].currentLat;
        targetLon = tr.wagons[wagonIdx].currentLon;
    }

    m_animStartLat = m_centerLat;
    m_animStartLon = m_centerLon;
    m_animStartZoom = m_zoom;
    m_animTargetLat = targetLat;
    m_animTargetLon = targetLon;
    m_animTargetZoom = std::max(m_zoom, 16.0);
    m_animProgress = 0.0;
    m_cameraAnimTimer->start();
    emit selectionChanged(m_selectedTrainIdx, m_selectedWagonIdx);
}

void RailMapWidget::zoomToSourceSinkNode(int nodeIdx)
{
    if (nodeIdx < 0 || nodeIdx >= m_sourceSinkNodes.size()) {
        return;
    }
    const SourceSinkNode &n = m_sourceSinkNodes[nodeIdx];
    m_animStartLat = m_centerLat;
    m_animStartLon = m_centerLon;
    m_animStartZoom = m_zoom;
    m_animTargetLat = n.lat;
    m_animTargetLon = n.lon;
    m_animTargetZoom = 15.6;
    m_animProgress = 0.0;
    m_activeSiteName = QString("%1: %2").arg(n.role, n.name);
    m_cameraAnimTimer->start();
    emit statusMessage(QStringLiteral("Zoom auf %1 [%2]: %3").arg(n.role, n.id, n.name));
}

void RailMapWidget::randomizePositionsOnTracks(bool voelklingenOnly)
{
    QRandomGenerator *rng = QRandomGenerator::global();
    QStringList logLines;
    logLines << QStringLiteral("=== [TEST-MODUS] ZUFÄLLIGE GLEISPOSITIONIERUNG (%1) ===")
                    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"));

    QVector<int> vkTrackIndices;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].siteId == QStringLiteral("Voelklingen")) {
            vkTrackIndices.push_back(i);
        }
    }

    for (int i = 0; i < m_trains.size(); ++i) {
        TrainConsist &tr = m_trains[i];
        if (voelklingenOnly && !vkTrackIndices.isEmpty()) {
            const int chosenIdx = vkTrackIndices[rng->bounded(vkTrackIndices.size())];
            tr.trackId = m_tracks[chosenIdx].id;
        }

        const RailTrack *trackPtr = nullptr;
        for (const RailTrack &t : m_tracks) {
            if (t.id == tr.trackId) {
                trackPtr = &t;
                break;
            }
        }
        if (!trackPtr) {
            continue;
        }

        const double minDist = std::min(250.0, trackPtr->totalLengthMeters * 0.15);
        const double maxDist = std::max(minDist + 10.0, trackPtr->totalLengthMeters * 0.88);
        tr.headDistanceMeters = minDist + rng->generateDouble() * (maxDist - minDist);
        tr.direction = (rng->bounded(2) == 0) ? 1 : -1;
    }

    updateAllTrainGeometries();
    for (const TrainConsist &tr : m_trains) {
        logLines << QStringLiteral("• %1 (%2) auf Gleis [%3] -> KM %4 | GPS (%5° N, %6° E)")
                        .arg(tr.id, tr.hasLocomotive ? QStringLiteral("MIT LOK") : QStringLiteral("OHNE LOK / GELAGERT"),
                             tr.trackId)
                        .arg(tr.headDistanceMeters / 1000.0, 0, 'f', 2)
                        .arg(tr.locoLat, 0, 'f', 5)
                        .arg(tr.locoLon, 0, 'f', 5);
    }

    emit trainsUpdated();
    emit apiLogMessage(logLines.join('\n'));
    emit statusMessage(QStringLiteral("Alle Züge und gelagerten Waggongruppen wurden zufällig auf den Gleisen positioniert."));
    update();
}

QString RailMapWidget::applySimulatedDbApiPositioning(const QString &endpointUrl, const QString &apiKey)
{
    QRandomGenerator *rng = QRandomGenerator::global();
    QJsonObject root;
    root["provider"] = QStringLiteral("DB Cargo Link / SaarRail SAVVY-Telematik API v2.6 (TAF-TSI / Ril 100 / VzG-Strecken-Snapping)");
    root["endpoint"] = endpointUrl.isEmpty()
                       ? QStringLiteral("https://apis.deutschebahn.com/db-api-marketplace/apis/cargo-wagon-tracking/v2/positions")
                       : endpointUrl;
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["locomotivePool"] = QJsonObject{
        {"totalLocos", m_totalLocosInPool},
        {"inUse", usedLocomotivesCount()},
        {"free", freeLocomotivesCount()}
    };
    root["authStatus"] = apiKey.isEmpty() ? QStringLiteral("SANDBOX_TELEMATICS_EMULATOR") : QStringLiteral("API_KEY_VERIFIED");

    // Realistischer Telematik-Zeitschritt statt Zufallssprung:
    // Fahrende Züge bewegen sich realistisch entlang ihrer Route (Quelle <-> Senke),
    // stehende Züge an Quelle/Senke oder ohne Lok behalten ihre echte Gleisposition.
    stepSimulation(1.5);

    auto ds100ForTrack = [](const QString &trackId, double distM) -> QString {
        if (trackId.startsWith(QStringLiteral("VK-"))) {
            return QStringLiteral("SVK (Bf Völklingen / Anschluss Saarstahl AG)");
        }
        if (trackId.startsWith(QStringLiteral("DL-"))) {
            return QStringLiteral("SDL (Bf Dillingen (Saar) / Anschluss Dillinger Hütte & ROGESA)");
        }
        if (trackId.startsWith(QStringLiteral("BB-"))) {
            return QStringLiteral("SBU (Bf Saarbrücken-Burbach / Anschluss Drahtwerk)");
        }
        if (trackId.startsWith(QStringLiteral("NK-"))) {
            return QStringLiteral("SNK (Neunkirchen (Saar) Hbf / Anschluss Walzwerk)");
        }
        // SAAR-CORRIDOR je nach Position entlang Dillingen <-> Völklingen <-> Burbach <-> Neunkirchen
        if (distM < 8000.0) {
            return QStringLiteral("SDL -> SVK (VzG 3230 Saarstrecke Abschn. Dillingen–Ensdorf–Bous)");
        }
        if (distM < 18000.0) {
            return QStringLiteral("SVK (VzG 3230 Knoten Völklingen)");
        }
        if (distM < 26000.0) {
            return QStringLiteral("SBU (VzG 3230 Abschn. Völklingen–Saarbrücken-Burbach)");
        }
        return QStringLiteral("SNK (VzG 3511 Abschn. Saarbrücken–Neunkirchen (Saar))");
    };

    auto vzgKilometerForTrack = [](const QString &trackId, double distM) -> QString {
        if (trackId.startsWith(QStringLiteral("VK-"))) {
            const double km = 10.6 + (distM / 1000.0);
            return QString("VzG 3230 (Saarstrecke) km %1").arg(km, 0, 'f', 2);
        }
        if (trackId.startsWith(QStringLiteral("DL-"))) {
            const double km = 24.8 + (distM / 1000.0);
            return QString("VzG 3230 / Werksnetz SDL km %1").arg(km, 0, 'f', 2);
        }
        if (trackId.startsWith(QStringLiteral("BB-"))) {
            const double km = 4.2 + (distM / 1000.0);
            return QString("VzG 3230 (Saarbrücken-Burbach) km %1").arg(km, 0, 'f', 2);
        }
        if (trackId.startsWith(QStringLiteral("NK-"))) {
            const double km = 19.4 + (distM / 1000.0);
            return QString("VzG 3511 (Nahetalbahn NK) km %1").arg(km, 0, 'f', 2);
        }
        const double km = std::max(0.5, 26.2 - (distM / 1000.0));
        return QString("VzG 3230/3511 Korridor km %1").arg(km, 0, 'f', 2);
    };

    QJsonArray trainArray;
    for (TrainConsist &tr : m_trains) {
        const RailTrack *trackPtr = nullptr;
        for (const RailTrack &t : m_tracks) {
            if (t.id == tr.trackId) {
                trackPtr = &t;
                break;
            }
        }

        // Simuliere reales GNSS-Rohsignal (inkl. ±1.2 m .. ±3.4 m GPS-Mehrwege-Drift in Werkhallennähe)
        // und führe echtes Map-Matching (Track-Snapping via findNearestTrackDistance) auf die Gleisachse durch:
        const double jitterLatDeg = (rng->generateDouble() - 0.5) * 0.000032; // ca. ±1.8 m Nord/Süd
        const double jitterLonDeg = (rng->generateDouble() - 0.5) * 0.000046; // ca. ±2.5 m Ost/West
        const double rawLocoLat = tr.locoLat + jitterLatDeg;
        const double rawLocoLon = tr.locoLon + jitterLonDeg;

        if (trackPtr) {
            tr.headDistanceMeters = findNearestTrackDistance(*trackPtr, rawLocoLat, rawLocoLon);
        }
    }

    updateAllTrainGeometries();

    for (const TrainConsist &tr : m_trains) {
        const double gnssAccMeters = 1.4 + (rng->generateDouble() * 2.1);
        const double rawLat = tr.locoLat + (rng->generateDouble() - 0.5) * 0.000024;
        const double rawLon = tr.locoLon + (rng->generateDouble() - 0.5) * 0.000036;

        QJsonObject tObj;
        tObj["consistId"] = tr.id;
        tObj["operatingPointDS100"] = ds100ForTrack(tr.trackId, tr.headDistanceMeters);
        tObj["vzgTrackKilometer"] = vzgKilometerForTrack(tr.trackId, tr.headDistanceMeters);
        tObj["operationState"] = operationStateText(tr);
        tObj["hasLocomotive"] = tr.hasLocomotive;
        tObj["locomotive"] = tr.locoName;
        tObj["sourceNode"] = tr.sourceNodeId;
        tObj["sinkNode"] = tr.sinkNodeId;
        tObj["trackSection"] = tr.trackId;
        tObj["speedKmh"] = (tr.hasLocomotive && tr.active &&
                            (tr.opState == TrainOperationState::MovingLoadedToSink ||
                             tr.opState == TrainOperationState::MovingEmptyToSource))
                           ? tr.speedKmh : 0.0;
        tObj["headingDeg"] = std::round(tr.locoHeadingDeg * 10.0) / 10.0;
        tObj["rawGnssLat"] = std::round(rawLat * 1000000.0) / 1000000.0;
        tObj["rawGnssLon"] = std::round(rawLon * 1000000.0) / 1000000.0;
        tObj["snappedTrackLat"] = std::round(tr.locoLat * 1000000.0) / 1000000.0;
        tObj["snappedTrackLon"] = std::round(tr.locoLon * 1000000.0) / 1000000.0;
        tObj["gnssAccuracyMeters"] = std::round(gnssAccMeters * 10.0) / 10.0;

        QJsonArray wArr;
        for (const WagonData &w : tr.wagons) {
            QJsonObject wObj;
            wObj["pos"] = w.positionIndex;
            wObj["uic"] = w.uicNumber;
            wObj["telematicsBoxId"] = QString("SAVVY-%1").arg(w.uicNumber.mid(6).remove(' '));
            wObj["wagonClass"] = shortCodeForWagon(w.category);
            wObj["axles"] = w.axleCount;
            wObj["lengthOverBuffersMeters"] = w.lengthMeters;
            wObj["product"] = productShortName(w.productType);
            wObj["tons"] = std::round(w.weightTons * 10.0) / 10.0;
            if (w.category == WagonCategory::Heavy_6Axle || w.productType == CargoProductType::LiquidPigIron) {
                wObj["torpedoHotMetalTempC"] = (w.productType == CargoProductType::EmptyWagon) ? 920 : 1420;
            }
            wObj["lat"] = std::round(w.currentLat * 1000000.0) / 1000000.0;
            wObj["lon"] = std::round(w.currentLon * 1000000.0) / 1000000.0;
            wArr.append(wObj);
        }
        tObj["wagons"] = wArr;
        trainArray.append(tObj);
    }
    root["consists"] = trainArray;

    const QString jsonStr = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    emit trainsUpdated();
    emit apiLogMessage(jsonStr);
    emit statusMessage(QStringLiteral("DB-API / SAVVY-Telematik-Ortung verarbeitet (%1 Verbände mit Ril-100/DS100, VzG-km & Track-Snapping).")
                           .arg(m_trains.size()));
    update();
    return jsonStr;
}

void RailMapWidget::triggerLiveNetworkApiQuery(const QString &endpointUrl,
                                               const QString &clientId,
                                               const QString &apiKey)
{
    if (endpointUrl.trimmed().isEmpty() || endpointUrl.contains(QStringLiteral("sandbox")) || apiKey.trimmed().isEmpty()) {
        applySimulatedDbApiPositioning(endpointUrl, apiKey);
        return;
    }

    QNetworkRequest req{QUrl(endpointUrl.trimmed())};
    req.setRawHeader("Accept", "application/json");
    if (!clientId.trimmed().isEmpty()) {
        req.setRawHeader("DB-Client-Id", clientId.trimmed().toUtf8());
    }
    if (!apiKey.trimmed().isEmpty()) {
        req.setRawHeader("DB-Api-Key", apiKey.trimmed().toUtf8());
    }

    QNetworkReply *reply = m_netManager->get(req);
    reply->setProperty("isDbApiCall", true);
    emit statusMessage(QStringLiteral("Sende HTTP-GET Anfrage an Bahn-API (%1) ...").arg(endpointUrl));
}

QPointF RailMapWidget::geoToScreen(double lat, double lon) const
{
    const double worldSize = 256.0 * std::pow(2.0, m_zoom);
    const double cx = lonToMercX(m_centerLon) * worldSize;
    const double cy = latToMercY(m_centerLat) * worldSize;
    const double px = lonToMercX(lon) * worldSize;
    const double py = latToMercY(lat) * worldSize;
    return QPointF(width() * 0.5 + (px - cx), height() * 0.5 + (py - cy));
}

GeoPoint RailMapWidget::screenToGeo(const QPointF &pt) const
{
    const double worldSize = 256.0 * std::pow(2.0, m_zoom);
    const double cx = lonToMercX(m_centerLon) * worldSize;
    const double cy = latToMercY(m_centerLat) * worldSize;
    const double px = cx + (pt.x() - width() * 0.5);
    const double py = cy + (pt.y() - height() * 0.5);
    GeoPoint gp;
    gp.lon = mercXToLon(px / worldSize);
    gp.lat = mercYToLat(py / worldSize);
    return gp;
}

double RailMapWidget::metersPerPixel() const
{
    const double latRad = degToRad(m_centerLat);
    return (std::cos(latRad) * 2.0 * kPi * kEarthRadiusMeters) / (256.0 * std::pow(2.0, m_zoom));
}

QString RailMapWidget::tileCachePath(int z, int x, int y, const QString &lyrs) const
{
    const QString cacheDir = QCoreApplication::applicationDirPath() + QStringLiteral("/tile_cache/") + lyrs;
    QDir().mkpath(cacheDir);
    return QString("%1/%2_%3_%4.png").arg(cacheDir).arg(z).arg(x).arg(y);
}

void RailMapWidget::requestTile(int z, int x, int y, const QString &lyrs)
{
    const QString key = QString("%1_%2_%3_%4").arg(lyrs).arg(z).arg(x).arg(y);
    if (m_tileCache.contains(key) || m_pendingTiles.contains(key)) {
        return;
    }

    const QString diskPath = tileCachePath(z, x, y, lyrs);
    if (QFile::exists(diskPath)) {
        QPixmap pm;
        if (pm.load(diskPath)) {
            m_tileCache.insert(key, pm);
            return;
        }
    }

    m_pendingTiles.insert(key);
    const QString urlStr = QString("https://mt1.google.com/vt/lyrs=%1&x=%2&y=%3&z=%4&hl=de")
                               .arg(lyrs).arg(x).arg(y).arg(z);
    QNetworkRequest req{QUrl(urlStr)};
    req.setRawHeader("User-Agent", "SHS-Logistics-Qt6-Client/1.0");
    QNetworkReply *reply = m_netManager->get(req);
    reply->setProperty("tileKey", key);
    reply->setProperty("diskPath", diskPath);
}

void RailMapWidget::onTileReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->property("isDbApiCall").toBool()) {
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray raw = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (doc.isObject() && doc.object().contains(QStringLiteral("consists"))) {
                const QJsonArray arr = doc.object().value(QStringLiteral("consists")).toArray();
                for (const QJsonValue &val : arr) {
                    const QJsonObject obj = val.toObject();
                    const QString cId = obj.value(QStringLiteral("consistId")).toString();
                    const double inLat = obj.value(QStringLiteral("snappedTrackLat")).toDouble(
                                         obj.value(QStringLiteral("gpsLat")).toDouble(0.0));
                    const double inLon = obj.value(QStringLiteral("snappedTrackLon")).toDouble(
                                         obj.value(QStringLiteral("gpsLon")).toDouble(0.0));
                    if (inLat > 45.0 && inLon > 4.0) {
                        for (TrainConsist &tr : m_trains) {
                            if (tr.id == cId) {
                                for (const RailTrack &t : m_tracks) {
                                    if (t.id == tr.trackId) {
                                        tr.headDistanceMeters = findNearestTrackDistance(t, inLat, inLon);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
                updateAllTrainGeometries();
                emit trainsUpdated();
                update();
            }
            emit apiLogMessage(QString::fromUtf8(raw));
            emit statusMessage(QStringLiteral("Live Bahn-API Antwort empfangen & auf Gleise projiziert (%1 Bytes).").arg(raw.size()));
        } else {
            applySimulatedDbApiPositioning(reply->url().toString(), QStringLiteral("FALLBACK"));
        }
        return;
    }

    const QString key = reply->property("tileKey").toString();
    const QString diskPath = reply->property("diskPath").toString();
    m_pendingTiles.remove(key);

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (pm.loadFromData(data)) {
            m_tileCache.insert(key, pm);
            QFile f(diskPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
            }
            update();
        }
    }
}

void RailMapWidget::drawMapTiles(QPainter &painter)
{
    if (m_bgMode == MapBackgroundMode::SimplifiedSchematicOnly) {
        return;
    }

    QString lyrs = QStringLiteral("m");
    if (m_bgMode == MapBackgroundMode::GoogleSatelliteAndRails) {
        lyrs = QStringLiteral("s");
    }

    const int tileZ = std::clamp(static_cast<int>(std::floor(m_zoom)), 10, 18);
    const double scale = std::pow(2.0, m_zoom - tileZ);
    const double tileSizePx = 256.0 * scale;
    const int numTiles = 1 << tileZ;

    const double centerTileX = lonToMercX(m_centerLon) * numTiles;
    const double centerTileY = latToMercY(m_centerLat) * numTiles;

    const int halfCols = static_cast<int>(std::ceil((width() * 0.5) / tileSizePx)) + 1;
    const int halfRows = static_cast<int>(std::ceil((height() * 0.5) / tileSizePx)) + 1;

    const int cX = static_cast<int>(std::floor(centerTileX));
    const int cY = static_cast<int>(std::floor(centerTileY));

    for (int tx = cX - halfCols; tx <= cX + halfCols; ++tx) {
        for (int ty = cY - halfRows; ty <= cY + halfRows; ++ty) {
            if (tx < 0 || tx >= numTiles || ty < 0 || ty >= numTiles) {
                continue;
            }
            const double screenX = width() * 0.5 + (tx - centerTileX) * tileSizePx;
            const double screenY = height() * 0.5 + (ty - centerTileY) * tileSizePx;
            const QRectF targetRect(screenX, screenY, tileSizePx + 0.5, tileSizePx + 0.5);

            const QString key = QString("%1_%2_%3_%4").arg(lyrs).arg(tileZ).arg(tx).arg(ty);
            auto it = m_tileCache.find(key);
            if (it != m_tileCache.end()) {
                painter.drawPixmap(targetRect, it.value(), QRectF(0, 0, it.value().width(), it.value().height()));
            } else {
                requestTile(tileZ, tx, ty, lyrs);
            }
        }
    }

    if (m_bgMode == MapBackgroundMode::HybridSimplifiedAndGoogle) {
        painter.fillRect(rect(), QColor(30, 41, 59, 85));
    }
}

void RailMapWidget::drawSchematicBackground(QPainter &painter)
{
    QLinearGradient bgGrad(0, 0, 0, height());
    bgGrad.setColorAt(0.0, QColor("#1E293B"));
    bgGrad.setColorAt(1.0, QColor("#0F172A"));
    painter.fillRect(rect(), bgGrad);

    painter.save();
    painter.setPen(QPen(QColor(148, 163, 184, 28), 1.0, Qt::DotLine));
    for (int x = 0; x < width(); x += 80) painter.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 80) painter.drawLine(0, y, width(), y);
    painter.restore();
}

void RailMapWidget::drawRiversAndRoads(QPainter &painter)
{
    if (!m_showRoadsAndRivers) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const SimplifiedRoad &rd : m_roads) {
        if (rd.path.size() < 2) continue;
        QPainterPath path;
        path.moveTo(geoToScreen(rd.path[0].lat, rd.path[0].lon));
        for (int i = 1; i < rd.path.size(); ++i) {
            path.lineTo(geoToScreen(rd.path[i].lat, rd.path[i].lon));
        }
        painter.setBrush(Qt::NoBrush);

        if (rd.roadClass == QStringLiteral("river")) {
            const double riverWidth = std::clamp((m_zoom - 10.0) * 3.8, 5.0, 26.0);
            painter.setPen(QPen(QColor(14, 116, 144, 180), riverWidth + 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(QColor(56, 189, 248, 200), riverWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        } else if (rd.roadClass == QStringLiteral("highway")) {
            const double hwWidth = std::clamp((m_zoom - 10.0) * 1.8, 3.5, 10.5);
            painter.setPen(QPen(QColor(120, 53, 15, 215), hwWidth + 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(QColor(251, 191, 36, 215), hwWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        } else if (rd.roadClass == QStringLiteral("primary")) {
            const double prWidth = std::clamp((m_zoom - 10.0) * 1.4, 2.5, 8.0);
            painter.setPen(QPen(QColor(51, 65, 85, 210), prWidth + 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
            painter.setPen(QPen(QColor(226, 232, 240, 205), prWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        } else {
            const double plWidth = std::clamp((m_zoom - 10.0) * 1.2, 2.0, 6.5);
            painter.setPen(QPen(QColor(249, 115, 22, 195), plWidth, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }
    }

    painter.restore();
}

void RailMapWidget::drawPlantAreasAndBuildings(QPainter &painter)
{
    if (!m_showSimplifiedPlant) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const PlantBuilding &b : m_buildings) {
        if (b.footprint.size() < 3) continue;
        QPolygonF poly;
        for (const GeoPoint &gp : b.footprint) {
            poly << geoToScreen(gp.lat, gp.lon);
        }
        const QRectF bounds = poly.boundingRect();
        if (!bounds.intersects(QRectF(rect()))) continue;

        const bool isPerimeter = b.name.contains(QStringLiteral("SAARSTAHL AG – WERK VÖLKLINGEN")) ||
                                 b.name.contains(QStringLiteral("DILLINGER HÜTTE & ROGESA"));

        painter.setBrush(b.fillColor);
        painter.setPen(QPen(b.borderColor, isPerimeter ? 2.5 : 1.8,
                            isPerimeter ? Qt::DashLine : Qt::SolidLine));
        painter.drawPolygon(poly);

        if (isPerimeter && m_zoom >= 14.0) {
            const QPointF topPt(bounds.center().x(), bounds.top() - 14.0);
            const QRectF tagRect(topPt.x() - 170, topPt.y() - 12, 340, 22);
            painter.setBrush(QColor(15, 23, 42, 225));
            painter.setPen(QPen(b.borderColor, 1.4));
            painter.drawRoundedRect(tagRect, 5, 5);
            QFont f = painter.font();
            f.setBold(true);
            f.setPointSize(9);
            painter.setFont(f);
            painter.setPen(QColor("#FBD50E"));
            painter.drawText(tagRect, Qt::AlignCenter, b.name);
        }
    }

    painter.restore();
}

void RailMapWidget::drawRailNetwork(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const RailTrack &tr : m_tracks) {
        if (tr.waypoints.size() < 2) continue;

        QPainterPath path;
        path.moveTo(geoToScreen(tr.waypoints[0].lat, tr.waypoints[0].lon));
        for (int i = 1; i < tr.waypoints.size(); ++i) {
            path.lineTo(geoToScreen(tr.waypoints[i].lat, tr.waypoints[i].lon));
        }

        painter.setBrush(Qt::NoBrush);

        // 1. Realistisches dunkles Schotterbett (Ballast)
        const double bedWidth = tr.isMainline ? 9.5 : 8.2;
        painter.setPen(QPen(QColor(15, 23, 42, 240), bedWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);

        // 2. Quer-Bahnschwellen (Holz-/Betonschwellen)
        QPen sleeperPen(QColor(203, 213, 225, 195), bedWidth - 1.2, Qt::CustomDashLine);
        sleeperPen.setDashPattern({0.38, 2.1});
        painter.setPen(sleeperPen);
        painter.drawPath(path);

        // 3. Farbige Gleis-Leitlinie & Stahlschienen-Profil
        painter.setPen(QPen(tr.trackColor, tr.isMainline ? 3.2 : 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);

        // 4. Prellböcke an offenen Gleisenden (bei Werks- und Abstellgleisen)
        if (!tr.isLoop && m_zoom >= 13.8) {
            const QPointF pStart = geoToScreen(tr.waypoints.first().lat, tr.waypoints.first().lon);
            const QPointF pEnd   = geoToScreen(tr.waypoints.last().lat,  tr.waypoints.last().lon);
            painter.setBrush(QColor("#EF4444"));
            painter.setPen(QPen(Qt::white, 1.2));
            painter.drawRect(QRectF(pStart.x() - 4, pStart.y() - 4, 8, 8));
            painter.drawRect(QRectF(pEnd.x() - 4,   pEnd.y() - 4,   8, 8));
        }

        // 5. Gleisbezeichnung
        if (m_showTrackLabels && m_zoom >= 13.2) {
            const int midIdx = tr.waypoints.size() / 2;
            const QPointF midPt = geoToScreen(tr.waypoints[midIdx].lat, tr.waypoints[midIdx].lon);
            if (rect().contains(midPt.toPoint())) {
                QFont f = painter.font();
                f.setPointSize(7);
                f.setBold(true);
                painter.setFont(f);
                const QRectF bRect(midPt.x() - 52, midPt.y() - 20, 104, 16);
                painter.setBrush(QColor(15, 23, 42, 225));
                painter.setPen(QPen(tr.trackColor, 1.2));
                painter.drawRoundedRect(bRect, 3, 3);
                painter.setPen(QColor("#F8FAFC"));
                painter.drawText(bRect, Qt::AlignCenter, tr.id);
            }
        }
    }

    painter.restore();
}

void RailMapWidget::drawSourcesAndSinks(QPainter &painter)
{
    if (!m_showSourcesAndSinks) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const SourceSinkNode &n : m_sourceSinkNodes) {
        const QPointF pt = geoToScreen(n.lat, n.lon);
        if (!rect().adjusted(-150, -80, 150, 80).contains(pt.toPoint())) {
            continue;
        }

        // Prüfe, ob gerade ein Zug an dieser Quelle Güter aufnimmt oder an dieser Senke Güter ablädt
        bool activeLoadingHere = false;
        bool activeUnloadingHere = false;
        for (const TrainConsist &tr : m_trains) {
            if (!tr.hasLocomotive) continue;
            if (tr.opState == TrainOperationState::LoadingAtSource && tr.sourceNodeId == n.id) {
                activeLoadingHere = true;
            }
            if (tr.opState == TrainOperationState::UnloadingAtSink && tr.sinkNodeId == n.id) {
                activeUnloadingHere = true;
            }
        }

        // Zeichne eine Verladebrücke / Kranstation direkt auf dem zugeordneten Gleis
        for (const RailTrack &rt : m_tracks) {
            if (rt.id == n.trackId) {
                double tLat = n.lat, tLon = n.lon, tHead = 0.0;
                sampleTrackAtDistance(rt, n.trackDistMeters, tLat, tLon, tHead);
                const QPointF trackPt = geoToScreen(tLat, tLon);

                // Gestrichelte Förder-/Kranlinie zwischen Werkhalle und Gleis-Verladepunkt
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(n.badgeColor, 1.8, Qt::DashLine));
                painter.drawLine(pt, trackPt);

                // 2D-Portalkran / Verladebrücke über dem Gleis
                painter.save();
                painter.translate(trackPt);
                painter.rotate(tHead - 90.0);
                painter.setBrush(QColor(15, 23, 42, 230));
                painter.setPen(QPen(activeLoadingHere ? QColor("#22C55E")
                                    : (activeUnloadingHere ? QColor("#38BDF8") : QColor("#FBD50E")), 2.0));
                painter.drawRoundedRect(QRectF(-15, -12, 30, 24), 3, 3);
                painter.drawLine(QPointF(-10, -12), QPointF(-10, 12));
                painter.drawLine(QPointF( 10, -12), QPointF( 10, 12));
                painter.restore();
                break;
            }
        }

        // Knoten-Symbol auf der Werkhalle
        painter.setBrush(n.badgeColor);
        painter.setPen(QPen(Qt::white, 2.0));
        painter.drawRoundedRect(QRectF(pt.x() - 8, pt.y() - 8, 16, 16), 4, 4);

        if (m_zoom >= 13.5) {
            const QRectF card(pt.x() - 126, pt.y() + 10, 252, 58);
            painter.setBrush(QColor(15, 23, 42, 238));
            const QColor borderCol = activeLoadingHere ? QColor("#22C55E")
                                     : (activeUnloadingHere ? QColor("#38BDF8") : n.badgeColor);
            painter.setPen(QPen(borderCol, (activeLoadingHere || activeUnloadingHere) ? 2.4 : 1.6));
            painter.drawRoundedRect(card, 5, 5);

            QFont f = painter.font();
            f.setPointSize(7);
            f.setBold(true);
            painter.setFont(f);

            QString rolePrefix = QStringLiteral("⬆️⬇️ QUELLE & SENKE");
            if (n.role.startsWith(QStringLiteral("Quelle")) && !n.role.contains(QStringLiteral("Senke"))) {
                rolePrefix = QStringLiteral("⬆️ QUELLE (AUFNAHME)");
            } else if (n.role.startsWith(QStringLiteral("Senke"))) {
                rolePrefix = QStringLiteral("⬇️ SENKE (ABLADUNG)");
            }
            painter.setPen(QColor("#FBD50E"));
            painter.drawText(QRectF(card.left() + 6, card.top() + 2, card.width() - 12, 13),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString("[%1] %2").arg(rolePrefix, n.name));

            f.setBold(false);
            painter.setFont(f);
            painter.setPen(QColor("#86EFAC"));
            painter.drawText(QRectF(card.left() + 6, card.top() + 15, card.width() - 12, 12),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString("⬆️ Aufnahme: %1").arg(n.producedGoods));

            painter.setPen(QColor("#7DD3FC"));
            painter.drawText(QRectF(card.left() + 6, card.top() + 27, card.width() - 12, 12),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QString("⬇️ Abladung: %1").arg(n.consumedGoods));

            // Kleiner Lagerbestand-Füllbalken + Bilanz (Aufgenommen / Abgeladen)
            const double fillRatio = std::clamp(n.currentStockTons / std::max(1.0, n.maxCapacityTons), 0.0, 1.0);
            const QRectF barBg(card.left() + 6, card.top() + 44, 88, 6);
            painter.setBrush(QColor("#334155"));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(barBg, 2, 2);
            painter.setBrush(n.badgeColor);
            painter.drawRoundedRect(QRectF(barBg.left(), barBg.top(), barBg.width() * fillRatio, barBg.height()), 2, 2);

            painter.setPen(QColor("#E2E8F0"));
            painter.drawText(QRectF(card.left() + 98, card.top() + 40, 148, 14),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString("⬆️%1t  ⬇️%2t  (%3t)")
                                 .arg(n.totalLoadedTons, 0, 'f', 0)
                                 .arg(n.totalUnloadedTons, 0, 'f', 0)
                                 .arg(n.currentStockTons, 0, 'f', 0));
        }
    }

    painter.restore();
}

void RailMapWidget::drawSiteMarkers(QPainter &painter)
{
    struct SitePin {
        QString title;
        double lat;
        double lon;
        QColor color;
    };

    const QVector<SitePin> pins = {
        {QStringLiteral("SAARSTAHL WERK VÖLKLINGEN"), 49.2465, 6.8555, QColor("#EF7D00")},
        {QStringLiteral("DILLINGER HÜTTE & ROGESA"),  49.3595, 6.7465, QColor("#F43F5E")},
        {QStringLiteral("SAARSTAHL WERK BURBACH"),    49.2402, 6.9480, QColor("#38BDF8")},
        {QStringLiteral("SAARSTAHL WERK NEUNKIRCHEN"),49.3445, 7.1665, QColor("#10B981")}
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (const SitePin &pin : pins) {
        const QPointF pt = geoToScreen(pin.lat, pin.lon);
        if (!rect().adjusted(-120, -60, 120, 60).contains(pt.toPoint())) continue;

        if (m_zoom <= 13.8) {
            painter.setBrush(QColor(pin.color.red(), pin.color.green(), pin.color.blue(), 60));
            painter.setPen(QPen(pin.color, 2.0));
            painter.drawEllipse(pt, 12.0, 12.0);

            QFont f = painter.font();
            f.setPointSize(8);
            f.setBold(true);
            painter.setFont(f);
            const QRectF box(pt.x() - 105, pt.y() - 34, 210, 20);
            painter.setBrush(QColor(15, 23, 42, 232));
            painter.setPen(QPen(pin.color, 1.5));
            painter.drawRoundedRect(box, 5, 5);
            painter.setPen(QColor("#FFFFFF"));
            painter.drawText(box, Qt::AlignCenter, pin.title);
        }
    }

    painter.restore();
}

void RailMapWidget::drawSingleWagon2D(QPainter &painter, const WagonData &wagon,
                                      bool isTrainSelected, bool isWagonSelected,
                                      bool isParkedWithoutLoco, double scalePxPerMeter)
{
    const QPointF screenPos = geoToScreen(wagon.currentLat, wagon.currentLon);

    const double minLenPx = m_exaggerateWagonSize ? 28.0 : 16.0;
    const double minWidPx = m_exaggerateWagonSize ? 13.0 : 8.0;
    const double lenPx = std::clamp(wagon.lengthMeters * scalePxPerMeter, minLenPx, 68.0);
    const double widPx = std::clamp(3.2 * scalePxPerMeter * 1.45, minWidPx, 25.0);

    const double hitRadius = std::max(lenPx * 0.6, 14.0);
    const_cast<WagonData&>(wagon).lastScreenBounds =
        QRectF(screenPos.x() - hitRadius, screenPos.y() - hitRadius, hitRadius * 2.0, hitRadius * 2.0);

    if (!rect().adjusted(-80, -80, 80, 80).contains(screenPos.toPoint())) {
        return;
    }

    painter.save();
    painter.translate(screenPos);
    painter.rotate(wagon.headingDeg - 90.0);

    const QColor bodyColor = colorForWagon(wagon.category, m_colorMode);

    // 1. Auswahl-Halo
    if (isWagonSelected) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#FBD50E"), 3.0));
        painter.drawRoundedRect(QRectF(-lenPx * 0.58, -widPx * 0.80, lenPx * 1.16, widPx * 1.60), 4, 4);
    } else if (isTrainSelected) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(251, 213, 14, 150), 1.6));
        painter.drawRoundedRect(QRectF(-lenPx * 0.54, -widPx * 0.70, lenPx * 1.08, widPx * 1.40), 3, 3);
    }

    // 2. Realistische Pufferbohlen (je 2 Seitenpuffer vorne und hinten) + Schraubenkupplung
    painter.setPen(QPen(QColor("#CBD5E1"), 2.0));
    painter.drawLine(QPointF(-lenPx * 0.58, 0.0), QPointF(lenPx * 0.58, 0.0));
    painter.setPen(QPen(QColor("#0F172A"), 1.8));
    painter.drawLine(QPointF(-lenPx * 0.55, -widPx * 0.32), QPointF(-lenPx * 0.50, -widPx * 0.32));
    painter.drawLine(QPointF(-lenPx * 0.55,  widPx * 0.32), QPointF(-lenPx * 0.50,  widPx * 0.32));
    painter.drawLine(QPointF( lenPx * 0.50, -widPx * 0.32), QPointF( lenPx * 0.55, -widPx * 0.32));
    painter.drawLine(QPointF( lenPx * 0.50,  widPx * 0.32), QPointF( lenPx * 0.55,  widPx * 0.32));

    // 3. DREHGESTELLE & RADSÄTZE (4-Achser = 2x2 Achsen vs. 6-Achser = 2x3 Achsen)
    painter.setPen(QPen(QColor("#0F172A"), 2.5));
    const double axleHalfSpan = widPx * 0.68;
    if (wagon.axleCount == 6) {
        const QVector<double> axleX = {
            -lenPx * 0.39, -lenPx * 0.31, -lenPx * 0.23,
             lenPx * 0.23,  lenPx * 0.31,  lenPx * 0.39
        };
        for (double ax : axleX) {
            painter.drawLine(QPointF(ax, -axleHalfSpan), QPointF(ax, axleHalfSpan));
        }
    } else {
        const QVector<double> axleX = {
            -lenPx * 0.36, -lenPx * 0.24,
             lenPx * 0.24,  lenPx * 0.36
        };
        for (double ax : axleX) {
            painter.drawLine(QPointF(ax, -axleHalfSpan), QPointF(ax, axleHalfSpan));
        }
    }

    // 4. Waggon-Hauptrahmen (Spezielle Torpedopfannenwagen-Geometrie vs. Flach-/Rungenwagen)
    const bool isTorpedoWagon = (wagon.category == WagonCategory::Heavy_6Axle) ||
                                (wagon.productType == CargoProductType::LiquidPigIron);
    if (isTorpedoWagon) {
        // Links & rechts: Die zwei schweren 3-achsigen Drehgestell-Tragbrücken des Torpedowagens
        painter.setBrush(bodyColor);
        painter.setPen(QPen(isParkedWithoutLoco ? QColor("#FDE047") : QColor("#0F172A"), 1.5));
        painter.drawRoundedRect(QRectF(-lenPx * 0.50, -widPx * 0.44, lenPx * 0.24, widPx * 0.88), 2.0, 2.0);
        painter.drawRoundedRect(QRectF( lenPx * 0.26, -widPx * 0.44, lenPx * 0.24, widPx * 0.88), 2.0, 2.0);

        // Dazwischen eingehängt: Spindelförmiger / zigarrenförmiger Torpedokessel (konisch zulaufend)
        QPolygonF torpedoVessel;
        torpedoVessel << QPointF(-lenPx * 0.34, -widPx * 0.18)
                      << QPointF(-lenPx * 0.16, -widPx * 0.58)
                      << QPointF( lenPx * 0.16, -widPx * 0.58)
                      << QPointF( lenPx * 0.34, -widPx * 0.18)
                      << QPointF( lenPx * 0.34,  widPx * 0.18)
                      << QPointF( lenPx * 0.16,  widPx * 0.58)
                      << QPointF(-lenPx * 0.16,  widPx * 0.58)
                      << QPointF(-lenPx * 0.34,  widPx * 0.18);

        const bool hasLiquidIron = (wagon.productType != CargoProductType::EmptyWagon);
        painter.setBrush(hasLiquidIron ? QColor("#CBD5E1") : QColor("#475569"));
        painter.setPen(QPen(hasLiquidIron ? QColor("#EA580C") : QColor("#0F172A"), 1.6));
        painter.drawPolygon(torpedoVessel);

        // Stahl-Versteifungsringe auf dem Torpedokessel
        painter.setPen(QPen(QColor("#1E293B"), 1.4));
        painter.drawLine(QPointF(-lenPx * 0.14, -widPx * 0.56), QPointF(-lenPx * 0.14, widPx * 0.56));
        painter.drawLine(QPointF( lenPx * 0.14, -widPx * 0.56), QPointF( lenPx * 0.14, widPx * 0.56));

        if (hasLiquidIron) {
            // Glühendes flüssiges Roheisen (1420 °C) in der Einfüll-/Abstichöffnung des Torpedowagens
            QRadialGradient glowGrad(QPointF(0.0, 0.0), widPx * 0.44);
            glowGrad.setColorAt(0.0, QColor("#FEF08A")); // Weißgelber 1420°C-Kern
            glowGrad.setColorAt(0.45, QColor("#F97316")); // Glühendes Flüssigeisen-Orange
            glowGrad.setColorAt(0.85, QColor("#DC2626")); // Rotglut
            glowGrad.setColorAt(1.0, QColor(220, 38, 38, 0));
            painter.setBrush(glowGrad);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QPointF(0.0, 0.0), widPx * 0.44, widPx * 0.44);

            painter.setBrush(QColor("#FEF08A"));
            painter.setPen(QPen(QColor("#EA580C"), 1.3));
            painter.drawEllipse(QPointF(0.0, 0.0), widPx * 0.23, widPx * 0.23);
        } else {
            // Leerer / abgekühlter Torpedowagen (nach dem Abstich im LD-Stahlwerk)
            painter.setBrush(QColor("#0F172A"));
            painter.setPen(QPen(QColor("#94A3B8"), 1.1));
            painter.drawEllipse(QPointF(0.0, 0.0), widPx * 0.20, widPx * 0.20);
        }
    } else {
        const QRectF bodyRect(-lenPx * 0.5, -widPx * 0.5, lenPx, widPx);
        painter.setBrush(bodyColor);
        painter.setPen(QPen(isParkedWithoutLoco ? QColor("#FDE047") : QColor("#0F172A"), 1.6));
        painter.drawRoundedRect(bodyRect, 2.5, 2.5);
    }

    // 5. Seitenrungen beim Rns-z (4-Achser Rungenwagen)
    if (wagon.category == WagonCategory::Rnsz_4Axle) {
        painter.setBrush(QColor("#FBD50E"));
        painter.setPen(Qt::NoPen);
        for (int r = -2; r <= 2; ++r) {
            const double rx = r * (lenPx * 0.18);
            painter.drawEllipse(QPointF(rx, -widPx * 0.45), 1.8, 1.8);
            painter.drawEllipse(QPointF(rx,  widPx * 0.45), 1.8, 1.8);
        }
    }

    // 6. REALITÄTSGETREUE DARSTELLUNG DES GELADENEN STAHLPRODUKTS AUF DEM WAGGON!
    if (!isTorpedoWagon) {
        switch (wagon.productType) {
        case CargoProductType::WireRodCoils: {
            // Aufgerollter Walzdraht (Drahtcoils): 4 konzentrische Draht-Spulen mit Innenloch
            const QVector<double> coilX = {-lenPx * 0.28, -lenPx * 0.09, lenPx * 0.09, lenPx * 0.28};
            const double rOuter = widPx * 0.28;
            const double rInner = widPx * 0.11;
            for (double cx : coilX) {
                painter.setBrush(QColor("#E2E8F0"));
                painter.setPen(QPen(QColor("#1E293B"), 1.2));
                painter.drawEllipse(QPointF(cx, 0.0), rOuter, rOuter);
                painter.setBrush(bodyColor.darker(130));
                painter.drawEllipse(QPointF(cx, 0.0), rInner, rInner);
            }
            break;
        }
        case CargoProductType::HeavyPlates: {
            // Dillinger Grobbleche: Gestapelte schwere Stahlblechtafeln + gelbe Zurrgurte
            painter.setBrush(QColor("#334155"));
            painter.setPen(QPen(QColor("#E2E8F0"), 1.0));
            painter.drawRect(QRectF(-lenPx * 0.40, -widPx * 0.32, lenPx * 0.80, widPx * 0.64));
            painter.setPen(QPen(QColor("#FBD50E"), 1.3));
            painter.drawLine(QPointF(-lenPx * 0.20, -widPx * 0.32), QPointF(-lenPx * 0.20, widPx * 0.32));
            painter.drawLine(QPointF( lenPx * 0.20, -widPx * 0.32), QPointF( lenPx * 0.20, widPx * 0.32));
            break;
        }
        case CargoProductType::SteelSlabs: {
            // Stahlbrammen (Slabs): 2 dicke, massive Brammenblöcke hintereinander
            painter.setBrush(QColor("#CBD5E1"));
            painter.setPen(QPen(QColor("#7C2D12"), 1.2));
            painter.drawRect(QRectF(-lenPx * 0.38, -widPx * 0.28, lenPx * 0.34, widPx * 0.56));
            painter.drawRect(QRectF( lenPx * 0.04, -widPx * 0.28, lenPx * 0.34, widPx * 0.56));
            break;
        }
        case CargoProductType::SteelBillets:
        case CargoProductType::BarSteelProfiles: {
            // Stahlknüppel / Stabstahl: 3 parallele Langstahl-Stangenbündel
            painter.setPen(QPen(QColor("#F8FAFC"), 1.6));
            painter.drawLine(QPointF(-lenPx * 0.38, -widPx * 0.20), QPointF(lenPx * 0.38, -widPx * 0.20));
            painter.drawLine(QPointF(-lenPx * 0.38,  0.0),          QPointF(lenPx * 0.38,  0.0));
            painter.drawLine(QPointF(-lenPx * 0.38,  widPx * 0.20), QPointF(lenPx * 0.38,  widPx * 0.20));
            break;
        }
        case CargoProductType::SteelBlooms:
        case CargoProductType::ForgedParts: {
            // Vorblöcke / Schmiedewellen: Massiver Schmiederohling in der Wagenmitte
            painter.setBrush(QColor("#E2E8F0"));
            painter.setPen(QPen(QColor("#0F172A"), 1.2));
            painter.drawRoundedRect(QRectF(-lenPx * 0.34, -widPx * 0.24, lenPx * 0.68, widPx * 0.48), 2, 2);
            break;
        }
        case CargoProductType::LiquidPigIron:
            break;
        case CargoProductType::EmptyWagon: {
            // Leerwagen (nach dem Abladen an der Senke): Sichtbarer dunkler Ladeboden mit Querträgern
            painter.setBrush(QColor(15, 23, 42, 165));
            painter.setPen(QPen(QColor("#94A3B8"), 1.0, Qt::DashLine));
            painter.drawRect(QRectF(-lenPx * 0.38, -widPx * 0.28, lenPx * 0.76, widPx * 0.56));
            break;
        }
        }
    }

    painter.restore();

    // 7. Positions-, Bauart- & Güter-Label über dem Waggon
    if (m_showWagonLabels && (m_zoom >= 14.0 || isTrainSelected || isWagonSelected)) {
        painter.save();
        QFont f = painter.font();
        f.setPointSize(7);
        f.setBold(true);
        painter.setFont(f);

        const QString label = (m_zoom >= 15.0 || isWagonSelected)
                              ? QString("#%1 %2:%3").arg(wagon.positionIndex)
                                    .arg(shortCodeForWagon(wagon.category), productShortName(wagon.productType))
                              : QString("#%1 (%2A)").arg(wagon.positionIndex).arg(wagon.axleCount);

        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(label) + 8;
        const QRectF tagRect(screenPos.x() - tw * 0.5, screenPos.y() - widPx * 0.9 - 15.0, tw, 14.0);

        painter.setBrush(isWagonSelected ? QColor("#FBD50E") : QColor(15, 23, 42, 225));
        painter.setPen(QPen(bodyColor, 1.2));
        painter.drawRoundedRect(tagRect, 3, 3);

        painter.setPen(isWagonSelected ? QColor("#0F172A") : QColor("#F8FAFC"));
        painter.drawText(tagRect, Qt::AlignCenter, label);
        painter.restore();
    }
}

void RailMapWidget::drawLocomotive2D(QPainter &painter, const TrainConsist &train,
                                     bool isSelected, double scalePxPerMeter)
{
    const QPointF screenPos = geoToScreen(train.locoLat, train.locoLon);

    const double minLenPx = m_exaggerateWagonSize ? 28.0 : 16.0;
    const double minWidPx = m_exaggerateWagonSize ? 14.0 : 8.5;
    const double lenPx = std::clamp(19.0 * scalePxPerMeter, minLenPx, 68.0);
    const double widPx = std::clamp(3.4 * scalePxPerMeter * 1.45, minWidPx, 26.0);

    const double hitR = std::max(lenPx * 0.65, 16.0);
    const_cast<TrainConsist&>(train).lastLocoScreenBounds =
        QRectF(screenPos.x() - hitR, screenPos.y() - hitR, hitR * 2.0, hitR * 2.0);

    if (!rect().adjusted(-100, -100, 100, 100).contains(screenPos.toPoint())) {
        return;
    }

    painter.save();
    painter.translate(screenPos);
    painter.rotate(train.locoHeadingDeg - 90.0);

    QPolygonF beam;
    beam << QPointF(lenPx * 0.5, -widPx * 0.25)
         << QPointF(lenPx * 1.35, -widPx * 0.85)
         << QPointF(lenPx * 1.35,  widPx * 0.85)
         << QPointF(lenPx * 0.5,  widPx * 0.25);
    painter.setBrush(QColor(251, 213, 14, 75));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(beam);

    if (isSelected) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#FBD50E"), 2.8));
        painter.drawRoundedRect(QRectF(-lenPx * 0.56, -widPx * 0.72, lenPx * 1.12, widPx * 1.44), 4, 4);
    }

    const QRectF locoRect(-lenPx * 0.5, -widPx * 0.5, lenPx, widPx);
    painter.setBrush(QColor("#334155"));
    painter.setPen(QPen(QColor("#FBD50E"), 2.0));
    painter.drawRoundedRect(locoRect, 3.5, 3.5);

    painter.setBrush(QColor("#FBD50E"));
    painter.setPen(Qt::NoPen);
    painter.drawRect(QRectF(lenPx * 0.15, -widPx * 0.42, lenPx * 0.26, widPx * 0.84));

    painter.restore();

    painter.save();
    QFont f = painter.font();
    f.setPointSize(8);
    f.setBold(true);
    painter.setFont(f);

    QString stateSuffix;
    if (train.opState == TrainOperationState::LoadingAtSource) {
        stateSuffix = QString(" | ⬆️ AUFNAHME %1%").arg(static_cast<int>(std::round(train.opProgress * 100.0)));
    } else if (train.opState == TrainOperationState::UnloadingAtSink) {
        stateSuffix = QString(" | ⬇️ ABLADUNG %1%").arg(static_cast<int>(std::round(train.opProgress * 100.0)));
    } else if (train.opState == TrainOperationState::MovingEmptyToSource) {
        stateSuffix = QStringLiteral(" | LEER -> QUELLE");
    }

    const QString tag = QString("🚂 %1 (%2 Wg.%3)").arg(train.id).arg(train.wagons.size()).arg(stateSuffix);
    const QFontMetrics fm(f);
    const int tw = fm.horizontalAdvance(tag) + 12;
    const QRectF tagRect(screenPos.x() - tw * 0.5, screenPos.y() - widPx - 24.0, tw, 18.0);

    QColor badgeBg = isSelected ? QColor("#EF7D00") : QColor(30, 41, 59, 235);
    if (train.opState == TrainOperationState::LoadingAtSource) {
        badgeBg = QColor(21, 128, 61, 240); // Grün: Güter-Aufnahme an Quelle
    } else if (train.opState == TrainOperationState::UnloadingAtSink) {
        badgeBg = QColor(3, 105, 161, 240); // Blau: Güter-Abladung an Senke
    }

    painter.setBrush(badgeBg);
    painter.setPen(QPen(QColor("#FBD50E"), isSelected ? 2.0 : 1.3));
    painter.drawRoundedRect(tagRect, 4, 4);
    painter.setPen(QColor("#FFFFFF"));
    painter.drawText(tagRect, Qt::AlignCenter, tag);

    // Falls der Zug gerade an einer Quelle aufnimmt oder an einer Senke ablädt: Fortschrittsbalken zeichnen
    if (train.opState == TrainOperationState::LoadingAtSource ||
        train.opState == TrainOperationState::UnloadingAtSink) {
        const QRectF pBg(tagRect.left() + 4, tagRect.bottom() + 2, tagRect.width() - 8, 5);
        painter.setBrush(QColor("#0F172A"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(pBg, 2, 2);
        painter.setBrush(train.opState == TrainOperationState::LoadingAtSource ? QColor("#4ADE80") : QColor("#38BDF8"));
        painter.drawRoundedRect(QRectF(pBg.left(), pBg.top(), pBg.width() * std::clamp(train.opProgress, 0.0, 1.0), pBg.height()), 2, 2);
    }

    painter.restore();
}

void RailMapWidget::drawParkedConsistMarker2D(QPainter &painter, const TrainConsist &train, bool isSelected)
{
    const QPointF screenPos = geoToScreen(train.locoLat, train.locoLon);
    const_cast<TrainConsist&>(train).lastLocoScreenBounds =
        QRectF(screenPos.x() - 20.0, screenPos.y() - 20.0, 40.0, 40.0);

    if (!rect().adjusted(-100, -100, 100, 100).contains(screenPos.toPoint())) {
        return;
    }

    painter.save();
    // Gelber Hemmschuh / Park-Marker am Kopf der abgestellten Waggongruppe
    painter.setBrush(QColor("#9333EA"));
    painter.setPen(QPen(QColor("#FDE047"), 2.2));
    painter.drawRoundedRect(QRectF(screenPos.x() - 9, screenPos.y() - 9, 18, 18), 4, 4);

    QFont f = painter.font();
    f.setPointSize(8);
    f.setBold(true);
    painter.setFont(f);
    const QString tag = QString("🅿️ %1 (KEINE LOK – GELAGERT)").arg(train.id);
    const QFontMetrics fm(f);
    const int tw = fm.horizontalAdvance(tag) + 12;
    const QRectF tagRect(screenPos.x() - tw * 0.5, screenPos.y() - 32.0, tw, 18.0);

    painter.setBrush(isSelected ? QColor("#9333EA") : QColor(88, 28, 135, 235));
    painter.setPen(QPen(QColor("#FDE047"), isSelected ? 2.0 : 1.4));
    painter.drawRoundedRect(tagRect, 4, 4);
    painter.setPen(QColor("#FEF08A"));
    painter.drawText(tagRect, Qt::AlignCenter, tag);
    painter.restore();
}

void RailMapWidget::drawTrainsAndWagons(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double mpp = std::max(0.05, metersPerPixel());
    const double scalePxPerMeter = 1.0 / mpp;

    for (int tIdx = 0; tIdx < m_trains.size(); ++tIdx) {
        const TrainConsist &tr = m_trains[tIdx];
        const bool trainSelected = (tIdx == m_selectedTrainIdx);

        QPainterPath couplerPath;
        couplerPath.moveTo(geoToScreen(tr.locoLat, tr.locoLon));
        for (const WagonData &w : tr.wagons) {
            couplerPath.lineTo(geoToScreen(w.currentLat, w.currentLon));
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(trainSelected ? QColor("#FBD50E") : QColor("#E2E8F0"), 2.4));
        painter.drawPath(couplerPath);

        for (int wIdx = tr.wagons.size() - 1; wIdx >= 0; --wIdx) {
            const bool wagonSelected = trainSelected && (wIdx == m_selectedWagonIdx);
            drawSingleWagon2D(painter, tr.wagons[wIdx], trainSelected, wagonSelected,
                              !tr.hasLocomotive, scalePxPerMeter);
        }

        if (tr.hasLocomotive) {
            drawLocomotive2D(painter, tr, trainSelected, scalePxPerMeter);
        } else {
            drawParkedConsistMarker2D(painter, tr, trainSelected);
        }
    }

    painter.restore();
}

void RailMapWidget::drawHudOverlay(QPainter &painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF topHud(14, 14, 465, 86);
    painter.setBrush(QColor(30, 41, 59, 232));
    painter.setPen(QPen(QColor("#5C839C"), 1.5));
    painter.drawRoundedRect(topHud, 6, 6);

    painter.setBrush(QColor("#EF7D00"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(14, 14, 6, 86), 3, 3);

    QFont titleFont = painter.font();
    titleFont.setPointSize(9);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor("#FBD50E"));
    painter.drawText(QRectF(28, 18, 440, 20), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("📍 ") + m_activeSiteName);

    QFont subFont = painter.font();
    subFont.setPointSize(8);
    subFont.setBold(false);
    painter.setFont(subFont);
    painter.setPen(QColor("#E2E8F0"));
    painter.drawText(QRectF(28, 40, 440, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QString("Lokomotiv-Pool: %1/%2 im Einsatz (%3 frei)  |  Gelagerte Waggongruppen ohne Lok: %4")
                         .arg(usedLocomotivesCount())
                         .arg(m_totalLocosInPool)
                         .arg(freeLocomotivesCount())
                         .arg(std::max(0, static_cast<int>(m_trains.size()) - usedLocomotivesCount())));
    painter.setPen(QColor("#94A3B8"));
    painter.drawText(QRectF(28, 60, 440, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QString("GPS: %1° N, %2° E | Zoom: %3x | Quellen & Senken aktiv: %4")
                         .arg(m_centerLat, 0, 'f', 4).arg(m_centerLon, 0, 'f', 4)
                         .arg(m_zoom, 0, 'f', 1).arg(m_sourceSinkNodes.size()));
    painter.drawText(QRectF(28, 78, 440, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Mausrad: Zoom | Ziehen: Karte bewegen | Klick auf Waggon: Güter & Achsen prüfen"));

    // Unten-Links: Farblegende
    const QRectF legendRect(14, height() - 126, 375, 112);
    painter.setBrush(QColor(30, 41, 59, 232));
    painter.setPen(QPen(QColor("#5C839C"), 1.5));
    painter.drawRoundedRect(legendRect, 6, 6);

    titleFont.setPointSize(9);
    painter.setFont(titleFont);
    painter.setPen(QColor("#FBD50E"));
    painter.drawText(QRectF(26, height() - 120, 350, 18),
                     QStringLiteral("Waggon- & Güter-Legende (Bauart & Achsen):"));

    struct LegendEntry { WagonCategory cat; QString text; };
    const QVector<LegendEntry> entries = {
        {WagonCategory::Samms_6Axle,    QStringLiteral("Samms [6-Achser]: Knüppel, Brammen, Vorblöcke, Schmiede")},
        {WagonCategory::Rnsz_4Axle,     QStringLiteral("Rns-z [4-Achser]: Dillinger Grobbleche, Stabstahl, Profile")},
        {WagonCategory::Standard_4Axle, QStringLiteral("4-Achser Shimmns: Aufgerollter Walzdraht (Drahtcoils)")},
        {WagonCategory::Heavy_6Axle,    QStringLiteral("6-Achser Torpedo/Faals: Flüssigroheisen (1420°C) & Erz")}
    };

    subFont.setPointSize(8);
    painter.setFont(subFont);
    for (int i = 0; i < entries.size(); ++i) {
        const double y = height() - 96 + i * 21.0;
        const QColor c = colorForWagon(entries[i].cat, m_colorMode);
        painter.setBrush(c);
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawRoundedRect(QRectF(26, y, 22, 13), 2, 2);
        painter.setPen(QColor("#F8FAFC"));
        painter.drawText(QRectF(56, y - 2, 325, 17), Qt::AlignLeft | Qt::AlignVCenter, entries[i].text);
    }

    // Oben-Rechts: Details zum aktuell ausgewählten Zug / Lagerverband
    if (m_selectedTrainIdx >= 0 && m_selectedTrainIdx < m_trains.size()) {
        const TrainConsist &tr = m_trains[m_selectedTrainIdx];
        const QRectF selBox(width() - 355, 14, 341, 124);
        painter.setBrush(QColor(30, 41, 59, 235));
        painter.setPen(QPen(tr.hasLocomotive ? QColor("#FBD50E") : QColor("#C084FC"), 1.6));
        painter.drawRoundedRect(selBox, 6, 6);

        titleFont.setPointSize(9);
        painter.setFont(titleFont);
        painter.setPen(tr.hasLocomotive ? QColor("#FBD50E") : QColor("#E9D5FF"));
        painter.drawText(QRectF(width() - 343, 20, 318, 20),
                         QString("Verband: %1 (%2 Wg. | %3)")
                             .arg(tr.id).arg(tr.wagons.size())
                             .arg(tr.hasLocomotive ? QStringLiteral("IN FAHRT") : QStringLiteral("GELAGERT OHNE LOK")));

        subFont.setPointSize(8);
        painter.setFont(subFont);
        painter.setPen(QColor("#86EFAC"));
        painter.drawText(QRectF(width() - 343, 42, 318, 18),
                         operationStateText(tr));
        painter.setPen(QColor("#E2E8F0"));
        painter.drawText(QRectF(width() - 343, 60, 318, 18),
                         QString("⬆️ Quelle: %1  ->  ⬇️ Senke: %2 (Zyklen: %3)")
                             .arg(tr.sourceNodeId, tr.sinkNodeId).arg(tr.completedDeliveries));

        if (m_selectedWagonIdx >= 0 && m_selectedWagonIdx < tr.wagons.size()) {
            const WagonData &w = tr.wagons[m_selectedWagonIdx];
            painter.setPen(colorForWagon(w.category, m_colorMode).lighter(135));
            painter.drawText(QRectF(width() - 343, 80, 318, 18),
                             QString("Waggon #%1: %2 | UIC %3")
                                 .arg(w.positionIndex).arg(shortCodeForWagon(w.category), w.uicNumber));
            painter.setPen(QColor("#FDE047"));
            painter.drawText(QRectF(width() - 343, 98, 318, 18),
                             QString("Gut: %1 (%2 t)").arg(productName(w.productType)).arg(w.weightTons, 0, 'f', 1));
        } else {
            painter.setPen(QColor("#94A3B8"));
            painter.drawText(QRectF(width() - 343, 86, 318, 18),
                             QStringLiteral("Klick auf einen Waggon für Einzel- & Güter-Details"));
        }
    }

    painter.restore();
}

void RailMapWidget::drawMiniOverviewMap(QPainter &painter)
{
    const QRectF miniRect(width() - 224, height() - 146, 210, 132);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setBrush(QColor(15, 23, 42, 235));
    painter.setPen(QPen(QColor("#5C839C"), 1.5));
    painter.drawRoundedRect(miniRect, 6, 6);

    const double minLat = 49.215, maxLat = 49.380;
    const double minLon = 6.700,  maxLon = 7.200;

    auto toMini = [&](double lat, double lon) -> QPointF {
        const double nx = (lon - minLon) / (maxLon - minLon);
        const double ny = 1.0 - (lat - minLat) / (maxLat - minLat);
        return QPointF(miniRect.left() + 10 + nx * (miniRect.width() - 20),
                       miniRect.top() + 22 + ny * (miniRect.height() - 30));
    };

    QFont f = painter.font();
    f.setPointSize(7);
    f.setBold(true);
    painter.setFont(f);
    painter.setPen(QColor("#FBD50E"));
    painter.drawText(QRectF(miniRect.left() + 8, miniRect.top() + 4, miniRect.width() - 16, 14),
                     QStringLiteral("Saarland-Netz (Quellen & Senken)"));

    painter.setBrush(Qt::NoBrush);
    for (const RailTrack &tr : m_tracks) {
        if (tr.waypoints.size() < 2) continue;
        QPainterPath p;
        p.moveTo(toMini(tr.waypoints[0].lat, tr.waypoints[0].lon));
        for (int i = 1; i < tr.waypoints.size(); ++i) {
            p.lineTo(toMini(tr.waypoints[i].lat, tr.waypoints[i].lon));
        }
        painter.setPen(QPen(tr.trackColor, 1.5));
        painter.drawPath(p);
    }

    struct MiniSite { QString code; double lat; double lon; QColor col; };
    const QVector<MiniSite> sites = {
        {"DL", 49.360, 6.745, QColor("#F43F5E")},
        {"VK", 49.246, 6.855, QColor("#EF7D00")},
        {"BB", 49.240, 6.948, QColor("#38BDF8")},
        {"NK", 49.344, 7.167, QColor("#10B981")}
    };
    for (const MiniSite &s : sites) {
        const QPointF pt = toMini(s.lat, s.lon);
        painter.setBrush(s.col);
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawEllipse(pt, 4.0, 4.0);
        painter.setPen(Qt::white);
        painter.drawText(pt + QPointF(5, 3), s.code);
    }

    for (const TrainConsist &tr : m_trains) {
        painter.setBrush(tr.hasLocomotive ? QColor("#FBD50E") : QColor("#C084FC"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(toMini(tr.locoLat, tr.locoLon), 2.6, 2.6);
    }

    const QPointF camPt = toMini(std::clamp(m_centerLat, minLat, maxLat),
                                 std::clamp(m_centerLon, minLon, maxLon));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#FBD50E"), 1.4));
    painter.drawRect(QRectF(camPt.x() - 7, camPt.y() - 5, 14, 10));

    painter.restore();
}

void RailMapWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    drawSchematicBackground(painter);
    drawMapTiles(painter);
    drawRiversAndRoads(painter);
    drawPlantAreasAndBuildings(painter);
    drawRailNetwork(painter);
    drawSourcesAndSinks(painter);
    drawSiteMarkers(painter);
    drawTrainsAndWagons(painter);
    drawHudOverlay(painter);
    drawMiniOverviewMap(painter);
}

void RailMapWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const QPointF clickPt = event->position();

        const QRectF miniRect(width() - 224, height() - 146, 210, 132);
        if (miniRect.contains(clickPt)) {
            const double minLat = 49.215, maxLat = 49.380;
            const double minLon = 6.700,  maxLon = 7.200;
            const double nx = std::clamp((clickPt.x() - (miniRect.left() + 10)) / (miniRect.width() - 20), 0.0, 1.0);
            const double ny = std::clamp((clickPt.y() - (miniRect.top() + 22)) / (miniRect.height() - 30), 0.0, 1.0);
            m_centerLon = minLon + nx * (maxLon - minLon);
            m_centerLat = minLat + (1.0 - ny) * (maxLat - minLat);
            updateAllTrainGeometries();
            update();
            return;
        }

        for (int tIdx = 0; tIdx < m_trains.size(); ++tIdx) {
            const TrainConsist &tr = m_trains[tIdx];
            if (tr.lastLocoScreenBounds.contains(clickPt)) {
                selectTrain(tIdx, -1, false);
                emit statusMessage(QStringLiteral("Verband %1 (%2) ausgewählt.").arg(tr.id, tr.name));
                return;
            }
            for (int wIdx = 0; wIdx < tr.wagons.size(); ++wIdx) {
                if (tr.wagons[wIdx].lastScreenBounds.contains(clickPt)) {
                    selectTrain(tIdx, wIdx, false);
                    const WagonData &w = tr.wagons[wIdx];
                    emit statusMessage(
                        QStringLiteral("Waggon #%1 [%2 | Gut: %3 (%4 t)] von %5 ausgewählt.")
                            .arg(w.positionIndex)
                            .arg(shortCodeForWagon(w.category), productShortName(w.productType))
                            .arg(w.weightTons, 0, 'f', 1)
                            .arg(tr.id));
                    return;
                }
            }
        }

        m_isDragging = true;
        m_dragStartPos = event->pos();
        m_dragStartLat = m_centerLat;
        m_dragStartLon = m_centerLon;
        setCursor(Qt::ClosedHandCursor);
    }
}

void RailMapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        const QPoint delta = event->pos() - m_dragStartPos;
        const double worldSize = 256.0 * std::pow(2.0, m_zoom);
        const double startX = lonToMercX(m_dragStartLon) * worldSize;
        const double startY = latToMercY(m_dragStartLat) * worldSize;
        m_centerLon = mercXToLon((startX - delta.x()) / worldSize);
        m_centerLat = std::clamp(mercYToLat((startY - delta.y()) / worldSize), 48.9, 49.7);
        updateAllTrainGeometries();
        update();
    }
}

void RailMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void RailMapWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QPointF clickPt = event->position();
    for (int tIdx = 0; tIdx < m_trains.size(); ++tIdx) {
        const TrainConsist &tr = m_trains[tIdx];
        for (int wIdx = 0; wIdx < tr.wagons.size(); ++wIdx) {
            if (tr.wagons[wIdx].lastScreenBounds.contains(clickPt)) {
                zoomToWagon(tIdx, wIdx);
                return;
            }
        }
    }

    const GeoPoint gp = screenToGeo(clickPt);
    m_centerLat = gp.lat;
    m_centerLon = gp.lon;
    setZoomLevel(m_zoom + 0.8);
}

void RailMapWidget::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) > 0.01) {
        setZoomLevel(m_zoom + steps * 0.45);
    }
    event->accept();
}
