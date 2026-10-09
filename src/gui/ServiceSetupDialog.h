#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QProcess>

namespace gnumon::gui {

class ServiceSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit ServiceSetupDialog(QWidget *parent = nullptr);
    ~ServiceSetupDialog() override = default;

public slots:
    void RefreshStatus();

private slots:
    void OnStartService();
    void OnStopService();
    void OnEnableService();
    void OnInstallUdevRules();
    void OnInstallLayers();

private:
    void SetupUi();
    void UpdateUiFromStatus(const QMap<QString, QString>& status);
    QString RunSetupCommand(const QString& action);

    // Daemon card UI
    QLabel *lblDaemonStatus_ = nullptr;
    QPushButton *btnStartService_ = nullptr;
    QPushButton *btnStopService_ = nullptr;
    QPushButton *btnEnableService_ = nullptr;

    // Permissions card UI
    QLabel *lblUdevStatus_ = nullptr;
    QLabel *lblInputAccess_ = nullptr;
    QPushButton *btnInstallUdev_ = nullptr;

    // Layers card UI
    QLabel *lblVulkan64Status_ = nullptr;
    QLabel *lblVulkan32Status_ = nullptr;
    QPushButton *btnInstallLayers_ = nullptr;

    QLabel *lblFeedback_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
};

} // namespace gnumon::gui
