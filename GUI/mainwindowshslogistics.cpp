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
#define SHS_APP_VERSION "1.0"
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

    setWindowTitle(QStringLiteral("SHS Logistics GmbH – Werksbahn-Simulation & Multi-Telematik Live-GPS (6 Portale)  |  Version %1")
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

    m_simApiPollTimer = new QTimer(this);
    m_simApiPollTimer->setInterval(5000);
    connect(m_simApiPollTimer, &QTimer::timeout, this, &MainWindowSHSLogistics::onQuerySimDbApiClicked);

    // Initiale Daten beider Modi befüllen (Start im interaktiven Simulationsmodus)
    refreshSimTrainSelectorAndControls();
    refreshSimWagonTable();
    refreshSimSourcesSinksAndCargoTables();
    refreshLiveWagonTable();
    refreshLiveNodesTable();
    updateHeaderAndModeUi();

    statusBar()->showMessage(
        QStringLiteral("SHS Logistics Leitsystem v%1 bereit: Oben jederzeit zwischen 🎮 Simulationsmodus (Spiel) und 📡 Live-GPS-Modus (6 Portale) wechseln!")
            .arg(QStringLiteral(SHS_APP_VERSION)));
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
    rootLayout->setSpacing(5);

    // =========================================================================
    // 1. SHS LOGISTICS CORPORATE HEADER
    // =========================================================================
    auto *headerFrame = new QFrame(root);
    headerFrame->setStyleSheet(
        "QFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #FFFFFF, stop:0.28 #E4EEF2, stop:0.55 #48555F, stop:1.0 #3B464E); "
        "border: 1px solid #56636D; border-radius: 5px; }");
    auto *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(12, 5, 12, 5);
    headerLayout->setSpacing(12);

    auto *lblLogo = new QLabel(headerFrame);
    QPixmap logoPm(QStringLiteral(":/SHS_Logo.png"));
    if (logoPm.isNull()) {
        logoPm.load(QStringLiteral("SHS_Logo.png"));
    }
    if (!logoPm.isNull()) {
        lblLogo->setPixmap(logoPm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    lblLogo->setStyleSheet("border: none; background: transparent;");
    headerLayout->addWidget(lblLogo);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(1);
    auto *lblCompany = new QLabel(QStringLiteral("SHS LOGISTICS GmbH"), headerFrame);
    lblCompany->setStyleSheet(
        "font-family: Verdana, Arial; font-size: 17px; font-weight: bold; "
        "color: #1E293B; letter-spacing: 1.1px; border: none; background: transparent;");
    auto *lblSlogan = new QLabel(
        QStringLiteral("Partner <span style='color:#5C839C;font-weight:bold;'>[</span>"
                       "schafft <i>Zukunft</i>"
                       "<span style='color:#EF7D00;font-weight:bold;'>]</span> Stahl "
                       "— Werksbahn-Simulation &amp; Multi-Telematik Live-GPS &nbsp;|&nbsp; "
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
    // 2. MODUS-UMSCHALTER + STANDORT-SCHNELLWAHL + KONTEXT-AKTIONEN
    // =========================================================================
    auto *navFrame = new QFrame(root);
    navFrame->setStyleSheet(
        "QFrame { background-color: #48555F; border: 1px solid #56636D; border-radius: 4px; }");
    auto *navOuter = new QVBoxLayout(navFrame);
    navOuter->setContentsMargins(7, 4, 7, 4);
    navOuter->setSpacing(4);

    // Zeile 1: Prominenter Modus-Umschalter (Spiel/Simulation <-> Live-GPS) + Standorte
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(5);

    auto *lblModeTag = new QLabel(QStringLiteral("MODUS:"), navFrame);
    lblModeTag->setStyleSheet("color: #FBD50E; font-weight: bold; font-size: 10px; border: none; background: transparent;");
    row1->addWidget(lblModeTag);

    m_btnModeSim = new QPushButton(QStringLiteral("🎮 SIMULATIONSMODUS (Spiel)"), navFrame);
    m_btnModeSim->setCheckable(true);
    m_btnModeSim->setChecked(true);
    m_btnModeSim->setToolTip(QStringLiteral("Interaktive 2D-Werksbahnsimulation mit Zügen, Waggon-Editor, Gütern, Quellen/Senken & Lok-Pool (Ctrl+1)"));

    m_btnModeLive = new QPushButton(QStringLiteral("📡 LIVE-GPS-MODUS (6 Portale)"), navFrame);
    m_btnModeLive->setCheckable(true);
    m_btnModeLive->setChecked(false);
    m_btnModeLive->setToolTip(QStringLiteral("Reale Live-GPS-Daten aus DB link2rail, IMT, DOT, TRANSWAGGON, Innofreight & Wascosa (Ctrl+2)"));

    row1->addWidget(m_btnModeSim);
    row1->addWidget(m_btnModeLive);

    auto *vSep = new QFrame(navFrame);
    vSep->setFrameShape(QFrame::VLine);
    vSep->setStyleSheet("color: #94A3B8; background-color: #94A3B8; max-width: 2px;");
    row1->addWidget(vSep);

    auto makeNavBtn = [&](QHBoxLayout *targetRow, const QString &text, const QString &accentHex = QString()) {
        auto *btn = new QPushButton(text, navFrame);
        if (!accentHex.isEmpty()) {
            btn->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: #FFFFFF; border: 1px solid #1E293B; "
                "padding: 4px 9px; font-weight: bold; } "
                "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }").arg(accentHex));
        }
        targetRow->addWidget(btn);
        return btn;
    };

    auto *btnZoomVK = makeNavBtn(row1, QStringLiteral("🏭 VÖLKLINGEN"), QStringLiteral("#EF7D00"));
    auto *btnZoomDL = makeNavBtn(row1, QStringLiteral("🔥 DILLINGEN"));
    auto *btnZoomBB = makeNavBtn(row1, QStringLiteral("⚙️ BURBACH"));
    auto *btnZoomNK = makeNavBtn(row1, QStringLiteral("🔩 NEUNKIRCHEN"));
    auto *btnZoomSL = makeNavBtn(row1, QStringLiteral("🗺️ SAARLAND"));
    auto *btnZoomDE = makeNavBtn(row1, QStringLiteral("🇩🇪 DEUTSCHLAND"));
    row1->addStretch(1);
    navOuter->addLayout(row1);

    // Zeile 2: Kontextabhängige Schnell-Aktionsleiste (je nach gewähltem Modus!)
    m_quickActionStack = new QStackedWidget(navFrame);
    m_quickActionStack->setMaximumHeight(32);

    // Seite 0: Simulations-Schnellaktionen
    auto *simQuickPage = new QWidget(m_quickActionStack);
    simQuickPage->setStyleSheet("background: transparent;");
    auto *simQuickLay = new QHBoxLayout(simQuickPage);
    simQuickLay->setContentsMargins(0, 0, 0, 0);
    simQuickLay->setSpacing(5);

    m_btnPlayPause          = makeNavBtn(simQuickLay, QStringLiteral("⏸ Simulation Pause (Space)"), QStringLiteral("#0284C7"));
    auto *btnQuickSamms     = makeNavBtn(simQuickLay, QStringLiteral("➕ Samms (6A)"), QStringLiteral("#EA580C"));
    auto *btnQuickRnsz      = makeNavBtn(simQuickLay, QStringLiteral("➕ Rns-z (4A)"), QStringLiteral("#0369A1"));
    auto *btnQuickTorpedo   = makeNavBtn(simQuickLay, QStringLiteral("🔥 + Torpedo (Flüssigeisen)"), QStringLiteral("#E11D48"));
    auto *btnQuickRemove    = makeNavBtn(simQuickLay, QStringLiteral("➖ Waggon ab"), QStringLiteral("#B91C1C"));
    auto *btnQuickLoadSrc   = makeNavBtn(simQuickLay, QStringLiteral("⬆️ Quelle beladen"), QStringLiteral("#15803D"));
    auto *btnQuickUnloadSnk = makeNavBtn(simQuickLay, QStringLiteral("⬇️ Senke entladen"), QStringLiteral("#0369A1"));
    auto *btnQuickRandom    = makeNavBtn(simQuickLay, QStringLiteral("🎲 Zufalls-Pos. (F8)"), QStringLiteral("#7C3AED"));
    auto *btnQuickSimApi    = makeNavBtn(simQuickLay, QStringLiteral("📡 Sim-Telematik"), QStringLiteral("#0D9488"));
    simQuickLay->addStretch(1);
    m_quickActionStack->addWidget(simQuickPage);

    // Seite 1: Live-GPS-Schnellaktionen
    auto *liveQuickPage = new QWidget(m_quickActionStack);
    liveQuickPage->setStyleSheet("background: transparent;");
    auto *liveQuickLay = new QHBoxLayout(liveQuickPage);
    liveQuickLay->setContentsMargins(0, 0, 0, 0);
    liveQuickLay->setSpacing(6);

    m_btnToolbarLogins      = makeNavBtn(liveQuickLay, QStringLiteral("🔐 Dienste && Logins (0/6)"), QStringLiteral("#DC2626"));
    m_btnToolbarSyncAll     = makeNavBtn(liveQuickLay, QStringLiteral("🔄 Alle Dienste abrufen (F9)"), QStringLiteral("#0D9488"));
    auto *btnZoomFit        = makeNavBtn(liveQuickLay, QStringLiteral("🎯 ALLE LIVE-WAGGONS (F7)"), QStringLiteral("#0284C7"));
    auto *btnQuickImp       = makeNavBtn(liveQuickLay, QStringLiteral("📂 JSON / XML / CSV laden"));
    auto *btnQuickExpJson   = makeNavBtn(liveQuickLay, QStringLiteral("💾 Gesamt-JSON"));
    liveQuickLay->addStretch(1);
    m_quickActionStack->addWidget(liveQuickPage);

    navOuter->addWidget(m_quickActionStack);
    rootLayout->addWidget(navFrame);

    // =========================================================================
    // 3. HAUPTBEREICH: LINKS MODUS-SEITENLEISTE + RECHTS 2D-KARTE
    // =========================================================================
    auto *mainSplitter = new QSplitter(Qt::Horizontal, root);
    mainSplitter->setHandleWidth(6);

    m_mapWidget = new RailMapWidget();

    m_sidePanelStack = new QStackedWidget(mainSplitter);
    m_sidePanelStack->setMinimumWidth(590);
    m_sidePanelStack->setMaximumWidth(820);

    buildSimulationSidePanel();
    buildLiveGpsSidePanel();

    m_sidePanelStack->addWidget(m_simSideTabs);
    m_sidePanelStack->addWidget(m_liveSideTabs);
    m_sidePanelStack->setCurrentIndex(0);

    mainSplitter->addWidget(m_sidePanelStack);
    mainSplitter->addWidget(m_mapWidget);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({640, 1280});

    rootLayout->addWidget(mainSplitter, 1);

    // Initiale Standard-Auswahl für neuen Güterauftrag setzen
    onNewOrderProductChanged(0);

    // =========================================================================
    // 4. SIGNAL-SLOT VERBINDUNGEN (MODUS, STANDORTE, TOOLBAR, KARTE)
    // =========================================================================
    connect(m_btnModeSim,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onSwitchToSimulationMode);
    connect(m_btnModeLive, &QPushButton::clicked, this, &MainWindowSHSLogistics::onSwitchToLiveGpsMode);

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

    // Simulation Quick-Bar
    connect(m_btnPlayPause,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onToggleSimClicked);
    connect(btnQuickSamms,      &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddSammsClicked);
    connect(btnQuickRnsz,       &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddRnszClicked);
    connect(btnQuickTorpedo,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onAdd6AxleClicked);
    connect(btnQuickRemove,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onRemoveWagonClicked);
    connect(btnQuickLoadSrc,    &QPushButton::clicked, this, [this]() {
        ensureSimulationModeForAction();
        m_mapWidget->loadTrainAtSourceNow(m_mapWidget->selectedTrainIndex());
    });
    connect(btnQuickUnloadSnk,  &QPushButton::clicked, this, [this]() {
        ensureSimulationModeForAction();
        m_mapWidget->unloadTrainAtSinkNow(m_mapWidget->selectedTrainIndex());
    });
    connect(btnQuickRandom,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    connect(btnQuickSimApi,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onQuerySimDbApiClicked);

    // Live-GPS Quick-Bar
    connect(m_btnToolbarLogins,  &QPushButton::clicked, this, [this]() {
        onSwitchToLiveGpsMode();
        m_liveSideTabs->setCurrentIndex(1);
    });
    connect(m_btnToolbarSyncAll, &QPushButton::clicked, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    connect(btnZoomFit,          &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });
    connect(btnQuickImp,         &QPushButton::clicked, this, &MainWindowSHSLogistics::onQuickImportActiveProviderClicked);
    connect(btnQuickExpJson,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onExportAllJsonClicked);

    // MapWidget Signale
    connect(m_mapWidget, &RailMapWidget::operatingModeChanged, this, &MainWindowSHSLogistics::onOperatingModeChanged);
    connect(m_mapWidget, &RailMapWidget::selectionChanged,     this, &MainWindowSHSLogistics::onMapSelectionChanged);
    connect(m_mapWidget, &RailMapWidget::trainsUpdated,        this, &MainWindowSHSLogistics::onTrainsUpdated);
    connect(m_mapWidget, &RailMapWidget::cargoAndNodesUpdated, this, &MainWindowSHSLogistics::onCargoAndNodesUpdated);
    connect(m_mapWidget, &RailMapWidget::simulationStepped,    this, &MainWindowSHSLogistics::onSimStepped);
    connect(m_mapWidget, &RailMapWidget::statusMessage,        this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 8000);
    });
    connect(m_mapWidget, &RailMapWidget::apiLogMessage,        this, [this](const QString &jsonText) {
        if (m_txtSimApiLog) m_txtSimApiLog->setPlainText(jsonText);
    });
}

void MainWindowSHSLogistics::buildSimulationSidePanel()
{
    m_simSideTabs = new QTabWidget(m_sidePanelStack);

    // -------------------------------------------------------------------------
    // SIM TAB 1: ZÜGE, WAGGONS & ZUGLÄNGE
    // -------------------------------------------------------------------------
    auto *tabTrains = new QWidget();
    auto *tabTrainsLayout = new QVBoxLayout(tabTrains);
    tabTrainsLayout->setContentsMargins(8, 8, 8, 8);
    tabTrainsLayout->setSpacing(7);

    auto *grpSelect = new QGroupBox(QStringLiteral("1. Zug / Gelagerte Waggongruppe && Gleis"), tabTrains);
    auto *grpSelGrid = new QGridLayout(grpSelect);

    grpSelGrid->addWidget(new QLabel(QStringLiteral("Verband wählen:")), 0, 0);
    m_comboTrain = new QComboBox(grpSelect);
    grpSelGrid->addWidget(m_comboTrain, 0, 1, 1, 2);

    grpSelGrid->addWidget(new QLabel(QStringLiteral("Gleisabschnitt:")), 1, 0);
    m_comboTrackAssign = new QComboBox(grpSelect);
    for (const RailTrack &tr : m_mapWidget->tracks()) {
        m_comboTrackAssign->addItem(QString("%1 (%2)").arg(tr.name, tr.id), tr.id);
    }
    grpSelGrid->addWidget(m_comboTrackAssign, 1, 1, 1, 2);

    auto *btnReverse = new QPushButton(QStringLiteral("🔄 Richtung wenden"), grpSelect);
    auto *btnCoupleLoco1 = new QPushButton(QStringLiteral("🚂 Freie Lok ankuppeln"), grpSelect);
    btnCoupleLoco1->setStyleSheet("background-color: #15803D; color: white;");
    auto *btnUncoupleLoco1 = new QPushButton(QStringLiteral("🅿️ Lok abkuppeln (Lagern)"), grpSelect);
    btnUncoupleLoco1->setStyleSheet("background-color: #7E22CE; color: white;");

    grpSelGrid->addWidget(btnReverse, 2, 0);
    grpSelGrid->addWidget(btnCoupleLoco1, 2, 1);
    grpSelGrid->addWidget(btnUncoupleLoco1, 2, 2);
    tabTrainsLayout->addWidget(grpSelect);

    auto *grpLength = new QGroupBox(QStringLiteral("2. Zuglänge ändern && Waggons anhängen / abhängen"), tabTrains);
    auto *grpLenGrid = new QGridLayout(grpLength);

    grpLenGrid->addWidget(new QLabel(QStringLiteral("Waggon-Anzahl:")), 0, 0);
    m_spinWagonCount = new QSpinBox(grpLength);
    m_spinWagonCount->setRange(1, 30);
    m_spinWagonCount->setValue(6);
    grpLenGrid->addWidget(m_spinWagonCount, 0, 1);

    m_lblTrainMetrics = new QLabel(QStringLiteral("Länge: - | Achsen: -"), grpLength);
    m_lblTrainMetrics->setStyleSheet("color: #334155; font-weight: bold;");
    grpLenGrid->addWidget(m_lblTrainMetrics, 0, 2, 1, 2);

    auto *btnAddSamms = new QPushButton(QStringLiteral("➕ Samms (6A)"), grpLength);
    btnAddSamms->setStyleSheet("background-color: #EF7D00; color: white;");
    auto *btnAddRnsz = new QPushButton(QStringLiteral("➕ Rns-z (4A)"), grpLength);
    btnAddRnsz->setStyleSheet("background-color: #0284C7; color: white;");
    auto *btnAdd4A = new QPushButton(QStringLiteral("➕ 4-Achser"), grpLength);
    btnAdd4A->setStyleSheet("background-color: #10B981; color: white;");
    auto *btnAdd6A = new QPushButton(QStringLiteral("🔥 + Torpedo (Flüssigeisen)"), grpLength);
    btnAdd6A->setStyleSheet("background-color: #E11D48; color: white;");
    auto *btnRemoveLast = new QPushButton(QStringLiteral("➖ Letzten Waggon abhängen (Zug verkürzen)"), grpLength);
    btnRemoveLast->setStyleSheet("background-color: #B91C1C; color: white;");

    grpLenGrid->addWidget(btnAddSamms, 1, 0);
    grpLenGrid->addWidget(btnAddRnsz,  1, 1);
    grpLenGrid->addWidget(btnAdd4A,    1, 2);
    grpLenGrid->addWidget(btnAdd6A,    1, 3);
    grpLenGrid->addWidget(btnRemoveLast, 2, 0, 1, 4);
    tabTrainsLayout->addWidget(grpLength);

    auto *grpWagons = new QGroupBox(QStringLiteral("3. Waggon-Positionen, Bauart && Geladene Güter"), tabTrains);
    auto *grpWagLayout = new QVBoxLayout(grpWagons);

    auto *colorRow = new QHBoxLayout();
    colorRow->addWidget(new QLabel(QStringLiteral("Farbmodus:")));
    m_comboSimColorMode = new QComboBox(grpWagons);
    m_comboSimColorMode->addItem(QStringLiteral("Alle 4 Bauarten getrennt (Samms=Orange, Rns-z=Blau, 4A=Grün, 6A=Rot)"));
    m_comboSimColorMode->addItem(QStringLiteral("Fokus Bauart: Samms vs. Rns-z hervorheben"));
    m_comboSimColorMode->addItem(QStringLiteral("Strikt nach Achsen: Alle 4-Achser (Blau) vs. Alle 6-Achser (Orange)"));
    colorRow->addWidget(m_comboSimColorMode, 1);
    grpWagLayout->addLayout(colorRow);

    m_tableSimWagons = new QTableWidget(0, 6, grpWagons);
    m_tableSimWagons->setHorizontalHeaderLabels({
        QStringLiteral("#Pos"),
        QStringLiteral("UIC-Nummer"),
        QStringLiteral("Bauart"),
        QStringLiteral("Achsen"),
        QStringLiteral("Geladenes Gut"),
        QStringLiteral("Gewicht")
    });
    m_tableSimWagons->setAlternatingRowColors(true);
    m_tableSimWagons->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableSimWagons->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableSimWagons->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableSimWagons->verticalHeader()->setVisible(false);
    m_tableSimWagons->horizontalHeader()->setStretchLastSection(true);
    m_tableSimWagons->setColumnWidth(0, 42);
    m_tableSimWagons->setColumnWidth(1, 122);
    m_tableSimWagons->setColumnWidth(2, 95);
    m_tableSimWagons->setColumnWidth(3, 52);
    m_tableSimWagons->setColumnWidth(4, 155);
    grpWagLayout->addWidget(m_tableSimWagons, 1);

    auto *editRow = new QHBoxLayout();
    m_comboEditWagonType = new QComboBox(grpWagons);
    m_comboEditWagonType->addItem(QStringLiteral("Samms [6A]"));
    m_comboEditWagonType->addItem(QStringLiteral("Rns-z [4A]"));
    m_comboEditWagonType->addItem(QStringLiteral("Shimmns [4A]"));
    m_comboEditWagonType->addItem(QStringLiteral("Torpedo [6A]"));

    m_comboEditWagonProduct = new QComboBox(grpWagons);
    for (int p = 0; p <= static_cast<int>(CargoProductType::EmptyWagon); ++p) {
        m_comboEditWagonProduct->addItem(RailMapWidget::productShortName(static_cast<CargoProductType>(p)));
    }
    auto *btnApplyWagonType = new QPushButton(QStringLiteral("✓ Waggon + Gut setzen"), grpWagons);
    editRow->addWidget(m_comboEditWagonType);
    editRow->addWidget(m_comboEditWagonProduct);
    editRow->addWidget(btnApplyWagonType);
    grpWagLayout->addLayout(editRow);

    auto *btnZoomSelectedWagon = new QPushButton(QStringLiteral("🔍 Auf ausgewählten Waggon zoomen"), grpWagons);
    btnZoomSelectedWagon->setStyleSheet("background-color: #0284C7; color: white;");
    grpWagLayout->addWidget(btnZoomSelectedWagon);

    tabTrainsLayout->addWidget(grpWagons, 1);
    m_simSideTabs->addTab(tabTrains, QStringLiteral("🚂 Züge && Waggons"));

    // -------------------------------------------------------------------------
    // SIM TAB 2: GÜTER-OBJEKTE, QUELLEN & SENKEN + LOKOMOTIV-ENGPASS
    // -------------------------------------------------------------------------
    auto *tabGoods = new QWidget();
    auto *tabGoodsLayout = new QVBoxLayout(tabGoods);
    tabGoodsLayout->setContentsMargins(8, 8, 8, 8);
    tabGoodsLayout->setSpacing(6);

    auto *grpLoco = new QGroupBox(QStringLiteral("1. Lokomotiv-Pool && Waggon-Lagerung bei Lokmangel"), tabGoods);
    auto *locoGrid = new QGridLayout(grpLoco);

    locoGrid->addWidget(new QLabel(QStringLiteral("Verfügbare Loks im Pool:")), 0, 0);
    m_spinLocoPool = new QSpinBox(grpLoco);
    m_spinLocoPool->setRange(1, 25);
    m_spinLocoPool->setValue(m_mapWidget->totalLocomotivesInPool());
    locoGrid->addWidget(m_spinLocoPool, 0, 1);

    m_lblLocoPoolInfo = new QLabel(grpLoco);
    m_lblLocoPoolInfo->setStyleSheet("color: #7E22CE; font-weight: bold;");
    locoGrid->addWidget(m_lblLocoPoolInfo, 0, 2);

    auto *btnCoupleLoco2 = new QPushButton(QStringLiteral("🚂 Freie Lok an gewählte Gruppe"), grpLoco);
    btnCoupleLoco2->setStyleSheet("background-color: #15803D; color: white;");
    auto *btnUncoupleLoco2 = new QPushButton(QStringLiteral("🅿️ Lok abkuppeln -> Auf Gleis lagern"), grpLoco);
    btnUncoupleLoco2->setStyleSheet("background-color: #7E22CE; color: white;");
    locoGrid->addWidget(btnCoupleLoco2, 1, 0, 1, 2);
    locoGrid->addWidget(btnUncoupleLoco2, 1, 2);
    tabGoodsLayout->addWidget(grpLoco);

    auto *grpNewOrder = new QGroupBox(QStringLiteral("2. Neues Güter-Objekt anlegen && auf Waggons verteilen"), tabGoods);
    auto *ordGrid = new QGridLayout(grpNewOrder);

    ordGrid->addWidget(new QLabel(QStringLiteral("Stahlprodukt:")), 0, 0);
    m_comboNewProduct = new QComboBox(grpNewOrder);
    for (int p = 0; p < static_cast<int>(CargoProductType::EmptyWagon); ++p) {
        m_comboNewProduct->addItem(RailMapWidget::productName(static_cast<CargoProductType>(p)), p);
    }
    ordGrid->addWidget(m_comboNewProduct, 0, 1, 1, 3);

    ordGrid->addWidget(new QLabel(QStringLiteral("Menge (Tonnen):")), 1, 0);
    m_spinNewTons = new QSpinBox(grpNewOrder);
    m_spinNewTons->setRange(50, 1200);
    m_spinNewTons->setSingleStep(50);
    m_spinNewTons->setValue(320);
    m_spinNewTons->setSuffix(QStringLiteral(" t"));
    ordGrid->addWidget(m_spinNewTons, 1, 1);

    ordGrid->addWidget(new QLabel(QStringLiteral("Waggontyp:")), 1, 2);
    m_comboNewWagonCat = new QComboBox(grpNewOrder);
    m_comboNewWagonCat->addItem(QStringLiteral("Samms [6A] (90 t/Wg.)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Rns-z [4A] (62 t/Wg.)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Shimmns [4A] (56 t/Wg.)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Torpedo [6A] (96 t/Wg.)"));
    ordGrid->addWidget(m_comboNewWagonCat, 1, 3);

    ordGrid->addWidget(new QLabel(QStringLiteral("Quelle (Start):")), 2, 0);
    m_comboNewSource = new QComboBox(grpNewOrder);
    ordGrid->addWidget(m_comboNewSource, 2, 1);

    ordGrid->addWidget(new QLabel(QStringLiteral("Senke (Ziel):")), 2, 2);
    m_comboNewSink = new QComboBox(grpNewOrder);
    ordGrid->addWidget(m_comboNewSink, 2, 3);

    for (const SourceSinkNode &n : m_mapWidget->sourceSinkNodes()) {
        const QString itemTxt = QString("%1 (%2)").arg(n.id, n.name.left(24));
        m_comboNewSource->addItem(itemTxt, n.id);
        m_comboNewSink->addItem(itemTxt, n.id);
    }

    auto *btnCreateOrder = new QPushButton(QStringLiteral("📦 Güter-Objekt erzeugen && auf Waggons verteilen"), grpNewOrder);
    btnCreateOrder->setStyleSheet("background-color: #EF7D00; color: white; padding: 6px;");
    ordGrid->addWidget(btnCreateOrder, 3, 0, 1, 4);

    auto *btnLoadSourceNow = new QPushButton(QStringLiteral("⬆️ Zug jetzt an Quelle aufnehmen (Beladen)"), grpNewOrder);
    btnLoadSourceNow->setStyleSheet("background-color: #15803D; color: white; padding: 5px;");
    auto *btnUnloadSinkNow = new QPushButton(QStringLiteral("⬇️ Zug jetzt an Senke abladen (Entladen)"), grpNewOrder);
    btnUnloadSinkNow->setStyleSheet("background-color: #0369A1; color: white; padding: 5px;");
    ordGrid->addWidget(btnLoadSourceNow, 4, 0, 1, 2);
    ordGrid->addWidget(btnUnloadSinkNow, 4, 2, 1, 2);
    tabGoodsLayout->addWidget(grpNewOrder);

    auto *grpNodes = new QGroupBox(QStringLiteral("3. Quellen && Senken (Aufnahme && Abladung | Klick = Zoom)"), tabGoods);
    auto *nodesLay = new QVBoxLayout(grpNodes);
    m_tableSimNodes = new QTableWidget(0, 5, grpNodes);
    m_tableSimNodes->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("Standort / Anlage"),
        QStringLiteral("Rolle"),
        QStringLiteral("⬆️ Aufnahme / ⬇️ Abladung"),
        QStringLiteral("Bestand")
    });
    m_tableSimNodes->setAlternatingRowColors(true);
    m_tableSimNodes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableSimNodes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableSimNodes->verticalHeader()->setVisible(false);
    m_tableSimNodes->horizontalHeader()->setStretchLastSection(true);
    m_tableSimNodes->setColumnWidth(0, 88);
    m_tableSimNodes->setColumnWidth(1, 165);
    m_tableSimNodes->setColumnWidth(2, 85);
    m_tableSimNodes->setColumnWidth(3, 145);
    nodesLay->addWidget(m_tableSimNodes);
    tabGoodsLayout->addWidget(grpNodes, 1);

    auto *grpOrders = new QGroupBox(QStringLiteral("4. Aktive Güter-Objekte && Transportaufträge (Klick = Verband wählen)"), tabGoods);
    auto *ordTabLay = new QVBoxLayout(grpOrders);
    m_tableCargoOrders = new QTableWidget(0, 6, grpOrders);
    m_tableCargoOrders->setHorizontalHeaderLabels({
        QStringLiteral("Auftrag"),
        QStringLiteral("Gut"),
        QStringLiteral("Menge"),
        QStringLiteral("Quelle -> Senke"),
        QStringLiteral("Verband"),
        QStringLiteral("Status")
    });
    m_tableCargoOrders->setAlternatingRowColors(true);
    m_tableCargoOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableCargoOrders->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableCargoOrders->verticalHeader()->setVisible(false);
    m_tableCargoOrders->horizontalHeader()->setStretchLastSection(true);
    m_tableCargoOrders->setColumnWidth(0, 68);
    m_tableCargoOrders->setColumnWidth(1, 88);
    m_tableCargoOrders->setColumnWidth(2, 56);
    m_tableCargoOrders->setColumnWidth(3, 140);
    m_tableCargoOrders->setColumnWidth(4, 96);
    ordTabLay->addWidget(m_tableCargoOrders);
    tabGoodsLayout->addWidget(grpOrders, 1);

    m_simSideTabs->addTab(tabGoods, QStringLiteral("📦 Güter && Quellen"));

    // -------------------------------------------------------------------------
    // SIM TAB 3: KARTE, LAYER & SIMULATIONSGESCHWINDIGKEIT
    // -------------------------------------------------------------------------
    auto *tabSimMap = new QWidget();
    auto *tabSimMapLay = new QVBoxLayout(tabSimMap);
    tabSimMapLay->setContentsMargins(8, 8, 8, 8);
    tabSimMapLay->setSpacing(9);

    auto *grpSimSites = new QGroupBox(QStringLiteral("1. Standort-Schnellwahl (Saarstahl AG && Dillinger Hütte)"), tabSimMap);
    auto *gSimSites = new QGridLayout(grpSimSites);
    auto *btnSimVK  = new QPushButton(QStringLiteral("🏭 Werk Völklingen (Saarstahl)"), grpSimSites);
    auto *btnSimDL  = new QPushButton(QStringLiteral("🔥 Dillinger Hütte && ROGESA"), grpSimSites);
    auto *btnSimBB  = new QPushButton(QStringLiteral("⚙️ Werk Burbach (Drahtwerk)"), grpSimSites);
    auto *btnSimNK  = new QPushButton(QStringLiteral("🔩 Werk Neunkirchen"), grpSimSites);
    auto *btnSimAll = new QPushButton(QStringLiteral("🗺️ Gesamtübersicht Saarland-Korridor"), grpSimSites);
    btnSimVK->setStyleSheet("background-color: #EF7D00; color: white;");
    gSimSites->addWidget(btnSimVK,  0, 0);
    gSimSites->addWidget(btnSimDL,  0, 1);
    gSimSites->addWidget(btnSimBB,  1, 0);
    gSimSites->addWidget(btnSimNK,  1, 1);
    gSimSites->addWidget(btnSimAll, 2, 0, 1, 2);
    tabSimMapLay->addWidget(grpSimSites);

    auto *grpSimMapStyle = new QGroupBox(QStringLiteral("2. Google Maps Hintergrund && Vereinfachtes Werk"), tabSimMap);
    auto *simMsLay = new QVBoxLayout(grpSimMapStyle);

    simMsLay->addWidget(new QLabel(QStringLiteral("Kartenhintergrund-Modus:")));
    m_comboSimMapMode = new QComboBox(grpSimMapStyle);
    m_comboSimMapMode->addItem(QStringLiteral("Hybrid: Google Maps + Vereinfachtes Werk (Empfohlen)"));
    m_comboSimMapMode->addItem(QStringLiteral("Nur Vereinfachte Werks- & Gleisdarstellung (Schematisch)"));
    m_comboSimMapMode->addItem(QStringLiteral("Google Maps Straßenkarte + Bahngleise"));
    m_comboSimMapMode->addItem(QStringLiteral("Google Maps Satellitenbild + Bahngleise"));
    simMsLay->addWidget(m_comboSimMapMode);

    m_chkSimSimplifiedPlant = new QCheckBox(QStringLiteral("Vereinfachtes Werksgelände && Werkhallen anzeigen"), grpSimMapStyle);
    m_chkSimSimplifiedPlant->setChecked(true);
    m_chkSimSourcesSinks    = new QCheckBox(QStringLiteral("Quellen, Senken && Lagerbestände auf der Karte anzeigen"), grpSimMapStyle);
    m_chkSimSourcesSinks->setChecked(true);
    m_chkSimRoadsRivers     = new QCheckBox(QStringLiteral("Vereinfachte Straßen (A620, B51) && Flüsse (Saar, Rossel)"), grpSimMapStyle);
    m_chkSimRoadsRivers->setChecked(true);
    m_chkSimTrackLabels     = new QCheckBox(QStringLiteral("Gleis-Bezeichnungen anzeigen"), grpSimMapStyle);
    m_chkSimTrackLabels->setChecked(true);
    m_chkSimWagonLabels     = new QCheckBox(QStringLiteral("Waggon-Positionsnummern && Gut-Labels anzeigen"), grpSimMapStyle);
    m_chkSimWagonLabels->setChecked(true);
    m_chkSimExaggerateSize  = new QCheckBox(QStringLiteral("2D-Waggons für Übersicht vergrößert darstellen"), grpSimMapStyle);
    m_chkSimExaggerateSize->setChecked(true);

    simMsLay->addWidget(m_chkSimSimplifiedPlant);
    simMsLay->addWidget(m_chkSimSourcesSinks);
    simMsLay->addWidget(m_chkSimRoadsRivers);
    simMsLay->addWidget(m_chkSimTrackLabels);
    simMsLay->addWidget(m_chkSimWagonLabels);
    simMsLay->addWidget(m_chkSimExaggerateSize);
    tabSimMapLay->addWidget(grpSimMapStyle);

    auto *grpSpeed = new QGroupBox(QStringLiteral("3. Simulationsgeschwindigkeit (2D-Eisenbahn)"), tabSimMap);
    auto *spdLay = new QHBoxLayout(grpSpeed);
    spdLay->addWidget(new QLabel(QStringLiteral("Tempo:")));
    m_sliderSimSpeed = new QSlider(Qt::Horizontal, grpSpeed);
    m_sliderSimSpeed->setRange(2, 100);
    m_sliderSimSpeed->setValue(25);
    m_lblSpeedValue = new QLabel(QStringLiteral("2.5x"), grpSpeed);
    m_lblSpeedValue->setMinimumWidth(42);
    spdLay->addWidget(m_sliderSimSpeed, 1);
    spdLay->addWidget(m_lblSpeedValue);
    tabSimMapLay->addWidget(grpSpeed);

    tabSimMapLay->addStretch(1);
    m_simSideTabs->addTab(tabSimMap, QStringLiteral("🗺️ Karte && Tempo"));

    // -------------------------------------------------------------------------
    // SIM TAB 4: ZUFALLS-TEST & SIMULIERTE TELEMATIK (Ril-100 / VzG)
    // -------------------------------------------------------------------------
    auto *tabSimTest = new QWidget();
    auto *tabSimTestLay = new QVBoxLayout(tabSimTest);
    tabSimTestLay->setContentsMargins(8, 8, 8, 8);
    tabSimTestLay->setSpacing(9);

    auto *grpRandomTest = new QGroupBox(QStringLiteral("1. Test-Modus: Zufällige Positionierung auf den Gleisen"), tabSimTest);
    auto *grpRandLay = new QVBoxLayout(grpRandomTest);
    auto *lblRandInfo = new QLabel(
        QStringLiteral("Platziert Züge und gelagerte Waggongruppen (Samms, Rns-z, 4-Achser, Torpedo) zum Testen "
                       "sofort an zufälligen, gültigen Positionen auf den Bahngleisen:"), grpRandomTest);
    lblRandInfo->setWordWrap(true);
    grpRandLay->addWidget(lblRandInfo);

    auto *btnRandAll = new QPushButton(QStringLiteral("🎲 Zufällige Positionen auf ALLEN Gleisen (Saarland)"), grpRandomTest);
    btnRandAll->setStyleSheet("background-color: #EF7D00; color: white; padding: 7px;");
    auto *btnRandVK = new QPushButton(QStringLiteral("🎲 Zufällige Positionen im Werk VÖLKLINGEN"), grpRandomTest);
    btnRandVK->setStyleSheet("background-color: #0284C7; color: white; padding: 7px;");
    grpRandLay->addWidget(btnRandAll);
    grpRandLay->addWidget(btnRandVK);
    tabSimTestLay->addWidget(grpRandomTest);

    auto *grpSimApi = new QGroupBox(QStringLiteral("2. Simuliertes Bahn-API / SAVVY Telematik-Protokoll (Ril-100 / VzG)"), tabSimTest);
    auto *simApiLay = new QVBoxLayout(grpSimApi);

    auto *btnQuerySimApi = new QPushButton(QStringLiteral("📡 Simulierte Telematik-Ortung (DS100 && Track-Snapping) ausführen"), grpSimApi);
    btnQuerySimApi->setStyleSheet("background-color: #0D9488; color: white; padding: 7px;");
    simApiLay->addWidget(btnQuerySimApi);

    m_chkSimAutoPoll = new QCheckBox(QStringLiteral("🔄 Auto-Telematik-Protokoll alle 5 Sek. aktualisieren"), grpSimApi);
    simApiLay->addWidget(m_chkSimAutoPoll);

    m_txtSimApiLog = new QPlainTextEdit(grpSimApi);
    m_txtSimApiLog->setReadOnly(true);
    m_txtSimApiLog->setStyleSheet(
        "background-color: #0F172A; color: #38BDF8; font-family: Consolas, monospace; font-size: 9.5px;");
    m_txtSimApiLog->setPlainText(
        QStringLiteral("// Simulations-Telematik bereit (Ril-100/DS100, VzG-km, Torpedo 1420 °C).\n"
                       "// Für ECHTE GPS-Daten aus DB link2rail, IMT, DOT, TRANSWAGGON, Innofreight & Wascosa\n"
                       "// klicke oben auf '📡 LIVE-GPS-MODUS (6 Portale)'!"));
    simApiLay->addWidget(m_txtSimApiLog, 1);

    auto *btnJumpLive = new QPushButton(QStringLiteral("📡 Zu den echten Live-GPS-Portalen (6 Dienste) wechseln ->"), grpSimApi);
    btnJumpLive->setStyleSheet("background-color: #0F766E; color: #FBD50E; padding: 7px;");
    simApiLay->addWidget(btnJumpLive);

    tabSimTestLay->addWidget(grpSimApi, 1);
    m_simSideTabs->addTab(tabSimTest, QStringLiteral("🎲 Test && Sim-API"));

    // --- Signal-Slot-Verbindungen Simulations-Panel ---
    connect(m_comboTrain,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onTrainComboChanged);
    connect(m_comboTrackAssign, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onTrackAssignChanged);
    connect(btnReverse,         &QPushButton::clicked, this, &MainWindowSHSLogistics::onReverseTrainClicked);
    connect(btnCoupleLoco1,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onCoupleLocoClicked);
    connect(btnUncoupleLoco1,   &QPushButton::clicked, this, &MainWindowSHSLogistics::onUncoupleLocoClicked);
    connect(m_spinWagonCount,   QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindowSHSLogistics::onWagonCountSpinChanged);
    connect(btnAddSamms,        &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddSammsClicked);
    connect(btnAddRnsz,         &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddRnszClicked);
    connect(btnAdd4A,           &QPushButton::clicked, this, &MainWindowSHSLogistics::onAdd4AxleClicked);
    connect(btnAdd6A,           &QPushButton::clicked, this, &MainWindowSHSLogistics::onAdd6AxleClicked);
    connect(btnRemoveLast,      &QPushButton::clicked, this, &MainWindowSHSLogistics::onRemoveWagonClicked);
    connect(m_comboSimColorMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onSimColorModeChanged);
    connect(m_tableSimWagons,   &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onWagonTableClicked);
    connect(m_tableSimWagons,   &QTableWidget::cellDoubleClicked, this, &MainWindowSHSLogistics::onWagonTableDoubleClicked);
    connect(btnApplyWagonType,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onChangeSelectedWagonCategory);
    connect(btnZoomSelectedWagon, &QPushButton::clicked, this, [this]() {
        m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), m_mapWidget->selectedWagonIndex());
    });

    connect(m_spinLocoPool,     QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindowSHSLogistics::onLocoPoolSpinChanged);
    connect(btnCoupleLoco2,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onCoupleLocoClicked);
    connect(btnUncoupleLoco2,   &QPushButton::clicked, this, &MainWindowSHSLogistics::onUncoupleLocoClicked);
    connect(m_comboNewProduct,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onNewOrderProductChanged);
    connect(btnCreateOrder,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onCreateCargoOrderClicked);
    connect(btnLoadSourceNow,   &QPushButton::clicked, this, [this]() {
        m_mapWidget->loadTrainAtSourceNow(m_mapWidget->selectedTrainIndex());
    });
    connect(btnUnloadSinkNow,   &QPushButton::clicked, this, [this]() {
        m_mapWidget->unloadTrainAtSinkNow(m_mapWidget->selectedTrainIndex());
    });
    connect(m_tableSimNodes,    &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onSimNodeTableClicked);
    connect(m_tableCargoOrders, &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onCargoOrderTableClicked);

    connect(btnSimVK,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Voelklingen"), true); });
    connect(btnSimDL,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Dillingen"), true); });
    connect(btnSimBB,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Burbach"), true); });
    connect(btnSimNK,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Neunkirchen"), true); });
    connect(btnSimAll, &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Gesamt"), true); });

    connect(m_comboSimMapMode,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onMapModeChanged);
    connect(m_chkSimSimplifiedPlant, &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowSimplifiedPlant(c); syncLayerControls(true); });
    connect(m_chkSimSourcesSinks,    &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowSourcesAndSinks(c); syncLayerControls(true); });
    connect(m_chkSimRoadsRivers,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowRoadsAndRivers(c); syncLayerControls(true); });
    connect(m_chkSimTrackLabels,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowTrackLabels(c); syncLayerControls(true); });
    connect(m_chkSimWagonLabels,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowWagonLabels(c); syncLayerControls(true); });
    connect(m_chkSimExaggerateSize,  &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setExaggerateWagonSize(c); syncLayerControls(true); });
    connect(m_sliderSimSpeed,        &QSlider::valueChanged, this, &MainWindowSHSLogistics::onSpeedSliderChanged);

    connect(btnRandAll,       &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    connect(btnRandVK,        &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeVoelklingenClicked);
    connect(btnQuerySimApi,   &QPushButton::clicked, this, &MainWindowSHSLogistics::onQuerySimDbApiClicked);
    connect(m_chkSimAutoPoll, &QCheckBox::toggled,   this, &MainWindowSHSLogistics::onSimAutoPollToggled);
    connect(btnJumpLive,      &QPushButton::clicked, this, [this]() {
        onSwitchToLiveGpsMode();
        m_liveSideTabs->setCurrentIndex(1);
    });
}

