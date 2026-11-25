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
    g->setVerticalSpacing(4);

    g->addWidget(new QLabel(QStringLiteral("Auth-Verfahren:")), 0, 0);
    m_comboAuthMode = new QComboBox(grpLogin);
    m_comboAuthMode->addItem(QStringLiteral("Portal-Login / JSON-POST (Benutzer + Passwort)"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Password Grant"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Client Credentials"));
    m_comboAuthMode->addItem(QStringLiteral("Direkter Token / Cookie / API-Key (Browser-Session)"));
    m_comboAuthMode->addItem(QStringLiteral("HTTP Basic Auth"));
    m_comboAuthMode->addItem(QStringLiteral("IMT Teno Login (userId + Account-Alias -> x-session-token)"));
    m_comboAuthMode->addItem(QStringLiteral("HTML-Formular-Login (z. B. TRANSWAGGON WOPS + Cookie)"));
    g->addWidget(m_comboAuthMode, 0, 1);

    g->addWidget(new QLabel(QStringLiteral("Benutzer / E-Mail:")), 1, 0);
    m_editUser = new QLineEdit(grpLogin);
    m_editUser->setPlaceholderText(QStringLiteral("Benutzername / userId für %1").arg(pi.shortName));
    g->addWidget(m_editUser, 1, 1);

    g->addWidget(new QLabel(QStringLiteral("Passwort:")), 2, 0);
    m_editPass = new QLineEdit(grpLogin);
    m_editPass->setEchoMode(QLineEdit::Password);
    m_editPass->setPlaceholderText(QStringLiteral("Passwort für %1").arg(pi.shortName));
    g->addWidget(m_editPass, 2, 1);

    m_lblClientId = new QLabel(QStringLiteral("Client-ID / Alias:"), grpLogin);
    g->addWidget(m_lblClientId, 3, 0);
    m_editClientId = new QLineEdit(grpLogin);
    m_editClientId->setPlaceholderText(QStringLiteral("Optional: IMT Account-Alias / DB-Client-Id / OAuth Client-ID"));
    g->addWidget(m_editClientId, 3, 1);

    g->addWidget(new QLabel(QStringLiteral("Login-URL (POST):")), 4, 0);
    m_editLoginUrl = new QLineEdit(grpLogin);
    g->addWidget(m_editLoginUrl, 4, 1);

    g->addWidget(new QLabel(QStringLiteral("GPS-API-URL (GET):")), 5, 0);
    m_editGpsUrl = new QLineEdit(grpLogin);
    g->addWidget(m_editGpsUrl, 5, 1);

    auto *btnRow1 = new QHBoxLayout();
    m_btnLogin = new QPushButton(QStringLiteral("🔐 Anmelden && Laden"), grpLogin);
    m_btnLogin->setStyleSheet(QString("background-color: %1; color: white; padding: 6px;").arg(pi.badgeColor.name()));
    m_btnLogout = new QPushButton(QStringLiteral("🚪 Abmelden"), grpLogin);
    m_btnLogout->setStyleSheet("background-color: #B91C1C; color: white; padding: 6px;");
    auto *btnProbe = new QPushButton(QStringLiteral("🔍 Test"), grpLogin);
    btnProbe->setToolTip(QStringLiteral("Prüft Erreichbarkeit, TLS/SSL und ob Endpunkt JSON oder HTML-Webseite liefert"));
    btnProbe->setStyleSheet("background-color: #0369A1; color: white; padding: 6px;");
    btnRow1->addWidget(m_btnLogin, 2);
    btnRow1->addWidget(btnProbe, 1);
    btnRow1->addWidget(m_btnLogout, 1);
    g->addLayout(btnRow1, 6, 0, 1, 2);

    auto *btnRow2 = new QHBoxLayout();
    auto *btnCurl = new QPushButton(QStringLiteral("📋 Browser-Session / cURL übernehmen (SSO / 2FA)"), grpLogin);
    btnCurl->setToolTip(QStringLiteral("Übernimmt eine aktive Browser-Anmeldung (Als cURL kopieren, Bearer-Token, x-session-token oder Cookie)"));
    btnCurl->setStyleSheet("background-color: #6D28D9; color: white; padding: 5px 8px;");
    auto *btnAdv = new QPushButton(QStringLiteral("⚙️ Erweitert ..."), grpLogin);
    btnAdv->setStyleSheet("background-color: #48555F; color: #FBD50E; padding: 5px 8px;");
    btnRow2->addWidget(btnCurl, 2);
    btnRow2->addWidget(btnAdv, 1);
    g->addLayout(btnRow2, 7, 0, 1, 2);

    root->addWidget(grpLogin);

    // 3. Synchronisation, Datei-Import & Rohdaten
    auto *grpSync = new QGroupBox(QStringLiteral("Synchronisation && Datei-Import (%1)").arg(pi.shortName), this);
    auto *gs = new QGridLayout(grpSync);
    gs->setVerticalSpacing(4);

    m_btnFetch = new QPushButton(QStringLiteral("🔄 %1 GPS-Daten jetzt abrufen").arg(pi.shortName), grpSync);
    m_btnFetch->setStyleSheet("background-color: #0284C7; color: white; padding: 5px;");
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
                       "// Tipp: '🔍 Test' prüft den Endpunkt. Bei Web-SSO/2FA (Salesforce, Entra, Flutter) nutze '📋 Browser-Session / cURL'!")
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
    connect(btnProbe,         &QPushButton::clicked, this, &ProviderLoginWidget::onProbeConnectionClicked);
    connect(btnCurl,          &QPushButton::clicked, this, &ProviderLoginWidget::openCurlImportDialog);
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
        syncUiFromConfig();
        refreshStatusCard();
        emit statusNotification(details);
    });
    connect(m_client, &Link2RailClient::loginFailed, this, [this](const QString &err) {
        refreshStatusCard();
        emit statusNotification(err);
        QMessageBox::warning(
            this,
            QStringLiteral("%1 – Anmeldung fehlgeschlagen").arg(m_client->info().shortName),
            err + QStringLiteral("\n\nLösungstipp:\n"
                                 "• Klicke '📋 Browser-Session / cURL übernehmen', um dich im Browser (inkl. SSO/2FA) anzumelden und Token/Cookie/URL direkt zu übernehmen.\n"
                                 "• Oder passe über '⚙️ Erweitert ...' Vorlage, Account-Alias, API-Key oder Endpunkte an."));
    });
    connect(m_client, &Link2RailClient::liveDataUpdated, this, [this](const QVector<Link2RailWagonRecord> &, const QString &summary) {
        refreshStatusCard();
        emit statusNotification(summary);
    });
    connect(m_client, &Link2RailClient::apiError, this, [this](const QString &err) {
        emit statusNotification(err);
    });
    connect(m_client, &Link2RailClient::connectionTestFinished, this, [this](bool, const QString &report) {
        emit statusNotification(report.section('\n', 0, 0));
    });

    syncUiFromConfig();
    refreshStatusCard();
}

