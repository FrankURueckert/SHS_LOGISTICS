#include "mainwindowshslogistics.h"
#include "./ui_mainwindowshslogistics.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QPainterPath>
#include <QAction>
#include <QMenu>
#include <QStatusBar>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDialog>
#include <QTextDocument>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QHash>
#include <algorithm>

#ifndef SHS_APP_VERSION
#define SHS_APP_VERSION "0.9"
#endif

namespace {
QString normalizeEvnForMerge(const QString &evn)
{
    QString s = evn.trimmed().toUpper();
    s.remove(' ');
    s.remove('-');
    return s;
}
} // namespace

MainWindowSHSLogistics::MainWindowSHSLogistics(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindowSHSLogistics)
{
    ui->setupUi(this);

    setWindowTitle(QStringLiteral("SHS Logistics GmbH – Multi-Telematik Live-GPS-Leitsystem (6 Portale)  |  Version %1")
                       .arg(QStringLiteral(SHS_APP_VERSION)));

    // Alle 6 unabhängigen Telematik-Clients anlegen:
    // 1. DB Cargo link2rail, 2. Intermodal Telematics (IMT), 3. DOT Telematik,
    // 4. TRANSWAGGON (WOPS), 5. Innofreight OS, 6. Wascosa
    const auto providers = Link2RailClient::allProviders();
    for (const auto &pi : providers) {
        auto *client = new Link2RailClient(pi.id, this);
        m_clients.append(client);
        connect(client, &Link2RailClient::authenticationChanged,
                this, &MainWindowSHSLogistics::onAnyProviderAuthChanged);
        connect(client, &Link2RailClient::liveDataUpdated,
                this, &MainWindowSHSLogistics::onAnyProviderDataUpdated);
    }

    setupCorporateTheme();
    buildMainLayout();
    setupMenuBar();

    refreshLiveWagonTable();
    refreshNodesTable();
    updateHeaderAndConnectionUi();

    statusBar()->showMessage(
        QStringLiteral("SHS Logistics Multi-Telematik-Leitsystem bereit (6 Dienste: DB link2rail, IMT, DOT, TRANSWAGGON, Innofreight, Wascosa)."));
}

MainWindowSHSLogistics::~MainWindowSHSLogistics()
{
    delete ui;
}

void MainWindowSHSLogistics::setupCorporateTheme()
{
    const QString qss = QStringLiteral(R"(
        QMainWindow {
            background-color: #A2B1BE;
        }
        QMenuBar {
            background-color: #3B464E;
            color: #FFFFFF;
            font-family: Verdana, Segoe UI, Arial;
            font-size: 12px;
            font-weight: bold;
            border-bottom: 1px solid #56636D;
            padding: 2px 6px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 5px 12px;
            color: #FFFFFF;
        }
        QMenuBar::item:selected {
            background-color: #48555F;
            color: #FBD50E;
        }
        QMenu {
            background-color: #48555F;
            color: #FFFFFF;
            border: 1px solid #5C839C;
            font-size: 12px;
        }
        QMenu::item {
            padding: 6px 24px;
        }
        QMenu::item:selected {
            background-color: #5C839C;
            color: #FBD50E;
        }
        QGroupBox {
            background-color: #F4F8FA;
            border: 1px solid #8FA4B5;
            border-radius: 5px;
            margin-top: 9px;
            padding-top: 9px;
            font-family: Verdana, Segoe UI, Arial;
            font-size: 10.5px;
            font-weight: bold;
            color: #3B464E;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 5px;
            color: #3B464E;
        }
        QTabWidget::pane {
            border: 1px solid #7A90A2;
            background-color: #E4EEF2;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #48555F;
            color: #FFFFFF;
            font-family: Verdana, Segoe UI, Arial;
            font-size: 10px;
            font-weight: bold;
            padding: 6px 8px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background-color: #5C839C;
            color: #FBD50E;
            border-bottom: 3px solid #FBD50E;
        }
        QTabBar::tab:hover {
            color: #FBD50E;
        }
        QPushButton {
            background-color: #48555F;
            color: #FFFFFF;
            border: 1px solid #333E46;
            border-radius: 4px;
            padding: 5px 9px;
            font-family: Verdana, Segoe UI, Arial;
            font-size: 10px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #5C839C;
            color: #FBD50E;
            border: 1px solid #FBD50E;
        }
        QPushButton:pressed {
            background-color: #2F3940;
        }
        QPushButton:disabled {
            background-color: #94A3B8;
            color: #E2E8F0;
        }
        QComboBox, QSpinBox, QLineEdit {
            background-color: #FFFFFF;
            color: #1E293B;
            border: 1px solid #7A90A2;
            border-radius: 3px;
            padding: 3px 5px;
            font-size: 10.5px;
        }
        QTableWidget {
            background-color: #FFFFFF;
            alternate-background-color: #EDF3F6;
            color: #0F172A;
            gridline-color: #CBD5E1;
            border: 1px solid #7A90A2;
            font-size: 10px;
        }
        QHeaderView::section {
            background-color: #48555F;
            color: #FFFFFF;
            font-weight: bold;
            font-size: 10px;
            padding: 3px;
            border: 1px solid #56636D;
        }
        QStatusBar {
            background-color: #3B464E;
            color: #F8FAFC;
            font-weight: bold;
            border-top: 1px solid #56636D;
        }
    )");
    setStyleSheet(qss);
}

