#include "mainwindowshslogistics.h"
#include "./ui_mainwindowshslogistics.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
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
#include <algorithm>

#ifndef SHS_APP_VERSION
#define SHS_APP_VERSION "0.8"
#endif

MainWindowSHSLogistics::MainWindowSHSLogistics(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindowSHSLogistics)
{
    ui->setupUi(this);

    // Titelleiste inkl. Versionsnummer
    setWindowTitle(QStringLiteral("SHS Logistics GmbH – Werksbahn-, Waggon- & Logistik-Leitsystem  |  Version %1")
                       .arg(QStringLiteral(SHS_APP_VERSION)));

    setupCorporateTheme();
    buildMainLayout();
    setupMenuBar();

    m_apiPollTimer = new QTimer(this);
    m_apiPollTimer->setInterval(5000);
    connect(m_apiPollTimer, &QTimer::timeout, this, &MainWindowSHSLogistics::onQueryDbApiClicked);

    refreshTrainSelectorAndControls();
    refreshWagonTable();
    refreshSourcesSinksAndCargoTables();

    statusBar()->showMessage(
        QStringLiteral("SHS Logistics GmbH – Leitsystem bereit. Hauptansicht: Werk Völklingen (Saarstahl AG) mit Quellen, Senken, Güter-Objekten & Lokomotiv-Pool."));
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
            font-size: 10.5px;
            font-weight: bold;
            padding: 7px 9px;
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
            font-size: 10.5px;
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
                       "— Quellen, Senken, Güter-Disposition &amp; Werksbahn-Simulation"),
        headerFrame);
    lblSlogan->setStyleSheet(
        "font-family: Verdana, Arial; font-size: 11px; color: #334155; border: none; background: transparent;");
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
    // 2. SCHNELLWAHL-LEISTE (Standort-Zoom & Schnell-Aktionen)
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

    auto *btnZoomVK  = makeNavBtn(QStringLiteral("🏭 WERK VÖLKLINGEN (Saarstahl)"), QStringLiteral("#EF7D00"));
    auto *btnZoomDL  = makeNavBtn(QStringLiteral("🔥 DILLINGER HÜTTE (Roheisen/Blech)"));
    auto *btnZoomBB  = makeNavBtn(QStringLiteral("⚙️ WERK BURBACH (Draht)"));
    auto *btnZoomNK  = makeNavBtn(QStringLiteral("🔩 WERK NEUNKIRCHEN"));
    auto *btnZoomAll = makeNavBtn(QStringLiteral("🗺️ ALLE STANDORTE"));

    navLayout->addSpacing(8);

    m_btnPlayPause           = makeNavBtn(QStringLiteral("⏸ Simulation Pause"), QStringLiteral("#0284C7"));
    auto *btnQuickAddSamms   = makeNavBtn(QStringLiteral("➕ Samms (6A)"), QStringLiteral("#EA580C"));
    auto *btnQuickAddRnsz    = makeNavBtn(QStringLiteral("➕ Rns-z (4A)"), QStringLiteral("#0284C7"));
    auto *btnQuickAddTorpedo = makeNavBtn(QStringLiteral("🔥 + Torpedo (Flüssigeisen)"), QStringLiteral("#E11D48"));
    auto *btnQuickRemove     = makeNavBtn(QStringLiteral("➖ Waggon ab"));
    auto *btnQuickRandom     = makeNavBtn(QStringLiteral("🎲 Zufalls-Pos."));
    auto *btnQuickDbApi      = makeNavBtn(QStringLiteral("📡 DB-API Ortung"), QStringLiteral("#0D9488"));

    rootLayout->addWidget(navBar);

    // =========================================================================
    // 3. HAUPTBEREICH: LINKS 4 STEUER-TABS + MITTE GROSSER KARTEN- & SIMULATIONS-SCREEN
    // =========================================================================
    auto *mainSplitter = new QSplitter(Qt::Horizontal, root);
    mainSplitter->setHandleWidth(6);

    // Zuerst das Map-Widget anlegen, damit Quellen/Senken für die ComboBoxen bereitstehen
    m_mapWidget = new RailMapWidget();

    m_sideTabs = new QTabWidget(mainSplitter);
    // Breit genug, damit Tab-Titel, Button-Beschriftungen und Tabellen nicht abgeschnitten werden
    m_sideTabs->setMinimumWidth(560);
    m_sideTabs->setMaximumWidth(760);

    // -------------------------------------------------------------------------
    // TAB 1: ZÜGE, ZUGLÄNGE & WAGGONS (Samms / Rns-z / 4A / 6A + Geladenes Gut)
    // -------------------------------------------------------------------------
    auto *tabTrains = new QWidget();
    auto *tabTrainsLayout = new QVBoxLayout(tabTrains);
    tabTrainsLayout->setContentsMargins(7, 7, 7, 7);
    tabTrainsLayout->setSpacing(6);

    auto *grpTrainSelect = new QGroupBox(QStringLiteral("1. Zugverband / Gelagerte Waggongruppe && Route"), tabTrains);
    auto *grpTrainLayout = new QGridLayout(grpTrainSelect);

    grpTrainLayout->addWidget(new QLabel(QStringLiteral("Verband wählen:")), 0, 0);
    m_comboTrain = new QComboBox(grpTrainSelect);
    grpTrainLayout->addWidget(m_comboTrain, 0, 1, 1, 2);

    grpTrainLayout->addWidget(new QLabel(QStringLiteral("Gleisabschnitt:")), 1, 0);
    m_comboTrackAssign = new QComboBox(grpTrainSelect);
    grpTrainLayout->addWidget(m_comboTrackAssign, 1, 1);

    auto *btnReverse = new QPushButton(QStringLiteral("⇄ Richtung wenden"), grpTrainSelect);
    grpTrainLayout->addWidget(btnReverse, 1, 2);

    // Lok an-/abkuppeln (für Lagerung von Waggons ohne Lok)
    auto *btnCoupleLoco1 = new QPushButton(QStringLiteral("🚂 Freie Lok ankuppeln (Start)"), grpTrainSelect);
    btnCoupleLoco1->setStyleSheet("background-color: #059669; color: white;");
    auto *btnUncoupleLoco1 = new QPushButton(QStringLiteral("🅿️ Lok abkuppeln (Waggons lagern)"), grpTrainSelect);
    btnUncoupleLoco1->setStyleSheet("background-color: #7E22CE; color: white;");
    grpTrainLayout->addWidget(btnCoupleLoco1,   2, 0, 1, 2);
    grpTrainLayout->addWidget(btnUncoupleLoco1, 2, 2, 1, 1);

    m_lblTrainSummary = new QLabel(grpTrainSelect);
    m_lblTrainSummary->setStyleSheet("color: #0F172A; font-weight: bold; font-size: 10.5px; padding: 2px;");
    m_lblTrainSummary->setWordWrap(true);
    grpTrainLayout->addWidget(m_lblTrainSummary, 3, 0, 1, 3);

    tabTrainsLayout->addWidget(grpTrainSelect);

    // Gruppe B: Zug verlängern / verkürzen
    auto *grpLength = new QGroupBox(QStringLiteral("2. Zug verlängern / verkürzen (Waggon-Steuerung)"), tabTrains);
    auto *grpLenLayout = new QGridLayout(grpLength);
    grpLenLayout->setColumnStretch(0, 1);
    grpLenLayout->setColumnStretch(1, 1);

    auto *lenTopRow = new QHBoxLayout();
    lenTopRow->addWidget(new QLabel(QStringLiteral("Zuglänge (Waggons):")));
    m_spinWagonCount = new QSpinBox(grpLength);
    m_spinWagonCount->setRange(1, 30);
    m_spinWagonCount->setSuffix(QStringLiteral(" Waggons"));
    lenTopRow->addWidget(m_spinWagonCount);
    auto *btnRemoveLast = new QPushButton(QStringLiteral("➖ Letzten Waggon abhängen"), grpLength);
    btnRemoveLast->setStyleSheet("background-color: #B91C1C; color: white;");
    lenTopRow->addWidget(btnRemoveLast, 1);
    grpLenLayout->addLayout(lenTopRow, 0, 0, 1, 2);

    auto *btnAddSamms = new QPushButton(QStringLiteral("➕ Samms [6-Achser] anhängen"), grpLength);
    btnAddSamms->setStyleSheet("background-color: #EF7D00; color: white;");
    auto *btnAddRnsz = new QPushButton(QStringLiteral("➕ Rns-z [4-Achser] anhängen"), grpLength);
    btnAddRnsz->setStyleSheet("background-color: #0284C7; color: white;");
    auto *btnAdd4A = new QPushButton(QStringLiteral("➕ 4-Achser (Draht-Coils) anhängen"), grpLength);
    btnAdd4A->setStyleSheet("background-color: #10B981; color: white;");
    auto *btnAdd6A = new QPushButton(QStringLiteral("🔥 + Torpedowagen (Flüssigeisen)"), grpLength);
    btnAdd6A->setStyleSheet("background-color: #E11D48; color: white;");

    grpLenLayout->addWidget(btnAddSamms, 1, 0);
    grpLenLayout->addWidget(btnAddRnsz,  1, 1);
    grpLenLayout->addWidget(btnAdd4A,    2, 0);
    grpLenLayout->addWidget(btnAdd6A,    2, 1);

    tabTrainsLayout->addWidget(grpLength);

    // Gruppe C: Waggon-Positionsliste inkl. geladenem Stahlprodukt
    auto *grpWagons = new QGroupBox(
        QStringLiteral("3. Waggon-Positionen && geladene Güter (Samms / Rns-z / 4A / 6A)"), tabTrains);
    auto *grpWagonsLayout = new QVBoxLayout(grpWagons);

    auto *colorRow = new QHBoxLayout();
    colorRow->addWidget(new QLabel(QStringLiteral("Farbmodus:")));
    m_comboColorMode = new QComboBox(grpWagons);
    m_comboColorMode->addItem(QStringLiteral("Alle 4 Bauarten getrennt (Samms=Orange, Rns-z=Blau, 4A=Grün, 6A=Rot)"));
    m_comboColorMode->addItem(QStringLiteral("Fokus Bauart: Samms vs. Rns-z hervorheben"));
    m_comboColorMode->addItem(QStringLiteral("Strikt nach Achsen: Alle 4-Achser (Blau) vs. Alle 6-Achser (Orange)"));
    colorRow->addWidget(m_comboColorMode, 1);
    grpWagonsLayout->addLayout(colorRow);

    m_tableWagons = new QTableWidget(0, 6, grpWagons);
    m_tableWagons->setHorizontalHeaderLabels({
        QStringLiteral("Pos."),
        QStringLiteral("Bauart"),
        QStringLiteral("Achsen"),
        QStringLiteral("Geladenes Gut (Produkt)"),
        QStringLiteral("Gewicht"),
        QStringLiteral("GPS / UIC")
    });
    m_tableWagons->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWagons->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableWagons->setAlternatingRowColors(true);
    m_tableWagons->verticalHeader()->setVisible(false);
    m_tableWagons->horizontalHeader()->setStretchLastSection(true);
    m_tableWagons->setColumnWidth(0, 38);
    m_tableWagons->setColumnWidth(1, 86);
    m_tableWagons->setColumnWidth(2, 54);
    m_tableWagons->setColumnWidth(3, 150);
    m_tableWagons->setColumnWidth(4, 54);
    grpWagonsLayout->addWidget(m_tableWagons, 1);

    auto *editWagonRow = new QGridLayout();
    editWagonRow->addWidget(new QLabel(QStringLiteral("Waggon-Bauart:")), 0, 0);
    m_comboEditWagonType = new QComboBox(grpWagons);
    m_comboEditWagonType->addItem(QStringLiteral("Samms (6-Achser Schwerlast)"));
    m_comboEditWagonType->addItem(QStringLiteral("Rns-z (4-Achser Rungenwagen)"));
    m_comboEditWagonType->addItem(QStringLiteral("4-Achser (Shimmns Coilwagen)"));
    m_comboEditWagonType->addItem(QStringLiteral("6-Achser (Torpedo / Faals)"));
    editWagonRow->addWidget(m_comboEditWagonType, 0, 1);

    editWagonRow->addWidget(new QLabel(QStringLiteral("Beladung (Gut):")), 1, 0);
    m_comboEditWagonProduct = new QComboBox(grpWagons);
    for (int p = 0; p <= static_cast<int>(CargoProductType::EmptyWagon); ++p) {
        m_comboEditWagonProduct->addItem(RailMapWidget::productName(static_cast<CargoProductType>(p)));
    }
    editWagonRow->addWidget(m_comboEditWagonProduct, 1, 1);

    auto *btnApplyWagonType = new QPushButton(QStringLiteral("Waggon && Gut setzen"), grpWagons);
    btnApplyWagonType->setStyleSheet("background-color: #EF7D00; color: white;");
    auto *btnZoomSelectedWagon = new QPushButton(QStringLiteral("🔍 Auf Waggon zoomen"), grpWagons);
    btnZoomSelectedWagon->setStyleSheet("background-color: #0284C7; color: white;");
    editWagonRow->addWidget(btnApplyWagonType,    0, 2);
    editWagonRow->addWidget(btnZoomSelectedWagon, 1, 2);
    grpWagonsLayout->addLayout(editWagonRow);

    tabTrainsLayout->addWidget(grpWagons, 1);
    m_sideTabs->addTab(tabTrains, QStringLiteral("🚂 Züge && Waggons"));

    // -------------------------------------------------------------------------
    // TAB 2: GÜTER-OBJEKTE, QUELLEN/SENKEN & LOKOMOTIV-ENGPASS
    // -------------------------------------------------------------------------
    auto *tabGoods = new QWidget();
    auto *tabGoodsLayout = new QVBoxLayout(tabGoods);
    tabGoodsLayout->setContentsMargins(7, 7, 7, 7);
    tabGoodsLayout->setSpacing(6);

    // Gruppe 1: Lokomotiv-Pool & Engpass-Steuerung (Waggons ohne Lok lagern!)
    auto *grpLocoPool = new QGroupBox(
        QStringLiteral("1. Lokomotiv-Pool && Engpass-Steuerung (Waggon-Zwischenlagerung)"), tabGoods);
    auto *grpLocoLayout = new QGridLayout(grpLocoPool);

    grpLocoLayout->addWidget(new QLabel(QStringLiteral("Verfügbare Lokomotiven im Pool:")), 0, 0, 1, 2);
    m_spinLocoPool = new QSpinBox(grpLocoPool);
    m_spinLocoPool->setRange(1, 20);
    m_spinLocoPool->setValue(m_mapWidget->totalLocomotivesInPool());
    m_spinLocoPool->setSuffix(QStringLiteral(" Lokomotiven"));
    grpLocoLayout->addWidget(m_spinLocoPool, 0, 2, 1, 2);

    auto *btnCoupleLoco2 = new QPushButton(QStringLiteral("🚂 Freie Lok ankuppeln (Start)"), grpLocoPool);
    btnCoupleLoco2->setStyleSheet("background-color: #059669; color: white;");
    auto *btnUncoupleLoco2 = new QPushButton(QStringLiteral("🅿️ Lok abkuppeln (Waggons lagern)"), grpLocoPool);
    btnUncoupleLoco2->setStyleSheet("background-color: #7E22CE; color: white;");
    grpLocoLayout->addWidget(btnCoupleLoco2,   1, 0, 1, 2);
    grpLocoLayout->addWidget(btnUncoupleLoco2, 1, 2, 1, 2);

    m_lblLocoStatus = new QLabel(grpLocoPool);
    m_lblLocoStatus->setWordWrap(true);
    m_lblLocoStatus->setStyleSheet("color: #0F172A; font-weight: bold; font-size: 10px;");
    grpLocoLayout->addWidget(m_lblLocoStatus, 2, 0, 1, 4);

    tabGoodsLayout->addWidget(grpLocoPool);

    // Gruppe 2: Neues Güter-Objekt anlegen & auf Waggons verteilen
    auto *grpNewCargo = new QGroupBox(
        QStringLiteral("2. Neues Güter-Objekt erzeugen && auf Waggons verteilen"), tabGoods);
    auto *grpCargoLayout = new QGridLayout(grpNewCargo);

    grpCargoLayout->addWidget(new QLabel(QStringLiteral("Stahlprodukt (Gut):")), 0, 0);
    m_comboNewProduct = new QComboBox(grpNewCargo);
    for (int p = 0; p < static_cast<int>(CargoProductType::EmptyWagon); ++p) {
        m_comboNewProduct->addItem(RailMapWidget::productName(static_cast<CargoProductType>(p)));
    }
    grpCargoLayout->addWidget(m_comboNewProduct, 0, 1, 1, 3);

    grpCargoLayout->addWidget(new QLabel(QStringLiteral("Quelle (Herstellung):")), 1, 0);
    m_comboNewSource = new QComboBox(grpNewCargo);
    grpCargoLayout->addWidget(m_comboNewSource, 1, 1);

    grpCargoLayout->addWidget(new QLabel(QStringLiteral("Senke (Ziel/Kunde):")), 1, 2);
    m_comboNewSink = new QComboBox(grpNewCargo);
    grpCargoLayout->addWidget(m_comboNewSink, 1, 3);

    for (const SourceSinkNode &n : m_mapWidget->sourceSinkNodes()) {
        const QString itemTxt = QString("%1 — %2").arg(n.id, n.name);
        m_comboNewSource->addItem(itemTxt, n.id);
        m_comboNewSink->addItem(itemTxt, n.id);
    }

    grpCargoLayout->addWidget(new QLabel(QStringLiteral("Menge [Tonnen]:")), 2, 0);
    m_spinNewTons = new QSpinBox(grpNewCargo);
    m_spinNewTons->setRange(50, 1800);
    m_spinNewTons->setSingleStep(50);
    m_spinNewTons->setValue(360);
    m_spinNewTons->setSuffix(QStringLiteral(" t"));
    grpCargoLayout->addWidget(m_spinNewTons, 2, 1);

    grpCargoLayout->addWidget(new QLabel(QStringLiteral("Waggon-Typ:")), 2, 2);
    m_comboNewWagonCat = new QComboBox(grpNewCargo);
    m_comboNewWagonCat->addItem(QStringLiteral("Samms [6A] (bis 90 t)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Rns-z [4A] (bis 62 t)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Shimmns [4A Coil] (bis 56 t)"));
    m_comboNewWagonCat->addItem(QStringLiteral("Torpedo/Faals [6A] (bis 96 t)"));
    grpCargoLayout->addWidget(m_comboNewWagonCat, 2, 3);

    auto *btnCreateOrder = new QPushButton(
        QStringLiteral("📦 Güter-Objekt erzeugen && an Quelle auf Waggons aufnehmen"), grpNewCargo);
    btnCreateOrder->setStyleSheet("background-color: #EF7D00; color: white; padding: 5px;");
    grpCargoLayout->addWidget(btnCreateOrder, 3, 0, 1, 4);

    auto *btnLoadSourceNow = new QPushButton(
        QStringLiteral("⬆️ An Quelle aufnehmen (Beladen)"), grpNewCargo);
    btnLoadSourceNow->setStyleSheet("background-color: #15803D; color: white; padding: 5px;");
    auto *btnUnloadSinkNow = new QPushButton(
        QStringLiteral("⬇️ An Senke abladen (Entladen)"), grpNewCargo);
    btnUnloadSinkNow->setStyleSheet("background-color: #0369A1; color: white; padding: 5px;");
    grpCargoLayout->addWidget(btnLoadSourceNow, 4, 0, 1, 2);
    grpCargoLayout->addWidget(btnUnloadSinkNow, 4, 2, 1, 2);

    tabGoodsLayout->addWidget(grpNewCargo);

    // Gruppe 3: Tabelle aller Quellen (Güter-Aufnahme) & Senken (Güter-Abladung)
    auto *grpNodes = new QGroupBox(
        QStringLiteral("3. Quellen (⬆️ Güter-Aufnahme) && Senken (⬇️ Güter-Abladung) — Klick = Zoom"), tabGoods);
    auto *grpNodesLayout = new QVBoxLayout(grpNodes);

    m_tableNodes = new QTableWidget(0, 5, grpNodes);
    m_tableNodes->setHorizontalHeaderLabels({
        QStringLiteral("Knoten-ID"),
        QStringLiteral("Rolle (Aufn./Abl.)"),
        QStringLiteral("Anlage / Standort"),
        QStringLiteral("⬆️ Aufnahme (Quelle) / ⬇️ Abladung (Senke)"),
        QStringLiteral("⬆️ Aufg. / ⬇️ Abgel.")
    });
    m_tableNodes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableNodes->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableNodes->setAlternatingRowColors(true);
    m_tableNodes->verticalHeader()->setVisible(false);
    m_tableNodes->horizontalHeader()->setStretchLastSection(true);
    m_tableNodes->setColumnWidth(0, 82);
    m_tableNodes->setColumnWidth(1, 95);
    m_tableNodes->setColumnWidth(2, 130);
    m_tableNodes->setColumnWidth(3, 145);
    grpNodesLayout->addWidget(m_tableNodes, 1);

    tabGoodsLayout->addWidget(grpNodes, 1);

    // Gruppe 4: Tabelle aller verwalteten Güter-Objekte (Cargo Orders)
    auto *grpOrders = new QGroupBox(
        QStringLiteral("4. Güter-Objekte (Aufnahme an Quelle -> Transport -> Abladung an Senke)"), tabGoods);
    auto *grpOrdersLayout = new QVBoxLayout(grpOrders);

    m_tableCargoOrders = new QTableWidget(0, 6, grpOrders);
    m_tableCargoOrders->setHorizontalHeaderLabels({
        QStringLiteral("Gut-ID"),
        QStringLiteral("Stahlprodukt"),
        QStringLiteral("Menge"),
        QStringLiteral("⬆️ Quelle -> ⬇️ Senke"),
        QStringLiteral("Waggons"),
        QStringLiteral("Status (Aufnahme / Fahrt / Abladung)")
    });
    m_tableCargoOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableCargoOrders->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableCargoOrders->setAlternatingRowColors(true);
    m_tableCargoOrders->verticalHeader()->setVisible(false);
    m_tableCargoOrders->horizontalHeader()->setStretchLastSection(true);
    m_tableCargoOrders->setColumnWidth(0, 62);
    m_tableCargoOrders->setColumnWidth(1, 115);
    m_tableCargoOrders->setColumnWidth(2, 52);
    m_tableCargoOrders->setColumnWidth(3, 118);
    m_tableCargoOrders->setColumnWidth(4, 78);
    grpOrdersLayout->addWidget(m_tableCargoOrders, 1);

    tabGoodsLayout->addWidget(grpOrders, 1);
    m_sideTabs->addTab(tabGoods, QStringLiteral("📦 Güter, Quellen && Senken"));

    // -------------------------------------------------------------------------
    // TAB 3: KARTE, GOOGLE MAPS & STANDORTE
    // -------------------------------------------------------------------------
    auto *tabMap = new QWidget();
    auto *tabMapLayout = new QVBoxLayout(tabMap);
    tabMapLayout->setContentsMargins(8, 8, 8, 8);
    tabMapLayout->setSpacing(10);

    auto *grpSites = new QGroupBox(QStringLiteral("1. Standorte direkt anspringen (Zoom)"), tabMap);
    auto *grpSitesLayout = new QVBoxLayout(grpSites);

    auto *btnSiteVK = new QPushButton(
        QStringLiteral("🏭 Hauptwerk Völklingen (Saarstahl AG – LD-Stahlwerk, Walzwerk, Schmiede)"), grpSites);
    btnSiteVK->setStyleSheet("background-color: #EF7D00; color: white; text-align: left; padding: 8px;");
    auto *btnSiteDL = new QPushButton(
        QStringLiteral("🔥 Dillinger Hütte & ROGESA (Quellen: Roheisen, Brammen & Grobbleche)"), grpSites);
    btnSiteDL->setStyleSheet("background-color: #B91C1C; color: white; text-align: left; padding: 8px;");
    auto *btnSiteBB = new QPushButton(
        QStringLiteral("⚙️ Werk Burbach (Senke: Knüppel | Quelle: Aufgerollter Walzdraht / Coils)"), grpSites);
    btnSiteBB->setStyleSheet("background-color: #0284C7; color: white; text-align: left; padding: 8px;");
    auto *btnSiteNK = new QPushButton(
        QStringLiteral("🔩 Werk Neunkirchen (Senke: Vorblöcke | Quelle: Stabstahl & Edelbaustahl)"), grpSites);
    btnSiteNK->setStyleSheet("background-color: #059669; color: white; text-align: left; padding: 8px;");
    auto *btnSiteAll = new QPushButton(
        QStringLiteral("🗺️ Gesamtübersicht Saarland-Bahnkorridor (Alle 4 Standorte & Züge)"), grpSites);
    btnSiteAll->setStyleSheet("background-color: #334155; color: #FBD50E; text-align: left; padding: 8px;");

    grpSitesLayout->addWidget(btnSiteVK);
    grpSitesLayout->addWidget(btnSiteDL);
    grpSitesLayout->addWidget(btnSiteBB);
    grpSitesLayout->addWidget(btnSiteNK);
    grpSitesLayout->addWidget(btnSiteAll);
    tabMapLayout->addWidget(grpSites);

    auto *grpLayers = new QGroupBox(QStringLiteral("2. Google Maps && Vereinfachte Werksdarstellung"), tabMap);
    auto *grpLayersLayout = new QVBoxLayout(grpLayers);

    grpLayersLayout->addWidget(new QLabel(QStringLiteral("Kartenhintergrund-Modus:")));
    m_comboMapMode = new QComboBox(grpLayers);
    m_comboMapMode->addItem(QStringLiteral("Hybrid: Google Maps + Vereinfachtes Werk Völklingen (Empfohlen)"));
    m_comboMapMode->addItem(QStringLiteral("Nur Vereinfachte Werks- & Gleisdarstellung (Schematisch / CAD)"));
    m_comboMapMode->addItem(QStringLiteral("Google Maps Straßenkarte + Bahngleise"));
    m_comboMapMode->addItem(QStringLiteral("Google Maps Satellitenbild + Bahngleise"));
    grpLayersLayout->addWidget(m_comboMapMode);

    m_chkSimplifiedPlant = new QCheckBox(QStringLiteral("Vereinfachtes Werksgelände & Werkhallen anzeigen"), grpLayers);
    m_chkSimplifiedPlant->setChecked(true);
    m_chkSourcesSinks = new QCheckBox(QStringLiteral("Quellen, Senken & Lagerbestände [t] auf Karte anzeigen"), grpLayers);
    m_chkSourcesSinks->setChecked(true);
    m_chkRoadsRivers = new QCheckBox(QStringLiteral("Vereinfachte Straßen (A620, B51, Werksstr.) & Saar anzeigen"), grpLayers);
    m_chkRoadsRivers->setChecked(true);
    m_chkTrackLabels = new QCheckBox(QStringLiteral("Gleis-Bezeichnungen (VK-RING, KORRIDOR etc.) einblenden"), grpLayers);
    m_chkTrackLabels->setChecked(true);
    m_chkWagonLabels = new QCheckBox(QStringLiteral("Waggon-Positionsnummern & Güter (#1 Samms:Knüppel) einblenden"), grpLayers);
    m_chkWagonLabels->setChecked(true);
    m_chkExaggerateSize = new QCheckBox(QStringLiteral("2D-Züge & Waggons für Übersicht vergrößert darstellen"), grpLayers);
    m_chkExaggerateSize->setChecked(true);

    grpLayersLayout->addWidget(m_chkSimplifiedPlant);
    grpLayersLayout->addWidget(m_chkSourcesSinks);
    grpLayersLayout->addWidget(m_chkRoadsRivers);
    grpLayersLayout->addWidget(m_chkTrackLabels);
    grpLayersLayout->addWidget(m_chkWagonLabels);
    grpLayersLayout->addWidget(m_chkExaggerateSize);
    tabMapLayout->addWidget(grpLayers);

    auto *grpSimSpeed = new QGroupBox(QStringLiteral("3. Simulationsgeschwindigkeit"), tabMap);
    auto *grpSimLayout = new QHBoxLayout(grpSimSpeed);
    grpSimLayout->addWidget(new QLabel(QStringLiteral("Tempo:")));
    m_sliderSimSpeed = new QSlider(Qt::Horizontal, grpSimSpeed);
    m_sliderSimSpeed->setRange(2, 100);
    m_sliderSimSpeed->setValue(25);
    grpSimLayout->addWidget(m_sliderSimSpeed, 1);
    m_lblSimSpeedVal = new QLabel(QStringLiteral("2.5x"), grpSimSpeed);
    grpSimLayout->addWidget(m_lblSimSpeedVal);
    tabMapLayout->addWidget(grpSimSpeed);

    tabMapLayout->addStretch(1);
    m_sideTabs->addTab(tabMap, QStringLiteral("🗺️ Karte && Standorte"));

    // -------------------------------------------------------------------------
    // TAB 4: BAHN-API (DB API) & ZUFÄLLIGE GLEISPOSITIONIERUNG
    // -------------------------------------------------------------------------
    auto *tabApi = new QWidget();
    auto *tabApiLayout = new QVBoxLayout(tabApi);
    tabApiLayout->setContentsMargins(8, 8, 8, 8);
    tabApiLayout->setSpacing(10);

    auto *grpRandomTest = new QGroupBox(QStringLiteral("1. Test-Modus: Zufällige Positionierung auf den Gleisen"), tabApi);
    auto *grpRandLayout = new QVBoxLayout(grpRandomTest);
    auto *lblRandInfo = new QLabel(
        QStringLiteral("Platziert Züge und gelagerte Waggongruppen (Samms, Rns-z, 4-Achser, 6-Achser) zum Testen "
                       "sofort an zufälligen, gültigen Positionen auf den Bahngleisen:"), grpRandomTest);
    lblRandInfo->setWordWrap(true);
    grpRandLayout->addWidget(lblRandInfo);

    auto *btnRandAll = new QPushButton(
        QStringLiteral("🎲 Zufällige Positionen auf ALLEN Gleisen (Saarland)"), grpRandomTest);
    btnRandAll->setStyleSheet("background-color: #EF7D00; color: white; padding: 8px;");
    auto *btnRandVK = new QPushButton(
        QStringLiteral("🎲 Zufällige Positionen im Werk VÖLKLINGEN"), grpRandomTest);
    btnRandVK->setStyleSheet("background-color: #0284C7; color: white; padding: 8px;");
    grpRandLayout->addWidget(btnRandAll);
    grpRandLayout->addWidget(btnRandVK);
    tabApiLayout->addWidget(grpRandomTest);

    auto *grpDbApi = new QGroupBox(QStringLiteral("2. Bahn-API Schnittstelle (DB Cargo / SaarRail Telematik)"), tabApi);
    auto *grpApiLayout = new QGridLayout(grpDbApi);

    grpApiLayout->addWidget(new QLabel(QStringLiteral("API-Endpunkt:")), 0, 0);
    m_editApiUrl = new QLineEdit(
        QStringLiteral("https://apis.deutschebahn.com/db-api-marketplace/apis/cargo-wagon-tracking/v2/positions"),
        grpDbApi);
    grpApiLayout->addWidget(m_editApiUrl, 0, 1);

    grpApiLayout->addWidget(new QLabel(QStringLiteral("DB-Client-Id:")), 1, 0);
    m_editClientId = new QLineEdit(QStringLiteral("shs-logistics-taps-client"), grpDbApi);
    grpApiLayout->addWidget(m_editClientId, 1, 1);

    grpApiLayout->addWidget(new QLabel(QStringLiteral("DB-Api-Key:")), 2, 0);
    m_editApiKey = new QLineEdit(grpDbApi);
    m_editApiKey->setPlaceholderText(QStringLiteral("Optionaler API-Key (leer = integrierter DB-Telematik-Simulator)"));
    m_editApiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    grpApiLayout->addWidget(m_editApiKey, 2, 1);

    auto *btnQueryApi = new QPushButton(
        QStringLiteral("📡 Waggon-Positionen über Bahn-API abrufen"), grpDbApi);
    btnQueryApi->setStyleSheet("background-color: #0D9488; color: white; padding: 8px;");
    grpApiLayout->addWidget(btnQueryApi, 3, 0, 1, 2);

    m_chkAutoPollApi = new QCheckBox(
        QStringLiteral("🔄 Live-API-Synchronisation aktivieren (alle 5 Sek. Positionen abrufen)"), grpDbApi);
    grpApiLayout->addWidget(m_chkAutoPollApi, 4, 0, 1, 2);

    grpApiLayout->addWidget(new QLabel(QStringLiteral("Bahn-API Telematik-Protokoll (JSON inkl. Güter & Lagerstatus):")), 5, 0, 1, 2);
    m_txtApiLog = new QPlainTextEdit(grpDbApi);
    m_txtApiLog->setReadOnly(true);
    m_txtApiLog->setStyleSheet(
        "background-color: #0F172A; color: #38BDF8; font-family: Consolas, monospace; font-size: 10px;");
    m_txtApiLog->setPlainText(
        QStringLiteral("// Bereit für DB-API Waggon-Ortung oder Zufalls-Testpositionierung.\n"
                       "// Klicken Sie auf '📡 Waggon-Positionen jetzt über Bahn-API abrufen' oder '🎲 Zufällige Positionen'."));
    grpApiLayout->addWidget(m_txtApiLog, 6, 0, 1, 2);

    tabApiLayout->addWidget(grpDbApi, 1);
    m_sideTabs->addTab(tabApi, QStringLiteral("📡 Bahn-API && Test"));

    mainSplitter->addWidget(m_sideTabs);
    mainSplitter->addWidget(m_mapWidget);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({640, 1280});

    rootLayout->addWidget(mainSplitter, 1);

    // Initiale Standard-Auswahl für neuen Güterauftrag setzen
    onNewOrderProductChanged(0);

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
    connectSiteBtn(btnZoomAll, QStringLiteral("Gesamt"));

    connectSiteBtn(btnSiteVK, QStringLiteral("Voelklingen"));
    connectSiteBtn(btnSiteDL, QStringLiteral("Dillingen"));
    connectSiteBtn(btnSiteBB, QStringLiteral("Burbach"));
    connectSiteBtn(btnSiteNK, QStringLiteral("Neunkirchen"));
    connectSiteBtn(btnSiteAll, QStringLiteral("Gesamt"));

    connect(m_btnPlayPause,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onToggleSimClicked);
    connect(btnQuickAddSamms,   &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddSammsClicked);
    connect(btnQuickAddRnsz,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onAddRnszClicked);
    connect(btnQuickAddTorpedo, &QPushButton::clicked, this, &MainWindowSHSLogistics::onAdd6AxleClicked);
    connect(btnQuickRemove,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onRemoveWagonClicked);
    connect(btnQuickRandom,     &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    connect(btnQuickDbApi,      &QPushButton::clicked, this, &MainWindowSHSLogistics::onQueryDbApiClicked);

    // Tab 1
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
    connect(m_comboColorMode,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onColorModeChanged);
    connect(m_tableWagons,      &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onWagonTableClicked);
    connect(m_tableWagons,      &QTableWidget::cellDoubleClicked, this, &MainWindowSHSLogistics::onWagonTableDoubleClicked);
    connect(btnApplyWagonType,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onChangeSelectedWagonCategory);
    connect(btnZoomSelectedWagon, &QPushButton::clicked, this, [this]() {
        m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), m_mapWidget->selectedWagonIndex());
    });

    // Tab 2 (Güter, Quellen/Senken & Lok-Pool)
    connect(m_spinLocoPool,    QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindowSHSLogistics::onLocoPoolSpinChanged);
    connect(btnCoupleLoco2,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onCoupleLocoClicked);
    connect(btnUncoupleLoco2,  &QPushButton::clicked, this, &MainWindowSHSLogistics::onUncoupleLocoClicked);
    connect(m_comboNewProduct, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onNewOrderProductChanged);
    connect(btnCreateOrder,    &QPushButton::clicked, this, &MainWindowSHSLogistics::onCreateCargoOrderClicked);
    connect(btnLoadSourceNow,  &QPushButton::clicked, this, [this]() {
        m_mapWidget->loadTrainAtSourceNow(m_mapWidget->selectedTrainIndex());
    });
    connect(btnUnloadSinkNow,  &QPushButton::clicked, this, [this]() {
        m_mapWidget->unloadTrainAtSinkNow(m_mapWidget->selectedTrainIndex());
    });
    connect(m_tableNodes,      &QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onNodeTableClicked);
    connect(m_tableCargoOrders,&QTableWidget::cellClicked, this, &MainWindowSHSLogistics::onCargoOrderTableClicked);

    // Tab 3
    connect(m_comboMapMode,       QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindowSHSLogistics::onMapModeChanged);
    connect(m_chkSimplifiedPlant, &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowSimplifiedPlant);
    connect(m_chkSourcesSinks,    &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowSourcesAndSinks);
    connect(m_chkRoadsRivers,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowRoadsAndRivers);
    connect(m_chkTrackLabels,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowTrackLabels);
    connect(m_chkWagonLabels,     &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setShowWagonLabels);
    connect(m_chkExaggerateSize,  &QCheckBox::toggled, m_mapWidget, &RailMapWidget::setExaggerateWagonSize);
    connect(m_sliderSimSpeed,     &QSlider::valueChanged, this, &MainWindowSHSLogistics::onSpeedSliderChanged);

    // Tab 4
    connect(btnRandAll,       &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    connect(btnRandVK,        &QPushButton::clicked, this, &MainWindowSHSLogistics::onRandomizeVoelklingenClicked);
    connect(btnQueryApi,      &QPushButton::clicked, this, &MainWindowSHSLogistics::onQueryDbApiClicked);
    connect(m_chkAutoPollApi, &QCheckBox::toggled,   this, &MainWindowSHSLogistics::onAutoPollToggled);

    // MapWidget Signale
    connect(m_mapWidget, &RailMapWidget::selectionChanged,     this, &MainWindowSHSLogistics::onMapSelectionChanged);
    connect(m_mapWidget, &RailMapWidget::trainsUpdated,        this, &MainWindowSHSLogistics::onTrainsUpdated);
    connect(m_mapWidget, &RailMapWidget::cargoAndNodesUpdated, this, &MainWindowSHSLogistics::onCargoAndNodesUpdated);
    connect(m_mapWidget, &RailMapWidget::simulationStepped,    this, &MainWindowSHSLogistics::onSimStepped);
    connect(m_mapWidget, &RailMapWidget::statusMessage,        this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 8000);
    });
    connect(m_mapWidget, &RailMapWidget::apiLogMessage,        this, [this](const QString &jsonText) {
        m_txtApiLog->setPlainText(jsonText);
    });
}

