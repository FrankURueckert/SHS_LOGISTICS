#ifndef LINK2RAILCLIENT_H
#define LINK2RAILCLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QColor>
#include <QVariantMap>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QTimer>

enum class TelematicsProviderId {
    Link2Rail = 0,            // DB Cargo link2rail
    IntermodalTelematics = 1, // Intermodal Telematics (IMT)
    DotTelematik = 2,         // DOT Telematik (DOT-Link / Unified API)
    Transwaggon = 3,          // TRANSWAGGON (WOPS / CIS)
    Innofreight = 4,          // Innofreight OS
    Wascosa = 5               // Wascosa (Nexxiot Connect / SAVVY Synergy / Wascosa Digital)
};

struct TelematicsProviderInfo {
    TelematicsProviderId id = TelematicsProviderId::Link2Rail;
    QString key;            // z. B. "link2rail", "imt", "dot", "transwaggon", "innofreight", "wascosa"
    QString shortName;      // z. B. "DB link2rail", "IMT", "DOT", "TRANSWAGGON", "Innofreight", "Wascosa"
    QString tabLabel;       // Reiter-Titel
    QString displayName;    // Vollständiger Dienstname
    QString portalUrl;      // Offizielle Login-/Portal-URL
    QString description;    // Kurzbeschreibung für den Login-Reiter
    QColor badgeColor;      // Eindeutige Dienst-Kennfarbe auf Karte & Tabelle
};

enum class Link2RailAuthMode {
    PortalLoginCidP = 0,     // Benutzername/E-Mail + Passwort -> JSON Login-POST (JWT / SessionID / Cookie)
    OAuth2PasswordGrant = 1, // OAuth2 Token-Endpunkt (grant_type=password + Client-ID/Secret)
    OAuth2ClientCreds = 2,   // OAuth2 Token-Endpunkt (grant_type=client_credentials)
    DirectApiKeyOrToken = 3, // Direkter Bearer-Token / Session-Token / Cookie / API-Key ohne Login-POST
    HttpBasicAuth = 4,       // HTTP Basic Auth (Benutzername + Passwort im Authorization-Header)
    ImtTenoAuth = 5,         // Intermodal Telematics (IMT Teno API: userId + password + accountAlias -> x-session-token)
    HtmlFormPost = 6         // HTML-Formular-POST mit Session-Cookie (z. B. TRANSWAGGON WOPS twa.portal.Home.cls)
};

struct Link2RailConfig {
    TelematicsProviderId providerId = TelematicsProviderId::Link2Rail;
    Link2RailAuthMode authMode = Link2RailAuthMode::PortalLoginCidP;
    QString environmentName;

    // Anmeldedaten
    QString username;
    QString password;
    QString clientId;                 // DB-Client-Id / OAuth Client-ID / IMT accountAlias
    QString clientSecret;
    QString apiKey;
    QString manualBearerToken;        // Direkter Bearer-Token oder x-session-token
    QString manualCookieHeader;       // Direkter Cookie-Header (z. B. aus Browser-Session / cURL)
    QString sessionTokenHeaderName;   // Optionaler Header-Name für Session-Token (z. B. "x-session-token")
    bool rememberSettings = true;
    bool rememberPassword = false;

    // Konfigurierbare Endpunkte
    QString loginUrl;
    QString gpsTelemetryUrl;
    QString trackTraceUrl;
    QString trainMgmtUrl;

    bool enableGpsEndpoint = true;
    bool enableTrackTraceEndpoint = false;
    bool enableTrainMgmtEndpoint = false;

    // Optionale Anfrage-Optionen (Filter & Extra-Header)
    QString wagonFilterEvns;   // Komma-/Zeilen-getrennte UIC/EVN-Nummern (optional)
    QString extraHeadersRaw;   // Zeilenweise "Header-Name: Wert"
    int pollIntervalSeconds = 60;
    bool autoPollEnabled = true;
};

