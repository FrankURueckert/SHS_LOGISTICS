#ifndef LINK2RAILLOGINDIALOG_H
#define LINK2RAILLOGINDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>

#include "link2railclient.h"

class Link2RailLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Link2RailLoginDialog(const Link2RailConfig &initialConfig, QWidget *parent = nullptr);

    Link2RailConfig resultConfig() const;

private slots:
    void onPresetChanged(int index);
    void onAuthModeChanged(int index);
    void onOpenPortalInBrowser();

private:
    void applyConfigToUi(const Link2RailConfig &cfg);

    TelematicsProviderId m_providerId = TelematicsProviderId::Link2Rail;

    QComboBox *m_comboPreset = nullptr;
    QComboBox *m_comboAuthMode = nullptr;

    QLineEdit *m_editUsername = nullptr;
    QLineEdit *m_editPassword = nullptr;
    QLineEdit *m_editClientId = nullptr;
    QLineEdit *m_editClientSecret = nullptr;
    QLineEdit *m_editApiKey = nullptr;
    QLineEdit *m_editBearerToken = nullptr;

    QCheckBox *m_chkRememberSettings = nullptr;
    QCheckBox *m_chkRememberPassword = nullptr;

    QLineEdit *m_editLoginUrl = nullptr;
    QCheckBox *m_chkGpsEndpoint = nullptr;
    QLineEdit *m_editGpsUrl = nullptr;
    QCheckBox *m_chkTrackTraceEndpoint = nullptr;
    QLineEdit *m_editTrackTraceUrl = nullptr;
    QCheckBox *m_chkTrainMgmtEndpoint = nullptr;
    QLineEdit *m_editTrainMgmtUrl = nullptr;

    QLineEdit *m_editWagonFilter = nullptr;
    QPlainTextEdit *m_txtExtraHeaders = nullptr;
    QSpinBox *m_spinPollSec = nullptr;
    QCheckBox *m_chkAutoPoll = nullptr;
};

#endif // LINK2RAILLOGINDIALOG_H
