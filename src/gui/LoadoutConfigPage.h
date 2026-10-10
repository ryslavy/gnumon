#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include "AppConfig.h"
#include "LoadoutRowWidget.h"

namespace gnumon::gui {

class LoadoutConfigPage : public QWidget {
    Q_OBJECT

public:
    explicit LoadoutConfigPage(AppConfig *config, QWidget *parent = nullptr);

    void ReloadFromConfig();

signals:
    void backRequested();
    void loadoutChanged();

private slots:
    void OnAddNewWidget();
    void OnClearAllWidgets();
    void OnSaveLoadout();
    void OnLoadLoadout();
    void OnRowChanged();
    void OnRowDuplicate(int index);
    void OnRowDelete(int index);

private:
    void SetupUi();
    void RebuildRows();

    AppConfig *config_ = nullptr;
    QComboBox *comboAdapter_ = nullptr;
    QPushButton *btnClearAll_ = nullptr;
    QPushButton *btnAddWidget_ = nullptr;
    QPushButton *btnSave_ = nullptr;
    QPushButton *btnLoad_ = nullptr;

    QWidget *rowsContainer_ = nullptr;
    QVBoxLayout *rowsLayout_ = nullptr;
    QVector<LoadoutRowWidget*> rowWidgets_;
};

} // namespace gnumon::gui
