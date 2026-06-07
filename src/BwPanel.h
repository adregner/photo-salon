#pragma once
#include "BwConverter.h"
#include <QWidget>

class QButtonGroup;
class QEnterEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

class BwPanel : public QWidget {
    Q_OBJECT
public:
    explicit BwPanel(QWidget *parent = nullptr);

    BwParams    params() const;
    void        setParams(const BwParams &p);
    void        setComparing(bool comparing);
    QPushButton *compareButton() const { return m_compareBtn; }

signals:
    void paramsChanged(const BwParams &p);
    void compareToggled(bool showOriginal);
    void resetToColorRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void selectLook(BwLook look);
    void onAnySliderChanged();
    void updateValueLabels();
    void updateDismissTimer();

    struct SliderRow {
        QSlider *slider;
        QLabel  *valueLabel;
    };

    BwLook        m_look = BwLook::Neutral;
    QButtonGroup *m_lookGroup = nullptr;
    SliderRow     m_bands[6];
    SliderRow     m_contrast{};
    QPushButton  *m_compareBtn = nullptr;
    QPushButton  *m_resetBtn   = nullptr;
    QTimer       *m_dismissTimer = nullptr;
    bool          m_mouseOver = false;
    bool          m_inhibitSignal = false;
};