struct Link2RailWagonRecord {
    // Herkunft / Telematik-Dienst
    TelematicsProviderId providerId = TelematicsProviderId::Link2Rail;
    QString providerKey = QStringLiteral("link2rail");
    QString providerName = QStringLiteral("DB link2rail");
    QColor providerColor = QColor("#0D9488");

    QString evn;                  // 12-stellige UIC/EVN-Wagennummer
    QString trainNumber;          // Zugnummer / Sendungsnummer / Consist-ID
    QString shipmentId;           // Sendungs- / Auftragsnummer
    QString wagonTypeCode;        // z. B. "Samms", "Rns-z", "Shimmns", "Eaos", "Torpedo"
    int axleCount = 0;            // 0 = automatisch aus Bauart/UIC ableiten
    double lengthMeters = 0.0;    // 0 = Standard nach Bauart

    // GPS & Position
    bool hasValidGps = false;
    double rawLat = 0.0;
    double rawLon = 0.0;
    double speedKmh = 0.0;
    double headingDeg = -1.0;     // < 0 = unbekannt
    double gpsAccuracyMeters = 0.0;

    // Betriebs- & Ereignisdaten
    QString statusText;           // z. B. "InTransit", "Standing", "InGeofence"
    QString lastEventType;        // z. B. "Departure", "Arrival", "Passage", "GPS"
    QString locationName;         // z. B. "Völklingen", "Dillingen (Saar)", "Mannheim Rbf"
    QString locationCode;         // z. B. DS100 "SVK", "SDL" oder PLC
    QString geofenceSite;         // Geofence-Bereich (falls vorhanden)
    int standingTimeMinutes = 0;
    QString timestampIso;         // Zeitstempel des letzten GPS-/Track&Trace-Events

    // Fracht- & Routendaten
    QString cargoDescription;
    double weightTons = 0.0;
    bool isLoadedKnown = false;
    bool isLoaded = true;
    QString originName;
    QString destinationName;
    QString etaIso;
    QString telematicsBoxId;
    double temperatureC = -9999.0;
    int batteryPercent = -1;

    // Quelle & vollständige Rohattribute
    QString sourceEndpoint;
    QVariantMap rawAttributes;
};

class Link2RailClient : public QObject
{
    Q_OBJECT

public:
    explicit Link2RailClient(TelematicsProviderId providerId = TelematicsProviderId::Link2Rail,
                             QObject *parent = nullptr);
    ~Link2RailClient() override;

    // Statische Provider-Metadaten & Vorlagen
    static QVector<TelematicsProviderInfo> allProviders();
    static TelematicsProviderInfo providerInfo(TelematicsProviderId id);
    static QStringList presetNamesForProvider(TelematicsProviderId id);
    static Link2RailConfig presetConfig(TelematicsProviderId id, int presetIndex = 0);
    static Link2RailConfig presetConfig(int presetIndex) { return presetConfig(TelematicsProviderId::Link2Rail, presetIndex); }

    // Konfiguration pro Dienst getrennt laden / speichern (QSettings)
    static Link2RailConfig loadSavedConfig(TelematicsProviderId id = TelematicsProviderId::Link2Rail);
    static void saveConfig(const Link2RailConfig &cfg);

    // Browser-Session / "Als cURL kopieren" / Bearer-Token / Cookie Parser
    static bool parseCurlOrTokenText(const QString &inputText,
                                     Link2RailConfig &inOutConfig,
                                     QString *outSummary = nullptr);

    TelematicsProviderId providerId() const { return m_providerId; }
    TelematicsProviderInfo info() const { return providerInfo(m_providerId); }

    void setConfig(const Link2RailConfig &cfg);
    const Link2RailConfig& config() const { return m_config; }

    // Authentifizierungs-Status
    bool isAuthenticated() const { return m_authenticated; }
    bool isBusy() const { return m_pendingRequests > 0; }
    QString authenticatedUser() const { return m_authenticatedUser; }
    QString accessToken() const { return m_accessToken; }
    QString sessionId() const { return m_sessionId; }
    QDateTime tokenExpiresAt() const { return m_tokenExpiresAt; }
    QDateTime lastSyncTime() const { return m_lastSyncTime; }
    const QVector<Link2RailWagonRecord>& latestRecords() const { return m_records; }
    QString lastRawResponseJson() const { return m_lastRawResponse; }

