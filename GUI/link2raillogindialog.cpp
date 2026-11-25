#include "link2raillogindialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>

Link2RailLoginDialog::Link2RailLoginDialog(const Link2RailConfig &initialConfig, QWidget *parent)
    : QDialog(parent)
    , m_providerId(initialConfig.providerId)
{
    const TelematicsProviderInfo pi = Link2RailClient::providerInfo(m_providerId);

    setWindowTitle(QStringLiteral("%1 – Anmeldung & API-Konfiguration").arg(pi.displayName));
    setMinimumSize(680, 580);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // Header-Banner mit Dienst-Kennfarbe
    auto *hdr = new QFrame(this);
    hdr->setStyleSheet(QString(
        "QFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #FFFFFF, stop:0.35 #E4EEF2, stop:1.0 %1); "
        "border: 1.5px solid %1; border-radius: 5px; }").arg(pi.badgeColor.darker(135).name()));
    auto *hdrLay = new QHBoxLayout(hdr);
    hdrLay->setContentsMargins(10, 8, 12, 8);
    auto *lblLogo = new QLabel(hdr);
    QPixmap pm(QStringLiteral(":/SHS_Logo.png"));
    if (!pm.isNull()) {
        lblLogo->setPixmap(pm.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    lblLogo->setStyleSheet("border:none; background:transparent;");
    hdrLay->addWidget(lblLogo);

    auto *lblTitle = new QLabel(
        QStringLiteral("<b style='font-size:14px; color:#1E293B;'>%1</b><br>"
                       "<span style='font-size:10.5px; color:#334155;'>%2</span>")
            .arg(pi.displayName.toHtmlEscaped(), pi.description.toHtmlEscaped()),
        hdr);
    lblTitle->setWordWrap(true);
    lblTitle->setStyleSheet("border:none; background:transparent;");
    hdrLay->addWidget(lblTitle, 1);
    root->addWidget(hdr);

    auto *tabs = new QTabWidget(this);

    // ---------------- TAB 1: ANMELDUNG ----------------
    auto *tabLogin = new QWidget(tabs);
    auto *layLogin = new QVBoxLayout(tabLogin);
    layLogin->setSpacing(10);

    auto *grpAuth = new QGroupBox(QStringLiteral("1. Zugangsdaten für %1").arg(pi.shortName), tabLogin);
    auto *gAuth = new QGridLayout(grpAuth);

    gAuth->addWidget(new QLabel(QStringLiteral("Verbindungs-Vorlage:")), 0, 0);
    m_comboPreset = new QComboBox(grpAuth);
    for (const QString &pName : Link2RailClient::presetNamesForProvider(m_providerId)) {
        m_comboPreset->addItem(pName);
    }
    gAuth->addWidget(m_comboPreset, 0, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Anmeldeverfahren:")), 1, 0);
    m_comboAuthMode = new QComboBox(grpAuth);
    m_comboAuthMode->addItem(QStringLiteral("Portal-Login / JSON-POST (Benutzername + Passwort -> Token/Cookie)"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Password Grant (Benutzername + Passwort + Client-ID)"));
    m_comboAuthMode->addItem(QStringLiteral("OAuth2 Client Credentials (Client-ID + Client-Secret)"));
    m_comboAuthMode->addItem(QStringLiteral("Direkter Bearer-Token / API-Key (ohne Login-POST)"));
    m_comboAuthMode->addItem(QStringLiteral("HTTP Basic Auth (Benutzername + Passwort im Header)"));
    gAuth->addWidget(m_comboAuthMode, 1, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Benutzername / E-Mail:")), 2, 0);
    m_editUsername = new QLineEdit(grpAuth);
    m_editUsername->setPlaceholderText(QStringLiteral("Portal-Benutzername oder E-Mail-Adresse"));
    gAuth->addWidget(m_editUsername, 2, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Passwort:")), 3, 0);
    m_editPassword = new QLineEdit(grpAuth);
    m_editPassword->setEchoMode(QLineEdit::Password);
    m_editPassword->setPlaceholderText(QStringLiteral("Passwort für %1").arg(pi.shortName));
    gAuth->addWidget(m_editPassword, 3, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Client-Id (optional):")), 4, 0);
    m_editClientId = new QLineEdit(grpAuth);
    m_editClientId->setPlaceholderText(QStringLiteral("Optional: Client-ID / Tenant-ID"));
    gAuth->addWidget(m_editClientId, 4, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("API-Key (optional):")), 5, 0);
    m_editApiKey = new QLineEdit(grpAuth);
    m_editApiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_editApiKey->setPlaceholderText(QStringLiteral("Optional: API-Key (X-Api-Key / DB-Api-Key)"));
    gAuth->addWidget(m_editApiKey, 5, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Client-Secret (optional):")), 6, 0);
    m_editClientSecret = new QLineEdit(grpAuth);
    m_editClientSecret->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_editClientSecret->setPlaceholderText(QStringLiteral("Nur bei OAuth2 erforderlich"));
    gAuth->addWidget(m_editClientSecret, 6, 1);

    gAuth->addWidget(new QLabel(QStringLiteral("Manueller Bearer-Token:")), 7, 0);
    m_editBearerToken = new QLineEdit(grpAuth);
    m_editBearerToken->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_editBearerToken->setPlaceholderText(QStringLiteral("Nur für Modus 'Direkter Bearer-Token'"));
    gAuth->addWidget(m_editBearerToken, 7, 1);

    layLogin->addWidget(grpAuth);

    auto *grpOpt = new QGroupBox(QStringLiteral("2. Speicherung && Automatischer Live-Abruf"), tabLogin);
    auto *gOpt = new QGridLayout(grpOpt);
    m_chkRememberSettings = new QCheckBox(QStringLiteral("Benutzername && Endpunkt-URLs lokal speichern"), grpOpt);
    m_chkRememberPassword = new QCheckBox(QStringLiteral("Passwort / API-Key für nächsten Start lokal merken"), grpOpt);
    m_chkAutoPoll = new QCheckBox(QStringLiteral("GPS-Daten nach Anmeldung automatisch zyklisch aktualisieren"), grpOpt);

    gOpt->addWidget(m_chkRememberSettings, 0, 0, 1, 2);
    gOpt->addWidget(m_chkRememberPassword, 1, 0, 1, 2);
    gOpt->addWidget(m_chkAutoPoll, 2, 0);

    auto *pollRow = new QHBoxLayout();
    pollRow->addWidget(new QLabel(QStringLiteral("Abruf-Intervall:")));
    m_spinPollSec = new QSpinBox(grpOpt);
    m_spinPollSec->setRange(10, 3600);
    m_spinPollSec->setSuffix(QStringLiteral(" Sek."));
    m_spinPollSec->setValue(60);
    pollRow->addWidget(m_spinPollSec);
    gOpt->addLayout(pollRow, 2, 1);
    layLogin->addWidget(grpOpt);

    auto *portalHintRow = new QHBoxLayout();
    auto *lblPortalHint = new QLabel(
        QStringLiteral("Web-Portal dieses Dienstes direkt im Browser öffnen:"), tabLogin);
    lblPortalHint->setWordWrap(true);
    auto *btnOpenPortal = new QPushButton(QStringLiteral("🌐 %1 Portal öffnen").arg(pi.shortName), tabLogin);
    connect(btnOpenPortal, &QPushButton::clicked, this, &Link2RailLoginDialog::onOpenPortalInBrowser);
    portalHintRow->addWidget(lblPortalHint, 1);
    portalHintRow->addWidget(btnOpenPortal);
    layLogin->addLayout(portalHintRow);
    layLogin->addStretch(1);

    tabs->addTab(tabLogin, QStringLiteral("🔐 Anmeldung"));

    // ---------------- TAB 2: ENDPOINTS & FILTER ----------------
    auto *tabEp = new QWidget(tabs);
    auto *layEp = new QVBoxLayout(tabEp);
    layEp->setSpacing(8);

    auto *grpUrls = new QGroupBox(QStringLiteral("Konfigurierbare REST-Endpunkte für %1").arg(pi.shortName), tabEp);
    auto *gUrl = new QGridLayout(grpUrls);

    gUrl->addWidget(new QLabel(QStringLiteral("Login-/Token-URL (POST):")), 0, 0);
    m_editLoginUrl = new QLineEdit(grpUrls);
    gUrl->addWidget(m_editLoginUrl, 0, 1);

    m_chkGpsEndpoint = new QCheckBox(QStringLiteral("GPS / Positionen URL (GET):"), grpUrls);
    m_editGpsUrl = new QLineEdit(grpUrls);
    gUrl->addWidget(m_chkGpsEndpoint, 1, 0);
    gUrl->addWidget(m_editGpsUrl, 1, 1);

    m_chkTrackTraceEndpoint = new QCheckBox(QStringLiteral("Track && Trace / Details URL:"), grpUrls);
    m_editTrackTraceUrl = new QLineEdit(grpUrls);
    gUrl->addWidget(m_chkTrackTraceEndpoint, 2, 0);
    gUrl->addWidget(m_editTrackTraceUrl, 2, 1);

    m_chkTrainMgmtEndpoint = new QCheckBox(QStringLiteral("Zusatz-Endpunkt (Züge/Geofence):"), grpUrls);
    m_editTrainMgmtUrl = new QLineEdit(grpUrls);
    gUrl->addWidget(m_chkTrainMgmtEndpoint, 3, 0);
    gUrl->addWidget(m_editTrainMgmtUrl, 3, 1);

    layEp->addWidget(grpUrls);

    auto *grpFilter = new QGroupBox(QStringLiteral("Optionale Abfrage-Filter && Zusatz-Header"), tabEp);
    auto *gFilt = new QGridLayout(grpFilter);

    gFilt->addWidget(new QLabel(QStringLiteral("EVN / Wagennummern-Filter:")), 0, 0);
    m_editWagonFilter = new QLineEdit(grpFilter);
    m_editWagonFilter->setPlaceholderText(QStringLiteral("Leer = alle Waggons abrufen; sonst kommagetrennt z. B. 318048910012"));
    gFilt->addWidget(m_editWagonFilter, 0, 1);

    gFilt->addWidget(new QLabel(QStringLiteral("Extra HTTP-Header\n(Name: Wert pro Zeile):")), 1, 0);
    m_txtExtraHeaders = new QPlainTextEdit(grpFilter);
    m_txtExtraHeaders->setMaximumHeight(90);
    m_txtExtraHeaders->setPlaceholderText(QStringLiteral("Optional, z. B.\nX-Custom-Tenant: SHS"));
    gFilt->addWidget(m_txtExtraHeaders, 1, 1);

    layEp->addWidget(grpFilter);
    layEp->addStretch(1);

    tabs->addTab(tabEp, QStringLiteral("⚙️ API-Endpunkte && Filter"));
    root->addWidget(tabs, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto *btnCancel = new QPushButton(QStringLiteral("Abbrechen"), this);
    auto *btnOk = new QPushButton(QStringLiteral("🔐 Bei %1 anmelden && Live-Daten laden").arg(pi.shortName), this);
    btnOk->setDefault(true);
    btnOk->setStyleSheet(QString("background-color: %1; color: white; padding: 8px 16px; font-weight: bold;").arg(pi.badgeColor.name()));
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnOk);
    root->addLayout(btnRow);

    applyConfigToUi(initialConfig);

    connect(m_comboPreset, QOverload<int>::of(&QComboBox::activated),
            this, &Link2RailLoginDialog::onPresetChanged);
    connect(m_comboAuthMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Link2RailLoginDialog::onAuthModeChanged);
}

void Link2RailLoginDialog::applyConfigToUi(const Link2RailConfig &cfg)
{
    m_comboAuthMode->setCurrentIndex(static_cast<int>(cfg.authMode));
    m_editUsername->setText(cfg.username);
    m_editPassword->setText(cfg.password);
    m_editClientId->setText(cfg.clientId);
    m_editClientSecret->setText(cfg.clientSecret);
    m_editApiKey->setText(cfg.apiKey);
    m_editBearerToken->setText(cfg.manualBearerToken);
    m_chkRememberSettings->setChecked(cfg.rememberSettings);
    m_chkRememberPassword->setChecked(cfg.rememberPassword);

    m_editLoginUrl->setText(cfg.loginUrl);
    m_chkGpsEndpoint->setChecked(cfg.enableGpsEndpoint);
    m_editGpsUrl->setText(cfg.gpsTelemetryUrl);
    m_chkTrackTraceEndpoint->setChecked(cfg.enableTrackTraceEndpoint);
    m_editTrackTraceUrl->setText(cfg.trackTraceUrl);
    m_chkTrainMgmtEndpoint->setChecked(cfg.enableTrainMgmtEndpoint);
    m_editTrainMgmtUrl->setText(cfg.trainMgmtUrl);

    m_editWagonFilter->setText(cfg.wagonFilterEvns);
    m_txtExtraHeaders->setPlainText(cfg.extraHeadersRaw);
    m_spinPollSec->setValue(std::clamp(cfg.pollIntervalSeconds, 10, 3600));
    m_chkAutoPoll->setChecked(cfg.autoPollEnabled);

    onAuthModeChanged(static_cast<int>(cfg.authMode));
}

void Link2RailLoginDialog::onPresetChanged(int index)
{
    Link2RailConfig p = Link2RailClient::presetConfig(m_providerId, index);
    p.username = m_editUsername->text();
    p.password = m_editPassword->text();
    p.clientId = m_editClientId->text();
    p.clientSecret = m_editClientSecret->text();
    p.apiKey = m_editApiKey->text();
    p.manualBearerToken = m_editBearerToken->text();
    p.rememberSettings = m_chkRememberSettings->isChecked();
    p.rememberPassword = m_chkRememberPassword->isChecked();
    p.wagonFilterEvns = m_editWagonFilter->text();
    p.extraHeadersRaw = m_txtExtraHeaders->toPlainText();
    p.pollIntervalSeconds = m_spinPollSec->value();
    p.autoPollEnabled = m_chkAutoPoll->isChecked();
    applyConfigToUi(p);
}

void Link2RailLoginDialog::onAuthModeChanged(int index)
{
    const auto mode = static_cast<Link2RailAuthMode>(index);
    const bool needUserPass = (mode == Link2RailAuthMode::PortalLoginCidP ||
                               mode == Link2RailAuthMode::OAuth2PasswordGrant ||
                               mode == Link2RailAuthMode::HttpBasicAuth);
    const bool isDirect = (mode == Link2RailAuthMode::DirectApiKeyOrToken);
    const bool needLoginUrl = (!isDirect && mode != Link2RailAuthMode::HttpBasicAuth);
    m_editUsername->setEnabled(needUserPass || isDirect);
    m_editPassword->setEnabled(needUserPass);
    m_editBearerToken->setEnabled(isDirect);
    m_editLoginUrl->setEnabled(needLoginUrl);
}

void Link2RailLoginDialog::onOpenPortalInBrowser()
{
    QDesktopServices::openUrl(QUrl(Link2RailClient::providerInfo(m_providerId).portalUrl));
}

Link2RailConfig Link2RailLoginDialog::resultConfig() const
{
    Link2RailConfig cfg;
    cfg.providerId = m_providerId;
    cfg.environmentName = m_comboPreset->currentText();
    cfg.authMode = static_cast<Link2RailAuthMode>(m_comboAuthMode->currentIndex());
    cfg.username = m_editUsername->text().trimmed();
    cfg.password = m_editPassword->text();
    cfg.clientId = m_editClientId->text().trimmed();
    cfg.clientSecret = m_editClientSecret->text();
    cfg.apiKey = m_editApiKey->text().trimmed();
    cfg.manualBearerToken = m_editBearerToken->text().trimmed();
    cfg.rememberSettings = m_chkRememberSettings->isChecked();
    cfg.rememberPassword = m_chkRememberPassword->isChecked();

    cfg.loginUrl = m_editLoginUrl->text().trimmed();
    cfg.enableGpsEndpoint = m_chkGpsEndpoint->isChecked();
    cfg.gpsTelemetryUrl = m_editGpsUrl->text().trimmed();
    cfg.enableTrackTraceEndpoint = m_chkTrackTraceEndpoint->isChecked();
    cfg.trackTraceUrl = m_editTrackTraceUrl->text().trimmed();
    cfg.enableTrainMgmtEndpoint = m_chkTrainMgmtEndpoint->isChecked();
    cfg.trainMgmtUrl = m_editTrainMgmtUrl->text().trimmed();

    cfg.wagonFilterEvns = m_editWagonFilter->text().trimmed();
    cfg.extraHeadersRaw = m_txtExtraHeaders->toPlainText();
    cfg.pollIntervalSeconds = m_spinPollSec->value();
    cfg.autoPollEnabled = m_chkAutoPoll->isChecked();
    return cfg;
}