void MainWindowSHSLogistics::buildLiveGpsSidePanel()
{
    m_liveSideTabs = new QTabWidget(m_sidePanelStack);

    // -------------------------------------------------------------------------
    // LIVE TAB 1: ALLE LIVE-WAGGONS (VEREINIGTE ANSICHT ALLER 6 DIENSTE)
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
        QStringLiteral("<b style='color:#92400E;'>Multi-Telematik Live-GPS (6 Portale auf einer Karte):</b><br>"
                       "<span style='color:#78350F; font-size:10px;'>Melde dich im Tab '🔐 Dienste &amp; Logins' bei einem oder mehreren Diensten an (oder nutze '📋 Browser-Session / cURL' bei Web-SSO/2FA).</span>"),
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
    m_comboLiveColorMode = new QComboBox(grpFilter);
    m_comboLiveColorMode->addItem(QStringLiteral("Alle 4 Bauarten getrennt (Samms=Orange, Rns-z=Blau, 4A=Grün, 6A=Rot)"));
    m_comboLiveColorMode->addItem(QStringLiteral("Fokus Bauart: Samms vs. Rns-z hervorheben"));
    m_comboLiveColorMode->addItem(QStringLiteral("Strikt nach Achsen: Alle 4-Achser (Blau) vs. Alle 6-Achser (Orange)"));
    m_comboLiveColorMode->addItem(QStringLiteral("Nach Telematik-Dienst färben (link2rail / IMT / DOT / TRANSWAGGON / Innofreight / Wascosa)"));
    gFilt->addWidget(m_comboLiveColorMode, 3, 1);

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
    auto *btnZoomSelectedLive = new QPushButton(QStringLiteral("🔍 Auf Waggon zoomen"), grpTable);
    btnZoomSelectedLive->setStyleSheet("background-color: #0284C7; color: white;");
    auto *btnFitAllTab1 = new QPushButton(QStringLiteral("🎯 Alle GPS-Waggons zeigen"), grpTable);
    auto *btnCopyJson = new QPushButton(QStringLiteral("📋 Waggon-JSON kopieren"), grpTable);
    wActionRow->addWidget(btnZoomSelectedLive);
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

    m_liveSideTabs->addTab(tabLive, QStringLiteral("📡 Live-Waggons"));

    // -------------------------------------------------------------------------
    // LIVE TAB 2: DIENSTE & LOGINS — 6 EIGENE LOGIN-REITER (EIN REITER PRO DIENST!)
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
            onSwitchToLiveGpsMode();
            m_liveSideTabs->setCurrentIndex(0);
            m_mapWidget->fitAllLiveWagons(true);
        });
        m_providerLoginTabs->addTab(w, QStringLiteral("🔴 ") + client->info().tabLabel);
    }

    tabLoginsLay->addWidget(m_providerLoginTabs, 1);
    m_liveSideTabs->addTab(tabLogins, QStringLiteral("🔐 Dienste && Logins (6)"));

    // -------------------------------------------------------------------------
    // LIVE TAB 3: STANDORTE, WERKE & GEOFENCES
    // -------------------------------------------------------------------------
    auto *tabLiveNodes = new QWidget();
    auto *tabLiveNodesLay = new QVBoxLayout(tabLiveNodes);
    tabLiveNodesLay->setContentsMargins(8, 8, 8, 8);
    tabLiveNodesLay->setSpacing(8);

    auto *grpLiveNodes = new QGroupBox(QStringLiteral("SHS-Werke, Anlagen && Geofence-Bereiche (Klick = Karte zentrieren)"), tabLiveNodes);
    auto *nLay = new QVBoxLayout(grpLiveNodes);
    auto *lblNInfo = new QLabel(
        QStringLiteral("Zeigt alle 10 realen Standorte von Saarstahl AG & Dillinger Hütte / ROGESA sowie die Gesamtzahl der per GPS über alle 6 Telematik-Dienste dort georteten Waggons:"),
        grpLiveNodes);
    lblNInfo->setWordWrap(true);
    nLay->addWidget(lblNInfo);

    m_tableLiveNodes = new QTableWidget(0, 5, grpLiveNodes);
    m_tableLiveNodes->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("Werk / Anlage"),
        QStringLiteral("Rolle"),
        QStringLiteral("📡 Live-Waggons"),
        QStringLiteral("Typische Stahlgüter")
    });
    m_tableLiveNodes->setAlternatingRowColors(true);
    m_tableLiveNodes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableLiveNodes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableLiveNodes->verticalHeader()->setVisible(false);
    m_tableLiveNodes->horizontalHeader()->setStretchLastSection(true);
    m_tableLiveNodes->setColumnWidth(0, 98);
    m_tableLiveNodes->setColumnWidth(1, 175);
    m_tableLiveNodes->setColumnWidth(2, 95);
    m_tableLiveNodes->setColumnWidth(3, 95);
    nLay->addWidget(m_tableLiveNodes, 1);
    tabLiveNodesLay->addWidget(grpLiveNodes, 1);

    m_liveSideTabs->addTab(tabLiveNodes, QStringLiteral("🏭 Werke && Geofences"));

    // -------------------------------------------------------------------------
    // LIVE TAB 4: KARTE & LAYER (INKL. SICHTBARKEIT JE TELEMATIK-DIENST)
    // -------------------------------------------------------------------------
    auto *tabLiveMap = new QWidget();
    auto *tabLiveMapLay = new QVBoxLayout(tabLiveMap);
    tabLiveMapLay->setContentsMargins(8, 8, 8, 8);
    tabLiveMapLay->setSpacing(8);

    auto *grpSites = new QGroupBox(QStringLiteral("1. Standort- && Kamera-Schnellwahl"), tabLiveMap);
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
    tabLiveMapLay->addWidget(grpSites);

    auto *grpMapStyle = new QGroupBox(QStringLiteral("2. Google Maps Hintergrund && Werks-Layer"), tabLiveMap);
    auto *msLay = new QVBoxLayout(grpMapStyle);

    msLay->addWidget(new QLabel(QStringLiteral("Kartenhintergrund-Modus:")));
    m_comboLiveMapMode = new QComboBox(grpMapStyle);
    m_comboLiveMapMode->addItem(QStringLiteral("Hybrid: Google Maps + Vereinfachtes Werk (Empfohlen)"));
    m_comboLiveMapMode->addItem(QStringLiteral("Nur Vereinfachte Werks- & Gleisdarstellung (Schematisch)"));
    m_comboLiveMapMode->addItem(QStringLiteral("Google Maps Straßenkarte + Bahngleise"));
    m_comboLiveMapMode->addItem(QStringLiteral("Google Maps Satellitenbild + Bahngleise"));
    msLay->addWidget(m_comboLiveMapMode);

    m_chkLiveSimplifiedPlant = new QCheckBox(QStringLiteral("Vereinfachtes Werksgelände && Werkhallen anzeigen"), grpMapStyle);
    m_chkLiveSimplifiedPlant->setChecked(true);
    m_chkLiveSourcesSinks    = new QCheckBox(QStringLiteral("SHS-Werke && Geofence-Karten anzeigen"), grpMapStyle);
    m_chkLiveSourcesSinks->setChecked(true);
    m_chkLiveRoadsRivers     = new QCheckBox(QStringLiteral("Vereinfachte Straßen (A620, B51) && Flüsse (Saar, Rossel)"), grpMapStyle);
    m_chkLiveRoadsRivers->setChecked(true);
    m_chkLiveTrackLabels     = new QCheckBox(QStringLiteral("Gleis-Bezeichnungen anzeigen"), grpMapStyle);
    m_chkLiveTrackLabels->setChecked(true);
    m_chkLiveWagonLabels     = new QCheckBox(QStringLiteral("Waggon-EVN && Dienst-Labels auf der Karte anzeigen"), grpMapStyle);
    m_chkLiveWagonLabels->setChecked(true);
    m_chkLiveExaggerateSize  = new QCheckBox(QStringLiteral("2D-Waggons für Übersicht vergrößert darstellen"), grpMapStyle);
    m_chkLiveExaggerateSize->setChecked(true);
    m_chkSnapToTracks        = new QCheckBox(QStringLiteral("🧲 GPS-Positionen im Werksbereich auf Gleisachsen snappen (<= 120 m)"), grpMapStyle);
    m_chkSnapToTracks->setChecked(true);

    msLay->addWidget(m_chkLiveSimplifiedPlant);
    msLay->addWidget(m_chkLiveSourcesSinks);
    msLay->addWidget(m_chkLiveRoadsRivers);
    msLay->addWidget(m_chkLiveTrackLabels);
    msLay->addWidget(m_chkLiveWagonLabels);
    msLay->addWidget(m_chkLiveExaggerateSize);
    msLay->addWidget(m_chkSnapToTracks);
    tabLiveMapLay->addWidget(grpMapStyle);

    auto *grpProvLayer = new QGroupBox(QStringLiteral("3. Sichtbare Telematik-Dienste auf der gemeinsamen Karte"), tabLiveMap);
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
    tabLiveMapLay->addWidget(grpProvLayer);
    tabLiveMapLay->addStretch(1);
    m_liveSideTabs->addTab(tabLiveMap, QStringLiteral("🗺️ Karte && Layer"));

    // --- Signal-Slot-Verbindungen Live-GPS-Panel ---
    connect(btnBannerLogin,        &QPushButton::clicked, this, [this]() { m_liveSideTabs->setCurrentIndex(1); });
    connect(m_comboProviderFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_editSearchFilter,    &QLineEdit::textChanged, this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_comboCategoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onLiveFilterChanged);
    connect(m_comboLiveColorMode,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onLiveColorModeChanged);
    connect(m_tableLiveWagons,     &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onLiveWagonTableClicked);
    connect(m_tableLiveWagons,     &QTableWidget::cellDoubleClicked, this, &MainWindowSHSLogistics::onLiveWagonTableDoubleClicked);
    connect(btnZoomSelectedLive,   &QPushButton::clicked, this, [this]() {
        m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), m_mapWidget->selectedWagonIndex());
    });
    connect(btnFitAllTab1, &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });
    connect(btnCopyJson, &QPushButton::clicked, this, [this]() {
        const int tIdx = m_mapWidget->selectedTrainIndex();
        const int wIdx = m_mapWidget->selectedWagonIndex();
        const auto &trains = m_mapWidget->liveTrains();
        if (tIdx >= 0 && tIdx < trains.size() && wIdx >= 0 && wIdx < trains[tIdx].wagons.size()) {
            const QJsonObject obj = QJsonObject::fromVariantMap(trains[tIdx].wagons[wIdx].rawAttributes);
            QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
            statusBar()->showMessage(QStringLiteral("Waggon-JSON in die Zwischenablage kopiert."), 4000);
        }
    });

    connect(btnSyncAll,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    connect(btnExpAllJson, &QPushButton::clicked, this, &MainWindowSHSLogistics::onExportAllJsonClicked);
    connect(btnExpAllCsv,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onExportAllCsvClicked);

    connect(m_tableLiveNodes, &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onLiveNodeTableClicked);

    connect(btnSiteVK,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Voelklingen"), true); });
    connect(btnSiteDL,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Dillingen"), true); });
    connect(btnSiteBB,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Burbach"), true); });
    connect(btnSiteNK,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Neunkirchen"), true); });
    connect(btnSiteSL,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Gesamt"), true); });
    connect(btnSiteDE,  &QPushButton::clicked, this, [this]() { m_mapWidget->focusSite(QStringLiteral("Deutschland"), true); });
    connect(btnSiteFit, &QPushButton::clicked, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });

    connect(m_comboLiveMapMode,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onMapModeChanged);
    connect(m_chkLiveSimplifiedPlant, &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowSimplifiedPlant(c); syncLayerControls(false); });
    connect(m_chkLiveSourcesSinks,    &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowSourcesAndSinks(c); syncLayerControls(false); });
    connect(m_chkLiveRoadsRivers,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowRoadsAndRivers(c); syncLayerControls(false); });
    connect(m_chkLiveTrackLabels,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowTrackLabels(c); syncLayerControls(false); });
    connect(m_chkLiveWagonLabels,     &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setShowWagonLabels(c); syncLayerControls(false); });
    connect(m_chkLiveExaggerateSize,  &QCheckBox::toggled, this, [this](bool c) { m_mapWidget->setExaggerateWagonSize(c); syncLayerControls(false); });
    connect(m_chkSnapToTracks,        &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setSnapLiveGpsToTracks);
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

    QMenu *menuMode = mb->addMenu(QStringLiteral("&Modus"));
    QAction *actModeSim = menuMode->addAction(QStringLiteral("🎮 Simulationsmodus (Spiel – Züge, Güter, Lok-Pool)"));
    actModeSim->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(actModeSim, &QAction::triggered, this, &MainWindowSHSLogistics::onSwitchToSimulationMode);
    QAction *actModeLive = menuMode->addAction(QStringLiteral("📡 Live-GPS-Modus (6 Telematik-Portale)"));
    actModeLive->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(actModeLive, &QAction::triggered, this, &MainWindowSHSLogistics::onSwitchToLiveGpsMode);

    QMenu *menuSites = mb->addMenu(QStringLiteral("&Standorte && Karte"));
    auto addSiteAct = [&](const QString &label, const QString &site, const QKeySequence &key) {
        QAction *a = menuSites->addAction(label);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, site]() {
            m_mapWidget->focusSite(site, true);
        });
    };
    addSiteAct(QStringLiteral("🏭 Werk Völklingen (Saarstahl Hauptwerk)"), QStringLiteral("Voelklingen"), Qt::Key_F1);
    addSiteAct(QStringLiteral("🔥 Dillinger Hütte && ROGESA"), QStringLiteral("Dillingen"), Qt::Key_F2);
    addSiteAct(QStringLiteral("⚙️ Werk Burbach (Drahtwerk)"), QStringLiteral("Burbach"), Qt::Key_F3);
    addSiteAct(QStringLiteral("🔩 Werk Neunkirchen (Walzwerk)"), QStringLiteral("Neunkirchen"), Qt::Key_F4);
    addSiteAct(QStringLiteral("🗺️ Gesamtübersicht Saarland-Korridor"), QStringLiteral("Gesamt"), Qt::Key_F5);
    addSiteAct(QStringLiteral("🇩🇪 Deutschland-Übersicht"), QStringLiteral("Deutschland"), Qt::Key_F6);
    menuSites->addSeparator();
    QAction *actFitAll = menuSites->addAction(QStringLiteral("🎯 Alle Live-Waggons einpassen"));
    actFitAll->setShortcut(Qt::Key_F7);
    connect(actFitAll, &QAction::triggered, this, [this]() { m_mapWidget->fitAllLiveWagons(true); });

    QMenu *menuSim = mb->addMenu(QStringLiteral("&Simulation && Güter"));
    QAction *actPlayPause = menuSim->addAction(QStringLiteral("⏯ Simulation Start / Pause"));
    actPlayPause->setShortcut(Qt::Key_Space);
    connect(actPlayPause, &QAction::triggered, this, &MainWindowSHSLogistics::onToggleSimClicked);
    menuSim->addSeparator();
    QAction *actAddSamms = menuSim->addAction(QStringLiteral("➕ Zug verlängern: Samms (6-Achser) anhängen"));
    connect(actAddSamms, &QAction::triggered, this, &MainWindowSHSLogistics::onAddSammsClicked);
    QAction *actAddRnsz = menuSim->addAction(QStringLiteral("➕ Zug verlängern: Rns-z (4-Achser) anhängen"));
    connect(actAddRnsz, &QAction::triggered, this, &MainWindowSHSLogistics::onAddRnszClicked);
    QAction *actAddTorp = menuSim->addAction(QStringLiteral("🔥 Zug verlängern: Torpedowagen (1420 °C) anhängen"));
    connect(actAddTorp, &QAction::triggered, this, &MainWindowSHSLogistics::onAdd6AxleClicked);
    QAction *actRemWagon = menuSim->addAction(QStringLiteral("➖ Zug verkürzen: Letzten Waggon abhängen"));
    connect(actRemWagon, &QAction::triggered, this, &MainWindowSHSLogistics::onRemoveWagonClicked);
    menuSim->addSeparator();
    QAction *actCouple = menuSim->addAction(QStringLiteral("🚂 Freie Lokomotive an gewählte Gruppe ankuppeln"));
    connect(actCouple, &QAction::triggered, this, &MainWindowSHSLogistics::onCoupleLocoClicked);
    QAction *actUncouple = menuSim->addAction(QStringLiteral("🅿️ Lokomotive abkuppeln (Waggons auf Gleis lagern)"));
    connect(actUncouple, &QAction::triggered, this, &MainWindowSHSLogistics::onUncoupleLocoClicked);
    menuSim->addSeparator();
    QAction *actLoadSource = menuSim->addAction(QStringLiteral("⬆️ Güter jetzt an Quelle aufnehmen (Zug beladen)"));
    connect(actLoadSource, &QAction::triggered, this, [this]() {
        ensureSimulationModeForAction();
        m_mapWidget->loadTrainAtSourceNow(m_mapWidget->selectedTrainIndex());
    });
    QAction *actUnloadSink = menuSim->addAction(QStringLiteral("⬇️ Güter jetzt an Senke abladen (Zug entladen)"));
    connect(actUnloadSink, &QAction::triggered, this, [this]() {
        ensureSimulationModeForAction();
        m_mapWidget->unloadTrainAtSinkNow(m_mapWidget->selectedTrainIndex());
    });
    menuSim->addSeparator();
    QAction *actRandAll = menuSim->addAction(QStringLiteral("🎲 Zufällige Positionen auf allen Gleisen (Test)"));
    actRandAll->setShortcut(Qt::Key_F8);
    connect(actRandAll, &QAction::triggered, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    QAction *actRandVK = menuSim->addAction(QStringLiteral("🎲 Zufällige Positionen im Werk Völklingen"));
    connect(actRandVK, &QAction::triggered, this, &MainWindowSHSLogistics::onRandomizeVoelklingenClicked);
    menuSim->addSeparator();
    QAction *actDevTeam = menuSim->addAction(
        QStringLiteral("👩‍💻 Entwicklung: Frank U. Rückert && Sevda Dawd (sevdasdawd@gmail.com)"));
    connect(actDevTeam, &QAction::triggered, this, &MainWindowSHSLogistics::onShowAboutDialog);

    QMenu *menuDienste = mb->addMenu(QStringLiteral("&Telematik-Logins (6)"));
    const auto providers = Link2RailClient::allProviders();
    for (int i = 0; i < providers.size(); ++i) {
        QString safeName = providers[i].displayName;
        safeName.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QAction *a = menuDienste->addAction(QStringLiteral("🔐 Login: %1").arg(safeName));
        if (i == 0) a->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
        connect(a, &QAction::triggered, this, [this, i]() { onOpenProviderTab(i); });
    }
    menuDienste->addSeparator();
    QAction *actFetchAll = menuDienste->addAction(QStringLiteral("🔄 Alle angemeldeten Dienste jetzt abrufen"));
    actFetchAll->setShortcut(Qt::Key_F9);
    connect(actFetchAll, &QAction::triggered, this, &MainWindowSHSLogistics::onFetchAllAuthenticatedClicked);
    QAction *actLogoutAll = menuDienste->addAction(QStringLiteral("🚪 Von allen 6 Diensten abmelden"));
    connect(actLogoutAll, &QAction::triggered, this, &MainWindowSHSLogistics::onLogoutAllClicked);

    QMenu *menuHelp = mb->addMenu(QStringLiteral("&Hilfe"));
    QAction *actMainPdf = menuHelp->addAction(
        QStringLiteral("📕 Technisches Handbuch && GUI-Manual öffnen (PDF)"));
    actMainPdf->setShortcut(Qt::Key_F10);
    connect(actMainPdf, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenMainManualPdf);

    QAction *actQuickPdf = menuHelp->addAction(
        QStringLiteral("📘 Kurzanleitung && Schnellstart-Manual öffnen (PDF)"));
    actQuickPdf->setShortcut(Qt::Key_F11);
    connect(actQuickPdf, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenQuickStartPdf);

    QAction *actOpenFolder = menuHelp->addAction(
        QStringLiteral("📂 Handbuch- && Release-Ordner im Explorer öffnen"));
    connect(actOpenFolder, &QAction::triggered, this, &MainWindowSHSLogistics::onOpenManualsFolder);

    menuHelp->addSeparator();
    QAction *actSplash = menuHelp->addAction(
        QStringLiteral("🖼️ Start-Splashscreen (SHS Corporate Design) anzeigen"));
    connect(actSplash, &QAction::triggered, this, &MainWindowSHSLogistics::onShowSplashPreviewDialog);

    QAction *actAbout = menuHelp->addAction(QStringLiteral("ℹ️ Über SHS Logistics – Team: Frank U. Rückert && Sevda Dawd"));
    connect(actAbout, &QAction::triggered, this, &MainWindowSHSLogistics::onShowAboutDialog);
}

