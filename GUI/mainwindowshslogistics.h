#ifndef MAINWINDOWSHSLOGISTICS_H
#define MAINWINDOWSHSLOGISTICS_H

#include <QMainWindow>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>

#include "railmapwidget.h"

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
                                               const QString &statusText = QStringLiteral("Starte SHS Logistics Leitsystem ..."));

private slots:
    void onTrainComboChanged(int index);
    void onWagonTableClicked(int row, int column);
    void onWagonTableDoubleClicked(int row, int column);
    void onMapSelectionChanged(int trainIdx, int wagonIdx);
    void onTrainsUpdated();
    void onCargoAndNodesUpdated();
    void onSimStepped();

    // Zuglänge & Waggons
    void onAddSammsClicked();
    void onAddRnszClicked();
    void onAdd4AxleClicked();
    void onAdd6AxleClicked();
    void onRemoveWagonClicked();
    void onWagonCountSpinChanged(int count);
    void onChangeSelectedWagonCategory();

    // Lokomotiv-Pool & Abstellen/Lagern
    void onLocoPoolSpinChanged(int totalLocos);
    void onCoupleLocoClicked();
    void onUncoupleLocoClicked();

    // Güter-Objekte & Quellen/Senken
    void onNewOrderProductChanged(int index);
    void onCreateCargoOrderClicked();
    void onNodeTableClicked(int row, int column);
    void onCargoOrderTableClicked(int row, int column);

    // Simulation & Karte
    void onToggleSimClicked();
    void onReverseTrainClicked();
    void onTrackAssignChanged(int index);
    void onSpeedSliderChanged(int val);
    void onColorModeChanged(int index);
    void onMapModeChanged(int index);

    // Bahn-API & Test-Positionierung & Handbücher (PDF)
    void onRandomizeAllClicked();
    void onRandomizeVoelklingenClicked();
    void onQueryDbApiClicked();
    void onAutoPollToggled(bool checked);
    void onPrintMap();
    void onOpenMainManualPdf();
    void onOpenQuickStartPdf();
    void onOpenManualsFolder();
    void onShowSplashPreviewDialog();
    void onShowAboutDialog();

private:
    void setupCorporateTheme();
    void setupMenuBar();
    void buildMainLayout();
    void refreshTrainSelectorAndControls();
    void refreshWagonTable();
    void refreshSourcesSinksAndCargoTables();
    void updateLiveTelemetryLabels();
    void openPdfManualFile(const QString &pdfFileName, const QString &fallbackTitle);

    Ui::MainWindowSHSLogistics *ui = nullptr;
    RailMapWidget *m_mapWidget = nullptr;
    QTabWidget *m_sideTabs = nullptr;

    // Top-Toolbar Buttons
    QPushButton *m_btnPlayPause = nullptr;
    QLabel *m_lblHeaderStats = nullptr;

    // Tab 1: Züge & Waggons
    QComboBox *m_comboTrain = nullptr;
    QComboBox *m_comboTrackAssign = nullptr;
    QSpinBox *m_spinWagonCount = nullptr;
    QComboBox *m_comboColorMode = nullptr;
    QComboBox *m_comboEditWagonType = nullptr;
    QComboBox *m_comboEditWagonProduct = nullptr;
    QTableWidget *m_tableWagons = nullptr;
    QLabel *m_lblTrainSummary = nullptr;

    // Tab 2: Güter-Objekte, Quellen/Senken & Lokomotiv-Pool
    QSpinBox *m_spinLocoPool = nullptr;
    QLabel *m_lblLocoStatus = nullptr;
    QComboBox *m_comboNewProduct = nullptr;
    QSpinBox *m_spinNewTons = nullptr;
    QComboBox *m_comboNewSource = nullptr;
    QComboBox *m_comboNewSink = nullptr;
    QComboBox *m_comboNewWagonCat = nullptr;
    QTableWidget *m_tableNodes = nullptr;
    QTableWidget *m_tableCargoOrders = nullptr;

    // Tab 3: Karte & Standorte
    QComboBox *m_comboMapMode = nullptr;
    QCheckBox *m_chkSimplifiedPlant = nullptr;
    QCheckBox *m_chkRoadsRivers = nullptr;
    QCheckBox *m_chkSourcesSinks = nullptr;
    QCheckBox *m_chkTrackLabels = nullptr;
    QCheckBox *m_chkWagonLabels = nullptr;
    QCheckBox *m_chkExaggerateSize = nullptr;
    QSlider *m_sliderSimSpeed = nullptr;
    QLabel *m_lblSimSpeedVal = nullptr;

    // Tab 4: Bahn-API (DB) & Test-Ortung
    QLineEdit *m_editApiUrl = nullptr;
    QLineEdit *m_editClientId = nullptr;
    QLineEdit *m_editApiKey = nullptr;
    QCheckBox *m_chkAutoPollApi = nullptr;
    QPlainTextEdit *m_txtApiLog = nullptr;
    QTimer *m_apiPollTimer = nullptr;

    bool m_updatingUi = false;
    int m_stepCounter = 0;
};

#endif // MAINWINDOWSHSLOGISTICS_H