void MainWindowSHSLogistics::setupMenuBar()
{
    QMenuBar *mb = menuBar();
    mb->clear();

    QMenu *menuFile = mb->addMenu(QStringLiteral("&Datei"));
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
    addSiteAct(QStringLiteral("🔥 Dillinger Hütte & ROGESA (Roheisen/Blech)"), QStringLiteral("Dillingen"), Qt::Key_F2);
    addSiteAct(QStringLiteral("⚙️ Werk Burbach (Drahtwerk)"), QStringLiteral("Burbach"), Qt::Key_F3);
    addSiteAct(QStringLiteral("🔩 Werk Neunkirchen (Walzwerk)"), QStringLiteral("Neunkirchen"), Qt::Key_F4);
    addSiteAct(QStringLiteral("🗺️ Gesamtübersicht Saarland-Korridor"), QStringLiteral("Gesamt"), Qt::Key_F5);

    QMenu *menuSim = mb->addMenu(QStringLiteral("&Zug-Simulation && Güter"));
    QAction *actPlayPause = menuSim->addAction(QStringLiteral("⏯ Simulation Start / Pause"));
    actPlayPause->setShortcut(Qt::Key_Space);
    connect(actPlayPause, &QAction::triggered, this, &MainWindowSHSLogistics::onToggleSimClicked);
    menuSim->addSeparator();
    QAction *actAddSamms = menuSim->addAction(QStringLiteral("➕ Zug verlängern: Samms (6-Achser) anhängen"));
    connect(actAddSamms, &QAction::triggered, this, &MainWindowSHSLogistics::onAddSammsClicked);
    QAction *actAddRnsz = menuSim->addAction(QStringLiteral("➕ Zug verlängern: Rns-z (4-Achser) anhängen"));
    connect(actAddRnsz, &QAction::triggered, this, &MainWindowSHSLogistics::onAddRnszClicked);
    QAction *actRemWagon = menuSim->addAction(QStringLiteral("➖ Zug verkürzen: Letzten Waggon abhängen"));
    connect(actRemWagon, &QAction::triggered, this, &MainWindowSHSLogistics::onRemoveWagonClicked);
    menuSim->addSeparator();
    QAction *actCouple = menuSim->addAction(QStringLiteral("🚂 Freie Lokomotive an ausgewählte Waggongruppe ankuppeln"));
    connect(actCouple, &QAction::triggered, this, &MainWindowSHSLogistics::onCoupleLocoClicked);
    QAction *actUncouple = menuSim->addAction(QStringLiteral("🅿️ Lokomotive abkuppeln (Waggons mit Gütern auf Gleis lagern)"));
    connect(actUncouple, &QAction::triggered, this, &MainWindowSHSLogistics::onUncoupleLocoClicked);
    menuSim->addSeparator();
    QAction *actLoadSource = menuSim->addAction(QStringLiteral("⬆️ Güter jetzt an Quelle aufnehmen (Zug beladen)"));
    connect(actLoadSource, &QAction::triggered, this, [this]() {
        m_mapWidget->loadTrainAtSourceNow(m_mapWidget->selectedTrainIndex());
    });
    QAction *actUnloadSink = menuSim->addAction(QStringLiteral("⬇️ Güter jetzt an Senke abladen (Zug entladen)"));
    connect(actUnloadSink, &QAction::triggered, this, [this]() {
        m_mapWidget->unloadTrainAtSinkNow(m_mapWidget->selectedTrainIndex());
    });

    QMenu *menuApi = mb->addMenu(QStringLiteral("&Bahn-API && Ortung"));
    QAction *actRandAll = menuApi->addAction(QStringLiteral("🎲 Zufällige Positionen auf allen Gleisen (Test)"));
    actRandAll->setShortcut(Qt::Key_F8);
    connect(actRandAll, &QAction::triggered, this, &MainWindowSHSLogistics::onRandomizeAllClicked);
    QAction *actRandVK = menuApi->addAction(QStringLiteral("🎲 Zufällige Positionen im Werk Völklingen"));
    connect(actRandVK, &QAction::triggered, this, &MainWindowSHSLogistics::onRandomizeVoelklingenClicked);
    menuApi->addSeparator();
    QAction *actDbApi = menuApi->addAction(QStringLiteral("📡 Positionen über Bahn-API (DB API) abrufen"));
    actDbApi->setShortcut(Qt::Key_F9);
    connect(actDbApi, &QAction::triggered, this, &MainWindowSHSLogistics::onQueryDbApiClicked);

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

    QAction *actAbout = menuHelp->addAction(QStringLiteral("ℹ️ Über SHS Logistics GmbH – Leitsystem"));
    connect(actAbout, &QAction::triggered, this, &MainWindowSHSLogistics::onShowAboutDialog);
}