// =============================================================================
// MODUS-UMSCHALTUNG
// =============================================================================

void MainWindowSHSLogistics::onSwitchToSimulationMode()
{
    m_mapWidget->setOperatingMode(AppOperatingMode::Simulation);
    updateHeaderAndModeUi();
}

void MainWindowSHSLogistics::onSwitchToLiveGpsMode()
{
    m_mapWidget->setOperatingMode(AppOperatingMode::LiveGps);
    updateHeaderAndModeUi();
}

void MainWindowSHSLogistics::onOperatingModeChanged(AppOperatingMode mode)
{
    const bool isSim = (mode == AppOperatingMode::Simulation);
    m_quickActionStack->setCurrentIndex(isSim ? 0 : 1);
    m_sidePanelStack->setCurrentIndex(isSim ? 0 : 1);

    if (isSim) {
        refreshSimTrainSelectorAndControls();
        refreshSimWagonTable();
        refreshSimSourcesSinksAndCargoTables();
    } else {
        refreshLiveWagonTable();
        refreshLiveNodesTable();
    }
    updateHeaderAndModeUi();
}

void MainWindowSHSLogistics::ensureSimulationModeForAction()
{
    if (!m_mapWidget->isSimulationMode()) {
        onSwitchToSimulationMode();
    }
}

