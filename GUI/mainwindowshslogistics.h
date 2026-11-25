#ifndef MAINWINDOWSHSLOGISTICS_H
#define MAINWINDOWSHSLOGISTICS_H

#include <QMainWindow>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QTabWidget>
#include <QStackedWidget>
#include <QFrame>
#include <QTimer>
#include <QVector>

#include "railmapwidget.h"
#include "link2railclient.h"
#include "providerloginwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindowSHSLogistics; }
QT_END_NAMESPACE

class MainWindowSHSLogistics : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindowSHSLogistics(QWidget *parent = nullptr);
    ~MainWindowSHSLogistics() override;

    void scheduleAutoScreenshots(const QString &outDir);
    static QPixmap createCorporateSplashPixmap(int progressPercent = 100,
                                               const QString &statusText = QStringLiteral("Starte SHS Logistics Leitsystem (Simulation & Live-GPS) ..."));

private slots:
    // Modus-Umschaltung: Simulation (Spiel) <-> Reale Live-GPS-Daten (6 Portale)
    void onSwitchToSimulationMode();
    void onSwitchToLiveGpsMode();
    void onOperatingModeChanged(AppOperatingMode mode);

    // =========================================================================
    // SIMULATIONSMODUS (SPIELMODUS) – Züge, Waggons, Güter, Quellen/Senken, Lok-Pool
    // =========================================================================
    void onTrainComboChanged(int index);
    void onTrackAssignChanged(int index);
    void onReverseTrainClicked();
    void onWagonCountSpinChanged(int value);
    void onAddSammsClicked();
    void onAddRnszClicked();
    void onAdd4AxleClicked();
    void onAdd6AxleClicked();
    void onRemoveWagonClicked();
    void onWagonTableClicked(int row, int column);
    void onWagonTableDoubleClicked(int row, int column);
    void onChangeSelectedWagonCategory();
    void onSimColorModeChanged(int index);
    void onToggleSimClicked();
    void onSpeedSliderChanged(int value);
    void onLocoPoolSpinChanged(int value);
    void onCoupleLocoClicked();
    void onUncoupleLocoClicked();
    void onNewOrderProductChanged(int index);
    void onCreateCargoOrderClicked();
    void onSimNodeTableClicked(int row, int column);
    void onCargoOrderTableClicked(int row, int column);
    void onRandomizeAllClicked();
    void onRandomizeVoelklingenClicked();
    void onQuerySimDbApiClicked();
    void onSimAutoPollToggled(bool checked);
    void onSimStepped();

    // =========================================================================
    // LIVE-GPS-MODUS – 6 Telematik-Dienste (link2rail, IMT, DOT, TRANSWAGGON, Innofreight, Wascosa)
    // =========================================================================
    void onOpenProviderTab(int providerIndex);
    void onFetchAllAuthenticatedClicked();
    void onLogoutAllClicked();
    void onExportAllJsonClicked();
    void onExportAllCsvClicked();
    void onQuickImportActiveProviderClicked();
    void onAnyProviderAuthChanged();
    void onAnyProviderDataUpdated();
    void onLiveFilterChanged();
    void onLiveWagonTableClicked(int row, int column);
    void onLiveWagonTableDoubleClicked(int row, int column);
    void onLiveColorModeChanged(int index);
    void onLiveNodeTableClicked(int row, int column);

    // Gemeinsame Karten-, Datei- & Hilfe-Slots
    void onMapSelectionChanged(int trainIdx, int wagonIdx);
    void onTrainsUpdated();
    void onCargoAndNodesUpdated();
    void onMapModeChanged(int index);
    void onPrintMap();
    void onOpenMainManualPdf();
    void onOpenQuickStartPdf();
    void onOpenManualsFolder();
    void onShowSplashPreviewDialog();
    void onShowAboutDialog();

