#ifndef MAINWINDOWSHSLOGISTICS_H
#define MAINWINDOWSHSLOGISTICS_H

#include <QMainWindow>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>
#include <QFrame>
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
                                               const QString &statusText = QStringLiteral("Starte SHS Logistics Multi-Telematik Leitsystem ..."));

private slots:
    // Multi-Provider Steuerung
    void onOpenProviderTab(int providerIndex);
    void onFetchAllAuthenticatedClicked();
    void onLogoutAllClicked();
    void onExportAllJsonClicked();
    void onExportAllCsvClicked();
    void onQuickImportActiveProviderClicked();

    // Aggregations-Slots von allen 6 Telematik-Clients
    void onAnyProviderAuthChanged();
    void onAnyProviderDataUpdated();

    // Tabellen, Filter & Karte
    void onLiveFilterChanged();
    void onLiveWagonTableClicked(int row, int column);
    void onLiveWagonTableDoubleClicked(int row, int column);
    void onMapSelectionChanged(int trainIdx, int wagonIdx);
    void onTrainsUpdated();
    void onNodesUpdated();
    void onNodeTableClicked(int row, int column);
    void onColorModeChanged(int index);
    void onMapModeChanged(int index);

    // Datei & Hilfe
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
    void rebuildUnifiedLiveData();
    QVector<Link2RailWagonRecord> collectUnifiedRecords() const;
    void refreshLiveWagonTable();
    void refreshSelectedWagonInspector();
    void refreshNodesTable();
    void updateHeaderAndConnectionUi();
    void openPdfManualFile(const QString &pdfFileName, const QString &fallbackTitle);

    Ui::MainWindowSHSLogistics *ui = nullptr;
    RailMapWidget *m_mapWidget = nullptr;
    QTabWidget *m_sideTabs = nullptr;

    // Alle 6 unabhängigen Telematik-Clients & ihre eigenen Login-Reiter
    QVector<Link2RailClient*> m_clients;
    QVector<ProviderLoginWidget*> m_loginWidgets;
    QTabWidget *m_providerLoginTabs = nullptr;

    // Header & Toolbar
    QLabel *m_lblHeaderStats = nullptr;
    QPushButton *m_btnToolbarLogins = nullptr;
    QPushButton *m_btnToolbarSyncAll = nullptr;

    // Tab 1: Alle Live-Waggons (vereinigte Ansicht aller 6 Dienste)
    QFrame *m_bannerNotLoggedIn = nullptr;
    QComboBox *m_comboProviderFilter = nullptr;
    QLineEdit *m_editSearchFilter = nullptr;
    QComboBox *m_comboCategoryFilter = nullptr;
    QComboBox *m_comboColorMode = nullptr;
    QLabel *m_lblLiveSummary = nullptr;
    QTableWidget *m_tableLiveWagons = nullptr;
    QTableWidget *m_tableWagonDetails = nullptr;
    QVector<FlatWagonRef> m_filteredRows;

    // Tab 3: Standorte & Geofences
    QTableWidget *m_tableNodes = nullptr;

    // Tab 4: Karte & Layer (inkl. Sichtbarkeit je Dienst)
    QComboBox *m_comboMapMode = nullptr;
    QCheckBox *m_chkSimplifiedPlant = nullptr;
    QCheckBox *m_chkSourcesSinks = nullptr;
    QCheckBox *m_chkRoadsRivers = nullptr;
    QCheckBox *m_chkTrackLabels = nullptr;
    QCheckBox *m_chkWagonLabels = nullptr;
    QCheckBox *m_chkExaggerateSize = nullptr;
    QCheckBox *m_chkSnapToTracks = nullptr;

    bool m_updatingUi = false;
};

#endif // MAINWINDOWSHSLOGISTICS_H
