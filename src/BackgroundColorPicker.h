#pragma once
#include <QWidget>

class QEnterEvent;
class QKeyEvent;
class QLabel;
class QSlider;
class QTimer;

class BackgroundColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundColorPicker(QWidget *parent = nullptr);
    void setCurrentValue(int grey);

signals:
    void greyChanged(int value);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateDismissTimer();

    QSlider *m_slider;
    QLabel *m_label;
    QTimer *m_dismissTimer;
    bool m_mouseOver = false;
};