void MainWindowSHSLogistics::syncLayerControls(bool fromSim)
{
    if (m_updatingUi) return;
    m_updatingUi = true;
    if (fromSim) {
        m_chkLiveSimplifiedPlant->setChecked(m_chkSimSimplifiedPlant->isChecked());
        m_chkLiveSourcesSinks->setChecked(m_chkSimSourcesSinks->isChecked());
        m_chkLiveRoadsRivers->setChecked(m_chkSimRoadsRivers->isChecked());
        m_chkLiveTrackLabels->setChecked(m_chkSimTrackLabels->isChecked());
        m_chkLiveWagonLabels->setChecked(m_chkSimWagonLabels->isChecked());
        m_chkLiveExaggerateSize->setChecked(m_chkSimExaggerateSize->isChecked());
    } else {
        m_chkSimSimplifiedPlant->setChecked(m_chkLiveSimplifiedPlant->isChecked());
        m_chkSimSourcesSinks->setChecked(m_chkLiveSourcesSinks->isChecked());
        m_chkSimRoadsRivers->setChecked(m_chkLiveRoadsRivers->isChecked());
        m_chkSimTrackLabels->setChecked(m_chkLiveTrackLabels->isChecked());
        m_chkSimWagonLabels->setChecked(m_chkLiveWagonLabels->isChecked());
        m_chkSimExaggerateSize->setChecked(m_chkLiveExaggerateSize->isChecked());
    }
    m_updatingUi = false;
}