void MainWindowSHSLogistics::buildMainLayout()
{
    QWidget *root = ui->centralwidget;
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // =========================================================================
    // 1. SHS LOGISTICS CORPORATE HEADER
    // =========================================================================
    auto *headerFrame = new QFrame(root);
    headerFrame->setStyleSheet(
        "QFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #FFFFFF, stop:0.28 #E4EEF2, stop:0.55 #48555F, stop:1.0 #3B464E); "
        "border: 1px solid #56636D; border-radius: 5px; }");
    auto *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(12, 6, 12, 6);
    headerLayout->setSpacing(14);

    auto *lblLogo = new QLabel(headerFrame);
    QPixmap logoPm(QStringLiteral(":/SHS_Logo.png"));
    if (logoPm.isNull()) {
        logoPm.load(QStringLiteral("SHS_Logo.png"));
    }
    if (!logoPm.isNull()) {
        lblLogo->setPixmap(logoPm.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    lblLogo->setStyleSheet("border: none; background: transparent;");
    headerLayout->addWidget(lblLogo);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(1);
    auto *lblCompany = new QLabel(QStringLiteral("SHS LOGISTICS GmbH"), headerFrame);
    lblCompany->setStyleSheet(
        "font-family: Verdana, Arial; font-size: 18px; font-weight: bold; "
        "color: #1E293B; letter-spacing: 1.2px; border: none; background: transparent;");
    auto *lblSlogan = new QLabel(
        QStringLiteral("Partner <span style='color:#5C839C;font-weight:bold;'>[</span>"
                       "schafft <i>Zukunft</i>"
                       "<span style='color:#EF7D00;font-weight:bold;'>]</span> Stahl "
                       "— Multi-Telematik Live-GPS-Leitsystem &nbsp;|&nbsp; "
                       "<span style='color:#0369A1;font-weight:bold;'>Entwicklung: Frank U. Rückert &amp; Sevda Dawd</span>"),
        headerFrame);
    lblSlogan->setStyleSheet(
        "font-family: Verdana, Arial; font-size: 10.5px; color: #334155; border: none; background: transparent;");
    titleBox->addWidget(lblCompany);
    titleBox->addWidget(lblSlogan);
    headerLayout->addLayout(titleBox);

    headerLayout->addStretch(1);

    m_lblHeaderStats = new QLabel(headerFrame);
    m_lblHeaderStats->setStyleSheet(
        "font-family: Verdana, Arial; font-size: 10.5px; font-weight: bold; "
        "color: #FBD50E; border: none; background: transparent;");
    headerLayout->addWidget(m_lblHeaderStats);

    rootLayout->addWidget(headerFrame);

    // =========================================================================
    // 2. SCHNELLWAHL-LEISTE (Standort-Zoom & Multi-Dienst Live-Aktionen)
    // =========================================================================
    auto *navBar = new QFrame(root);
    navBar->setStyleSheet(
        "QFrame { background-color: #48555F; border: 1px solid #56636D; border-radius: 4px; }");
    auto *navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(8, 5, 8, 5);
    navLayout->setSpacing(6);

    auto makeNavBtn = [&](const QString &text, const QString &accentHex = QString()) {
        auto *btn = new QPushButton(text, navBar);
        if (!accentHex.isEmpty()) {
            btn->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: #FFFFFF; border: 1px solid #1E293B; "
                "padding: 5px 10px; font-weight: bold; } "
                "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }").arg(accentHex));
        }
        navLayout->addWidget(btn);
        return btn;
    };

    auto *btnZoomVK  = makeNavBtn(QStringLiteral("🏭 VÖLKLINGEN"), QStringLiteral("#EF7D00"));
    auto *btnZoomDL  = makeNavBtn(QStringLiteral("🔥 DILLINGEN"));
    auto *btnZoomBB  = makeNavBtn(QStringLiteral("⚙️ BURBACH"));
    auto *btnZoomNK  = makeNavBtn(QStringLiteral("🔩 NEUNKIRCHEN"));
    auto *btnZoomSL  = makeNavBtn(QStringLiteral("🗺️ SAARLAND"));
    auto *btnZoomDE  = makeNavBtn(QStringLiteral("🇩🇪 DEUTSCHLAND"));
    auto *btnZoomFit = makeNavBtn(QStringLiteral("🎯 ALLE LIVE-WAGGONS"), QStringLiteral("#0284C7"));

    navLayout->addStretch(1);

    m_btnToolbarLogins  = makeNavBtn(QStringLiteral("🔐 Dienste && Logins (0/6)"), QStringLiteral("#DC2626"));
    m_btnToolbarSyncAll = makeNavBtn(QStringLiteral("🔄 Alle Dienste abrufen"), QStringLiteral("#0D9488"));
    auto *btnQuickImp   = makeNavBtn(QStringLiteral("📂 Datei laden"));

    rootLayout->addWidget(navBar);

    // =========================================================================
    // 3. HAUPTBEREICH: LINKS 4 HAUPT-TABS (INKL. 6 LOGIN-REITERN) + MITTE 2D-KARTE
    // =========================================================================
    auto *mainSplitter = new QSplitter(Qt::Horizontal, root);
    mainSplitter->setHandleWidth(6);

    m_mapWidget = new RailMapWidget();

    m_sideTabs = new QTabWidget(mainSplitter);
    m_sideTabs->setMinimumWidth(590);
    m_sideTabs->setMaximumWidth(820);

    // -------------------------------------------------------------------------
    // TAB 1: ALLE LIVE-WAGGONS (VEREINIGTE ANSICHT ALLER 6 DIENSTE)
    // -------------------------------------------------------------------------
    auto *tabLive = new QWidget();
    auto *tabLiveLay = new QVBoxLayout(tabLive);
    tabLiveLay->setContentsMargins(8, 8, 8, 8);
    tabLiveLay->setSpacing(8);

    m_bannerNotLoggedIn = new QFrame(tabLive);
    m_bannerNotLoggedIn->setStyleSheet(
        "QFrame { background-color: #FEF3C7; border: 1.5px solid #D97706; border-radius: 5px; }");
    auto *bannerLay = new QHBoxLayout(m_bannerNotLoggedIn);
    bannerLay->setContentsMargins(8, 6, 8, 6);
    auto *lblBanner = new QLabel(
        QStringLiteral("<b style='color:#92400E;'>Multi-Telematik Live-Betrieb (6 Portale auf einer Karte):</b><br>"
                       "<span style='color:#78350F; font-size:10px;'>Melde dich im Tab '🔐 Dienste &amp; Logins' bei einem oder mehreren Diensten an (link2rail, IMT, DOT, TRANSWAGGON, Innofreight, Wascosa).</span>"),
        m_bannerNotLoggedIn);
    lblBanner->setWordWrap(true);
    lblBanner->setStyleSheet("border:none; background:transparent;");
    auto *btnBannerLogin = new QPushButton(QStringLiteral("🔐 Zu den Logins"), m_bannerNotLoggedIn);
    btnBannerLogin->setStyleSheet("background-color: #0D9488; color: white; padding: 6px 12px;");
    bannerLay->addWidget(lblBanner, 1);
    bannerLay->addWidget(btnBannerLogin);
    tabLiveLay->addWidget(m_bannerNotLoggedIn);

    auto *grpFilter = new QGroupBox(QStringLiteral("1. Alle Live-Waggons filtern && optisch unterscheiden"), tabLive);
    auto *gFilt = new QGridLayout(grpFilter);

    gFilt->addWidget(new QLabel(QStringLiteral("Telematik-Dienst:")), 0, 0);
    m_comboProviderFilter = new QComboBox(grpFilter);
    m_comboProviderFilter->addItem(QStringLiteral("Alle 6 Telematik-Dienste gemeinsam anzeigen"), QString());
    for (const auto &pi : Link2RailClient::allProviders()) {
        m_comboProviderFilter->addItem(pi.displayName, pi.key);
    }
    gFilt->addWidget(m_comboProviderFilter, 0, 1);

    gFilt->addWidget(new QLabel(QStringLiteral("Suche (EVN/Zug/Ort):")), 1, 0);
    m_editSearchFilter = new QLineEdit(grpFilter);
    m_editSearchFilter->setPlaceholderText(QStringLiteral("z. B. 31804891..., Samms, Völklingen, Wascosa ..."));
    gFilt->addWidget(m_editSearchFilter, 1, 1);

    gFilt->addWidget(new QLabel(QStringLiteral("Bauart-Filter:")), 2, 0);
    m_comboCategoryFilter = new QComboBox(grpFilter);
    m_comboCategoryFilter->addItem(QStringLiteral("Alle Bauarten anzeigen"));
    m_comboCategoryFilter->addItem(QStringLiteral("Samms [6-Achser]"));
    m_comboCategoryFilter->addItem(QStringLiteral("Rns-z [4-Achser]"));
    m_comboCategoryFilter->addItem(QStringLiteral("4-Achser Shimmns / Eaos"));
    m_comboCategoryFilter->addItem(QStringLiteral("6-Achser Torpedo / Faals"));
    gFilt->addWidget(m_comboCategoryFilter, 2, 1);

    gFilt->addWidget(new QLabel(QStringLiteral("Karten-Farbmodus:")), 3, 0);
    m_comboColorMode = new QComboBox(grpFilter);
    m_comboColorMode->addItem(QStringLiteral("Alle 4 Bauarten getrennt (Samms=Orange, Rns-z=Blau, 4A=Grün, 6A=Rot)"));
    m_comboColorMode->addItem(QStringLiteral("Fokus Bauart: Samms vs. Rns-z hervorheben"));
    m_comboColorMode->addItem(QStringLiteral("Strikt nach Achsen: Alle 4-Achser (Blau) vs. Alle 6-Achser (Orange)"));
    m_comboColorMode->addItem(QStringLiteral("Nach Telematik-Dienst färben (link2rail / IMT / DOT / TRANSWAGGON / Innofreight / Wascosa)"));
    gFilt->addWidget(m_comboColorMode, 3, 1);

    m_lblLiveSummary = new QLabel(QStringLiteral("Keine Live-Daten geladen."), grpFilter);
    m_lblLiveSummary->setStyleSheet("color: #334155; font-size: 10px;");
    gFilt->addWidget(m_lblLiveSummary, 4, 0, 1, 2);
    tabLiveLay->addWidget(grpFilter);

    auto *grpTable = new QGroupBox(QStringLiteral("2. Vereinigte Live-Waggon-Liste aller Dienste (Klick = Auswählen, Doppelklick = GPS-Zoom)"), tabLive);
    auto *tLay = new QVBoxLayout(grpTable);

    m_tableLiveWagons = new QTableWidget(0, 8, grpTable);
    m_tableLiveWagons->setHorizontalHeaderLabels({
        QStringLiteral("Dienst"),
        QStringLiteral("UIC / EVN"),
        QStringLiteral("Zug / Sendung"),
        QStringLiteral("Bauart"),
        QStringLiteral("Status / Event"),
        QStringLiteral("Ort / Geofence"),
        QStringLiteral("GPS (Lat, Lon)"),
        QStringLiteral("Zeitstempel")
    });
    m_tableLiveWagons->setAlternatingRowColors(true);
    m_tableLiveWagons->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableLiveWagons->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableLiveWagons->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableLiveWagons->verticalHeader()->setVisible(false);
    m_tableLiveWagons->horizontalHeader()->setStretchLastSection(true);
    m_tableLiveWagons->setColumnWidth(0, 96);
    m_tableLiveWagons->setColumnWidth(1, 114);
    m_tableLiveWagons->setColumnWidth(2, 82);
    m_tableLiveWagons->setColumnWidth(3, 72);
    m_tableLiveWagons->setColumnWidth(4, 92);
    m_tableLiveWagons->setColumnWidth(5, 102);
    m_tableLiveWagons->setColumnWidth(6, 104);
    tLay->addWidget(m_tableLiveWagons, 2);

    auto *wActionRow = new QHBoxLayout();
    auto *btnZoomSelectedWagon = new QPushButton(QStringLiteral("🔍 Auf Waggon zoomen"), grpTable);
    btnZoomSelectedWagon->setStyleSheet("background-color: #0284C7; color: white;");
    auto *btnFitAllTab1 = new QPushButton(QStringLiteral("🎯 Alle GPS-Waggons zeigen"), grpTable);
    auto *btnCopyJson = new QPushButton(QStringLiteral("📋 Waggon-JSON kopieren"), grpTable);
    wActionRow->addWidget(btnZoomSelectedWagon);
    wActionRow->addWidget(btnFitAllTab1);
    wActionRow->addWidget(btnCopyJson);
    tLay->addLayout(wActionRow);
    tabLiveLay->addWidget(grpTable, 2);

    auto *grpDetails = new QGroupBox(QStringLiteral("3. Alle übernommenen API-Felder (ausgewählter Waggon)"), tabLive);
    auto *detLay = new QVBoxLayout(grpDetails);
    m_tableWagonDetails = new QTableWidget(0, 2, grpDetails);
    m_tableWagonDetails->setHorizontalHeaderLabels({QStringLiteral("API-Feld / Attribut"), QStringLiteral("Wert aus Telematik-Dienst")});
    m_tableWagonDetails->setAlternatingRowColors(true);
    m_tableWagonDetails->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWagonDetails->verticalHeader()->setVisible(false);
    m_tableWagonDetails->horizontalHeader()->setStretchLastSection(true);
    m_tableWagonDetails->setColumnWidth(0, 210);
    detLay->addWidget(m_tableWagonDetails);
    tabLiveLay->addWidget(grpDetails, 1);

    m_sideTabs->addTab(tabLive, QStringLiteral("📡 Live-Waggons"));

    // -------------------------------------------------------------------------
    // TAB 2: DIENSTE & LOGINS — 6 EIGENE LOGIN-REITER (EIN REITER PRO DIENST!)
    // -------------------------------------------------------------------------
    auto *tabLogins = new QWidget();
    auto *tabLoginsLay = new QVBoxLayout(tabLogins);
    tabLoginsLay->setContentsMargins(6, 6, 6, 6);
    tabLoginsLay->setSpacing(6);

    auto *globalSyncBar = new QHBoxLayout();
    auto *btnSyncAll = new QPushButton(QStringLiteral("🔄 Alle angemeldeten Dienste abrufen (F9)"), tabLogins);
    btnSyncAll->setStyleSheet("background-color: #0D9488; color: white; padding: 6px;");
    auto *btnExpAllJson = new QPushButton(QStringLiteral("💾 Gesamt-JSON"), tabLogins);
    auto *btnExpAllCsv  = new QPushButton(QStringLiteral("💾 Gesamt-CSV"), tabLogins);
    globalSyncBar->addWidget(btnSyncAll, 1);
    globalSyncBar->addWidget(btnExpAllJson);
    globalSyncBar->addWidget(btnExpAllCsv);
    tabLoginsLay->addLayout(globalSyncBar);

    // 6 eigene Login-Reiter – alle 6 Dienste gleichzeitig sichtbar ohne Scroll-Pfeile!
    m_providerLoginTabs = new QTabWidget(tabLogins);
    m_providerLoginTabs->setTabPosition(QTabWidget::North);
    m_providerLoginTabs->setUsesScrollButtons(false);
    m_providerLoginTabs->setStyleSheet(
        "QTabBar::tab { font-size: 9px; padding: 5px 5px; margin-right: 1px; } "
        "QTabBar::tab:selected { background-color: #334155; color: #FBD50E; border-bottom: 3px solid #EF7D00; }");

    for (Link2RailClient *client : m_clients) {
        auto *w = new ProviderLoginWidget(client, m_providerLoginTabs);
        m_loginWidgets.append(w);
        connect(w, &ProviderLoginWidget::statusNotification, this, [this](const QString &msg) {
            statusBar()->showMessage(msg, 8000);
        });
        connect(w, &ProviderLoginWidget::importSucceededFitRequested, this, [this]() {
            m_sideTabs->setCurrentIndex(0);
            m_mapWidget->fitAllLiveWagons(true);
        });
        m_providerLoginTabs->addTab(w, QStringLiteral("🔴 ") + client->info().tabLabel);
    }

    tabLoginsLay->addWidget(m_providerLoginTabs, 1);
    m_sideTabs->addTab(tabLogins, QStringLiteral("🔐 Dienste && Logins (6)"));

    // -------------------------------------------------------------------------
    // TAB 3: STANDORTE, WERKE & GEOFENCES (LIVE-BELEGUNG ALLER 6 DIENSTE)
    // -------------------------------------------------------------------------
    auto *tabNodes = new QWidget();
    auto *tabNodesLay = new QVBoxLayout(tabNodes);
    tabNodesLay->setContentsMargins(8, 8, 8, 8);
    tabNodesLay->setSpacing(8);

    auto *grpNodes = new QGroupBox(QStringLiteral("SHS-Werke, Anlagen && Geofence-Bereiche (Klick = Karte zentrieren)"), tabNodes);
    auto *nLay = new QVBoxLayout(grpNodes);
    auto *lblNInfo = new QLabel(
        QStringLiteral("Zeigt alle 10 realen Standorte von Saarstahl AG & Dillinger Hütte / ROGESA sowie die Gesamtzahl der per GPS über alle 6 Telematik-Dienste dort georteten Waggons:"),
        grpNodes);
    lblNInfo->setWordWrap(true);
    nLay->addWidget(lblNInfo);

    m_tableNodes = new QTableWidget(0, 5, grpNodes);
    m_tableNodes->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("Werk / Anlage"),
        QStringLiteral("Rolle"),
        QStringLiteral("📡 Live-Waggons"),
        QStringLiteral("Typische Stahlgüter")
    });
    m_tableNodes->setAlternatingRowColors(true);
    m_tableNodes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableNodes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableNodes->verticalHeader()->setVisible(false);
    m_tableNodes->horizontalHeader()->setStretchLastSection(true);
    m_tableNodes->setColumnWidth(0, 98);
    m_tableNodes->setColumnWidth(1, 175);
    m_tableNodes->setColumnWidth(2, 95);
    m_tableNodes->setColumnWidth(3, 95);
    nLay->addWidget(m_tableNodes, 1);
    tabNodesLay->addWidget(grpNodes, 1);

    m_sideTabs->addTab(tabNodes, QStringLiteral("🏭 Werke && Geofences"));

    // -------------------------------------------------------------------------
    // TAB 4: KARTE & DARSTELLUNG (INKL. SICHTBARKEIT JE TELEMATIK-DIENST)
    // -------------------------------------------------------------------------
    auto *tabMap = new QWidget();
    auto *tabMapLay = new QVBoxLayout(tabMap);
    tabMapLay->setContentsMargins(8, 8, 8, 8);
    tabMapLay->setSpacing(8);

    auto *grpSites = new QGroupBox(QStringLiteral("1. Standort- && Kamera-Schnellwahl"), tabMap);
    auto *gSites = new QGridLayout(grpSites);
    auto *btnSiteVK  = new QPushButton(QStringLiteral("🏭 Werk Völklingen (Saarstahl)"), grpSites);
    auto *btnSiteDL  = new QPushButton(QStringLiteral("🔥 Dillinger Hütte && ROGESA"), grpSites);
    auto *btnSiteBB  = new QPushButton(QStringLiteral("⚙️ Werk Burbach (Drahtwerk)"), grpSites);
    auto *btnSiteNK  = new QPushButton(QStringLiteral("🔩 Werk Neunkirchen"), grpSites);
    auto *btnSiteSL  = new QPushButton(QStringLiteral("🗺️ Gesamtübersicht Saarland"), grpSites);
    auto *btnSiteDE  = new QPushButton(QStringLiteral("🇩🇪 Deutschland-Übersicht"), grpSites);
    auto *btnSiteFit = new QPushButton(QStringLiteral("🎯 Alle Live-Waggons einpassen"), grpSites);
    btnSiteVK->setStyleSheet("background-color: #EF7D00; color: white;");
    btnSiteFit->setStyleSheet("background-color: #0284C7; color: white;");
    gSites->addWidget(btnSiteVK,  0, 0);
    gSites->addWidget(btnSiteDL,  0, 1);
    gSites->addWidget(btnSiteBB,  1, 0);
    gSites->addWidget(btnSiteNK,  1, 1);
    gSites->addWidget(btnSiteSL,  2, 0);
    gSites->addWidget(btnSiteDE,  2, 1);
    gSites->addWidget(btnSiteFit, 3, 0, 1, 2);
    tabMapLay->addWidget(grpSites);

    auto *grpMapStyle = new QGroupBox(QStringLiteral("2. Google Maps Hintergrund && Werks-Layer"), tabMap);
    auto *msLay = new QVBoxLayout(grpMapStyle);

    msLay->addWidget(new QLabel(QStringLiteral("Kartenhintergrund-Modus:")));
    m_comboMapMode = new QComboBox(grpMapStyle);
    m_comboMapMode->addItem(QStringLiteral("Hybrid: Google Maps + Vereinfachtes Werk (Empfohlen)"));
    m_comboMapMode->addItem(QStringLiteral("Nur Vereinfachte Werks- & Gleisdarstellung (Schematisch)"));
    m_comboMapMode->addItem(QStringLiteral("Google Maps Straßenkarte + Bahngleise"));
    m_comboMapMode->addItem(QStringLiteral("Google Maps Satellitenbild + Bahngleise"));
    msLay->addWidget(m_comboMapMode);

    m_chkSimplifiedPlant = new QCheckBox(QStringLiteral("Vereinfachtes Werksgelände && Werkhallen anzeigen"), grpMapStyle);
    m_chkSimplifiedPlant->setChecked(true);
    m_chkSourcesSinks    = new QCheckBox(QStringLiteral("SHS-Werke && Geofence-Karten anzeigen"), grpMapStyle);
    m_chkSourcesSinks->setChecked(true);
    m_chkRoadsRivers     = new QCheckBox(QStringLiteral("Vereinfachte Straßen (A620, B51) && Flüsse (Saar, Rossel)"), grpMapStyle);
    m_chkRoadsRivers->setChecked(true);
    m_chkTrackLabels     = new QCheckBox(QStringLiteral("Gleis-Bezeichnungen anzeigen"), grpMapStyle);
    m_chkTrackLabels->setChecked(true);
    m_chkWagonLabels     = new QCheckBox(QStringLiteral("Waggon-EVN && Dienst-Labels auf der Karte anzeigen"), grpMapStyle);
    m_chkWagonLabels->setChecked(true);
    m_chkExaggerateSize  = new QCheckBox(QStringLiteral("2D-Waggons für Übersicht vergrößert darstellen"), grpMapStyle);
    m_chkExaggerateSize->setChecked(true);
    m_chkSnapToTracks    = new QCheckBox(QStringLiteral("🧲 GPS-Positionen im Werksbereich auf Gleisachsen snappen (<= 120 m)"), grpMapStyle);
    m_chkSnapToTracks->setChecked(true);

    msLay->addWidget(m_chkSimplifiedPlant);
    msLay->addWidget(m_chkSourcesSinks);
    msLay->addWidget(m_chkRoadsRivers);
    msLay->addWidget(m_chkTrackLabels);
    msLay->addWidget(m_chkWagonLabels);
    msLay->addWidget(m_chkExaggerateSize);
    msLay->addWidget(m_chkSnapToTracks);
    tabMapLay->addWidget(grpMapStyle);

    // Sichtbarkeit pro Telematik-Dienst auf der gemeinsamen Karte
    auto *grpProvLayer = new QGroupBox(QStringLiteral("3. Sichtbare Telematik-Dienste auf der gemeinsamen Karte"), tabMap);
    auto *plLay = new QGridLayout(grpProvLayer);
    const auto allProv = Link2RailClient::allProviders();
    for (int i = 0; i < allProv.size(); ++i) {
        const auto &pi = allProv[i];
        auto *chk = new QCheckBox(QStringLiteral("● %1").arg(pi.shortName), grpProvLayer);
        chk->setChecked(true);
        chk->setStyleSheet(QString("QCheckBox { color: %1; font-weight: bold; }").arg(pi.badgeColor.darker(115).name()));
        connect(chk, &QCheckBox::toggled, this, [this, key = pi.key](bool checked) {
            m_mapWidget->setProviderVisible(key, checked);
        });
        plLay->addWidget(chk, i / 2, i % 2);
    }
    tabMapLay->addWidget(grpProvLayer);

    tabMapLay->addStretch(1);
    m_sideTabs->addTab(tabMap, QStringLiteral("🗺️ Karte && Layer"));

    mainSplitter->addWidget(m_sideTabs);
    mainSplitter->addWidget(m_mapWidget);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({650, 1270});

    rootLayout->addWidget(mainSplitter, 1);

    // =========================================================================
    // 4. SIGNAL-SLOT VERBINDUNGEN
    // =========================================================================
    auto connectSiteBtn = [this](QPushButton *btn, const QString &site) {
        connect(btn, &QPushButton::clicked, this, [this, site]() {
            m_mapWidget->focusSite(site, true);
        });
    };
    connectSiteBtn(btnZoomVK, QStringLiteral("Voelklingen"));
    connectSiteBtn(btnZoomDL, QStringLiteral("Dillingen"));
    connectSiteBtn(btnZoomBB, QStringLiteral("Burbach"));
    connectSiteBtn(btnZoomNK, QStringLiteral("Neunkirchen"));
    connectSiteBtn(btnZoomSL, QStringLiteral("Gesamt"));
    connectSiteBtn(btnZoomDE, QStringLiteral("Deutschland"));
    connect(btnZoomFit, &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });

    connectSiteBtn(btnSiteVK, QStringLiteral("Voelklingen"));
    connectSiteBtn(btnSiteDL, QStringLiteral("Dillingen"));
    connectSiteBtn(btnSiteBB, QStringLiteral("Burbach"));
    connectSiteBtn(btnSiteNK, QStringLiteral("Neunkirchen"));
    connectSiteBtn(btnSiteSL, QStringLiteral("Gesamt"));
    connectSiteBtn(btnSiteDE, QStringLiteral("Deutschland"));
    connect(btnSiteFit, &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });

    connect(m_btnToolbarLogins,  &QPushButton::clicked, this, [this]() { m_sideTabs->setCurrentIndex(1); });
    connect(m_btnToolbarSyncAll, &QPushButton::clicked, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    connect(btnQuickImp,         &QPushButton::clicked, this, &MainWindowSHSLogistics::onQuickImportActiveProviderClicked);
    connect(btnBannerLogin,      &QPushButton::clicked, this, [this]() { m_sideTabs->setCurrentIndex(1); });

    // Tab 1
    connect(m_comboProviderFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_editSearchFilter,    &QLineEdit::textChanged, this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_comboCategoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_comboColorMode,      QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onColorModeChanged);
    connect(m_tableLiveWagons,     &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onLiveWagonTableClicked);
    connect(m_tableLiveWagons,     &QTableWidget::cellDoubleClicked, this, &MainWindowSHSLogistics::onLiveWagonTableDoubleClicked);
    connect(btnZoomSelectedWagon,  &QPushButton::clicked, this, [this]() {
        m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), m_mapWidget->selectedWagonIndex());
    });
    connect(btnFitAllTab1, &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });
    connect(btnCopyJson, &QPushButton::clicked, this, [this]() {
        const int tIdx = m_mapWidget->selectedTrainIndex();
        const int wIdx = m_mapWidget->selectedWagonIndex();
        const auto &trains = m_mapWidget->trains();
        if (tIdx >= 0 && tIdx < trains.size() && wIdx >= 0 && wIdx < trains[tIdx].wagons.size()) {
            const QJsonObject obj = QJsonObject::fromVariantMap(trains[tIdx].wagons[wIdx].rawAttributes);
            QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
            statusBar()->showMessage(QStringLiteral("Waggon-JSON in die Zwischenablage kopiert."), 4000);
        }
    });

    // Tab 2 Global Buttons
    connect(btnSyncAll,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    connect(btnExpAllJson, &QPushButton::clicked, this, &MainWindowSHSLogistics::onExportAllJsonClicked);
    connect(btnExpAllCsv,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onExportAllCsvClicked);

    // Tab 3
    connect(m_tableNodes, &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onNodeTableClicked);

    // Tab 4
    connect(m_comboMapMode,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onMapModeChanged);
    connect(m_chkSimplifiedPlant, &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowSimplifiedPlant);
    connect(m_chkSourcesSinks,    &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowSourcesAndSinks);
    connect(m_chkRoadsRivers,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowRoadsAndRivers);
    connect(m_chkTrackLabels,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowTrackLabels);
    connect(m_chkWagonLabels,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowWagonLabels);
    connect(m_chkExaggerateSize,  &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setExaggerateWagonSize);
    connect(m_chkSnapToTracks,    &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setSnapLiveGpsToTracks);

    // MapWidget Signale
    connect(m_mapWidget, &RailMapWidget::selectionChanged,     this, &MainWindowSHSLogistics::onMapSelectionChanged);
    connect(m_mapWidget, &RailMapWidget::trainsUpdated,        this, &MainWindowSHSLogistics::onTrainsUpdated);
    connect(m_mapWidget, &RailMapWidget::cargoAndNodesUpdated, this, &MainWindowSHSLogistics::onNodesUpdated);
    connect(m_mapWidget, &RailMapWidget::statusMessage,        this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 8000);
    });
}

