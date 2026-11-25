#ifndef PROVIDERLOGINWIDGET_H
#define PROVIDERLOGINWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QTimer>

#include "link2railclient.h"

class ProviderLoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProviderLoginWidget(Link2RailClient *client, QWidget *parent = nullptr);

    Link2RailClient* client() const { return m_client; }
    void openAdvancedDialog();
    void syncUiFromConfig();
    void refreshStatusCard();

signals:
    void statusNotification(const QString &msg);
    void importSucceededFitRequested();

private slots:
    void onLoginClicked();
    void onLogoutClicked();
    void onFetchClicked();
    void onAuthModeChanged(int index);
    void onAutoPollToggled(bool checked);
    void onPollIntervalChanged(int seconds);
    void onImportFileClicked();
    void onShowRawClicked();
    void onOpenPortalClicked();

private:
    Link2RailClient *m_client = nullptr;
    QTimer *m_pollTimer = nullptr;

    QLabel *m_lblStatusCard = nullptr;
    QComboBox *m_comboAuthMode = nullptr;
    QLineEdit *m_editUser = nullptr;
    QLineEdit *m_editPass = nullptr;
    QLineEdit *m_editLoginUrl = nullptr;
    QLineEdit *m_editGpsUrl = nullptr;

    QPushButton *m_btnLogin = nullptr;
    QPushButton *m_btnLogout = nullptr;
    QPushButton *m_btnFetch = nullptr;
    QCheckBox *m_chkAutoPoll = nullptr;
    QSpinBox *m_spinPollSec = nullptr;
    QPlainTextEdit *m_txtLog = nullptr;

    bool m_updatingUi = false;
};

#endif // PROVIDERLOGINWIDGET_H