Link2RailConfig ProviderLoginWidget::buildConfigFromFields() const
{
    Link2RailConfig cfg = m_client->config();
    cfg.authMode = static_cast<Link2RailAuthMode>(m_comboAuthMode->currentIndex());
    cfg.username = m_editUser->text().trimmed();
    cfg.password = m_editPass->text();
    cfg.clientId = m_editClientId->text().trimmed();
    cfg.loginUrl = m_editLoginUrl->text().trimmed();
    cfg.gpsTelemetryUrl = m_editGpsUrl->text().trimmed();
    cfg.pollIntervalSeconds = m_spinPollSec->value();
    return cfg;
}

void ProviderLoginWidget::syncUiFromConfig()
{
    m_updatingUi = true;
    const Link2RailConfig &cfg = m_client->config();
    m_comboAuthMode->setCurrentIndex(std::clamp(static_cast<int>(cfg.authMode), 0, m_comboAuthMode->count() - 1));
    m_editUser->setText(cfg.username);
    m_editPass->setText(cfg.password);
    m_editClientId->setText(cfg.clientId);
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
                               mode == Link2RailAuthMode::HttpBasicAuth ||
                               mode == Link2RailAuthMode::ImtTenoAuth ||
                               mode == Link2RailAuthMode::HtmlFormPost);
    const bool needLoginUrl = (mode != Link2RailAuthMode::DirectApiKeyOrToken &&
                               mode != Link2RailAuthMode::HttpBasicAuth);
    m_editUser->setEnabled(needUserPass || mode == Link2RailAuthMode::DirectApiKeyOrToken);
    m_editPass->setEnabled(needUserPass);
    m_editLoginUrl->setEnabled(needLoginUrl);

    if (mode == Link2RailAuthMode::ImtTenoAuth) {
        m_lblClientId->setText(QStringLiteral("IMT Account-Alias:"));
        m_editClientId->setPlaceholderText(QStringLiteral("Firmen-Alias aus IMT Portal (z. B. shs-logistics)"));
    } else if (mode == Link2RailAuthMode::HtmlFormPost) {
        m_lblClientId->setText(QStringLiteral("Zusatz-ID (opt.):"));
        m_editClientId->setPlaceholderText(QStringLiteral("Optional (für WOPS meist leer lassen)"));
    } else {
        m_lblClientId->setText(QStringLiteral("Client-ID / Alias:"));
        m_editClientId->setPlaceholderText(QStringLiteral("Optional: DB-Client-Id, OAuth Client-ID oder IMT Alias"));
    }
}

