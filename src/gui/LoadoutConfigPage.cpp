#include "LoadoutConfigPage.h"
#include <QHBoxLayout>
#include <QFileDialog>

namespace gnumon::gui {

LoadoutConfigPage::LoadoutConfigPage(AppConfig *config, QWidget *parent)
    : QWidget(parent), config_(config)
{
    SetupUi();
    ReloadFromConfig();
}

void LoadoutConfigPage::SetupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 20, 40, 20);
    mainLayout->setSpacing(16);

    setStyleSheet(
        "LoadoutConfigPage {"
        "  background-color: #0d0e14;"
        "}"
        "QLabel {"
        "  color: #ffffff;"
        "}"
        "QPushButton {"
        "  background-color: #1f222e;"
        "  color: #e0e0e5;"
        "  border: 1px solid #363948;"
        "  border-radius: 4px;"
        "  padding: 8px 18px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2b2f40;"
        "  border-color: #4a5066;"
        "}"
        "QComboBox {"
        "  background-color: #1a1b24;"
        "  border: 1px solid #363948;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  color: #e0e0e5;"
        "}"
    );

    // Top Header: < Loadout Configuration
    auto *headerLayout = new QHBoxLayout();
    auto *btnBack = new QPushButton("< Loadout Configuration", this);
    btnBack->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "  color: #ffffff;"
        "  text-align: left;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  color: #64b5f6;"
        "}"
    );
    btnBack->setCursor(Qt::PointingHandCursor);
    connect(btnBack, &QPushButton::clicked, this, &LoadoutConfigPage::backRequested);
    headerLayout->addWidget(btnBack);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Toolbar: Default adapter + Clear all widgets
    auto *toolbarLayout = new QHBoxLayout();
    auto *lblAdapter = new QLabel("Default adapter:", this);
    lblAdapter->setStyleSheet("color: #a0a4b8; font-size: 13px;");
    comboAdapter_ = new QComboBox(this);
    comboAdapter_->setMinimumWidth(260);
    comboAdapter_->addItem("Auto-detect / Primary GPU", "");

    btnClearAll_ = new QPushButton("CLEAR ALL WIDGETS", this);
    btnClearAll_->setStyleSheet(
        "QPushButton {"
        "  background-color: #2b2326;"
        "  color: #ff8a80;"
        "  border: 1px solid #543238;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  padding: 6px 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3b282c;"
        "  color: #ff5252;"
        "}"
    );
    connect(btnClearAll_, &QPushButton::clicked, this, &LoadoutConfigPage::OnClearAllWidgets);

    toolbarLayout->addWidget(lblAdapter);
    toolbarLayout->addWidget(comboAdapter_);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(btnClearAll_);
    mainLayout->addLayout(toolbarLayout);

    // Scrollable rows area
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    rowsContainer_ = new QWidget(scrollArea);
    rowsContainer_->setStyleSheet("background: transparent;");
    rowsLayout_ = new QVBoxLayout(rowsContainer_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    rowsLayout_->addStretch();

    scrollArea->setWidget(rowsContainer_);
    mainLayout->addWidget(scrollArea, 1);

    // Add New Widget Button
    auto *addBtnLayout = new QHBoxLayout();
    addBtnLayout->addStretch();
    btnAddWidget_ = new QPushButton("ADD NEW WIDGET", this);
    btnAddWidget_->setStyleSheet(
        "QPushButton {"
        "  background-color: #1e2433;"
        "  color: #ffffff;"
        "  border: 1px dashed #3f4b66;"
        "  border-radius: 4px;"
        "  padding: 10px 40px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #273147;"
        "  border-color: #64b5f6;"
        "}"
    );
    connect(btnAddWidget_, &QPushButton::clicked, this, &LoadoutConfigPage::OnAddNewWidget);
    addBtnLayout->addWidget(btnAddWidget_);
    addBtnLayout->addStretch();
    mainLayout->addLayout(addBtnLayout);

    // Bottom row: Save and Load
    auto *bottomLayout = new QHBoxLayout();
    btnSave_ = new QPushButton("SAVE", this);
    btnSave_->setFixedWidth(140);
    connect(btnSave_, &QPushButton::clicked, this, &LoadoutConfigPage::OnSaveLoadout);

    btnLoad_ = new QPushButton("LOAD", this);
    btnLoad_->setFixedWidth(140);
    connect(btnLoad_, &QPushButton::clicked, this, &LoadoutConfigPage::OnLoadLoadout);

    bottomLayout->addWidget(btnSave_);
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnLoad_);
    mainLayout->addLayout(bottomLayout);
}