void MainWindowSHSLogistics::setupMenuBar()
{
    QMenuBar *mb = menuBar();
    mb->clear();

    QMenu *menuFile = mb->addMenu(QStringLiteral("&Datei"));
    QAction *actImport = menuFile->addAction(QStringLiteral("📂 Telematik-Datei importieren (JSON / XML / CSV) ..."));
    connect(actImport, &QAction::triggered, this, &MainWindowSHSLogistics::onQuickImportActiveProviderClicked);
    QAction *actExpJson = menuFile->addAction(QStringLiteral("💾 Alle Live-Daten als JSON exportieren ..."));
    connect(actExpJson, &QAction::triggered, this, &MainWindowSHSLogistics::onExportAllJsonClicked);
    QAction *actExpCsv = menuFile->addAction(QStringLiteral("💾 Alle Live-Daten als CSV exportieren ..."));
    connect(actExpCsv, &QAction::triggered, this, &MainWindowSHSLogistics::onExportAllCsvClicked);
    menuFile->addSeparator();
    QAction *actPrint = menuFile->addAction(QStringLiteral("🖨️ Kartenansicht drucken / PDF ..."));
    actPrint->setShortcut(QKeySequence::Print);
    connect(actPrint, &QAction::triggered, this, &MainWindowSHSLogistics::onPrintMap);
    menuFile->addSeparator();
    QAction *actExit = menuFile->addAction(QStringLiteral("Beenden"));
    actExit->setShortcut(QKeySequence::Quit);
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    QMenu *menuSites = mb->addMenu(QStringLiteral("&Standorte && Karte"));
    auto addSiteAct = [&](const QString &label, const QString &site, const QKeySequence &key) {
        QAction *a = menuSites->addAction(label);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, site]() {
            m_mapWidget->focusSite(site, true);
        });
    };
    addSiteAct(QStringLiteral("🏭 Werk Völklingen (Saarstahl Hauptwerk)"), QStringLiteral("Voelklingen"), Qt::Key_F1);
    addSiteAct(QStringLiteral("🔥 Dillinger Hütte & ROGESA"), QStringLiteral("Dillingen"), Qt::Key_F2);
    addSiteAct(QStringLiteral("⚙️ Werk Burbach (Drahtwerk)"), QStringLiteral("Burbach"), Qt::Key_F3);
    addSiteAct(QStringLiteral("🔩 Werk Neunkirchen (Walzwerk)"), QStringLiteral("Neunkirchen"), Qt::Key_F4);
    addSiteAct(QStringLiteral("🗺️ Gesamtübersicht Saarland-Korridor"), QStringLiteral("Gesamt"), Qt::Key_F5);
    addSiteAct(QStringLiteral("🇩🇪 Deutschland-Übersicht"), QStringLiteral("Deutschland"), Qt::Key_F6);
    menuSites->addSeparator();
    QAction *actFitAll = menuSites->addAction(QStringLiteral("🎯 Alle Live-Waggons einpassen"));
    actFitAll->setShortcut(Qt::Key_F7);
    connect(actFitAll, &QAction::triggered, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });

    QMenu *menuDienste = mb->addMenu(QStringLiteral("&Telematik && Simulation (6)"));
    const auto providers = Link2RailClient::allProviders();
    for (int i = 0; i < providers.size(); ++i) {
        QAction *a = menuDienste->addAction(QStringLiteral("🔐 Login: %1").arg(providers[i].displayName));
        if (i == 0) a->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
        connect(a, &QAction::triggered, this, [this, i]() { onOpenProviderTab(i); });
    }
    menuDienste->addSeparator();
    QAction *actFetchAll = menuDienste->addAction(QStringLiteral("🔄 Alle angemeldeten Dienste jetzt abrufen"));
    actFetchAll->setShortcut(Qt::Key_F9);
    connect(actFetchAll, &QAction::triggered, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    QAction *actLogoutAll = menuDienste->addAction(QStringLiteral("🚪 Von allen 6 Diensten abmelden"));
    connect(actLogoutAll, &QAction::triggered, this, &MainWindowSHSLogistics::onLogoutAllClicked);
    menuDienste->addSeparator();
    QAction *actDevTeam = menuDienste->addAction(
        QStringLiteral("👩‍💻 Entwicklung: Frank U. Rückert && Sevda Dawd (sevdasdawd@gmail.com)"));
    connect(actDevTeam, &QAction::triggered, this, &MainWindowSHSLogistics::onShowAboutDialog);

    QMenu *menuHelp = mb->addMenu(QStringLiteral("&Hilfe"));
    QAction *actMainPdf = menuHelp->addAction(
        QStringLiteral("📕 Technisches Handbuch & GUI-Manual öffnen (PDF)"));
    actMainPdf->setShortcut(Qt::Key_F10);
    connect(actMainPdf, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenMainManualPdf);

    QAction *actQuickPdf = menuHelp->addAction(
        QStringLiteral("📘 Kurzanleitung & Schnellstart-Manual öffnen (PDF)"));
    actQuickPdf->setShortcut(Qt::Key_F11);
    connect(actQuickPdf, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenQuickStartPdf);

    QAction *actOpenFolder = menuHelp->addAction(
        QStringLiteral("📂 Handbuch- & Release-Ordner im Explorer öffnen"));
    connect(actOpenFolder, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenManualsFolder);

    menuHelp->addSeparator();
    QAction *actSplash = menuHelp->addAction(
        QStringLiteral("🖼️ Start-Splashscreen (SHS Corporate Design) anzeigen"));
    connect(actSplash, &QAction::triggered, this, &MainWindowSHSLogistics::onShowSplashPreviewDialog);

    QAction *actAbout = menuHelp->addAction(QStringLiteral("ℹ️ Über SHS Logistics – Team: Frank U. Rückert && Sevda Dawd"));
    connect(actAbout, &QAction::triggered, this, &MainWindowSHSLogistics::onShowAboutDialog);
}

