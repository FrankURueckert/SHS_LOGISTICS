#include "providerloginwidget.h"
#include "link2raillogindialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QJsonDocument>
#include <QSslSocket>
#include <algorithm>

ProviderLoginWidget::ProviderLoginWidget(Link2RailClient *client, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
{
    const TelematicsProviderInfo pi = m_client->info();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // 1. Dienst-Status-Karte + Web-Portal-Link
    auto *topRow = new QHBoxLayout();
    m_lblStatusCard = new QLabel(this);
    m_lblStatusCard->setWordWrap(true);
    topRow->addWidget(m_lblStatusCard, 1);

    auto *btnPortal = new QPushButton(QStringLiteral("🌐 Portal"), this);
    btnPortal->setToolTip(QStringLiteral("Öffnet %1 im Webbrowser").arg(pi.portalUrl));
    btnPortal->setStyleSheet(QString("background-color: %1; color: white; padding: 6px 10px;").arg(pi.badgeColor.darker(115).name()));
    topRow->addWidget(btnPortal);
    root->addLayout(topRow);

    // 2. Direkte Anmeldemaske für diesen Dienst
    QString safeDisplay = pi.displayName;
    safeDisplay.replace(QLatin1Char('&'), QStringLiteral("&&"));
    auto *grpLogin = new QGroupBox(QStringLiteral("Anmeldung: %1").arg(safeDisplay), this);
    auto *g = new QGridLayout(grpLogin);
    g->setVerticalSpacing(5);

    g->addWidget(new QLabel(QStringLiteral("Auth-Verfahren:")), 0, 0);
    m_comboAuthMode = new QComboBox(grpLogin);
    m_comboAuthMode->addItem(QStringLiteral("Portal-Login / JSON-POST (Benutzer + Passwort)"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Password Grant"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Client Credentials"));
    m_comboAuthMode->addItem(QStringLiteral("Direkter Bearer-Token / API-Key"));
    m_comboAuthMode->addItem(QStringLiteral("HTTP Basic Auth"));
    g->addWidget(m_comboAuthMode, 0, 1);

    g->addWidget(new QLabel(QStringLiteral("Benutzer / E-Mail:")), 1, 0);
    m_editUser = new QLineEdit(grpLogin);
    m_editUser->setPlaceholderText(QStringLiteral("Benutzername für %1").arg(pi.shortName));
    g->addWidget(m_editUser, 1, 1);

    g->addWidget(new QLabel(QStringLiteral("Passwort:")), 2, 0);
    m_editPass = new QLineEdit(grpLogin);
    m_editPass->setEchoMode(QLineEdit::Password);
    m_editPass->setPlaceholderText(QStringLiteral("Passwort für %1").arg(pi.shortName));
    g->addWidget(m_editPass, 2, 1);

    g->addWidget(new QLabel(QStringLiteral("Login-URL (POST):")), 3, 0);
    m_editLoginUrl = new QLineEdit(grpLogin);
    g->addWidget(m_editLoginUrl, 3, 1);

    g->addWidget(new QLabel(QStringLiteral("GPS-API-URL (GET):")), 4, 0);
    m_editGpsUrl = new QLineEdit(grpLogin);
    g->addWidget(m_editGpsUrl, 4, 1);

    auto *btnRow = new QHBoxLayout();
    m_btnLogin = new QPushButton(QStringLiteral("🔐 Anmelden && Laden"), grpLogin);
    m_btnLogin->setStyleSheet(QString("background-color: %1; color: white; padding: 6px;").arg(pi.badgeColor.name()));
    m_btnLogout = new QPushButton(QStringLiteral("🚪 Abmelden"), grpLogin);
    m_btnLogout->setStyleSheet("background-color: #B91C1C; color: white; padding: 6px;");
    auto *btnAdv = new QPushButton(QStringLiteral("⚙️ Erweitert ..."), grpLogin);
    btnAdv->setStyleSheet("background-color: #48555F; color: #FBD50E; padding: 6px;");
    btnRow->addWidget(m_btnLogin);
    btnRow->addWidget(m_btnLogout);
    btnRow->addWidget(btnAdv);
    g->addLayout(btnRow, 5, 0, 1, 2);
    root->addWidget(grpLogin);

    // 3. Synchronisation, Datei-Import & Rohdaten
    auto *grpSync = new QGroupBox(QStringLiteral("Synchronisation && Datei-Import (%1)").arg(pi.shortName), this);
    auto *gs = new QGridLayout(grpSync);
    gs->setVerticalSpacing(5);

    m_btnFetch = new QPushButton(QStringLiteral("🔄 %1 GPS-Daten jetzt abrufen").arg(pi.shortName), grpSync);
    m_btnFetch->setStyleSheet("background-color: #0284C7; color: white; padding: 6px;");
    gs->addWidget(m_btnFetch, 0, 0, 1, 2);

    m_chkAutoPoll = new QCheckBox(QStringLiteral("Auto-Sync alle:"), grpSync);
    m_spinPollSec = new QSpinBox(grpSync);
    m_spinPollSec->setRange(10, 3600);
    m_spinPollSec->setValue(60);
    m_spinPollSec->setSuffix(QStringLiteral(" Sek."));
    gs->addWidget(m_chkAutoPoll, 1, 0);
    gs->addWidget(m_spinPollSec, 1, 1);

    auto *ioRow = new QHBoxLayout();
    auto *btnImport = new QPushButton(QStringLiteral("📂 JSON / XML / CSV laden"), grpSync);
    auto *btnRaw = new QPushButton(QStringLiteral("🧾 Letzte Roh-Antwort"), grpSync);
    ioRow->addWidget(btnImport);
    ioRow->addWidget(btnRaw);
    gs->addLayout(ioRow, 2, 0, 1, 2);
    root->addWidget(grpSync);

    // 4. Dienst-Protokoll
    auto *grpLog = new QGroupBox(QStringLiteral("Verbindungs- && API-Protokoll (%1)").arg(pi.shortName), this);
    auto *logLay = new QVBoxLayout(grpLog);
    m_txtLog = new QPlainTextEdit(grpLog);
    m_txtLog->setReadOnly(true);
    m_txtLog->setStyleSheet(
        "background-color: #0F172A; color: #38BDF8; font-family: Consolas, monospace; font-size: 9.5px;");
    m_txtLog->setPlainText(
        QStringLiteral("// %1 bereit (TLS/HTTPS: %2).\n"
                       "// Portal: %3\n"
                       "// Zugangsdaten eingeben und '🔐 Anmelden & Laden' klicken (oder '⚙️ Erweitert ...' für API-Key / OAuth2 / Vorlagen).")
            .arg(pi.displayName,
                 QSslSocket::supportsSsl() ? QStringLiteral("VERFÜGBAR") : QStringLiteral("NICHT VERFÜGBAR"),
                 pi.portalUrl));
    logLay->addWidget(m_txtLog);
    root->addWidget(grpLog, 1);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(std::max(10, m_client->config().pollIntervalSeconds) * 1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ProviderLoginWidget::onFetchClicked);

    // Verbindungen
    connect(btnPortal,        &QPushButton::clicked, this, &ProviderLoginWidget::onOpenPortalClicked);
    connect(m_btnLogin,       &QPushButton::clicked, this, &ProviderLoginWidget::onLoginClicked);
    connect(m_btnLogout,      &QPushButton::clicked, this, &ProviderLoginWidget::onLogoutClicked);
    connect(btnAdv,           &QPushButton::clicked, this, &ProviderLoginWidget::openAdvancedDialog);
    connect(m_btnFetch,       &QPushButton::clicked, this, &ProviderLoginWidget::onFetchClicked);
    connect(m_comboAuthMode,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ProviderLoginWidget::onAuthModeChanged);
    connect(m_chkAutoPoll,    &QCheckBox::toggled,   this, &ProviderLoginWidget::onAutoPollToggled);
    connect(m_spinPollSec,    QOverload<int>::of(&QSpinBox::valueChanged), this, &ProviderLoginWidget::onPollIntervalChanged);
    connect(btnImport,        &QPushButton::clicked, this, &ProviderLoginWidget::onImportFileClicked);
    connect(btnRaw,           &QPushButton::clicked, this, &ProviderLoginWidget::onShowRawClicked);

    connect(m_client, &Link2RailClient::protocolLogAppended, this, [this](const QString &line) {
        m_txtLog->appendPlainText(line);
    });
    connect(m_client, &Link2RailClient::authenticationChanged, this, [this](bool auth, const QString &msg) {
        Q_UNUSED(auth);
        refreshStatusCard();
        emit statusNotification(msg);
    });
    connect(m_client, &Link2RailClient::loginSucceeded, this, [this](const QString &, const QString &details) {
        if (m_client->config().autoPollEnabled) {
            m_updatingUi = true;
            m_chkAutoPoll->setChecked(true);
            m_updatingUi = false;
            m_pollTimer->setInterval(std::max(10, m_client->config().pollIntervalSeconds) * 1000);
            m_pollTimer->start();
        }
        refreshStatusCard();
        emit statusNotification(details);
    });
    connect(m_client, &Link2RailClient::loginFailed, this, [this](const QString &err) {
        refreshStatusCard();
        emit statusNotification(err);
        QMessageBox::warning(
            this,
            QStringLiteral("%1 – Anmeldung fehlgeschlagen").arg(m_client->info().shortName),
            err + QStringLiteral("\n\nTipp: Über '⚙️ Erweitert ...' lassen sich Vorlagen, Auth-Verfahren (JSON, OAuth2, Basic Auth, API-Key), "
                                 "Login-/API-Endpunkte und Zusatz-Header exakt an deinen Account anpassen."));
    });
    connect(m_client, &Link2RailClient::liveDataUpdated, this, [this](const QVector<Link2RailWagonRecord> &, const QString &summary) {
        refreshStatusCard();
        emit statusNotification(summary);
    });
    connect(m_client, &Link2RailClient::apiError, this, [this](const QString &err) {
        emit statusNotification(err);
    });

    syncUiFromConfig();
    refreshStatusCard();
}

void ProviderLoginWidget::syncUiFromConfig()
{
    m_updatingUi = true;
    const Link2RailConfig &cfg = m_client->config();
    m_comboAuthMode->setCurrentIndex(static_cast<int>(cfg.authMode));
    m_editUser->setText(cfg.username);
    m_editPass->setText(cfg.password);
    m_editLoginUrl->setText(cfg.loginUrl);
    m_editGpsUrl->setText(cfg.gpsTelemetryUrl);
    m_spinPollSec->setValue(std::clamp(cfg.pollIntervalSeconds, 10, 3600));
    m_chkAutoPoll->setChecked(cfg.autoPollEnabled && m_client->isAuthenticated());
    onAuthModeChanged(static_cast<int>(cfg.authMode));
    m_updatingUi = false;
}

void ProviderLoginWidget::refreshStatusCard()
{
    const TelematicsProviderInfo pi = m_client->info();
    const bool auth = m_client->isAuthenticated();
    const int total = m_client->latestRecords().size();
    int gps = 0;
    for (const auto &r : m_client->latestRecords()) {
        if (r.hasValidGps) gps++;
    }
    const QString syncStr = m_client->lastSyncTime().isValid()
                                ? m_client->lastSyncTime().toString(QStringLiteral("HH:mm:ss"))
                                : QStringLiteral("—");

    if (auth) {
        m_lblStatusCard->setText(
            QString("<b>🟢 %1 verbunden</b> (%2) | Waggons: <b>%3</b> (GPS: %4) | Sync: %5")
                .arg(pi.shortName, m_client->authenticatedUser()).arg(total).arg(gps).arg(syncStr));
        m_lblStatusCard->setStyleSheet(
            "background-color: #064E3B; color: #A7F3D0; border: 1.5px solid #10B981; "
            "border-radius: 5px; padding: 6px; font-size: 10px;");
    } else {
        m_lblStatusCard->setText(
            QString("<b>🔴 %1: Nicht angemeldet</b> | Geladene Waggons: %2 (GPS: %3)")
                .arg(pi.shortName).arg(total).arg(gps));
        m_lblStatusCard->setStyleSheet(QString(
            "background-color: #1E293B; color: #FDE047; border: 1.5px solid %1; "
            "border-radius: 5px; padding: 6px; font-size: 10px;").arg(pi.badgeColor.name()));
    }
    m_btnLogout->setEnabled(auth || total > 0);
}

void ProviderLoginWidget::onAuthModeChanged(int index)
{
    const auto mode = static_cast<Link2RailAuthMode>(index);
    const bool needUserPass = (mode == Link2RailAuthMode::PortalLoginCidP ||
                               mode == Link2RailAuthMode::OAuth2PasswordGrant ||
                               mode == Link2RailAuthMode::HttpBasicAuth);
    const bool needLoginUrl = (mode != Link2RailAuthMode::DirectApiKeyOrToken &&
                               mode != Link2RailAuthMode::HttpBasicAuth);
    m_editUser->setEnabled(needUserPass || mode == Link2RailAuthMode::DirectApiKeyOrToken);
    m_editPass->setEnabled(needUserPass);
    m_editLoginUrl->setEnabled(needLoginUrl);
}

void ProviderLoginWidget::onLoginClicked()
{
    Link2RailConfig cfg = m_client->config();
    cfg.authMode = static_cast<Link2RailAuthMode>(m_comboAuthMode->currentIndex());
    cfg.username = m_editUser->text().trimmed();
    cfg.password = m_editPass->text();
    cfg.loginUrl = m_editLoginUrl->text().trimmed();
    cfg.gpsTelemetryUrl = m_editGpsUrl->text().trimmed();
    cfg.pollIntervalSeconds = m_spinPollSec->value();

    if (cfg.authMode == Link2RailAuthMode::DirectApiKeyOrToken &&
        cfg.manualBearerToken.trimmed().isEmpty() && cfg.apiKey.trimmed().isEmpty()) {
        openAdvancedDialog();
        return;
    }
    m_client->login(cfg);
}

void ProviderLoginWidget::onLogoutClicked()
{
    m_pollTimer->stop();
    m_updatingUi = true;
    m_chkAutoPoll->setChecked(false);
    m_updatingUi = false;
    m_client->logout();
    refreshStatusCard();
}

void ProviderLoginWidget::onFetchClicked()
{
    if (!m_client->isAuthenticated()) {
        onLoginClicked();
        return;
    }
    m_client->fetchAllLiveData();
}

void ProviderLoginWidget::openAdvancedDialog()
{
    Link2RailConfig cfg = m_client->config();
    cfg.authMode = static_cast<Link2RailAuthMode>(m_comboAuthMode->currentIndex());
    if (!m_editUser->text().trimmed().isEmpty()) cfg.username = m_editUser->text().trimmed();
    if (!m_editPass->text().isEmpty()) cfg.password = m_editPass->text();
    if (!m_editLoginUrl->text().trimmed().isEmpty()) cfg.loginUrl = m_editLoginUrl->text().trimmed();
    if (!m_editGpsUrl->text().trimmed().isEmpty()) cfg.gpsTelemetryUrl = m_editGpsUrl->text().trimmed();
    cfg.pollIntervalSeconds = m_spinPollSec->value();

    Link2RailLoginDialog dlg(cfg, this);
    if (dlg.exec() == QDialog::Accepted) {
        const Link2RailConfig newCfg = dlg.resultConfig();
        m_client->setConfig(newCfg);
        syncUiFromConfig();
        m_pollTimer->setInterval(std::max(10, newCfg.pollIntervalSeconds) * 1000);
        m_client->login(newCfg);
    }
}

void ProviderLoginWidget::onAutoPollToggled(bool checked)
{
    if (m_updatingUi) return;
    if (checked) {
        if (!m_client->isAuthenticated()) {
            m_updatingUi = true;
            m_chkAutoPoll->setChecked(false);
            m_updatingUi = false;
            emit statusNotification(QStringLiteral("[%1] Bitte zuerst anmelden, bevor Auto-Sync aktiviert wird.").arg(m_client->info().shortName));
            return;
        }
        m_pollTimer->setInterval(std::max(10, m_spinPollSec->value()) * 1000);
        m_pollTimer->start();
        emit statusNotification(QStringLiteral("[%1] Auto-Sync aktiv (alle %2 Sek.).").arg(m_client->info().shortName).arg(m_spinPollSec->value()));
    } else {
        m_pollTimer->stop();
        emit statusNotification(QStringLiteral("[%1] Auto-Sync gestoppt.").arg(m_client->info().shortName));
    }
}

void ProviderLoginWidget::onPollIntervalChanged(int seconds)
{
    if (m_updatingUi) return;
    Link2RailConfig cfg = m_client->config();
    cfg.pollIntervalSeconds = seconds;
    m_client->setConfig(cfg);
    m_pollTimer->setInterval(std::max(10, seconds) * 1000);
}

void ProviderLoginWidget::onImportFileClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("%1 Export-Datei laden (JSON / GeoJSON / XML / CSV)").arg(m_client->info().shortName),
        QString(),
        QStringLiteral("Telematik-Daten (*.json *.geojson *.xml *.csv *.txt);;Alle Dateien (*.*)"));
    if (path.isEmpty()) return;

    QString err;
    if (!m_client->importFromFile(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Import fehlgeschlagen"), err);
    } else {
        refreshStatusCard();
        emit importSucceededFitRequested();
    }
}

void ProviderLoginWidget::onShowRawClicked()
{
    const QString raw = m_client->lastRawResponseJson();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Letzte Roh-Antwort – %1").arg(m_client->info().displayName));
    dlg.resize(780, 540);
    auto *lay = new QVBoxLayout(&dlg);
    auto *ed = new QPlainTextEdit(&dlg);
    ed->setReadOnly(true);
    ed->setStyleSheet("background-color:#0F172A; color:#38BDF8; font-family:Consolas,monospace; font-size:10.5px;");
    if (raw.isEmpty()) {
        ed->setPlainText(QStringLiteral("// Noch keine API-Antwort für %1 empfangen.").arg(m_client->info().shortName));
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        ed->setPlainText(doc.isNull() ? raw : QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    }
    lay->addWidget(ed);
    dlg.exec();
}

void ProviderLoginWidget::onOpenPortalClicked()
{
    QDesktopServices::openUrl(QUrl(m_client->info().portalUrl));
}