// =============================================================================
// SIMULATIONSMODUS SLOTS & TABELLEN-REFRESH (100% AUS V0.8 WIEDERHERGESTELLT)
// =============================================================================

void MainWindowSHSLogistics::refreshSimTrainSelectorAndControls()
{
    if (!m_mapWidget->isSimulationMode()) return;
    m_updatingUi = true;

    const auto &trains = m_mapWidget->trains();
    const int selIdx = std::clamp(m_mapWidget->selectedTrainIndex(), 0, std::max(0, static_cast<int>(trains.size()) - 1));

    m_comboTrain->clear();
    for (const TrainConsist &tr : trains) {
        const QString prefix = tr.hasLocomotive ? QStringLiteral("🚂 ") : QStringLiteral("🅿️ [OHNE LOK] ");
        m_comboTrain->addItem(QString("%1%2 (%3 Wg.)").arg(prefix, tr.id).arg(tr.wagons.size()));
    }
    if (selIdx >= 0 && selIdx < m_comboTrain->count()) {
        m_comboTrain->setCurrentIndex(selIdx);
    }

    if (selIdx >= 0 && selIdx < trains.size()) {
        const TrainConsist &tr = trains[selIdx];
        m_spinWagonCount->setValue(tr.wagons.size());

        for (int i = 0; i < m_comboTrackAssign->count(); ++i) {
            if (m_comboTrackAssign->itemData(i).toString() == tr.trackId) {
                m_comboTrackAssign->setCurrentIndex(i);
                break;
            }
        }

        double totalLen = tr.hasLocomotive ? 16.0 : 0.0;
        int totalAxles = tr.hasLocomotive ? 4 : 0;
        double totalTons = 0.0;
        for (const WagonData &w : tr.wagons) {
            totalLen += w.lengthMeters;
            totalAxles += w.axleCount;
            totalTons += w.weightTons;
        }
        m_lblTrainMetrics->setText(
            QString("%1 m | %2 A | %3 t").arg(totalLen, 0, 'f', 0).arg(totalAxles).arg(totalTons, 0, 'f', 0));
    }

    m_spinLocoPool->setValue(m_mapWidget->totalLocomotivesInPool());
    m_lblLocoPoolInfo->setText(
        QString("Belegt: %1 | Frei: %2 | Gelagert: %3")
            .arg(m_mapWidget->usedLocomotivesCount())
            .arg(m_mapWidget->freeLocomotivesCount())
            .arg(std::max(0, static_cast<int>(trains.size()) - m_mapWidget->usedLocomotivesCount())));

    m_updatingUi = false;
}