void MainWindowSHSLogistics::onOpenProviderTab(int providerIndex)
{
    m_sideTabs->setCurrentIndex(1);
    if (m_providerLoginTabs && providerIndex >= 0 && providerIndex < m_providerLoginTabs->count()) {
        m_providerLoginTabs->setCurrentIndex(providerIndex);
    }
}

void MainWindowSHSLogistics::onFetchAllAuthenticatedClicked()
{
    int triggered = 0;
    for (Link2RailClient *c : m_clients) {
        if (c->isAuthenticated()) {
            c->fetchAllLiveData();
            triggered++;
        }
    }
    if (triggered == 0) {
        m_sideTabs->setCurrentIndex(1);
        statusBar()->showMessage(
            QStringLiteral("Aktuell ist kein Telematik-Dienst angemeldet. Bitte in einem der 6 Login-Reiter anmelden."), 6000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Live-GPS-Abruf für %1 angemeldete(n) Telematik-Dienst(e) gestartet ...").arg(triggered), 5000);
    }
}

void MainWindowSHSLogistics::onLogoutAllClicked()
{
    for (ProviderLoginWidget *w : m_loginWidgets) {
        if (w->client()->isAuthenticated() || !w->client()->latestRecords().isEmpty()) {
            w->client()->logout();
            w->refreshStatusCard();
        }
    }
    rebuildUnifiedLiveData();
}

void MainWindowSHSLogistics::onQuickImportActiveProviderClicked()
{
    const int idx = (m_providerLoginTabs && m_providerLoginTabs->currentIndex() >= 0)
                        ? m_providerLoginTabs->currentIndex()
                        : 0;
    if (idx < 0 || idx >= m_clients.size()) return;
    Link2RailClient *client = m_clients[idx];

    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Telematik-Datei laden für %1 (JSON / GeoJSON / XML / CSV)").arg(client->info().shortName),
        QString(),
        QStringLiteral("Telematik-Daten (*.json *.geojson *.xml *.csv *.txt);;Alle Dateien (*.*)"));
    if (path.isEmpty()) return;

    QString err;
    if (!client->importFromFile(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Import fehlgeschlagen"), err);
    } else {
        m_sideTabs->setCurrentIndex(0);
        m_mapWidget->fitAllLiveWagons(true);
    }
}

void MainWindowSHSLogistics::onExportAllJsonClicked()
{
    const auto all = collectUnifiedRecords();
    if (all.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("Aktuell sind keine Live-Waggondaten geladen."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Alle Live-Waggondaten als JSON speichern"),
        QStringLiteral("shs_multi_telematik_live.json"), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    if (Link2RailClient::exportRecordsToJsonFile(all, path, &err)) {
        statusBar()->showMessage(QStringLiteral("Gesamt-JSON erfolgreich gespeichert (%1 Waggons): %2").arg(all.size()).arg(path), 6000);
    } else {
        QMessageBox::warning(this, QStringLiteral("Export-Fehler"), err);
    }
}

void MainWindowSHSLogistics::onExportAllCsvClicked()
{
    const auto all = collectUnifiedRecords();
    if (all.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("Aktuell sind keine Live-Waggondaten geladen."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Alle Live-Waggondaten als CSV speichern"),
        QStringLiteral("shs_multi_telematik_live.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString err;
    if (Link2RailClient::exportRecordsToCsvFile(all, path, &err)) {
        statusBar()->showMessage(QStringLiteral("Gesamt-CSV erfolgreich gespeichert (%1 Waggons): %2").arg(all.size()).arg(path), 6000);
    } else {
        QMessageBox::warning(this, QStringLiteral("Export-Fehler"), err);
    }
}

void MainWindowSHSLogistics::onAnyProviderAuthChanged()
{
    updateHeaderAndConnectionUi();
}

void MainWindowSHSLogistics::onAnyProviderDataUpdated()
{
    rebuildUnifiedLiveData();
}

QVector<Link2RailWagonRecord> MainWindowSHSLogistics::collectUnifiedRecords() const
{
    QVector<Link2RailWagonRecord> merged;
    QHash<QString, int> evnToIndex;

    for (const Link2RailClient *client : m_clients) {
        for (const Link2RailWagonRecord &rec : client->latestRecords()) {
            const QString key = normalizeEvnForMerge(rec.evn);
            if (!key.isEmpty() && evnToIndex.contains(key)) {
                // Gleicher Waggon aus zwei Diensten (z. B. DB link2rail + Wascosa): intelligent anreichern!
                Link2RailWagonRecord &dst = merged[evnToIndex.value(key)];
                if (!dst.hasValidGps && rec.hasValidGps) {
                    dst.hasValidGps = true;
                    dst.rawLat = rec.rawLat;
                    dst.rawLon = rec.rawLon;
                }
                if (dst.trainNumber.isEmpty() && !rec.trainNumber.isEmpty()) dst.trainNumber = rec.trainNumber;
                if (dst.locationName.isEmpty() && !rec.locationName.isEmpty()) dst.locationName = rec.locationName;
                if (dst.geofenceSite.isEmpty() && !rec.geofenceSite.isEmpty()) dst.geofenceSite = rec.geofenceSite;
                if (dst.cargoDescription.isEmpty() && !rec.cargoDescription.isEmpty()) dst.cargoDescription = rec.cargoDescription;
                if (dst.weightTons <= 0.1 && rec.weightTons > 0.1) dst.weightTons = rec.weightTons;
                if (!dst.providerName.contains(rec.providerName)) {
                    dst.providerName += QStringLiteral(" + ") + rec.providerName;
                }
                for (auto it = rec.rawAttributes.constBegin(); it != rec.rawAttributes.constEnd(); ++it) {
                    dst.rawAttributes.insert(rec.providerKey + QStringLiteral(".") + it.key(), it.value());
                }
            } else {
                if (!key.isEmpty()) {
                    evnToIndex.insert(key, merged.size());
                }
                merged.append(rec);
            }
        }
    }
    return merged;
}

void MainWindowSHSLogistics::rebuildUnifiedLiveData()
{
    const auto unified = collectUnifiedRecords();
    m_mapWidget->applyLink2RailRecords(unified);
    updateHeaderAndConnectionUi();
}

void MainWindowSHSLogistics::onLiveFilterChanged()
{
    refreshLiveWagonTable();
}

void MainWindowSHSLogistics::refreshLiveWagonTable()
{
    m_updatingUi = true;
    m_filteredRows.clear();

    const QString provFilter = m_comboProviderFilter ? m_comboProviderFilter->currentData().toString() : QString();
    const QString query = m_editSearchFilter ? m_editSearchFilter->text().trimmed().toLower() : QString();
    const int catFilter = m_comboCategoryFilter ? m_comboCategoryFilter->currentIndex() : 0;
    const auto &trains = m_mapWidget->trains();

    int totalWagons = 0;
    int totalGps = 0;

    for (int tIdx = 0; tIdx < trains.size(); ++tIdx) {
        const TrainConsist &tr = trains[tIdx];
        for (int wIdx = 0; wIdx < tr.wagons.size(); ++wIdx) {
            const WagonData &w = tr.wagons[wIdx];
            totalWagons++;
            if (w.hasValidGps) totalGps++;

            if (!provFilter.isEmpty() && w.providerKey != provFilter) {
                continue;
            }
            if (catFilter > 0 && static_cast<int>(w.category) != (catFilter - 1)) {
                continue;
            }
            if (!query.isEmpty()) {
                const QString hay = (w.providerName + ' ' + w.uicNumber + ' ' + tr.id + ' ' + w.wagonTypeRaw + ' ' +
                                     w.locationName + ' ' + w.geofenceSite + ' ' + w.locationCode + ' ' +
                                     w.liveStatusText + ' ' + w.cargoDescription + ' ' +
                                     w.originName + ' ' + w.destinationName).toLower();
                if (!hay.contains(query)) {
                    continue;
                }
            }
            m_filteredRows.append({tIdx, wIdx});
        }
    }

    m_tableLiveWagons->setRowCount(m_filteredRows.size());
    for (int row = 0; row < m_filteredRows.size(); ++row) {
        const auto ref = m_filteredRows[row];
        const TrainConsist &tr = trains[ref.trainIdx];
        const WagonData &w = tr.wagons[ref.wagonIdx];

        auto *itPrv = new QTableWidgetItem(w.providerName);
        itPrv->setBackground(w.providerColor.isValid() ? w.providerColor : QColor("#0D9488"));
        itPrv->setForeground(Qt::white);

        auto *itEvn = new QTableWidgetItem(w.uicNumber);
        auto *itTrn = new QTableWidgetItem(tr.isSingleWagon ? QStringLiteral("-") : tr.id);
        auto *itTyp = new QTableWidgetItem(w.wagonTypeRaw);
        const QString stTxt = !w.lastEventType.isEmpty()
                                  ? QString("%1 (%2)").arg(w.liveStatusText, w.lastEventType)
                                  : (w.liveStatusText.isEmpty() ? QStringLiteral("Aktiv") : w.liveStatusText);
        auto *itSt  = new QTableWidgetItem(stTxt);
        const QString loc = !w.geofenceSite.isEmpty() ? w.geofenceSite
                                                      : (!w.locationName.isEmpty() ? w.locationName : w.locationCode);
        auto *itLoc = new QTableWidgetItem(loc.isEmpty() ? QStringLiteral("-") : loc);
        auto *itGps = new QTableWidgetItem(w.hasValidGps
                                               ? QString("%1, %2").arg(w.rawGpsLat, 0, 'f', 4).arg(w.rawGpsLon, 0, 'f', 4)
                                               : QStringLiteral("kein GPS"));
        auto *itTs  = new QTableWidgetItem(w.timestampIso.isEmpty() ? QStringLiteral("-") : w.timestampIso);

        const QColor c = RailMapWidget::colorForWagon(w.category, m_mapWidget->wagonColorMode());
        itTyp->setBackground(c);
        itTyp->setForeground(Qt::white);

        m_tableLiveWagons->setItem(row, 0, itPrv);
        m_tableLiveWagons->setItem(row, 1, itEvn);
        m_tableLiveWagons->setItem(row, 2, itTrn);
        m_tableLiveWagons->setItem(row, 3, itTyp);
        m_tableLiveWagons->setItem(row, 4, itSt);
        m_tableLiveWagons->setItem(row, 5, itLoc);
        m_tableLiveWagons->setItem(row, 6, itGps);
        m_tableLiveWagons->setItem(row, 7, itTs);

        if (ref.trainIdx == m_mapWidget->selectedTrainIndex() &&
            ref.wagonIdx == m_mapWidget->selectedWagonIndex()) {
            m_tableLiveWagons->selectRow(row);
        }
    }

    m_lblLiveSummary->setText(
        QString("Angezeigt: %1 von %2 Waggons (%3 mit GPS) aus allen aktiven Telematik-Diensten")
            .arg(m_filteredRows.size()).arg(totalWagons).arg(totalGps));

    m_updatingUi = false;
    refreshSelectedWagonInspector();
}

void MainWindowSHSLogistics::refreshSelectedWagonInspector()
{
    m_tableWagonDetails->setRowCount(0);
    const int tIdx = m_mapWidget->selectedTrainIndex();
    const int wIdx = m_mapWidget->selectedWagonIndex();
    const auto &trains = m_mapWidget->trains();
    if (tIdx < 0 || tIdx >= trains.size()) return;
    const TrainConsist &tr = trains[tIdx];
    const int actualW = (wIdx >= 0 && wIdx < tr.wagons.size()) ? wIdx : (tr.wagons.isEmpty() ? -1 : 0);
    if (actualW < 0) return;

    const WagonData &w = tr.wagons[actualW];

    QVector<QPair<QString, QString>> rows;
    rows.append({QStringLiteral("Telematik-Dienst"), w.providerName});
    rows.append({QStringLiteral("UIC / EVN"), w.uicNumber});
    rows.append({QStringLiteral("Zug / Sendung"), tr.isSingleWagon ? QStringLiteral("Einzelwaggon") : tr.id});
    rows.append({QStringLiteral("Bauart (erkannt)"), QString("%1 -> %2 (%3 Achsen)").arg(w.wagonTypeRaw, RailMapWidget::shortCodeForWagon(w.category)).arg(w.axleCount)});
    rows.append({QStringLiteral("GPS Rohkoordinaten"), w.hasValidGps ? QString("%1° N, %2° E").arg(w.rawGpsLat, 0, 'f', 6).arg(w.rawGpsLon, 0, 'f', 6) : QStringLiteral("Keine GPS-Daten")});
    rows.append({QStringLiteral("Gleis-Snapping"), w.isSnappedToTrack ? QString("Ja (%1)").arg(tr.trackId) : QStringLiteral("Freie GPS-Position")});
    rows.append({QStringLiteral("Status / Ereignis"), QString("%1 %2").arg(w.liveStatusText, w.lastEventType.isEmpty() ? QString() : QString("(%1)").arg(w.lastEventType)).trimmed()});
    rows.append({QStringLiteral("Ort / Betriebsstelle"), QString("%1 %2").arg(w.locationName, w.locationCode.isEmpty() ? QString() : QString("[%1]").arg(w.locationCode)).trimmed()});
    rows.append({QStringLiteral("Geofence / Werk"), w.geofenceSite.isEmpty() ? QStringLiteral("-") : w.geofenceSite});
    rows.append({QStringLiteral("Geschwindigkeit / Standzeit"), QString("%1 km/h | Standzeit: %2 min").arg(w.speedKmh, 0, 'f', 1).arg(w.standingTimeMinutes)});
    rows.append({QStringLiteral("Route (Quelle -> Ziel)"), QString("%1 -> %2").arg(w.originName.isEmpty() ? QStringLiteral("-") : w.originName, w.destinationName.isEmpty() ? QStringLiteral("-") : w.destinationName)});
    rows.append({QStringLiteral("Fracht / Gewicht"), QString("%1 (%2 t)").arg(w.cargoDescription).arg(w.weightTons, 0, 'f', 1)});
    rows.append({QStringLiteral("Zeitstempel / Quelle"), QString("%1 [%2]").arg(w.timestampIso.isEmpty() ? QStringLiteral("-") : w.timestampIso, w.sourceEndpoint)});

    for (auto it = w.rawAttributes.constBegin(); it != w.rawAttributes.constEnd(); ++it) {
        rows.append({QStringLiteral("raw.") + it.key(), it.value().toString()});
    }

    m_tableWagonDetails->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        auto *kIt = new QTableWidgetItem(rows[i].first);
        if (!rows[i].first.startsWith(QStringLiteral("raw."))) {
            QFont f = kIt->font();
            f.setBold(true);
            kIt->setFont(f);
        }
        m_tableWagonDetails->setItem(i, 0, kIt);
        m_tableWagonDetails->setItem(i, 1, new QTableWidgetItem(rows[i].second));
    }
}

void MainWindowSHSLogistics::refreshNodesTable()
{
    const auto &nodes = m_mapWidget->sourceSinkNodes();
    m_tableNodes->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const SourceSinkNode &n = nodes[i];
        m_tableNodes->setItem(i, 0, new QTableWidgetItem(n.id));
        m_tableNodes->setItem(i, 1, new QTableWidgetItem(n.name));
        m_tableNodes->setItem(i, 2, new QTableWidgetItem(n.role));

        auto *itCnt = new QTableWidgetItem(QString("%1 Waggon(s)").arg(n.liveWagonCount));
        if (n.liveWagonCount > 0) {
            itCnt->setBackground(QColor("#065F46"));
            itCnt->setForeground(Qt::white);
        }
        m_tableNodes->setItem(i, 3, itCnt);
        m_tableNodes->setItem(i, 4, new QTableWidgetItem(n.producedGoods + QStringLiteral(" / ") + n.consumedGoods));
    }
}

void MainWindowSHSLogistics::updateHeaderAndConnectionUi()
{
    int activeCnt = 0;
    QStringList activeNames;
    QDateTime latestSync;

    for (int i = 0; i < m_clients.size(); ++i) {
        Link2RailClient *c = m_clients[i];
        const bool isAuth = c->isAuthenticated();
        const int recCnt = c->latestRecords().size();
        if (isAuth) {
            activeCnt++;
            activeNames << c->info().shortName;
        }
        if (c->lastSyncTime().isValid() && (!latestSync.isValid() || c->lastSyncTime() > latestSync)) {
            latestSync = c->lastSyncTime();
        }
        if (m_providerLoginTabs && i < m_providerLoginTabs->count()) {
            const QString dot = isAuth ? QStringLiteral("🟢 ") : (recCnt > 0 ? QStringLiteral("🟡 ") : QStringLiteral("🔴 "));
            m_providerLoginTabs->setTabText(i, dot + c->info().tabLabel);
        }
    }

    const int totalW = m_mapWidget->totalLiveWagonsCount();
    const int gpsW = m_mapWidget->totalLiveGpsWagonsCount();
    const QString syncStr = latestSync.isValid() ? latestSync.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("—");
    const QString summaryStr = activeNames.join(QStringLiteral(", "));

    m_mapWidget->setMultiProviderStatus(activeCnt, m_clients.size(), summaryStr, syncStr);

    if (activeCnt > 0) {
        m_lblHeaderStats->setText(
            QString("🟢 Dienste aktiv: %1/%2 (%3)  |  Waggons: %4 (GPS: %5)  |  Sync: %6")
                .arg(activeCnt).arg(m_clients.size()).arg(summaryStr).arg(totalW).arg(gpsW).arg(syncStr));
        m_btnToolbarLogins->setText(QString("🟢 Dienste && Logins (%1/%2)").arg(activeCnt).arg(m_clients.size()));
        m_btnToolbarLogins->setStyleSheet(
            "QPushButton { background-color: #15803D; color: #FFFFFF; border: 1px solid #1E293B; padding: 5px 10px; font-weight: bold; } "
            "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }");
    } else {
        m_lblHeaderStats->setText(
            QString("🔴 Dienste aktiv: 0/%1  |  Geladene Waggons: %2 (GPS: %3)")
                .arg(m_clients.size()).arg(totalW).arg(gpsW));
        m_btnToolbarLogins->setText(QString("🔐 Dienste && Logins (0/%1)").arg(m_clients.size()));
        m_btnToolbarLogins->setStyleSheet(
            "QPushButton { background-color: #DC2626; color: #FFFFFF; border: 1px solid #1E293B; padding: 5px 10px; font-weight: bold; } "
            "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }");
    }

    if (m_bannerNotLoggedIn) {
        m_bannerNotLoggedIn->setVisible(activeCnt == 0 && totalW == 0);
    }
}

void MainWindowSHSLogistics::onLiveWagonTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (m_updatingUi || row < 0 || row >= m_filteredRows.size()) return;
    const auto ref = m_filteredRows[row];
    m_mapWidget->selectTrain(ref.trainIdx, ref.wagonIdx, false);
    refreshSelectedWagonInspector();
}

void MainWindowSHSLogistics::onLiveWagonTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_filteredRows.size()) return;
    const auto ref = m_filteredRows[row];
    m_mapWidget->zoomToWagon(ref.trainIdx, ref.wagonIdx);
}

void MainWindowSHSLogistics::onMapSelectionChanged(int trainIdx, int wagonIdx)
{
    Q_UNUSED(trainIdx);
    Q_UNUSED(wagonIdx);
    if (m_updatingUi) return;
    refreshLiveWagonTable();
}

void MainWindowSHSLogistics::onTrainsUpdated()
{
    refreshLiveWagonTable();
    updateHeaderAndConnectionUi();
}

void MainWindowSHSLogistics::onNodesUpdated()
{
    refreshNodesTable();
}

void MainWindowSHSLogistics::onNodeTableClicked(int row, int column)
{
    Q_UNUSED(column);
    m_mapWidget->zoomToSourceSinkNode(row);
}

void MainWindowSHSLogistics::onColorModeChanged(int index)
{
    m_mapWidget->setWagonColorMode(static_cast<WagonColorMode>(index));
    refreshLiveWagonTable();
}

void MainWindowSHSLogistics::onMapModeChanged(int index)
{
    m_mapWidget->setMapBackgroundMode(static_cast<MapBackgroundMode>(index));
}

void MainWindowSHSLogistics::onPrintMap()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        QPainter p(&printer);
        const QPixmap pm = m_mapWidget->grab();
        const QRect vp = p.viewport();
        const QSize sz = pm.size().scaled(vp.size(), Qt::KeepAspectRatio);
        p.setViewport(vp.x(), vp.y(), sz.width(), sz.height());
        p.setWindow(pm.rect());
        p.drawPixmap(0, 0, pm);
    }
}