void ProviderLoginWidget::onLoginClicked()
{
    const Link2RailConfig cfg = buildConfigFromFields();
    if (cfg.authMode == Link2RailAuthMode::DirectApiKeyOrToken &&
        cfg.manualBearerToken.trimmed().isEmpty() &&
        cfg.apiKey.trimmed().isEmpty() &&
        cfg.manualCookieHeader.trimmed().isEmpty() &&
        cfg.extraHeadersRaw.trimmed().isEmpty()) {
        openCurlImportDialog();
        return;
    }
    m_client->login(cfg);
}

void ProviderLoginWidget::onProbeConnectionClicked()
{
    m_client->probeConnection(buildConfigFromFields());
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

void ProviderLoginWidget::openCurlImportDialog()
{
    const TelematicsProviderInfo pi = m_client->info();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Browser-Session / cURL übernehmen – %1").arg(pi.displayName));
    dlg.resize(650, 410);
    auto *lay = new QVBoxLayout(&dlg);

    auto *lblInfo = new QLabel(
        QStringLiteral("<b style='font-size:12px; color:#1E293B;'>Schnell-Anmeldung per Browser-Session für %1</b><br>"
                       "<span style='font-size:10.5px; color:#334155;'>"
                       "Ideal für Portale mit Single Sign-On (SSO), 2FA, Salesforce, Microsoft Entra, Flutter oder Angular:<br>"
                       "<b>Schritt 1:</b> Klicke auf <i>„🌐 Portal öffnen“</i> und melde dich im Browser normal an.<br>"
                       "<b>Schritt 2:</b> Drücke im Browser <b>F12</b> → Reiter <b>Netzwerk (Network)</b> → Filter <i>Fetch/XHR</i> → "
                       "Rechtsklick auf die Waggon-/GPS-Abfrage → <b>„Als cURL kopieren“</b>.<br>"
                       "<b>Schritt 3:</b> Füge den cURL-Befehl (oder direkt <code>Bearer ...</code> / <code>x-session-token: ...</code> / <code>Cookie: ...</code>) unten ein:</span>")
            .arg(pi.displayName.toHtmlEscaped()),
        &dlg);
    lblInfo->setWordWrap(true);
    lay->addWidget(lblInfo);

    auto *ed = new QPlainTextEdit(&dlg);
    ed->setStyleSheet("background-color:#0F172A; color:#E2E8F0; font-family:Consolas,monospace; font-size:10px;");
    ed->setPlaceholderText(
        QStringLiteral("Beispiel:\ncurl 'https://.../wagons' \\\n  -H 'Authorization: Bearer eyJ...' \\\n  -H 'Cookie: ...'\n\n"
                       "Oder einfach direkt ein Token / x-session-token / Cookie-String einfügen."));
    lay->addWidget(ed, 1);

    auto *btnRow = new QHBoxLayout();
    auto *btnPortal = new QPushButton(QStringLiteral("🌐 %1 Portal im Browser öffnen").arg(pi.shortName), &dlg);
    btnPortal->setStyleSheet(QString("background-color: %1; color: white; padding: 6px 10px;").arg(pi.badgeColor.name()));
    connect(btnPortal, &QPushButton::clicked, this, &ProviderLoginWidget::onOpenPortalClicked);

    auto *btnCancel = new QPushButton(QStringLiteral("Abbrechen"), &dlg);
    auto *btnApply = new QPushButton(QStringLiteral("✅ Sitzung übernehmen && Live-Daten laden"), &dlg);
    btnApply->setStyleSheet("background-color: #15803D; color: white; padding: 6px 14px; font-weight: bold;");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnApply, &QPushButton::clicked, &dlg, &QDialog::accept);

    btnRow->addWidget(btnPortal);
    btnRow->addStretch(1);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnApply);
    lay->addLayout(btnRow);

    if (dlg.exec() == QDialog::Accepted) {
        QString summary;
        if (m_client->applyFromCurlOrToken(ed->toPlainText(), &summary)) {
            syncUiFromConfig();
            refreshStatusCard();
            emit statusNotification(QStringLiteral("[%1] %2").arg(pi.shortName, summary));
        } else {
            QMessageBox::warning(this, QStringLiteral("Übernahme fehlgeschlagen"), summary);
        }
    }
}

void ProviderLoginWidget::openAdvancedDialog()
{
    const Link2RailConfig cfg = buildConfigFromFields();
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
