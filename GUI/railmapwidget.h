#ifndef RAILMAPWIDGET_H
#define RAILMAPWIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QHash>
#include <QPixmap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>

enum class WagonCategory {
    Samms_6Axle = 0,    // Samms 489/709 (6-Achser Schwerlast-Flachwagen)
    Rnsz_4Axle = 1,     // Rns-z 655 (4-Achser Drehgestell-Rungenwagen)
    Standard_4Axle = 2, // Shimmns / Sahimms / Eaos (4-Achser Coil-/Haubenwagen)
    Heavy_6Axle = 3     // Torpedopfannenwagen / Faals (6-Achser Spezialwagen)
};

enum class WagonColorMode {
    ByTypeAndAxles = 0,       // Alle 4 Kategorien farblich getrennt (Standard)
    HighlightSammsVsRnsz = 1, // Fokus: Samms vs. Rns-z
    StrictByAxleCount = 2     // Fokus: 4-Achser vs. 6-Achser
};

enum class MapBackgroundMode {
    HybridSimplifiedAndGoogle = 0, // Google Maps + Vereinfachte Werksdarstellung
    SimplifiedSchematicOnly = 1,   // Reine vereinfachte Werks- & Gleisdarstellung
    GoogleStreetsAndRails = 2,     // Google Maps Straßenkarte + Gleise
    GoogleSatelliteAndRails = 3    // Google Maps Satellit + Gleise
};

// Reale Stahlprodukte von Saarstahl AG & Dillinger Hütte
enum class CargoProductType {
    SteelBillets = 0,     // Stahlknüppel (180x180 mm, Völklingen -> Burbach / Nauweiler Gewann)
    HeavyPlates = 1,      // Dillinger Grobbleche (Dillinger Hütte -> Versand / Völklingen)
    SteelSlabs = 2,       // Stahlbrammen (Dillingen Strangguss -> Grobblechwalzwerk / Völklingen)
    WireRodCoils = 3,     // Aufgerollter Walzdraht / Drahtcoils (Burbach / Völklingen / Neunkirchen)
    SteelBlooms = 4,      // Vorblöcke (Völklingen Strangguss -> Neunkirchen / Schmiede)
    BarSteelProfiles = 5, // Stabstahl & Edelbaustahl-Profile (Neunkirchen / Völklingen)
    LiquidPigIron = 6,    // Flüssigroheisen 1420°C (ROGESA Hochöfen Dillingen -> LD-Stahlwerk Völklingen)
    ForgedParts = 7,      // Freiform-Schmiedestücke / Turbinenwellen (Saarschmiede Völklingen)
    EmptyWagon = 8        // Leerwagen (unbeladen)
};

enum class CargoOrderStatus {
    AtSourceStorage = 0,      // In Quelle produziert (Lagerbestand)
    LoadingAtSource = 1,      // Wird gerade an der Quelle auf Waggons aufgenommen (beladen)
    StoredOnParkedWagons = 2, // Auf Waggons verladen, aber ABGESTELLT (wartet auf freie Lok!)
    InTransit = 3,            // Mit Lokomotive unterwegs von Quelle zu Senke
    UnloadingAtSink = 4,      // Wird gerade an der Senke von den Waggons abgeladen (entladen)
    DeliveredToSink = 5       // In Ziel-Senke abgeladen & vereinnahmt
};

enum class TrainOperationState {
    MovingLoadedToSink = 0,   // Vollzug fährt beladen von Quelle -> Senke
    UnloadingAtSink = 1,      // Zug hält an Senke: Güter werden abgeladen (Waggons -> Leerwagen)
    MovingEmptyToSource = 2,  // Leerzug fährt zurück von Senke -> Quelle
    LoadingAtSource = 3,      // Zug hält an Quelle: Güter werden aufgenommen (Leerwagen -> Beladen)
    ParkedWithoutLoco = 4     // Abgestellte Waggongruppe ohne Lokomotive
};

struct GeoPoint {
    double lat = 49.2460;
    double lon = 6.8540;
};

struct PlantBuilding {
    QString name;
    QString subtitle;
    QString siteId;
    QVector<GeoPoint> footprint;
    QColor fillColor;
    QColor borderColor;
};