void MainWindowSHSLogistics::onShowAboutDialog()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(QStringLiteral("Über SHS Logistics GmbH – Multi-Telematik Live-GPS-Leitsystem"));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setIconPixmap(QPixmap(QStringLiteral(":/SHSIcon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    aboutBox.setText(QStringLiteral(
        "<h2 style='color:#3B464E; margin-bottom:2px;'>SHS LOGISTICS GmbH</h2>"
        "<h4 style='color:#EF7D00; margin-top:0px;'>Multi-Telematik Live-GPS-, Waggon- &amp; Werksbahn-Leitsystem</h4>"
        "<p style='color:#5C839C; font-style:italic;'>Partner [schafft Zukunft] Stahl</p>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<table style='font-size:11px;' cellpadding='2'>"
        "<tr><td><b>Version:</b></td><td><b>" SHS_APP_VERSION " (Multi-Telematik Live-GPS Edition – 6 Portale)</b></td></tr>"
        "<tr><td><b>Wissenschaftliche Leitung &amp; Entwicklung:</b></td><td><b>Frank U. Rückert</b> (<a href='mailto:frank.rueckert@htwsaar.de' style='color:#0284C7; font-weight:bold;'>frank.rueckert@htwsaar.de</a>)</td></tr>"
        "<tr><td><b>Entwicklerin:</b></td><td><b>Sevda Dawd</b> (<a href='mailto:sevdasdawd@gmail.com' style='color:#0284C7; font-weight:bold;'>sevdasdawd@gmail.com</a>)</td></tr>"
        "<tr><td><b>Institution:</b></td><td>htw saar – Hochschule für Technik und Wirtschaft des Saarlandes</td></tr>"
        "<tr><td><b>Kooperationspartner:</b></td><td>SHS Logistics GmbH (Stahl-Holding-Saar), Werkstraße 1, 66763 Dillingen/Saar</td></tr>"
        "<tr><td><b>Software-Plattform:</b></td><td>C++17 / Qt 6.9.0 (MinGW 64-Bit, TLS/HTTPS SChannel, CMake + Ninja)</td></tr>"
        "</table>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<p><b>Integrierte Telematik-Dienste (jeweils mit eigenem Login-Reiter auf einer gemeinsamen Karte):</b></p>"
        "<ul style='font-size:10.5px;'>"
        "<li><b>DB Cargo link2rail:</b> <code>link2rail.dbcargo.com</code> (CIDP Login API V3, Geofence Control, Track &amp; Trace)</li>"
        "<li><b>Intermodal Telematics (IMT):</b> <code>portal.intermodaltelematics.com</code></li>"
        "<li><b>DOT Telematik:</b> <code>portal.dot-telematik.com/login</code> (DOT-Link &amp; Unified API)</li>"
        "<li><b>TRANSWAGGON (WOPS):</b> <code>portal.transwaggon.de/csp/wops/</code> (JSON / XML / CSV)</li>"
        "<li><b>Innofreight OS:</b> <code>os.innofreight.com/login</code></li>"
        "<li><b>Wascosa:</b> <code>wascosa.com</code> (inkl. Vorlagen für SAVVY® Synergy &amp; Nexxiot Connect Cloud)</li>"
        "</ul>"));
    aboutBox.setStandardButtons(QMessageBox::Ok);
    aboutBox.exec();
}