void MainWindowSHSLogistics::refreshSimWagonTable()
{
    if (!m_mapWidget->isSimulationMode()) return;
    m_updatingUi = true;

    const auto &trains = m_mapWidget->trains();
    const int tIdx = m_mapWidget->selectedTrainIndex();
    if (tIdx < 0 || tIdx >= trains.size()) {
        m_tableSimWagons->setRowCount(0);
        m_updatingUi = false;
        return;
    }

    const TrainConsist &tr = trains[tIdx];
    m_tableSimWagons->setRowCount(tr.wagons.size());

    for (int i = 0; i < tr.wagons.size(); ++i) {
        const WagonData &w = tr.wagons[i];
        auto *itPos = new QTableWidgetItem(QString("#%1").arg(w.positionIndex));
        auto *itUic = new QTableWidgetItem(w.uicNumber);
        auto *itTyp = new QTableWidgetItem(RailMapWidget::shortCodeForWagon(w.category));
        auto *itAxl = new QTableWidgetItem(QString("%1A").arg(w.axleCount));
        auto *itPrd = new QTableWidgetItem(RailMapWidget::productShortName(w.productType));
        auto *itWgt = new QTableWidgetItem(QString("%1 t").arg(w.weightTons, 0, 'f', 1));

        const QColor c = RailMapWidget::colorForWagon(w.category, m_mapWidget->wagonColorMode());
        itTyp->setBackground(c);
        itTyp->setForeground(Qt::white);

        itPos->setTextAlignment(Qt::AlignCenter);
        itAxl->setTextAlignment(Qt::AlignCenter);
        itWgt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_tableSimWagons->setItem(i, 0, itPos);
        m_tableSimWagons->setItem(i, 1, itUic);
        m_tableSimWagons->setItem(i, 2, itTyp);
        m_tableSimWagons->setItem(i, 3, itAxl);
        m_tableSimWagons->setItem(i, 4, itPrd);
        m_tableSimWagons->setItem(i, 5, itWgt);
    }

    const int wIdx = m_mapWidget->selectedWagonIndex();
    if (wIdx >= 0 && wIdx < tr.wagons.size()) {
        m_tableSimWagons->selectRow(wIdx);
        m_comboEditWagonType->setCurrentIndex(static_cast<int>(tr.wagons[wIdx].category));
        m_comboEditWagonProduct->setCurrentIndex(static_cast<int>(tr.wagons[wIdx].productType));
    }

    m_updatingUi = false;
}

void MainWindowSHSLogistics::refreshSimSourcesSinksAndCargoTables()
{
    const auto &nodes = m_mapWidget->sourceSinkNodes();
    m_tableSimNodes->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const SourceSinkNode &n = nodes[i];
        m_tableSimNodes->setItem(i, 0, new QTableWidgetItem(n.id));
        m_tableSimNodes->setItem(i, 1, new QTableWidgetItem(n.name));
        m_tableSimNodes->setItem(i, 2, new QTableWidgetItem(n.role));
        m_tableSimNodes->setItem(i, 3, new QTableWidgetItem(
            QString("⬆️ %1 t | ⬇️ %2 t").arg(n.totalLoadedTons, 0, 'f', 0).arg(n.totalUnloadedTons, 0, 'f', 0)));
        m_tableSimNodes->setItem(i, 4, new QTableWidgetItem(
            QString("%1 / %2 t").arg(n.currentStockTons, 0, 'f', 0).arg(n.maxCapacityTons, 0, 'f', 0)));
    }

    const auto &orders = m_mapWidget->cargoOrders();
    m_tableCargoOrders->setRowCount(orders.size());
    for (int i = 0; i < orders.size(); ++i) {
        const CargoOrder &o = orders[i];
        QString statusTxt;
        QColor stBg("#334155");
        switch (o.status) {
        case CargoOrderStatus::AtSourceStorage:      statusTxt = QStringLiteral("Im Lager"); stBg = QColor("#475569"); break;
        case CargoOrderStatus::LoadingAtSource:      statusTxt = QStringLiteral("⬆️ Aufnahme"); stBg = QColor("#15803D"); break;
        case CargoOrderStatus::StoredOnParkedWagons: statusTxt = QStringLiteral("🅿️ OHNE LOK"); stBg = QColor("#7E22CE"); break;
        case CargoOrderStatus::InTransit:            statusTxt = QStringLiteral("🚂 In Fahrt"); stBg = QColor("#0284C7"); break;
        case CargoOrderStatus::UnloadingAtSink:      statusTxt = QStringLiteral("⬇️ Abladung"); stBg = QColor("#0369A1"); break;
        case CargoOrderStatus::DeliveredToSink:      statusTxt = QStringLiteral("✓ Geliefert"); stBg = QColor("#166534"); break;
        }
        m_tableCargoOrders->setItem(i, 0, new QTableWidgetItem(o.orderId));
        m_tableCargoOrders->setItem(i, 1, new QTableWidgetItem(RailMapWidget::productShortName(o.product)));
        m_tableCargoOrders->setItem(i, 2, new QTableWidgetItem(QString("%1 t").arg(o.weightTons, 0, 'f', 0)));
        m_tableCargoOrders->setItem(i, 3, new QTableWidgetItem(QString("%1 -> %2").arg(o.sourceNodeId, o.sinkNodeId)));
        m_tableCargoOrders->setItem(i, 4, new QTableWidgetItem(o.assignedConsistId));
        auto *itSt = new QTableWidgetItem(statusTxt);
        itSt->setBackground(stBg);
        itSt->setForeground(Qt::white);
        m_tableCargoOrders->setItem(i, 5, itSt);
    }
}