// Reale Quellen (Güter-Aufnahme) & Senken (Güter-Abladung)
struct SourceSinkNode {
    QString id;
    QString name;
    QString siteId;   // "Voelklingen", "Dillingen", "Burbach", "Neunkirchen"
    QString role;     // "Quelle", "Senke", "Quelle & Senke", "Puffer-Abstellgleis"
    QString trackId;  // Zugeordnetes Gleis
    double trackDistMeters = 500.0;
    double lat = 49.2460;
    double lon = 6.8540;
    QString producedGoods; // Güter, die hier an der Quelle AUFGENOMMEN werden
    QString consumedGoods; // Güter, die hier an der Senke ABGELADEN werden
    double currentStockTons = 350.0;
    double maxCapacityTons = 1200.0;
    double totalLoadedTons = 0.0;   // Kumulierte Güter-Aufnahme (Beladung auf Waggons)
    double totalUnloadedTons = 0.0; // Kumulierte Güter-Abladung (Entladung von Waggons)
    QColor badgeColor;
};

// Verwaltbares Güter-Objekt (Charge / Transportauftrag zwischen Quelle und Senke)
struct CargoOrder {
    QString orderId;
    CargoProductType product = CargoProductType::SteelBillets;
    double weightTons = 360.0;
    QString sourceNodeId;
    QString sinkNodeId;
    QString assignedConsistId;
    int wagonCountUsed = 4;
    WagonCategory preferredWagon = WagonCategory::Samms_6Axle;
    CargoOrderStatus status = CargoOrderStatus::InTransit;
};

struct SimplifiedRoad {
    QString name;
    QString roadClass; // "highway", "primary", "plant", "river"
    QVector<GeoPoint> path;
};

struct RailTrack {
    QString id;
    QString name;
    QString siteId; // "Voelklingen", "Dillingen", "Burbach", "Neunkirchen", "Corridor"
    bool isLoop = false;
    bool isMainline = false;
    bool isStorageSiding = false; // Abstell-/Puffergleis für Waggons ohne Lok
    QColor trackColor;
    QVector<GeoPoint> waypoints;
    QVector<double> cumulativeMeters;
    double totalLengthMeters = 0.0;
};

struct WagonData {
    int positionIndex = 1;
    QString uicNumber;
    WagonCategory category = WagonCategory::Samms_6Axle;
    CargoProductType productType = CargoProductType::SteelBillets;         // Aktuell geladenes Gut (oder EmptyWagon nach Abladung)
    CargoProductType assignedProductType = CargoProductType::SteelBillets; // Soll-Gut bei Aufnahme an der Quelle
    QString cargoOrderId;
    int axleCount = 6;
    double lengthMeters = 16.4;
    QString cargoDescription;
    double weightTons = 85.0;         // Aktuelles Ladungsgewicht (0 t wenn an Senke abgeladen)
    double assignedWeightTons = 85.0; // Soll-Ladungsgewicht bei Aufnahme an der Quelle
    double currentLat = 49.2460;
    double currentLon = 6.8540;
    double headingDeg = 0.0;
    QRectF lastScreenBounds;
};

struct TrainConsist {
    QString id;
    QString name;
    QString locoName;
    bool hasLocomotive = true; // false = Abgestellte Waggongruppe (wartet auf freie Lok!)
    QString sourceNodeId;      // Quelle (wo Güter aufgenommen werden)
    QString sinkNodeId;        // Senke (wo Güter abgeladen werden)
    double sourceTrackDistMeters = 220.0; // Gleiskilometer der Güter-Aufnahme (Quelle)
    double sinkTrackDistMeters = 2100.0;  // Gleiskilometer der Güter-Abladung (Senke)
    TrainOperationState opState = TrainOperationState::MovingLoadedToSink;
    double opProgress = 0.0;   // 0.0..1.0 Fortschritt beim Aufnehmen (Quelle) oder Abladen (Senke)
    int completedDeliveries = 0;
    QString trackId;
    double headDistanceMeters = 0.0;
    double speedKmh = 30.0;
    int direction = 1; // +1 or -1
    bool active = true;
    double locoLat = 49.2460;
    double locoLon = 6.8540;
    double locoHeadingDeg = 0.0;
    QRectF lastLocoScreenBounds;
    QVector<WagonData> wagons;
};

class RailMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RailMapWidget(QWidget *parent = nullptr);
    ~RailMapWidget() override;

    // Standort-Zoom / Kamera
    void focusSite(const QString &siteName, bool animate = true);
    void zoomIn();
    void zoomOut();
    void setZoomLevel(double zoom);
    double zoomLevel() const { return m_zoom; }

    // Darstellungs-Optionen
    void setMapBackgroundMode(MapBackgroundMode mode);
    MapBackgroundMode mapBackgroundMode() const { return m_bgMode; }

    void setWagonColorMode(WagonColorMode mode);
    WagonColorMode wagonColorMode() const { return m_colorMode; }

    void setShowSimplifiedPlant(bool show);
    void setShowRoadsAndRivers(bool show);
    void setShowTrackLabels(bool show);
    void setShowWagonLabels(bool show);
    void setShowSourcesAndSinks(bool show);
    void setExaggerateWagonSize(bool exaggerate);

    // Simulation Steuerung
    void setSimulationRunning(bool running);
    bool isSimulationRunning() const { return m_simRunning; }
    void setSimulationSpeedMultiplier(double factor);
    double simulationSpeedMultiplier() const { return m_simSpeedFactor; }
    void stepSimulation(double dtSeconds);

    // Zug-, Waggon-, Quellen/Senken- & Güter-Zugriff
    const QVector<TrainConsist>& trains() const { return m_trains; }
    QVector<TrainConsist>& trainsMutable() { return m_trains; }
    const QVector<RailTrack>& tracks() const { return m_tracks; }
    const QVector<SourceSinkNode>& sourceSinkNodes() const { return m_sourceSinkNodes; }
    const QVector<CargoOrder>& cargoOrders() const { return m_cargoOrders; }

    // Lokomotiv-Pool & Engpass-Steuerung
    int totalLocomotivesInPool() const { return m_totalLocosInPool; }
    int usedLocomotivesCount() const;
    int freeLocomotivesCount() const;
    void setTotalLocomotivesInPool(int totalLocos);
    bool coupleFreeLocoToConsist(int trainIdx);
    void uncoupleLocoFromConsist(int trainIdx);

    // Güter-Objekte (Cargo Orders) erzeugen & auf Waggons verteilen
    QString createAndDistributeCargoOrder(CargoProductType product,
                                          double totalTons,
                                          const QString &sourceId,
                                          const QString &sinkId,
                                          WagonCategory preferredWagon);

    // Güter gezielt an der Quelle aufnehmen (beladen) oder an der Senke abladen (entladen)
    void loadTrainAtSourceNow(int trainIdx);
    void unloadTrainAtSinkNow(int trainIdx);
    static QString operationStateText(const TrainConsist &train);

    int selectedTrainIndex() const { return m_selectedTrainIdx; }
    int selectedWagonIndex() const { return m_selectedWagonIdx; }
    void selectTrain(int trainIdx, int wagonIdx = -1, bool centerCamera = false);

    // Züge verlängern / verkürzen & bearbeiten
    void appendWagonToTrain(int trainIdx, WagonCategory category,
                            CargoProductType product = CargoProductType::SteelBillets);
    void removeLastWagonFromTrain(int trainIdx);
    void setTrainWagonCount(int trainIdx, int targetCount, WagonCategory defaultCategoryForNew);
    void setWagonCategoryAt(int trainIdx, int wagonIdx, WagonCategory category);
    void setWagonProductAt(int trainIdx, int wagonIdx, CargoProductType product);
    void reverseTrainDirection(int trainIdx);
    void assignTrainToTrack(int trainIdx, const QString &trackId);
    void zoomToWagon(int trainIdx, int wagonIdx);
    void zoomToSourceSinkNode(int nodeIdx);

    // Bahn-API & Zufalls-Testpositionierung
    void randomizePositionsOnTracks(bool voelklingenOnly = false);
    QString applySimulatedDbApiPositioning(const QString &endpointUrl, const QString &apiKey);
    void triggerLiveNetworkApiQuery(const QString &endpointUrl, const QString &clientId, const QString &apiKey);

    // Hilfsfunktionen für Waggons & Güter
    static QColor colorForWagon(WagonCategory cat, WagonColorMode mode);
    static QString typeNameForWagon(WagonCategory cat);
    static QString shortCodeForWagon(WagonCategory cat);
    static int axleCountForCategory(WagonCategory cat);
    static double lengthForCategory(WagonCategory cat);
    static double maxPayloadTonsForCategory(WagonCategory cat);

    static QString productName(CargoProductType prod);
    static QString productShortName(CargoProductType prod);
    static WagonCategory recommendedWagonForProduct(CargoProductType prod);
    static QString defaultSourceForProduct(CargoProductType prod);
    static QString defaultSinkForProduct(CargoProductType prod);

signals:
    void selectionChanged(int trainIndex, int wagonIndex);
    void trainsUpdated();
    void cargoAndNodesUpdated();
    void simulationStepped();
    void statusMessage(const QString &message);
    void apiLogMessage(const QString &logText);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onSimTimerTick();
    void onCameraAnimTick();
    void onTileReplyFinished(QNetworkReply *reply);