void MainWindowSHSLogistics::refreshTrainSelectorAndControls()
{
    m_updatingUi = true;

    m_comboTrain->clear();
    const auto &trains = m_mapWidget->trains();
    for (const TrainConsist &tr : trains) {
        const QString prefix = tr.hasLocomotive ? QStringLiteral("🚂 ") : QStringLiteral("🅿️ ");
        m_comboTrain->addItem(QString("%1%2 — %3 (%4 Wg.)")
                                  .arg(prefix, tr.id, tr.name)
                                  .arg(tr.wagons.size()));
    }

    m_comboTrackAssign->clear();
    const auto &tracks = m_mapWidget->tracks();
    for (const RailTrack &tk : tracks) {
        m_comboTrackAssign->addItem(QString("%1 (%2)").arg(tk.id, tk.name), tk.id);
    }

    const int selTrain = std::clamp(m_mapWidget->selectedTrainIndex(), 0, static_cast<int>(trains.size()) - 1);
    m_comboTrain->setCurrentIndex(selTrain);

    if (selTrain >= 0 && selTrain < trains.size()) {
        const TrainConsist &tr = trains[selTrain];
        m_spinWagonCount->setValue(tr.wagons.size());
        const int tkIdx = m_comboTrackAssign->findData(tr.trackId);
        if (tkIdx >= 0) {
            m_comboTrackAssign->setCurrentIndex(tkIdx);
        }
    }

    m_spinLocoPool->setValue(m_mapWidget->totalLocomotivesInPool());
    m_updatingUi = false;
    updateLiveTelemetryLabels();
}

