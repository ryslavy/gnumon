#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

namespace gnumon::gui {

class HotkeyPillWidget : public QWidget {
    Q_OBJECT

public:
    explicit HotkeyPillWidget(const QString& hotkey = "Ctrl+Shift+O", QWidget *parent = nullptr);

    QString hotkey() const { return hotkey_; }
    void setHotkey(const QString& hotkey);

    QSize sizeHint() const override;

signals:
    void hotkeyChanged(const QString& newHotkey);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QString hotkey_;
    bool recording_ = false;
};

} // namespace gnumon::gui