void MainWindowSHSLogistics::onTrainComboChanged(int index)
{
    if (m_updatingUi || index < 0 || !m_mapWidget->isSimulationMode()) return;
    m_mapWidget->selectTrain(index, -1, true);
    refreshSimTrainSelectorAndControls();
    refreshSimWagonTable();
}

void MainWindowSHSLogistics::onTrackAssignChanged(int index)
{
    if (m_updatingUi || index < 0 || !m_mapWidget->isSimulationMode()) return;
    const QString trackId = m_comboTrackAssign->itemData(index).toString();
    m_mapWidget->assignTrainToTrack(m_mapWidget->selectedTrainIndex(), trackId);
}

void MainWindowSHSLogistics::onReverseTrainClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->reverseTrainDirection(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onWagonCountSpinChanged(int value)
{
    if (m_updatingUi || !m_mapWidget->isSimulationMode()) return;
    m_mapWidget->setTrainWagonCount(m_mapWidget->selectedTrainIndex(), value, WagonCategory::Samms_6Axle);
}

void MainWindowSHSLogistics::onAddSammsClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Samms_6Axle, CargoProductType::SteelBillets);
}

void MainWindowSHSLogistics::onAddRnszClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Rnsz_4Axle, CargoProductType::HeavyPlates);
}

void MainWindowSHSLogistics::onAdd4AxleClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils);
}

void MainWindowSHSLogistics::onAdd6AxleClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron);
}

void MainWindowSHSLogistics::onRemoveWagonClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->removeLastWagonFromTrain(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onWagonTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (m_updatingUi || !m_mapWidget->isSimulationMode()) return;
    m_mapWidget->selectTrain(m_mapWidget->selectedTrainIndex(), row, false);
    const auto &trains = m_mapWidget->trains();
    const int tIdx = m_mapWidget->selectedTrainIndex();
    if (tIdx >= 0 && tIdx < trains.size() && row >= 0 && row < trains[tIdx].wagons.size()) {
        m_comboEditWagonType->setCurrentIndex(static_cast<int>(trains[tIdx].wagons[row].category));
        m_comboEditWagonProduct->setCurrentIndex(static_cast<int>(trains[tIdx].wagons[row].productType));
    }
}

void MainWindowSHSLogistics::onWagonTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (!m_mapWidget->isSimulationMode()) return;
    m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), row);
}

void MainWindowSHSLogistics::onChangeSelectedWagonCategory()
{
    ensureSimulationModeForAction();
    const int tIdx = m_mapWidget->selectedTrainIndex();
    int wIdx = m_mapWidget->selectedWagonIndex();
    if (wIdx < 0) wIdx = m_tableSimWagons->currentRow();
    if (wIdx < 0) return;

    m_mapWidget->setWagonCategoryAt(tIdx, wIdx, static_cast<WagonCategory>(m_comboEditWagonType->currentIndex()));
    m_mapWidget->setWagonProductAt(tIdx, wIdx, static_cast<CargoProductType>(m_comboEditWagonProduct->currentIndex()));
}

void MainWindowSHSLogistics::onSimColorModeChanged(int index)
{
    if (m_updatingUi) return;
    m_mapWidget->setWagonColorMode(static_cast<WagonColorMode>(index));
    refreshSimWagonTable();
}

void MainWindowSHSLogistics::onToggleSimClicked()
{
    ensureSimulationModeForAction();
    const bool nextRun = !m_mapWidget->isSimulationRunning();
    m_mapWidget->setSimulationRunning(nextRun);
    m_btnPlayPause->setText(nextRun ? QStringLiteral("⏸ Simulation Pause (Space)")
                                    : QStringLiteral("▶ Simulation Start (Space)"));
    updateHeaderAndModeUi();
}

void MainWindowSHSLogistics::onSpeedSliderChanged(int value)
{
    const double factor = value / 10.0;
    m_lblSpeedValue->setText(QString("%1x").arg(factor, 0, 'f', 1));
    m_mapWidget->setSimulationSpeedMultiplier(factor);
}

void MainWindowSHSLogistics::onLocoPoolSpinChanged(int value)
{
    if (m_updatingUi || !m_mapWidget->isSimulationMode()) return;
    m_mapWidget->setTotalLocomotivesInPool(value);
}

void MainWindowSHSLogistics::onCoupleLocoClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->coupleFreeLocoToConsist(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onUncoupleLocoClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->uncoupleLocoFromConsist(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onNewOrderProductChanged(int index)
{
    if (index < 0) return;
    const auto prod = static_cast<CargoProductType>(index);
    m_comboNewWagonCat->setCurrentIndex(static_cast<int>(RailMapWidget::recommendedWagonForProduct(prod)));

    const QString defSrc = RailMapWidget::defaultSourceForProduct(prod);
    const QString defSnk = RailMapWidget::defaultSinkForProduct(prod);
    for (int i = 0; i < m_comboNewSource->count(); ++i) {
        if (m_comboNewSource->itemData(i).toString() == defSrc) {
            m_comboNewSource->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_comboNewSink->count(); ++i) {
        if (m_comboNewSink->itemData(i).toString() == defSnk) {
            m_comboNewSink->setCurrentIndex(i);
            break;
        }
    }
}

void MainWindowSHSLogistics::onCreateCargoOrderClicked()
{
    ensureSimulationModeForAction();
    const auto prod = static_cast<CargoProductType>(m_comboNewProduct->currentIndex());
    const double tons = static_cast<double>(m_spinNewTons->value());
    const QString srcId = m_comboNewSource->currentData().toString();
    const QString snkId = m_comboNewSink->currentData().toString();
    const auto cat = static_cast<WagonCategory>(m_comboNewWagonCat->currentIndex());

    m_mapWidget->createAndDistributeCargoOrder(prod, tons, srcId, snkId, cat);
}

void MainWindowSHSLogistics::onSimNodeTableClicked(int row, int column)
{
    Q_UNUSED(column);
    m_mapWidget->zoomToSourceSinkNode(row);
}

void MainWindowSHSLogistics::onCargoOrderTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (!m_mapWidget->isSimulationMode()) return;
    const auto &orders = m_mapWidget->cargoOrders();
    if (row < 0 || row >= orders.size()) return;
    const QString cId = orders[row].assignedConsistId;
    const auto &trains = m_mapWidget->trains();
    for (int i = 0; i < trains.size(); ++i) {
        if (trains[i].id == cId) {
            m_mapWidget->selectTrain(i, 0, true);
            break;
        }
    }
}

void MainWindowSHSLogistics::onRandomizeAllClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->randomizePositionsOnTracks(false);
}

void MainWindowSHSLogistics::onRandomizeVoelklingenClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->randomizePositionsOnTracks(true);
    m_mapWidget->focusSite(QStringLiteral("Voelklingen"), true);
}

void MainWindowSHSLogistics::onQuerySimDbApiClicked()
{
    ensureSimulationModeForAction();
    m_mapWidget->applySimulatedDbApiPositioning(
        QStringLiteral("https://apis.deutschebahn.com/db-cargo/link2rail/simulation/v2/positions"),
        QString());
}

void MainWindowSHSLogistics::onSimAutoPollToggled(bool checked)
{
    if (checked) {
        m_simApiPollTimer->start();
        statusBar()->showMessage(QStringLiteral("Auto-Protokoll für Simulations-Telematik aktiv (alle 5 Sek.)."), 5000);
    } else {
        m_simApiPollTimer->stop();
        statusBar()->showMessage(QStringLiteral("Auto-Protokoll für Simulations-Telematik gestoppt."), 4000);
    }
}

void MainWindowSHSLogistics::onSimStepped()
{
    static int tickCounter = 0;
    if (++tickCounter % 15 == 0 && m_mapWidget->isSimulationMode()) {
        updateHeaderAndModeUi();
    }
}

// =============================================================================
// LIVE-GPS-MODUS SLOTS (6 TELEMATIK-PORTALE)
// =============================================================================

void MainWindowSHSLogistics::onOpenProviderTab(int providerIndex)
{
    onSwitchToLiveGpsMode();
    m_liveSideTabs->setCurrentIndex(1);
    if (m_providerLoginTabs && providerIndex >= 0 && providerIndex < m_providerLoginTabs->count()) {
        m_providerLoginTabs->setCurrentIndex(providerIndex);
    }
}