void MainWindowSHSLogistics::refreshWagonTable()
{
    m_updatingUi = true;
    const auto &trains = m_mapWidget->trains();
    const int selTrain = m_mapWidget->selectedTrainIndex();

    if (selTrain < 0 || selTrain >= trains.size()) {
        m_tableWagons->setRowCount(0);
        m_updatingUi = false;
        return;
    }

    const TrainConsist &tr = trains[selTrain];
    m_spinWagonCount->setValue(tr.wagons.size());
    m_tableWagons->setRowCount(tr.wagons.size());

    for (int i = 0; i < tr.wagons.size(); ++i) {
        const WagonData &w = tr.wagons[i];
        const QColor badgeCol = RailMapWidget::colorForWagon(w.category, m_mapWidget->wagonColorMode());

        auto *itemPos   = new QTableWidgetItem(QString("#%1").arg(w.positionIndex));
        auto *itemType  = new QTableWidgetItem(RailMapWidget::shortCodeForWagon(w.category));
        auto *itemAxles = new QTableWidgetItem(QString("%1-Achser").arg(w.axleCount));
        auto *itemCargo = new QTableWidgetItem(RailMapWidget::productName(w.productType));
        auto *itemTons  = new QTableWidgetItem(QString("%1 t").arg(w.weightTons, 0, 'f', 0));
        auto *itemGps   = new QTableWidgetItem(QString("%1, %2 (%3)")
                                                   .arg(w.currentLat, 0, 'f', 4)
                                                   .arg(w.currentLon, 0, 'f', 4)
                                                   .arg(w.uicNumber));

        itemPos->setTextAlignment(Qt::AlignCenter);
        itemAxles->setTextAlignment(Qt::AlignCenter);
        itemTons->setTextAlignment(Qt::AlignCenter);

        itemType->setBackground(badgeCol);
        itemType->setForeground(Qt::white);
        QFont bf = itemType->font();
        bf.setBold(true);
        itemType->setFont(bf);

        itemAxles->setBackground(w.axleCount == 6 ? QColor(234, 88, 12, 45) : QColor(2, 132, 199, 45));
        itemAxles->setFont(bf);

        m_tableWagons->setItem(i, 0, itemPos);
        m_tableWagons->setItem(i, 1, itemType);
        m_tableWagons->setItem(i, 2, itemAxles);
        m_tableWagons->setItem(i, 3, itemCargo);
        m_tableWagons->setItem(i, 4, itemTons);
        m_tableWagons->setItem(i, 5, itemGps);
    }

    const int selWagon = m_mapWidget->selectedWagonIndex();
    if (selWagon >= 0 && selWagon < tr.wagons.size()) {
        m_tableWagons->selectRow(selWagon);
        m_comboEditWagonType->setCurrentIndex(static_cast<int>(tr.wagons[selWagon].category));
        m_comboEditWagonProduct->setCurrentIndex(static_cast<int>(tr.wagons[selWagon].productType));
    }

    m_updatingUi = false;
    updateLiveTelemetryLabels();
}