void LoadoutConfigPage::ReloadFromConfig() {
    RebuildRows();
}

void LoadoutConfigPage::RebuildRows() {
    // Clear existing widgets
    for (auto *rw : rowWidgets_) {
        rowsLayout_->removeWidget(rw);
        rw->deleteLater();
    }
    rowWidgets_.clear();

    // Re-create from config
    for (int i = 0; i < config_->loadout.widgets.size(); ++i) {
        auto *rw = new LoadoutRowWidget(config_->loadout.widgets[i], i, rowsContainer_);
        connect(rw, &LoadoutRowWidget::changed, this, &LoadoutConfigPage::OnRowChanged);
        connect(rw, &LoadoutRowWidget::duplicateRequested, this, &LoadoutConfigPage::OnRowDuplicate);
        connect(rw, &LoadoutRowWidget::deleteRequested, this, &LoadoutConfigPage::OnRowDelete);

        rowsLayout_->insertWidget(i, rw);
        rowWidgets_.append(rw);
    }
}

void LoadoutConfigPage::OnRowChanged() {
    config_->loadout.widgets.clear();
    for (auto *rw : rowWidgets_) {
        config_->loadout.widgets.append(rw->widget());
    }
    config_->Save();
    emit loadoutChanged();
}

void LoadoutConfigPage::OnRowDuplicate(int index) {
    if (index >= 0 && index < config_->loadout.widgets.size()) {
        LoadoutWidget copy = config_->loadout.widgets[index];
        copy.key = static_cast<int>(config_->loadout.widgets.size());
        config_->loadout.widgets.insert(index + 1, copy);
        config_->Save();
        RebuildRows();
        emit loadoutChanged();
    }
}

void LoadoutConfigPage::OnRowDelete(int index) {
    if (index >= 0 && index < config_->loadout.widgets.size()) {
        config_->loadout.widgets.removeAt(index);
        config_->Save();
        RebuildRows();
        emit loadoutChanged();
    }
}

void LoadoutConfigPage::OnAddNewWidget() {
    LoadoutWidget w;
    w.key = static_cast<int>(config_->loadout.widgets.size());
    w.widgetType = WidgetType::Readout;
    w.graphType = GraphType::Line;
    LoadoutMetricItem m;
    m.metricId = 12; // FPS-Presents
    m.statId = 1;   // avg
    w.metrics.append(m);

    config_->loadout.widgets.append(w);
    config_->Save();
    RebuildRows();
    emit loadoutChanged();
}

void LoadoutConfigPage::OnClearAllWidgets() {
    config_->loadout.widgets.clear();
    config_->Save();
    RebuildRows();
    emit loadoutChanged();
}

void LoadoutConfigPage::OnSaveLoadout() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Loadout JSON", QDir::homePath() + "/loadout.json", "JSON Files (*.json)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(config_->loadout.ToJson());
        }
    }
}

void LoadoutConfigPage::OnLoadLoadout() {
    QString fileName = QFileDialog::getOpenFileName(this, "Load Loadout JSON", QDir::homePath(), "JSON Files (*.json)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            if (config_->loadout.FromJson(file.readAll())) {
                config_->Save();
                RebuildRows();
                emit loadoutChanged();
            }
        }
    }
}

} // namespace gnumon::gui