QPixmap MainWindowSHSLogistics::createCorporateSplashPixmap(int progressPercent, const QString &statusText)
{
    constexpr int W = 860;
    constexpr int H = 510;
    QPixmap pm(W, H);
    pm.fill(QColor("#3B464E"));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor("#3B464E"));
    bgGrad.setColorAt(0.5, QColor("#48555F"));
    bgGrad.setColorAt(1.0, QColor("#293238"));
    p.fillRect(0, 0, W, H, bgGrad);

    p.setPen(QPen(QColor("#EF7D00"), 3.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.0, 2.0, W - 4.0, H - 4.0), 8.0, 8.0);

    QRectF headerRect(16.0, 14.0, W - 32.0, 74.0);
    QLinearGradient hdrGrad(headerRect.left(), 0, headerRect.right(), 0);
    hdrGrad.setColorAt(0.0, QColor("#FFFFFF"));
    hdrGrad.setColorAt(0.36, QColor("#E4EEF2"));
    hdrGrad.setColorAt(0.68, QColor("#48555F"));
    hdrGrad.setColorAt(1.0, QColor("#333E46"));
    p.setBrush(hdrGrad);
    p.setPen(QPen(QColor("#5C839C"), 1.5));
    p.drawRoundedRect(headerRect, 6.0, 6.0);

    QPixmap logoPm(QStringLiteral(":/SHS_Logo.png"));
    if (logoPm.isNull()) {
        logoPm.load(QStringLiteral("SHS_Logo.png"));
    }
    if (!logoPm.isNull()) {
        const QPixmap scaledLogo = logoPm.scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap(28, 22, scaledLogo);
    }

    p.setPen(QColor("#1E293B"));
    p.setFont(QFont("Verdana", 16, QFont::Bold));
    p.drawText(QRectF(100.0, 16.0, 480.0, 26.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("SHS LOGISTICS GmbH"));

    p.setPen(QColor("#334155"));
    p.setFont(QFont("Verdana", 8.5, QFont::Bold));
    p.drawText(QRectF(100.0, 41.0, 500.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Partner [schafft Zukunft] Stahl — Multi-Telematik Live-GPS-Leitsystem"));

    p.setPen(QColor("#0284C7"));
    p.setFont(QFont("Segoe UI", 7.8, QFont::Bold));
    p.drawText(QRectF(100.0, 60.0, 515.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Leitung: Frank U. Rückert (frank.rueckert@htwsaar.de)  |  Entwicklung: Sevda Dawd (sevdasdawd@gmail.com)"));

    QRectF verRect(W - 245.0, 27.0, 215.0, 44.0);
    p.setBrush(QColor(15, 23, 42, 185));
    p.setPen(QPen(QColor("#FBD50E"), 1.5));
    p.drawRoundedRect(verRect, 6.0, 6.0);
    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.drawText(verRect.adjusted(8, 4, -8, -20), Qt::AlignCenter,
               QStringLiteral("Version %1  •  C++17 / Qt 6.9").arg(QStringLiteral(SHS_APP_VERSION)));
    p.setPen(QColor("#E2E8F0"));
    p.setFont(QFont("Segoe UI", 8));
    p.drawText(verRect.adjusted(8, 22, -8, -4), Qt::AlignCenter,
               QStringLiteral("6 Telematik-Portale | Live-GPS"));

    QRectF heroImgRect(16.0, 98.0, 548.0, 308.0);
    p.setBrush(QColor("#FFFFFF"));
    p.setPen(QPen(QColor("#A2B1BE"), 2.0));
    p.drawRoundedRect(heroImgRect, 7.0, 7.0);

    QPixmap heroPm(QStringLiteral(":/startseite_img_de.jpg"));
    if (heroPm.isNull()) {
        heroPm.load(QStringLiteral("startseite_img_de.jpg"));
    }
    if (!heroPm.isNull()) {
        const QRectF innerImg = heroImgRect.adjusted(4.0, 4.0, -4.0, -4.0);
        const QPixmap scaledHero = heroPm.scaled(innerImg.size().toSize(),
                                                 Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation);
        QPainterPath clipPath;
        clipPath.addRoundedRect(innerImg, 5.0, 5.0);
        p.save();
        p.setClipPath(clipPath);
        p.drawPixmap(innerImg.topLeft(), scaledHero);
        p.restore();
    }

    QRectF sideRect(576.0, 98.0, W - 592.0, 308.0);
    p.setBrush(QColor(22, 30, 38, 228));
    p.setPen(QPen(QColor("#5C839C"), 1.5));
    p.drawRoundedRect(sideRect, 7.0, 7.0);

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Verdana", 9.2, QFont::Bold));
    p.drawText(QRectF(sideRect.left() + 12, sideRect.top() + 10, sideRect.width() - 24, 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("INTEGRIERTE TELEMATIK-DIENSTE"));
    p.setPen(QPen(QColor("#EF7D00"), 2.0));
    p.drawLine(QPointF(sideRect.left() + 12, sideRect.top() + 34),
               QPointF(sideRect.right() - 12, sideRect.top() + 34));

    const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("🔐 DB Cargo link2rail"),        QStringLiteral("CIDP Login, Geofence & Track&Trace")},
        {QStringLiteral("🔐 Intermodal Telematics"),     QStringLiteral("portal.intermodaltelematics.com")},
        {QStringLiteral("🔐 DOT Telematik"),             QStringLiteral("portal.dot-telematik.com/login")},
        {QStringLiteral("🔐 TRANSWAGGON (WOPS)"),        QStringLiteral("portal.transwaggon.de (JSON/XML)")},
        {QStringLiteral("🔐 Innofreight OS"),            QStringLiteral("os.innofreight.com/login")},
        {QStringLiteral("🔐 Wascosa Telematik"),         QStringLiteral("SAVVY Synergy / Nexxiot / Wascosa")}
    };

    double yCur = sideRect.top() + 44.0;
    for (const auto &it : items) {
        QRectF card(sideRect.left() + 10, yCur, sideRect.width() - 20, 38.0);
        p.setBrush(QColor(46, 58, 68, 215));
        p.setPen(QPen(QColor("#56636D"), 1.0));
        p.drawRoundedRect(card, 5.0, 5.0);

        p.setPen(QColor("#FFFFFF"));
        p.setFont(QFont("Segoe UI", 8.5, QFont::Bold));
        p.drawText(card.adjusted(8, 3, -6, -18), Qt::AlignLeft | Qt::AlignVCenter, it.first);

        p.setPen(QColor("#CBD5E1"));
        p.setFont(QFont("Segoe UI", 7.8));
        p.drawText(card.adjusted(14, 19, -6, -2), Qt::AlignLeft | Qt::AlignVCenter, it.second);
        yCur += 42.5;
    }

    QRectF footRect(16.0, 416.0, W - 32.0, 78.0);
    p.setBrush(QColor(15, 23, 42, 230));
    p.setPen(QPen(QColor("#5C839C"), 1.4));
    p.drawRoundedRect(footRect, 6.0, 6.0);

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.drawText(QRectF(footRect.left() + 14, footRect.top() + 8, footRect.width() - 95, 18),
               Qt::AlignLeft | Qt::AlignVCenter, statusText);

    p.setPen(QColor("#E2E8F0"));
    p.drawText(QRectF(footRect.right() - 78, footRect.top() + 8, 64, 18),
               Qt::AlignRight | Qt::AlignVCenter,
               QString("%1 %").arg(std::clamp(progressPercent, 0, 100)));

    QRectF barBg(footRect.left() + 14, footRect.top() + 31, footRect.width() - 28, 14.0);
    p.setBrush(QColor("#1E293B"));
    p.setPen(QPen(QColor("#475569"), 1.0));
    p.drawRoundedRect(barBg, 4.0, 4.0);

    const double fillW = (barBg.width() - 2.0) * (std::clamp(progressPercent, 0, 100) / 100.0);
    if (fillW > 2.0) {
        QRectF barFill(barBg.left() + 1.0, barBg.top() + 1.0, fillW, barBg.height() - 2.0);
        QLinearGradient barGrad(barFill.left(), 0, barFill.right(), 0);
        barGrad.setColorAt(0.0, QColor("#EA580C"));
        barGrad.setColorAt(0.6, QColor("#EF7D00"));
        barGrad.setColorAt(1.0, QColor("#FBD50E"));
        p.setBrush(barGrad);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(barFill, 3.0, 3.0);
    }

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 7.6, QFont::Bold));
    p.drawText(QRectF(footRect.left() + 14, footRect.top() + 52, footRect.width() - 235, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Leitung: Frank U. Rückert (frank.rueckert@htwsaar.de) • Entwicklung: Sevda Dawd (sevdasdawd@gmail.com)"));

    p.setPen(QColor("#94A3B8"));
    p.setFont(QFont("Segoe UI", 7.6));
    p.drawText(QRectF(footRect.right() - 220, footRect.top() + 52, 206, 18),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("SHS Logistics GmbH • htw saar"));

    return pm;
}

void MainWindowSHSLogistics::openPdfManualFile(const QString &pdfFileName, const QString &fallbackTitle)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QLatin1Char('/') + pdfFileName,
        QDir::cleanPath(appDir + QStringLiteral("/../../..") + QLatin1Char('/') + pdfFileName),
        QDir::currentPath() + QLatin1Char('/') + pdfFileName
    };

    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            statusBar()->showMessage(QStringLiteral("Öffne PDF-Manual: %1").arg(path), 6000);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
    }

    const QString generatedPdf = appDir + QLatin1Char('/') + pdfFileName;
    QPrinter pdfPrinter(QPrinter::HighResolution);
    pdfPrinter.setOutputFormat(QPrinter::PdfFormat);
    pdfPrinter.setOutputFileName(generatedPdf);

    QTextDocument doc;
    doc.setHtml(QStringLiteral(
        "<h1 style='color:#3B464E;'>%1</h1>"
        "<h3 style='color:#EF7D00;'>SHS Logistics GmbH – Multi-Telematik Live-GPS-Leitsystem (Version %2)</h3>"
        "<p><b>Leitung &amp; Entwicklung:</b> Frank U. Rückert (frank.rueckert@htwsaar.de) &amp; Sevda Dawd (sevdasdawd@gmail.com), htw saar</p>"
        "<p><b>6 integrierte Telematik-Dienste:</b> DB Cargo link2rail, Intermodal Telematics (IMT), DOT Telematik, TRANSWAGGON (WOPS), Innofreight OS, Wascosa.</p>")
            .arg(fallbackTitle, QStringLiteral(SHS_APP_VERSION)));
    doc.print(&pdfPrinter);

    statusBar()->showMessage(QStringLiteral("Öffne PDF-Manual: %1").arg(generatedPdf), 6000);
    QDesktopServices::openUrl(QUrl::fromLocalFile(generatedPdf));
}