void MainWindowSHSLogistics::refreshSourcesSinksAndCargoTables()
{
    m_updatingUi = true;

    // 1. Quellen (Güter-Aufnahme) & Senken (Güter-Abladung) Tabelle
    const auto &nodes = m_mapWidget->sourceSinkNodes();
    m_tableNodes->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const SourceSinkNode &n = nodes[i];
        auto *itId    = new QTableWidgetItem(n.id);
        auto *itRole  = new QTableWidgetItem(n.role);
        auto *itName  = new QTableWidgetItem(n.name);
        auto *itGoods = new QTableWidgetItem(QString("⬆️ %1 | ⬇️ %2").arg(n.producedGoods, n.consumedGoods));
        auto *itStock = new QTableWidgetItem(QString("⬆️%1t / ⬇️%2t (%3t)")
                                                 .arg(n.totalLoadedTons, 0, 'f', 0)
                                                 .arg(n.totalUnloadedTons, 0, 'f', 0)
                                                 .arg(n.currentStockTons, 0, 'f', 0));

        QFont bf = itRole->font();
        bf.setBold(true);
        itRole->setFont(bf);
        itRole->setBackground(n.badgeColor);
        itRole->setForeground(Qt::white);

        m_tableNodes->setItem(i, 0, itId);
        m_tableNodes->setItem(i, 1, itRole);
        m_tableNodes->setItem(i, 2, itName);
        m_tableNodes->setItem(i, 3, itGoods);
        m_tableNodes->setItem(i, 4, itStock);
    }

    // 2. Güter-Objekte (Cargo Orders) Tabelle
    const auto &orders = m_mapWidget->cargoOrders();
    m_tableCargoOrders->setRowCount(orders.size());
    for (int i = 0; i < orders.size(); ++i) {
        const CargoOrder &o = orders[i];
        auto *itId    = new QTableWidgetItem(o.orderId);
        auto *itProd  = new QTableWidgetItem(RailMapWidget::productShortName(o.product));
        auto *itTons  = new QTableWidgetItem(QString("%1 t").arg(o.weightTons, 0, 'f', 0));
        auto *itRoute = new QTableWidgetItem(QString("⬆️%1 -> ⬇️%2").arg(o.sourceNodeId, o.sinkNodeId));
        auto *itWag   = new QTableWidgetItem(QString("%1x %2")
                                                 .arg(o.wagonCountUsed)
                                                 .arg(RailMapWidget::shortCodeForWagon(o.preferredWagon)));

        QString statusText;
        QColor statusColor;
        switch (o.status) {
        case CargoOrderStatus::LoadingAtSource:
            statusText = QStringLiteral("⬆️ AUFNAHME AN QUELLE (%1)").arg(o.sourceNodeId);
            statusColor = QColor("#15803D");
            break;
        case CargoOrderStatus::StoredOnParkedWagons:
            statusText = QStringLiteral("🅿️ GELAGERT OHNE LOK (%1)").arg(o.assignedConsistId);
            statusColor = QColor("#9333EA");
            break;
        case CargoOrderStatus::InTransit:
            statusText = QStringLiteral("🚛 UNTERWEGS -> SENKE (%1)").arg(o.sinkNodeId);
            statusColor = QColor("#059669");
            break;
        case CargoOrderStatus::UnloadingAtSink:
            statusText = QStringLiteral("⬇️ ABLADUNG AN SENKE (%1)").arg(o.sinkNodeId);
            statusColor = QColor("#0284C7");
            break;
        case CargoOrderStatus::DeliveredToSink:
            statusText = QStringLiteral("✅ AN SENKE ABGELADEN (%1)").arg(o.sinkNodeId);
            statusColor = QColor("#0D9488");
            break;
        case CargoOrderStatus::AtSourceStorage:
        default:
            statusText = QStringLiteral("📦 Bereit an Quelle");
            statusColor = QColor("#475569");
            break;
        }
        auto *itStat = new QTableWidgetItem(statusText);
        itStat->setBackground(statusColor);
        itStat->setForeground(Qt::white);
        QFont bf = itStat->font();
        bf.setBold(true);
        itStat->setFont(bf);

        m_tableCargoOrders->setItem(i, 0, itId);
        m_tableCargoOrders->setItem(i, 1, itProd);
        m_tableCargoOrders->setItem(i, 2, itTons);
        m_tableCargoOrders->setItem(i, 3, itRoute);
        m_tableCargoOrders->setItem(i, 4, itWag);
        m_tableCargoOrders->setItem(i, 5, itStat);
    }

    m_updatingUi = false;
    updateLiveTelemetryLabels();
}