    // Aktionen
    void login(const Link2RailConfig &cfg);
    void logout();
    void fetchAllLiveData();
    void probeConnection(const Link2RailConfig &cfg);
    bool applyFromCurlOrToken(const QString &inputText, QString *outSummary = nullptr);

    // Lokaler Import / Export (JSON, GeoJSON, XML & CSV)
    bool importFromFile(const QString &filePath, QString *outError = nullptr);
    static bool exportRecordsToJsonFile(const QVector<Link2RailWagonRecord> &records,
                                        const QString &filePath, QString *outError = nullptr);
    static bool exportRecordsToCsvFile(const QVector<Link2RailWagonRecord> &records,
                                       const QString &filePath, QString *outError = nullptr);
    bool exportToJsonFile(const QString &filePath, QString *outError = nullptr) const {
        return exportRecordsToJsonFile(m_records, filePath, outError);
    }
    bool exportToCsvFile(const QString &filePath, QString *outError = nullptr) const {
        return exportRecordsToCsvFile(m_records, filePath, outError);
    }

    // Statische Parser (JSON, GeoJSON, XML, CSV)
    static QVector<Link2RailWagonRecord> parseJsonPayload(const QByteArray &rawJson,
                                                          const QString &sourceTag,
                                                          QString *outSummary = nullptr,
                                                          TelematicsProviderId providerId = TelematicsProviderId::Link2Rail);
    static QVector<Link2RailWagonRecord> parseXmlPayload(const QByteArray &rawXml,
                                                         const QString &sourceTag,
                                                         QString *outSummary = nullptr,
                                                         TelematicsProviderId providerId = TelematicsProviderId::Link2Rail);
    static QVector<Link2RailWagonRecord> parseCsvPayload(const QByteArray &rawCsv,
                                                         const QString &sourceTag,
                                                         QString *outSummary = nullptr,
                                                         TelematicsProviderId providerId = TelematicsProviderId::Link2Rail);

signals:
    void authenticationChanged(bool authenticated, const QString &summaryText);
    void loginSucceeded(const QString &user, const QString &details);
    void loginFailed(const QString &errorMessage);
    void liveDataUpdated(const QVector<Link2RailWagonRecord> &records, const QString &summary);
    void apiError(const QString &errorMessage);
    void protocolLogAppended(const QString &logEntry);
    void connectionTestFinished(bool reachable, const QString &diagnosticReport);

private slots:
    void onLoginReplyFinished(QNetworkReply *reply);
    void onDataReplyFinished(QNetworkReply *reply);
    void onProbeReplyFinished(QNetworkReply *reply);
    void onTokenRefreshTimer();

private:
    void applyAuthHeaders(QNetworkRequest &req) const;
    void querySingleEndpoint(const QString &urlTemplate, const QString &sourceTag);
    void mergeBatchRecords(const QVector<Link2RailWagonRecord> &incoming);
    void appendLog(const QString &line);
    bool isTokenExpiringSoon() const;

    TelematicsProviderId m_providerId = TelematicsProviderId::Link2Rail;
    QNetworkAccessManager *m_netManager = nullptr;
    QNetworkCookieJar *m_cookieJar = nullptr;
    QTimer *m_tokenTimer = nullptr;

    Link2RailConfig m_config;
    bool m_authenticated = false;
    bool m_fetchAfterLogin = false;
    int m_pendingRequests = 0;

    QString m_authenticatedUser;
    QString m_accessToken;
    QString m_refreshToken;
    QString m_tokenType = QStringLiteral("Bearer");
    QString m_sessionId;
    QDateTime m_tokenExpiresAt;
    QDateTime m_lastSyncTime;

    QVector<Link2RailWagonRecord> m_records;
    QVector<Link2RailWagonRecord> m_pendingBatch;
    QString m_lastRawResponse;
};

#endif // LINK2RAILCLIENT_H