private:
    void initGeographyAndTracks();
    void initSourcesAndSinks();
    void initDefaultTrainsAndCargo();
    void computeTrackMetrics(RailTrack &track);
    void updateAllTrainGeometries();
    void sampleTrackAtDistance(const RailTrack &track, double distMeters, double &outLat, double &outLon, double &outHeadingDeg) const;
    double findNearestTrackDistance(const RailTrack &track, double lat, double lon) const;

    // Projektionsfunktionen (WGS84 <-> Screen Pixel)
    QPointF geoToScreen(double lat, double lon) const;
    GeoPoint screenToGeo(const QPointF &pt) const;
    double metersPerPixel() const;

    // Tile-Lader (Google Maps / OSM Fallback)
    void drawMapTiles(QPainter &painter);
    void requestTile(int z, int x, int y, const QString &lyrs);
    QString tileCachePath(int z, int x, int y, const QString &lyrs) const;

    // Zeichenroutinen
    void drawSchematicBackground(QPainter &painter);
    void drawRiversAndRoads(QPainter &painter);
    void drawPlantAreasAndBuildings(QPainter &painter);
    void drawSourcesAndSinks(QPainter &painter);
    void drawSiteMarkers(QPainter &painter);
    void drawRailNetwork(QPainter &painter);
    void drawTrainsAndWagons(QPainter &painter);
    void drawSingleWagon2D(QPainter &painter, const WagonData &wagon, bool isTrainSelected, bool isWagonSelected, bool isParkedWithoutLoco, double scalePxPerMeter);
    void drawLocomotive2D(QPainter &painter, const TrainConsist &train, bool isSelected, double scalePxPerMeter);
    void drawParkedConsistMarker2D(QPainter &painter, const TrainConsist &train, bool isSelected);
    void drawHudOverlay(QPainter &painter);
    void drawMiniOverviewMap(QPainter &painter);

    // Kamera-Status (Zentriert auf das echte Saarstahl-Werksgelände Völklingen)
    double m_centerLat = 49.2465;
    double m_centerLon = 6.8555;
    double m_zoom = 15.2;

    // Kamera-Animation (Fly-To)
    QTimer *m_cameraAnimTimer = nullptr;
    double m_animStartLat = 49.2465;
    double m_animStartLon = 6.8555;
    double m_animStartZoom = 15.2;
    double m_animTargetLat = 49.2465;
    double m_animTargetLon = 6.8555;
    double m_animTargetZoom = 15.2;
    double m_animProgress = 1.0;
    QString m_activeSiteName = "Werk Völklingen (Saarstahl AG)";

    // Maus-Interaktion
    bool m_isDragging = false;
    QPoint m_dragStartPos;
    double m_dragStartLat = 0.0;
    double m_dragStartLon = 0.0;

    // Darstellung
    MapBackgroundMode m_bgMode = MapBackgroundMode::HybridSimplifiedAndGoogle;
    WagonColorMode m_colorMode = WagonColorMode::ByTypeAndAxles;
    bool m_showSimplifiedPlant = true;
    bool m_showRoadsAndRivers = true;
    bool m_showTrackLabels = true;
    bool m_showWagonLabels = true;
    bool m_showSourcesAndSinks = true;
    bool m_exaggerateWagonSize = true;

    // Simulation
    QTimer *m_simTimer = nullptr;
    bool m_simRunning = true;
    double m_simSpeedFactor = 2.5;

    // Lokomotiv-Pool (begrenzte Anzahl an Lokomotiven -> führt zu Waggon-Lagerung bei Engpass)
    int m_totalLocosInPool = 6;

    // Geometrie-, Quellen/Senken- & Güterdaten
    QVector<SimplifiedRoad> m_roads;
    QVector<PlantBuilding> m_buildings;
    QVector<RailTrack> m_tracks;
    QVector<SourceSinkNode> m_sourceSinkNodes;
    QVector<CargoOrder> m_cargoOrders;
    QVector<TrainConsist> m_trains;

    int m_selectedTrainIdx = 0;
    int m_selectedWagonIdx = -1;
    int m_nextUicSerial = 101;
    int m_nextOrderSerial = 1001;

    // Tile-Netzwerk & Cache
    QNetworkAccessManager *m_netManager = nullptr;
    QHash<QString, QPixmap> m_tileCache;
    QSet<QString> m_pendingTiles;
};

#endif // RAILMAPWIDGET_H