void MainWindowSHSLogistics::updateLiveTelemetryLabels()
{
    const auto &trains = m_mapWidget->trains();
    int totalWagons = 0;
    int totalSamms = 0, totalRnsz = 0, total4A = 0, total6A = 0;
    int parkedGroups = 0;

    for (const TrainConsist &tr : trains) {
        if (!tr.hasLocomotive) {
            ++parkedGroups;
        }
        totalWagons += tr.wagons.size();
        for (const WagonData &w : tr.wagons) {
            if (w.category == WagonCategory::Samms_6Axle) ++totalSamms;
            if (w.category == WagonCategory::Rnsz_4Axle)  ++totalRnsz;
            if (w.axleCount == 4) ++total4A;
            if (w.axleCount == 6) ++total6A;
        }
    }

    m_lblHeaderStats->setText(
        QString("Loks: %1/%2  |  Gelagerte Gruppen ohne Lok: %3  |  Waggons: %4 (Samms [6A]: %5 | Rns-z [4A]: %6)")
            .arg(m_mapWidget->usedLocomotivesCount())
            .arg(m_mapWidget->totalLocomotivesInPool())
            .arg(parkedGroups)
            .arg(totalWagons)
            .arg(totalSamms)
            .arg(totalRnsz));

    m_lblLocoStatus->setText(
        QString("Lokomotiven im Einsatz: %1 von %2  |  Freie Loks: %3  |  Auf Gleisen zwischengelagerte Waggongruppen ohne Lok: %4")
            .arg(m_mapWidget->usedLocomotivesCount())
            .arg(m_mapWidget->totalLocomotivesInPool())
            .arg(m_mapWidget->freeLocomotivesCount())
            .arg(parkedGroups));

    const int selTrain = m_mapWidget->selectedTrainIndex();
    if (selTrain >= 0 && selTrain < trains.size()) {
        const TrainConsist &tr = trains[selTrain];
        double totalTons = 0.0;
        for (const WagonData &w : tr.wagons) {
            totalTons += w.weightTons;
        }
        m_lblTrainSummary->setText(
            QString("%1  |  ⬆️ Quelle: %2 -> ⬇️ Senke: %3  |  Ladung: %4 t (%5 Zyklen)")
                .arg(RailMapWidget::operationStateText(tr))
                .arg(tr.sourceNodeId, tr.sinkNodeId)
                .arg(totalTons, 0, 'f', 0)
                .arg(tr.completedDeliveries));
    }
}

void MainWindowSHSLogistics::onTrainComboChanged(int index)
{
    if (m_updatingUi || index < 0) return;
    m_mapWidget->selectTrain(index, 0, true);
    refreshTrainSelectorAndControls();
    refreshWagonTable();
}

void MainWindowSHSLogistics::onWagonTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (m_updatingUi) return;
    m_mapWidget->selectTrain(m_mapWidget->selectedTrainIndex(), row, false);
    const auto &trains = m_mapWidget->trains();
    const int tIdx = m_mapWidget->selectedTrainIndex();
    if (tIdx >= 0 && tIdx < trains.size() && row >= 0 && row < trains[tIdx].wagons.size()) {
        m_updatingUi = true;
        m_comboEditWagonType->setCurrentIndex(static_cast<int>(trains[tIdx].wagons[row].category));
        m_comboEditWagonProduct->setCurrentIndex(static_cast<int>(trains[tIdx].wagons[row].productType));
        m_updatingUi = false;
    }
}

void MainWindowSHSLogistics::onWagonTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    m_mapWidget->zoomToWagon(m_mapWidget->selectedTrainIndex(), row);
}

void MainWindowSHSLogistics::onMapSelectionChanged(int trainIdx, int wagonIdx)
{
    Q_UNUSED(trainIdx);
    Q_UNUSED(wagonIdx);
    refreshTrainSelectorAndControls();
    refreshWagonTable();
}

void MainWindowSHSLogistics::onTrainsUpdated()
{
    refreshTrainSelectorAndControls();
    refreshWagonTable();
    refreshSourcesSinksAndCargoTables();
}

void MainWindowSHSLogistics::onCargoAndNodesUpdated()
{
    refreshSourcesSinksAndCargoTables();
}

void MainWindowSHSLogistics::onSimStepped()
{
    ++m_stepCounter;
    if (m_stepCounter % 6 == 0) {
        updateLiveTelemetryLabels();
    }
    if (m_stepCounter % 18 != 0) return;
    const auto &trains = m_mapWidget->trains();
    const int selTrain = m_mapWidget->selectedTrainIndex();
    if (selTrain < 0 || selTrain >= trains.size()) return;
    const TrainConsist &tr = trains[selTrain];
    if (m_tableWagons->rowCount() != tr.wagons.size()) {
        refreshWagonTable();
        return;
    }
    for (int i = 0; i < tr.wagons.size(); ++i) {
        if (QTableWidgetItem *it = m_tableWagons->item(i, 5)) {
            it->setText(QString("%1, %2 (%3)")
                            .arg(tr.wagons[i].currentLat, 0, 'f', 4)
                            .arg(tr.wagons[i].currentLon, 0, 'f', 4)
                            .arg(tr.wagons[i].uicNumber));
        }
    }
}

void MainWindowSHSLogistics::onAddSammsClicked()
{
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Samms_6Axle, CargoProductType::SteelBillets);
}

void MainWindowSHSLogistics::onAddRnszClicked()
{
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Rnsz_4Axle, CargoProductType::HeavyPlates);
}

void MainWindowSHSLogistics::onAdd4AxleClicked()
{
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Standard_4Axle, CargoProductType::WireRodCoils);
}

void MainWindowSHSLogistics::onAdd6AxleClicked()
{
    m_mapWidget->appendWagonToTrain(m_mapWidget->selectedTrainIndex(), WagonCategory::Heavy_6Axle, CargoProductType::LiquidPigIron);
}