private:
    struct FlatWagonRef {
        int trainIdx = -1;
        int wagonIdx = -1;
    };

    void setupCorporateTheme();
    void setupMenuBar();
    void buildMainLayout();
    void buildSimulationSidePanel();
    void buildLiveGpsSidePanel();
    void ensureSimulationModeForAction();

    // Simulation UI-Refresh
    void refreshSimTrainSelectorAndControls();
    void refreshSimWagonTable();
    void refreshSimSourcesSinksAndCargoTables();

    // Live-GPS UI-Refresh
    void rebuildUnifiedLiveData();
    QVector<Link2RailWagonRecord> collectUnifiedRecords() const;
    void refreshLiveWagonTable();
    void refreshSelectedWagonInspector();
    void refreshLiveNodesTable();

    void updateHeaderAndModeUi();
    void syncLayerControls(bool fromSim);
    void openPdfManualFile(const QString &pdfFileName, const QString &fallbackTitle);

    Ui::MainWindowSHSLogistics *ui = nullptr;
    RailMapWidget *m_mapWidget = nullptr;

    // Alle 6 unabhängigen Telematik-Clients & ihre eigenen Login-Reiter
    QVector<Link2RailClient*> m_clients;
    QVector<ProviderLoginWidget*> m_loginWidgets;
    QTabWidget *m_providerLoginTabs = nullptr;

    // Header, Modus-Umschalter & kontextabhängige Toolbar
    QLabel *m_lblHeaderStats = nullptr;
    QPushButton *m_btnModeSim = nullptr;
    QPushButton *m_btnModeLive = nullptr;
    QStackedWidget *m_quickActionStack = nullptr;
    QStackedWidget *m_sidePanelStack = nullptr;
    QTabWidget *m_simSideTabs = nullptr;
    QTabWidget *m_liveSideTabs = nullptr;

    // Toolbar-Buttons (Simulationsmodus & Live-GPS-Modus)
    QPushButton *m_btnPlayPause = nullptr;
    QPushButton *m_btnToolbarLogins = nullptr;
    QPushButton *m_btnToolbarSyncAll = nullptr;

    // ---------------- SIMULATIONSMODUS WIDGETS ----------------
    // Sim Tab 1: Züge & Waggons
    QComboBox *m_comboTrain = nullptr;
    QComboBox *m_comboTrackAssign = nullptr;
    QSpinBox *m_spinWagonCount = nullptr;
    QComboBox *m_comboSimColorMode = nullptr;
    QLabel *m_lblTrainMetrics = nullptr;
    QTableWidget *m_tableSimWagons = nullptr;
    QComboBox *m_comboEditWagonType = nullptr;
    QComboBox *m_comboEditWagonProduct = nullptr;

    // Sim Tab 2: Güter, Quellen & Senken + Lok-Pool
    QSpinBox *m_spinLocoPool = nullptr;
    QLabel *m_lblLocoPoolInfo = nullptr;
    QComboBox *m_comboNewProduct = nullptr;
    QSpinBox *m_spinNewTons = nullptr;
    QComboBox *m_comboNewSource = nullptr;
    QComboBox *m_comboNewSink = nullptr;
    QComboBox *m_comboNewWagonCat = nullptr;
    QTableWidget *m_tableSimNodes = nullptr;
    QTableWidget *m_tableCargoOrders = nullptr;

    // Sim Tab 3: Karte & Tempo
    QComboBox *m_comboSimMapMode = nullptr;
    QCheckBox *m_chkSimSimplifiedPlant = nullptr;
    QCheckBox *m_chkSimSourcesSinks = nullptr;
    QCheckBox *m_chkSimRoadsRivers = nullptr;
    QCheckBox *m_chkSimTrackLabels = nullptr;
    QCheckBox *m_chkSimWagonLabels = nullptr;
    QCheckBox *m_chkSimExaggerateSize = nullptr;
    QSlider *m_sliderSimSpeed = nullptr;
    QLabel *m_lblSpeedValue = nullptr;

    // Sim Tab 4: Zufalls-Test & Sim-Telematik
    QCheckBox *m_chkSimAutoPoll = nullptr;
    QPlainTextEdit *m_txtSimApiLog = nullptr;
    QTimer *m_simApiPollTimer = nullptr;

    // ---------------- LIVE-GPS-MODUS WIDGETS ----------------
    // Live Tab 1: Vereinigte Live-Waggons
    QFrame *m_bannerNotLoggedIn = nullptr;
    QComboBox *m_comboProviderFilter = nullptr;
    QLineEdit *m_editSearchFilter = nullptr;
    QComboBox *m_comboCategoryFilter = nullptr;
    QComboBox *m_comboLiveColorMode = nullptr;
    QLabel *m_lblLiveSummary = nullptr;
    QTableWidget *m_tableLiveWagons = nullptr;
    QTableWidget *m_tableWagonDetails = nullptr;
    QVector<FlatWagonRef> m_filteredRows;

    // Live Tab 3: Werke & Geofences
    QTableWidget *m_tableLiveNodes = nullptr;

    // Live Tab 4: Karte & Layer (inkl. Sichtbarkeit je Telematik-Dienst)
    QComboBox *m_comboLiveMapMode = nullptr;
    QCheckBox *m_chkLiveSimplifiedPlant = nullptr;
    QCheckBox *m_chkLiveSourcesSinks = nullptr;
    QCheckBox *m_chkLiveRoadsRivers = nullptr;
    QCheckBox *m_chkLiveTrackLabels = nullptr;
    QCheckBox *m_chkLiveWagonLabels = nullptr;
    QCheckBox *m_chkLiveExaggerateSize = nullptr;
    QCheckBox *m_chkSnapToTracks = nullptr;

    bool m_updatingUi = false;
};

#endif // MAINWINDOWSHSLOGISTICS_H