void MainWindowSHSLogistics::onOpenMainManualPdf()
{
    openPdfManualFile(
        QStringLiteral("SHS_Logistics_Handbuch_und_Projektbeschreibung.pdf"),
        QStringLiteral("SHS Logistics GmbH – Technisches Handbuch & GUI-Bedienungsanleitung"));
}

void MainWindowSHSLogistics::onOpenQuickStartPdf()
{
    openPdfManualFile(
        QStringLiteral("SHS_Logistics_Kurzanleitung_und_Schnellstart.pdf"),
        QStringLiteral("SHS Logistics GmbH – Kurzanleitung & Schnellstart-Manual"));
}

void MainWindowSHSLogistics::onOpenManualsFolder()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(appDir));
}

void MainWindowSHSLogistics::onShowSplashPreviewDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("SHS Logistics GmbH – Start-Splashscreen (Version %1)").arg(QStringLiteral(SHS_APP_VERSION)));
    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *lbl = new QLabel(&dlg);
    lbl->setPixmap(createCorporateSplashPixmap(
        100, QStringLiteral("Multi-Telematik Live-GPS-Leitsystem aktiv (6 Portale).")));
    lay->addWidget(lbl);
    dlg.setFixedSize(860, 510);
    dlg.exec();
}

void MainWindowSHSLogistics::scheduleAutoScreenshots(const QString &outDir)
{
    createCorporateSplashPixmap(
        100, QStringLiteral("Alle 6 Telematik-Dienste bereit – bitte in den Login-Reitern anmelden!"))
        .save(outDir + QStringLiteral("/preview_splash.png"));

    QTimer::singleShot(2000, this, [this, outDir]() {
        this->grab().save(outDir + QStringLiteral("/preview_voelklingen.png"));
        m_sideTabs->setCurrentIndex(1); // Tab 2: 6 Login-Reiter
        if (m_providerLoginTabs) m_providerLoginTabs->setCurrentIndex(0);
        QTimer::singleShot(700, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/preview_api_tab.png"));
            m_sideTabs->setCurrentIndex(2); // Tab 3: Werke & Geofences
            m_mapWidget->focusSite(QStringLiteral("Dillingen"), false);
            QTimer::singleShot(900, this, [this, outDir]() {
                this->grab().save(outDir + QStringLiteral("/preview_dillingen.png"));
                m_mapWidget->focusSite(QStringLiteral("Gesamt"), false);
                QTimer::singleShot(900, this, [this, outDir]() {
                    this->grab().save(outDir + QStringLiteral("/preview_corridor.png"));
                    this->close();
                });
            });
        });
    });
}