void MainWindowSHSLogistics::onFetchAllAuthenticatedClicked()
{
    onSwitchToLiveGpsMode();
    int triggered = 0;
    for (Link2RailClient *c : m_clients) {
        if (c->isAuthenticated()) {
            c->fetchAllLiveData();
            triggered++;
        }
    }
    if (triggered == 0) {
        m_liveSideTabs->setCurrentIndex(1);
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
    onSwitchToLiveGpsMode();
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
        m_liveSideTabs->setCurrentIndex(0);
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
    updateHeaderAndModeUi();
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
    refreshLiveWagonTable();
    refreshLiveNodesTable();
    updateHeaderAndModeUi();
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
    const auto &trains = m_mapWidget->liveTrains();

    int totalWagons = 0;
    int totalGps = 0;

    for (int tIdx = 0; tIdx < trains.size(); ++tIdx) {
        const TrainConsist &tr = trains[tIdx];
        for (int wIdx = 0; wIdx < tr.wagons.size(); ++wIdx) {
            const WagonData &w = tr.wagons[wIdx];
            totalWagons++;
            if (w.hasValidGps) totalGps++;

            if (!provFilter.isEmpty() && w.providerKey != provFilter) continue;
            if (catFilter > 0 && static_cast<int>(w.category) != (catFilter - 1)) continue;
            if (!query.isEmpty()) {
                const QString hay = (w.providerName + ' ' + w.uicNumber + ' ' + tr.id + ' ' + w.wagonTypeRaw + ' ' +
                                     w.locationName + ' ' + w.geofenceSite + ' ' + w.locationCode + ' ' +
                                     w.liveStatusText + ' ' + w.cargoDescription + ' ' +
                                     w.originName + ' ' + w.destinationName).toLower();
                if (!hay.contains(query)) continue;
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

        if (m_mapWidget->isLiveGpsMode() &&
            ref.trainIdx == m_mapWidget->selectedTrainIndex() &&
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
    if (!m_mapWidget->isLiveGpsMode()) return;
    const int tIdx = m_mapWidget->selectedTrainIndex();
    const int wIdx = m_mapWidget->selectedWagonIndex();
    const auto &trains = m_mapWidget->liveTrains();
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

void MainWindowSHSLogistics::refreshLiveNodesTable()
{
    const auto &nodes = m_mapWidget->sourceSinkNodes();
    m_tableLiveNodes->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const SourceSinkNode &n = nodes[i];
        m_tableLiveNodes->setItem(i, 0, new QTableWidgetItem(n.id));
        m_tableLiveNodes->setItem(i, 1, new QTableWidgetItem(n.name));
        m_tableLiveNodes->setItem(i, 2, new QTableWidgetItem(n.role));

        auto *itCnt = new QTableWidgetItem(QString("%1 Waggon(s)").arg(n.liveWagonCount));
        if (n.liveWagonCount > 0) {
            itCnt->setBackground(QColor("#065F46"));
            itCnt->setForeground(Qt::white);
        }
        m_tableLiveNodes->setItem(i, 3, itCnt);
        m_tableLiveNodes->setItem(i, 4, new QTableWidgetItem(n.producedGoods + QStringLiteral(" / ") + n.consumedGoods));
    }
}

void MainWindowSHSLogistics::updateHeaderAndModeUi()
{
    const bool isSim = m_mapWidget->isSimulationMode();

    // Modus-Toggle-Buttons optisch hervorheben
    m_btnModeSim->setChecked(isSim);
    m_btnModeLive->setChecked(!isSim);
    m_btnModeSim->setStyleSheet(isSim
        ? "QPushButton { background-color: #EF7D00; color: #FFFFFF; border: 2px solid #FBD50E; padding: 5px 11px; font-weight: bold; }"
        : "QPushButton { background-color: #334155; color: #CBD5E1; border: 1px solid #64748B; padding: 5px 11px; font-weight: bold; } QPushButton:hover { background-color: #475569; color: #FBD50E; }");
    m_btnModeLive->setStyleSheet(!isSim
        ? "QPushButton { background-color: #0D9488; color: #FFFFFF; border: 2px solid #FBD50E; padding: 5px 11px; font-weight: bold; }"
        : "QPushButton { background-color: #334155; color: #CBD5E1; border: 1px solid #64748B; padding: 5px 11px; font-weight: bold; } QPushButton:hover { background-color: #475569; color: #FBD50E; }");

    // Live-Provider-Status berechnen
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

    const int totalLiveW = m_mapWidget->totalLiveWagonsCount();
    const int gpsLiveW = m_mapWidget->totalLiveGpsWagonsCount();
    const QString syncStr = latestSync.isValid() ? latestSync.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("—");
    const QString summaryStr = activeNames.join(QStringLiteral(", "));
    m_mapWidget->setMultiProviderStatus(activeCnt, m_clients.size(), summaryStr, syncStr);

    if (activeCnt > 0) {
        m_btnToolbarLogins->setText(QString("🟢 Dienste && Logins (%1/%2)").arg(activeCnt).arg(m_clients.size()));
        m_btnToolbarLogins->setStyleSheet(
            "QPushButton { background-color: #15803D; color: #FFFFFF; border: 1px solid #1E293B; padding: 4px 10px; font-weight: bold; } "
            "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }");
    } else {
        m_btnToolbarLogins->setText(QString("🔐 Dienste && Logins (0/%1)").arg(m_clients.size()));
        m_btnToolbarLogins->setStyleSheet(
            "QPushButton { background-color: #DC2626; color: #FFFFFF; border: 1px solid #1E293B; padding: 4px 10px; font-weight: bold; } "
            "QPushButton:hover { background-color: #FBD50E; color: #0F172A; }");
    }

    if (isSim) {
        int activeLoco = 0, parked = 0, simWags = 0;
        for (const TrainConsist &tr : m_mapWidget->simTrains()) {
            if (tr.hasLocomotive) activeLoco++;
            else parked++;
            simWags += tr.wagons.size();
        }
        m_lblHeaderStats->setText(
            QString("🎮 SIMULATION | Züge: %1 | 🅿️ Gelagert: %2 | Waggons: %3 | Lok-Pool: %4/%5 | %6")
                .arg(activeLoco).arg(parked).arg(simWags)
                .arg(m_mapWidget->freeLocomotivesCount()).arg(m_mapWidget->totalLocomotivesInPool())
                .arg(m_mapWidget->isSimulationRunning() ? QStringLiteral("▶ LÄUFT") : QStringLiteral("⏸ PAUSE")));
    } else {
        if (activeCnt > 0) {
            m_lblHeaderStats->setText(
                QString("📡 LIVE-GPS | 🟢 Dienste: %1/%2 (%3) | Waggons: %4 (GPS: %5) | Sync: %6")
                    .arg(activeCnt).arg(m_clients.size()).arg(summaryStr).arg(totalLiveW).arg(gpsLiveW).arg(syncStr));
        } else {
            m_lblHeaderStats->setText(
                QString("📡 LIVE-GPS | 🔴 Dienste: 0/%1 | Geladene Waggons: %2 (GPS: %3)")
                    .arg(m_clients.size()).arg(totalLiveW).arg(gpsLiveW));
        }
    }

    if (m_bannerNotLoggedIn) {
        m_bannerNotLoggedIn->setVisible(activeCnt == 0 && totalLiveW == 0);
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

void MainWindowSHSLogistics::onLiveColorModeChanged(int index)
{
    if (m_updatingUi) return;
    m_mapWidget->setWagonColorMode(static_cast<WagonColorMode>(index));
    refreshLiveWagonTable();
}

void MainWindowSHSLogistics::onLiveNodeTableClicked(int row, int column)
{
    Q_UNUSED(column);
    m_mapWidget->zoomToSourceSinkNode(row);
}

void MainWindowSHSLogistics::onMapSelectionChanged(int trainIdx, int wagonIdx)
{
    Q_UNUSED(trainIdx);
    Q_UNUSED(wagonIdx);
    if (m_updatingUi) return;
    if (m_mapWidget->isSimulationMode()) {
        refreshSimTrainSelectorAndControls();
        refreshSimWagonTable();
    } else {
        refreshLiveWagonTable();
    }
}

void MainWindowSHSLogistics::onTrainsUpdated()
{
    if (m_mapWidget->isSimulationMode()) {
        refreshSimTrainSelectorAndControls();
        refreshSimWagonTable();
    } else {
        refreshLiveWagonTable();
    }
    updateHeaderAndModeUi();
}

void MainWindowSHSLogistics::onCargoAndNodesUpdated()
{
    refreshSimSourcesSinksAndCargoTables();
    refreshLiveNodesTable();
}

void MainWindowSHSLogistics::onMapModeChanged(int index)
{
    if (m_updatingUi) return;
    m_updatingUi = true;
    m_comboSimMapMode->setCurrentIndex(index);
    m_comboLiveMapMode->setCurrentIndex(index);
    m_updatingUi = false;
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
    aboutBox.setWindowTitle(QStringLiteral("Über SHS Logistics GmbH – Werksbahn-Simulation & Multi-Telematik"));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setIconPixmap(QPixmap(QStringLiteral(":/SHSIcon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    aboutBox.setText(QStringLiteral(
        "<h2 style='color:#3B464E; margin-bottom:2px;'>SHS LOGISTICS GmbH</h2>"
        "<h4 style='color:#EF7D00; margin-top:0px;'>Dual-Mode Werksbahn-Simulation &amp; Multi-Telematik Live-GPS-Leitsystem</h4>"
        "<p style='color:#5C839C; font-style:italic;'>Partner [schafft Zukunft] Stahl</p>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<table style='font-size:11px;' cellpadding='2'>"
        "<tr><td><b>Version:</b></td><td><b>" SHS_APP_VERSION " (Dual-Mode: Simulationsmodus + 6 Live-GPS-Portale)</b></td></tr>"
        "<tr><td><b>Wissenschaftliche Leitung &amp; Entwicklung:</b></td><td><b>Frank U. Rückert</b> (<a href='mailto:frank.rueckert@htwsaar.de' style='color:#0284C7; font-weight:bold;'>frank.rueckert@htwsaar.de</a>)</td></tr>"
        "<tr><td><b>Entwicklerin:</b></td><td><b>Sevda Dawd</b> (<a href='mailto:sevdasdawd@gmail.com' style='color:#0284C7; font-weight:bold;'>sevdasdawd@gmail.com</a>)</td></tr>"
        "<tr><td><b>Institution:</b></td><td>htw saar – Hochschule für Technik und Wirtschaft des Saarlandes</td></tr>"
        "<tr><td><b>Kooperationspartner:</b></td><td>SHS Logistics GmbH (Stahl-Holding-Saar), Werkstraße 1, 66763 Dillingen/Saar</td></tr>"
        "<tr><td><b>Software-Plattform:</b></td><td>C++17 / Qt 6.9.0 (MinGW 64-Bit, TLS/HTTPS SChannel, CMake + Ninja)</td></tr>"
        "</table>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<p><b>Zwei nahtlos umschaltbare Betriebsmodi (Ctrl+1 / Ctrl+2):</b></p>"
        "<ul style='font-size:10.5px;'>"
        "<li><b>🎮 Simulationsmodus (Spiel):</b> Interaktive 2D-Züge, Torpedowagen (1420 °C Flüssigeisen), Samms/Rns-z Waggon-Editor, Güter-Objekte, 10 reale Quellen/Senken, Lokomotiv-Pool &amp; Zufalls-Positionierung.</li>"
        "<li><b>📡 Live-GPS-Modus (6 Portale):</b> DB Cargo link2rail, Intermodal Telematics (IMT Teno API), DOT Telematik, TRANSWAGGON (WOPS Form-Login), Innofreight OS &amp; Wascosa (Nexxiot/SAVVY) inkl. Verbindungstest &amp; Browser-Session/cURL-Import.</li>"
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
               QStringLiteral("Partner [schafft Zukunft] Stahl — Simulation & Multi-Telematik Live-GPS"));

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
               QStringLiteral("Simulation + 6 Live-GPS-Portale"));

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
    p.setFont(QFont("Verdana", 9.0, QFont::Bold));
    p.drawText(QRectF(sideRect.left() + 12, sideRect.top() + 10, sideRect.width() - 24, 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("DUAL-MODE LEITSYSTEM"));
    p.setPen(QPen(QColor("#EF7D00"), 2.0));
    p.drawLine(QPointF(sideRect.left() + 12, sideRect.top() + 34),
               QPointF(sideRect.right() - 12, sideRect.top() + 34));

    const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("🎮 2D-Werksbahn-Simulation"),   QStringLiteral("Züge, Torpedo 1420°C, Quellen/Senken")},
        {QStringLiteral("🔐 DB Cargo link2rail"),        QStringLiteral("API V3, Geofence & Browser-Session")},
        {QStringLiteral("🔐 Intermodal Telematics"),     QStringLiteral("IMT Teno API (x-session-token)")},
        {QStringLiteral("🔐 DOT Telematik"),             QStringLiteral("DOT-Link & Unified API / cURL")},
        {QStringLiteral("🔐 TRANSWAGGON (WOPS)"),        QStringLiteral("WOPS Form-Login & JSON/XML/CSV")},
        {QStringLiteral("🔐 Innofreight & Wascosa"),     QStringLiteral("Innofreight OS, Nexxiot & SAVVY")}
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
        "<h3 style='color:#EF7D00;'>SHS Logistics GmbH – Dual-Mode Werksbahn-Simulation &amp; Multi-Telematik Live-GPS (Version %2)</h3>"
        "<p><b>Leitung &amp; Entwicklung:</b> Frank U. Rückert (frank.rueckert@htwsaar.de) &amp; Sevda Dawd (sevdasdawd@gmail.com), htw saar</p>"
        "<p><b>Zwei Betriebsmodi:</b> 🎮 Simulationsmodus (Spiel) &amp; 📡 Live-GPS-Modus (DB Cargo link2rail, IMT, DOT, TRANSWAGGON, Innofreight, Wascosa).</p>")
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
        100, QStringLiteral("Dual-Mode Leitsystem aktiv: Simulationsmodus + 6 Live-GPS-Portale.")));
    lay->addWidget(lbl);
    dlg.setFixedSize(860, 510);
    dlg.exec();
}

void MainWindowSHSLogistics::scheduleAutoScreenshots(const QString &outDir)
{
    createCorporateSplashPixmap(
        100, QStringLiteral("Dual-Mode bereit: 🎮 Simulationsmodus (Spiel) & 📡 Live-GPS (6 Portale)!"))
        .save(outDir + QStringLiteral("/preview_splash.png"));

    // 1. Simulationsmodus: Werk Völklingen (Züge, Torpedos, Samms/Rns-z)
    QTimer::singleShot(2000, this, [this, outDir]() {
        this->grab().save(outDir + QStringLiteral("/preview_voelklingen.png"));
        // 2. Simulationsmodus: Tab 2 Güter, Quellen & Senken
        m_simSideTabs->setCurrentIndex(1);
        QTimer::singleShot(700, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/preview_goods_tab.png"));
            // 3. Simulationsmodus: Dillingen (ROGESA & Dillinger Hütte)
            m_mapWidget->focusSite(QStringLiteral("Dillingen"), false);
            QTimer::singleShot(900, this, [this, outDir]() {
                this->grab().save(outDir + QStringLiteral("/preview_dillingen.png"));
                // 4. Simulationsmodus: Gesamtübersicht Saarland-Korridor
                m_mapWidget->focusSite(QStringLiteral("Gesamt"), false);
                QTimer::singleShot(900, this, [this, outDir]() {
                    this->grab().save(outDir + QStringLiteral("/preview_corridor.png"));
                    // 5. Live-GPS-Modus: Tab 2 (6 Login-Reiter + cURL/Test)
                    onSwitchToLiveGpsMode();
                    m_mapWidget->focusSite(QStringLiteral("Voelklingen"), false);
                    m_liveSideTabs->setCurrentIndex(1);
                    if (m_providerLoginTabs) m_providerLoginTabs->setCurrentIndex(1); // IMT Teno Tab zeigen
                    QTimer::singleShot(800, this, [this, outDir]() {
                        this->grab().save(outDir + QStringLiteral("/preview_api_tab.png"));
                        // 6. Live-GPS-Modus: Tab 1 (Vereinigte Live-Waggons)
                        m_liveSideTabs->setCurrentIndex(0);
                        QTimer::singleShot(500, this, [this, outDir]() {
                            this->grab().save(outDir + QStringLiteral("/preview_live_tab.png"));
                            this->close();
                        });
                    });
                });
            });
        });
    });
}