void MainWindowSHSLogistics::onRemoveWagonClicked()
{
    m_mapWidget->removeLastWagonFromTrain(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onWagonCountSpinChanged(int count)
{
    if (m_updatingUi) return;
    const auto chosenCat = static_cast<WagonCategory>(m_comboEditWagonType->currentIndex());
    m_mapWidget->setTrainWagonCount(m_mapWidget->selectedTrainIndex(), count, chosenCat);
}

void MainWindowSHSLogistics::onChangeSelectedWagonCategory()
{
    const int tIdx = m_mapWidget->selectedTrainIndex();
    int wIdx = m_mapWidget->selectedWagonIndex();
    if (wIdx < 0) wIdx = 0;
    const auto cat  = static_cast<WagonCategory>(m_comboEditWagonType->currentIndex());
    const auto prod = static_cast<CargoProductType>(m_comboEditWagonProduct->currentIndex());
    m_mapWidget->setWagonCategoryAt(tIdx, wIdx, cat);
    m_mapWidget->setWagonProductAt(tIdx, wIdx, prod);
}

void MainWindowSHSLogistics::onLocoPoolSpinChanged(int totalLocos)
{
    if (m_updatingUi) return;
    m_mapWidget->setTotalLocomotivesInPool(totalLocos);
}

void MainWindowSHSLogistics::onCoupleLocoClicked()
{
    m_mapWidget->coupleFreeLocoToConsist(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onUncoupleLocoClicked()
{
    m_mapWidget->uncoupleLocoFromConsist(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onNewOrderProductChanged(int index)
{
    if (index < 0) return;
    const auto prod = static_cast<CargoProductType>(index);
    const WagonCategory recWagon = RailMapWidget::recommendedWagonForProduct(prod);
    m_comboNewWagonCat->setCurrentIndex(static_cast<int>(recWagon));

    const QString defSrc = RailMapWidget::defaultSourceForProduct(prod);
    const QString defDst = RailMapWidget::defaultSinkForProduct(prod);
    const int srcIdx = m_comboNewSource->findData(defSrc);
    const int dstIdx = m_comboNewSink->findData(defDst);
    if (srcIdx >= 0) m_comboNewSource->setCurrentIndex(srcIdx);
    if (dstIdx >= 0) m_comboNewSink->setCurrentIndex(dstIdx);
}

void MainWindowSHSLogistics::onCreateCargoOrderClicked()
{
    const auto prod = static_cast<CargoProductType>(m_comboNewProduct->currentIndex());
    const double tons = m_spinNewTons->value();
    const QString srcId = m_comboNewSource->currentData().toString();
    const QString dstId = m_comboNewSink->currentData().toString();
    const auto wCat = static_cast<WagonCategory>(m_comboNewWagonCat->currentIndex());
    m_mapWidget->createAndDistributeCargoOrder(prod, tons, srcId, dstId, wCat);
}

void MainWindowSHSLogistics::onNodeTableClicked(int row, int column)
{
    Q_UNUSED(column);
    m_mapWidget->zoomToSourceSinkNode(row);
}

void MainWindowSHSLogistics::onCargoOrderTableClicked(int row, int column)
{
    Q_UNUSED(column);
    const auto &orders = m_mapWidget->cargoOrders();
    if (row < 0 || row >= orders.size()) return;
    const QString consistId = orders[row].assignedConsistId;
    const auto &trains = m_mapWidget->trains();
    for (int i = 0; i < trains.size(); ++i) {
        if (trains[i].id == consistId) {
            m_mapWidget->selectTrain(i, 0, true);
            break;
        }
    }
}

void MainWindowSHSLogistics::onToggleSimClicked()
{
    const bool nextState = !m_mapWidget->isSimulationRunning();
    m_mapWidget->setSimulationRunning(nextState);
    m_btnPlayPause->setText(nextState ? QStringLiteral("⏸ Simulation Pause")
                                      : QStringLiteral("▶ Simulation Start"));
}

void MainWindowSHSLogistics::onReverseTrainClicked()
{
    m_mapWidget->reverseTrainDirection(m_mapWidget->selectedTrainIndex());
}

void MainWindowSHSLogistics::onTrackAssignChanged(int index)
{
    if (m_updatingUi || index < 0) return;
    const QString trackId = m_comboTrackAssign->itemData(index).toString();
    m_mapWidget->assignTrainToTrack(m_mapWidget->selectedTrainIndex(), trackId);
}

void MainWindowSHSLogistics::onSpeedSliderChanged(int val)
{
    const double factor = val / 10.0;
    m_lblSimSpeedVal->setText(QString("%1x").arg(factor, 0, 'f', 1));
    m_mapWidget->setSimulationSpeedMultiplier(factor);
}

void MainWindowSHSLogistics::onColorModeChanged(int index)
{
    m_mapWidget->setWagonColorMode(static_cast<WagonColorMode>(index));
    refreshWagonTable();
}

void MainWindowSHSLogistics::onMapModeChanged(int index)
{
    m_mapWidget->setMapBackgroundMode(static_cast<MapBackgroundMode>(index));
}

void MainWindowSHSLogistics::onRandomizeAllClicked()
{
    m_mapWidget->randomizePositionsOnTracks(false);
}

void MainWindowSHSLogistics::onRandomizeVoelklingenClicked()
{
    m_mapWidget->randomizePositionsOnTracks(true);
    m_mapWidget->focusSite(QStringLiteral("Voelklingen"), true);
}

void MainWindowSHSLogistics::onQueryDbApiClicked()
{
    m_mapWidget->triggerLiveNetworkApiQuery(
        m_editApiUrl->text(),
        m_editClientId->text(),
        m_editApiKey->text());
}

void MainWindowSHSLogistics::onAutoPollToggled(bool checked)
{
    if (checked) {
        m_apiPollTimer->start();
        statusBar()->showMessage(QStringLiteral("Automatische Bahn-API Synchronisation aktiviert (Intervall: 5 s)."));
    } else {
        m_apiPollTimer->stop();
        statusBar()->showMessage(QStringLiteral("Automatische Bahn-API Synchronisation gestoppt."));
    }
}

void MainWindowSHSLogistics::onPrintMap()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted) {
        QPainter p(&printer);
        const QPixmap pm = m_mapWidget->grab();
        const QRect vp = p.viewport();
        QSize sz = pm.size();
        sz.scale(vp.size(), Qt::KeepAspectRatio);
        p.setViewport(vp.x(), vp.y(), sz.width(), sz.height());
        p.setWindow(pm.rect());
        p.drawPixmap(0, 0, pm);
    }
}

void MainWindowSHSLogistics::onShowAboutDialog()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(QStringLiteral("Über SHS Logistics GmbH – Werksbahn- & Güter-Disposition"));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setIconPixmap(QPixmap(QStringLiteral(":/SHSIcon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    aboutBox.setText(QStringLiteral(
        "<h2 style='color:#3B464E; margin-bottom:2px;'>SHS LOGISTICS GmbH</h2>"
        "<h4 style='color:#EF7D00; margin-top:0px;'>Werksbahn-, Waggon-, Güter- &amp; Bahn-API-Leitsystem</h4>"
        "<p style='color:#5C839C; font-style:italic;'>Partner [schafft Zukunft] Stahl</p>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<table style='font-size:11px;' cellpadding='2'>"
        "<tr><td><b>Version:</b></td><td><b>" SHS_APP_VERSION "</b></td></tr>"
        "<tr><td><b>Wissenschaftliche Leitung &amp; Entwicklung:</b></td><td><b>Frank U. Rückert</b></td></tr>"
        "<tr><td><b>Kontakt / E-Mail:</b></td><td><a href='mailto:frank.rueckert@htwsaar.de' style='color:#0284C7; font-weight:bold;'>frank.rueckert@htwsaar.de</a></td></tr>"
        "<tr><td><b>Institution:</b></td><td>htw saar – Hochschule für Technik und Wirtschaft des Saarlandes</td></tr>"
        "<tr><td><b>Kooperationspartner:</b></td><td>SHS Logistics GmbH (Stahl-Holding-Saar), Werkstraße 1, 66763 Dillingen/Saar</td></tr>"
        "<tr><td><b>Webseite:</b></td><td><a href='https://www.shs-logistics.de' style='color:#EF7D00;'>www.shs-logistics.de</a></td></tr>"
        "<tr><td><b>Software-Plattform:</b></td><td>C++17 / Qt 6.9.0 (MinGW 64-Bit, CMake + Ninja)</td></tr>"
        "</table>"
        "<hr style='border:0; height:1px; background:#CBD5E1;'>"
        "<p><b>Integrierte Kernfunktionen:</b></p>"
        "<ul style='font-size:10.5px;'>"
        "<li><b>Standorte (F1–F5):</b> Saarstahl Hauptwerk Völklingen, Dillinger Hütte &amp; ROGESA (1420 °C Flüssigroheisen), Werk Burbach (Drahtwerk), Werk Neunkirchen (Walzwerk) &amp; Gesamtkorridor.</li>"
        "<li><b>Waggon-Disposition:</b> Samms [6A] (Orange), Rns-z [4A] (Stahlblau), 4-Achser Shimmns [4A] (Grün), Torpedopfannenwagen [6A] (Karmesinrot mit glühendem 1420 °C Kern).</li>"
        "<li><b>4-Phasen-Logistikzyklus:</b> Quellen (⬆️ Güter-Aufnahme / Beladen) und Senken (⬇️ Güter-Abladung / Entladen) mit 2D-Verladebrücken und animiertem Ladefortschritt.</li>"
        "<li><b>Lokomotiv-Pool &amp; Lagerung:</b> Automatisches Abstellen beladener Waggongruppen bei Lokmangel (🅿️ GELAGERT OHNE LOK).</li>"
        "<li><b>Telematik (F9):</b> DB Cargo / SAVVY Schnittstelle mit DS100-Betriebsstellen (SVK, SDL, SBU, SNK), VzG-Streckenkilometern &amp; GPS-Track-Snapping.</li>"
        "<li><b>Handbücher (F10/F11):</b> Technisches Handbuch (F10) und Kurzanleitung (F11) direkt als PDF im Release enthalten.</li>"
        "</ul>"));
    aboutBox.setStandardButtons(QMessageBox::Ok);
    aboutBox.exec();
}

QPixmap MainWindowSHSLogistics::createCorporateSplashPixmap(int progressPercent, const QString &statusText)
{
    const int W = 860;
    const int H = 510;
    QPixmap pm(W, H);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // 1. Äußerer Rahmen im SHS Corporate Design (Stahl-Anthrazit #3B464E / #48555F)
    QPainterPath outerPath;
    outerPath.addRoundedRect(QRectF(2.0, 2.0, W - 4.0, H - 4.0), 10.0, 10.0);
    QLinearGradient bgGrad(0, 0, 0, H);
    bgGrad.setColorAt(0.0, QColor("#48555F"));
    bgGrad.setColorAt(0.55, QColor("#3B464E"));
    bgGrad.setColorAt(1.0, QColor("#293238"));
    p.fillPath(outerPath, bgGrad);
    p.setPen(QPen(QColor("#EF7D00"), 2.5));
    p.drawPath(outerPath);

    // 2. Oberer SHS Header-Streifen (Weiß -> Hellblau #E4EEF2 -> Stahl-Anthrazit)
    QRectF hdrRect(14.0, 14.0, W - 28.0, 74.0);
    QPainterPath hdrPath;
    hdrPath.addRoundedRect(hdrRect, 7.0, 7.0);
    QLinearGradient hdrGrad(hdrRect.left(), 0, hdrRect.right(), 0);
    hdrGrad.setColorAt(0.0, QColor("#FFFFFF"));
    hdrGrad.setColorAt(0.32, QColor("#E4EEF2"));
    hdrGrad.setColorAt(0.62, QColor("#48555F"));
    hdrGrad.setColorAt(1.0, QColor("#333E46"));
    p.fillPath(hdrPath, hdrGrad);
    p.setPen(QPen(QColor("#5C839C"), 1.5));
    p.drawPath(hdrPath);

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
               QStringLiteral("Partner [schafft Zukunft] Stahl — Werksbahn-, Waggon- & Güter-Leitsystem"));

    p.setPen(QColor("#0284C7"));
    p.setFont(QFont("Segoe UI", 8.5, QFont::Bold));
    p.drawText(QRectF(100.0, 60.0, 500.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Wissenschaftliche Leitung & Entwicklung: Frank U. Rückert (frank.rueckert@htwsaar.de)"));

    // Rechts im Header: Versions-Badge
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
               QStringLiteral("Völklingen | Dillingen | Burbach | NK"));

    // 3. Hauptmotiv-Bereich mit startseite_img_de.jpg (links) + Standort-/Modul-Übersicht (rechts)
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
        p.drawPixmap(innerImg.topLeft().toPoint(), scaledHero);
        p.restore();
    }

    // Rechte Infokarte im Corporate Stahlblau/Anthrazit
    QRectF infoCard(576.0, 98.0, W - 592.0, 308.0);
    p.setBrush(QColor("#2F3940"));
    p.setPen(QPen(QColor("#5C839C"), 1.5));
    p.drawRoundedRect(infoCard, 7.0, 7.0);

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 10, QFont::Bold));
    p.drawText(QRectF(infoCard.left() + 12.0, infoCard.top() + 10.0, infoCard.width() - 24.0, 22.0),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("INTEGRIERTE WERKE & MODULE"));

    p.setPen(QPen(QColor("#EF7D00"), 1.5));
    p.drawLine(QPointF(infoCard.left() + 12.0, infoCard.top() + 34.0),
               QPointF(infoCard.right() - 12.0, infoCard.top() + 34.0));

    const QStringList features = {
        QStringLiteral("🏭 Werk Völklingen (Saarstahl AG)\n    LD-Stahlwerk, Strangguss, Walzwerk"),
        QStringLiteral("🔥 Dillinger Hütte & ROGESA\n    Hochöfen (1420 °C), Brammen, Bleche"),
        QStringLiteral("⚙️ Werk Burbach & 🔩 Neunkirchen\n    Walzdraht-Coils & Edelbaustahl"),
        QStringLiteral("🚂 2D-Waggons & Torpedopfannen\n    Samms [6A], Rns-z [4A], Torpedo [6A]"),
        QStringLiteral("⬆️ Quellen (Aufnahme) & ⬇️ Senken\n    Vollautomatischer Be-/Entladezyklus"),
        QStringLiteral("📡 DB Cargo / SAVVY Telematik\n    Ril-100/DS100, VzG-km & GPS-Snapping")
    };

    double fy = infoCard.top() + 42.0;
    for (const QString &feat : features) {
        QRectF pill(infoCard.left() + 10.0, fy, infoCard.width() - 20.0, 39.0);
        p.setBrush(QColor("#3B464E"));
        p.setPen(QPen(QColor("#56636D"), 1.0));
        p.drawRoundedRect(pill, 4.0, 4.0);
        p.setPen(QColor("#F8FAFC"));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(pill.adjusted(7.0, 2.0, -5.0, -2.0), Qt::AlignLeft | Qt::AlignVCenter, feat);
        fy += 43.0;
    }

    // 4. Unterer Ladebalken & Statusbereich
    QRectF footerRect(16.0, 414.0, W - 32.0, 82.0);
    p.setBrush(QColor("#232B30"));
    p.setPen(QPen(QColor("#56636D"), 1.2));
    p.drawRoundedRect(footerRect, 7.0, 7.0);

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.drawText(QRectF(30.0, 420.0, W - 140.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter, statusText);

    const int clampedPct = std::clamp(progressPercent, 0, 100);
    p.setPen(QColor("#FFFFFF"));
    p.drawText(QRectF(W - 110.0, 420.0, 80.0, 20.0), Qt::AlignRight | Qt::AlignVCenter,
               QString("%1 %").arg(clampedPct));

    QRectF barBg(30.0, 444.0, W - 60.0, 16.0);
    p.setBrush(QColor("#0F172A"));
    p.setPen(QPen(QColor("#475569"), 1.0));
    p.drawRoundedRect(barBg, 5.0, 5.0);

    if (clampedPct > 0) {
        const double fillW = std::max(12.0, (barBg.width() - 2.0) * (clampedPct / 100.0));
        QRectF barFill(barBg.left() + 1.0, barBg.top() + 1.0, fillW, barBg.height() - 2.0);
        QLinearGradient barGrad(barFill.left(), 0, barFill.right(), 0);
        barGrad.setColorAt(0.0, QColor("#EF7D00"));
        barGrad.setColorAt(0.7, QColor("#F59E0B"));
        barGrad.setColorAt(1.0, QColor("#FBD50E"));
        p.setBrush(barGrad);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(barFill, 4.0, 4.0);
    }

    p.setPen(QColor("#FBD50E"));
    p.setFont(QFont("Segoe UI", 8, QFont::Bold));
    p.drawText(QRectF(30.0, 466.0, 560.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Leitung & Entwicklung: Frank U. Rückert • Kontakt: frank.rueckert@htwsaar.de • htw saar"));

    p.setPen(QColor("#94A3B8"));
    p.setFont(QFont("Segoe UI", 7.5));
    p.drawText(QRectF(W - 270.0, 466.0, 240.0, 18.0), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("SHS Logistics GmbH • www.shs-logistics.de"));

    return pm;
}

void MainWindowSHSLogistics::openPdfManualFile(const QString &pdfFileName, const QString &fallbackTitle)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {
        appDir,
        appDir + QStringLiteral("/Manuals"),
        QDir(appDir).filePath(QStringLiteral("..")),
        QDir(appDir).filePath(QStringLiteral("../..")),
        QDir(appDir).filePath(QStringLiteral("../../..")),
        QDir(appDir).filePath(QStringLiteral("../../../Release_Standalone"))
    };

    for (const QString &dirPath : searchDirs) {
        const QString candidate = QDir(dirPath).absoluteFilePath(pdfFileName);
        if (QFileInfo::exists(candidate)) {
            statusBar()->showMessage(QStringLiteral("Öffne PDF-Manual: %1").arg(candidate), 6000);
            QDesktopServices::openUrl(QUrl::fromLocalFile(candidate));
            return;
        }
    }

    // Fallback: Falls das PDF im aktuellen Ordner noch nicht existiert, direkt mit QPrinter erzeugen!
    const QString generatedPdf = QDir(appDir).absoluteFilePath(pdfFileName);
    QPrinter pdfPrinter(QPrinter::HighResolution);
    pdfPrinter.setOutputFormat(QPrinter::PdfFormat);
    pdfPrinter.setOutputFileName(generatedPdf);

    QTextDocument doc;
    doc.setHtml(QStringLiteral(
        "<h1>%1</h1>"
        "<p><b>SHS Logistics GmbH – Partner [schafft Zukunft] Stahl</b></p>"
        "<p>Werksbahn-, Waggon-, Güter-, Quellen/Senken- &amp; Bahn-API-Leitsystem (Qt 6.9 / C++17).</p>"
        "<ul>"
        "<li><b>Standorte (F1–F5):</b> Werk Völklingen, Dillinger Hütte &amp; ROGESA, Werk Burbach, Werk Neunkirchen, Gesamtübersicht.</li>"
        "<li><b>Waggons:</b> Samms [6A] (Orange), Rns-z [4A] (Stahlblau), 4-Achser Shimmns (Grün), Torpedowagen [6A] (Rot, 1420 °C Flüssigroheisen).</li>"
        "<li><b>Quellen &amp; Senken:</b> Aktive Güter-Aufnahme (⬆️ Beladen) und Güter-Abladung (⬇️ Entladen) inkl. Lokomotiv-Pool.</li>"
        "<li><b>Bahn-API (F9) &amp; Zufalls-Test (F8):</b> Ril-100/DS100-Betriebsstellen, VzG-Streckenkilometer &amp; GPS-Track-Snapping.</li>"
        "</ul>").arg(fallbackTitle));
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
    dlg.setWindowTitle(QStringLiteral("SHS Logistics GmbH – Start-Splashscreen (Corporate Design)"));
    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *lbl = new QLabel(&dlg);
    lbl->setPixmap(createCorporateSplashPixmap(
        100, QStringLiteral("SHS Logistics Leitsystem aktiv — Alle Werke, Züge, Quellen & Senken geladen.")));
    lay->addWidget(lbl);
    dlg.setFixedSize(860, 510);
    dlg.exec();
}

void MainWindowSHSLogistics::scheduleAutoScreenshots(const QString &outDir)
{
    createCorporateSplashPixmap(
        100, QStringLiteral("Verbinde DB Cargo / SAVVY Telematik-Schnittstelle – Startbereit!"))
        .save(outDir + QStringLiteral("/preview_splash.png"));

    QTimer::singleShot(2500, this, [this, outDir]() {
        this->grab().save(outDir + QStringLiteral("/preview_voelklingen.png"));
        m_sideTabs->setCurrentIndex(1); // Tab 2: Güter, Quellen & Senken
        QTimer::singleShot(800, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/preview_goods_tab.png"));
            m_mapWidget->focusSite(QStringLiteral("Gesamt"), false);
            QTimer::singleShot(1000, this, [this, outDir]() {
                this->grab().save(outDir + QStringLiteral("/preview_corridor.png"));
                m_mapWidget->focusSite(QStringLiteral("Dillingen"), false);
                QTimer::singleShot(1000, this, [this, outDir]() {
                    this->grab().save(outDir + QStringLiteral("/preview_dillingen.png"));
                    m_sideTabs->setCurrentIndex(3); // Tab 4: Bahn-API & Test
                    onQueryDbApiClicked();
                    QTimer::singleShot(700, this, [this, outDir]() {
                        this->grab().save(outDir + QStringLiteral("/preview_api_tab.png"));
                        this->close();
                    });
                });
            });
        });
    });
}
