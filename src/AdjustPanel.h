#pragma once
#include "ImageAdjust.h"
#include <QWidget>

class QEnterEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;

// ---------------------------------------------------------------------------
// AdjustPanel — the pop-up that drives the light/tone and colour-balance edits.
// Styled like BwPanel (a translucent, auto-dismissing Tool window), but split
// into two tabs: "Light & Levels" (brightness, contrast, exposure, saturation,
// black/white levels) and "Color" (temperature, tint, per-channel R/G/B). The
// two tabs map to two independent edits in the manifest. The active tab is
// remembered across dismiss/reopen (and persisted in QSettings).
// ---------------------------------------------------------------------------
class AdjustPanel : public QWidget {
    Q_OBJECT
public:
    explicit AdjustPanel(QWidget *parent = nullptr);

    AdjustParams adjustParams() const;
    ColorParams  colorParams() const;
    void setAdjustParams(const AdjustParams &p);
    void setColorParams(const ColorParams &p);

    int  activeTab() const;
    void setActiveTab(int index);

signals:
    void adjustParamsChanged(const AdjustParams &p);
    void colorParamsChanged(const ColorParams &p);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Row {
        QSlider *slider = nullptr;
        QLabel  *value  = nullptr;
    };

    QWidget *buildToneTab();
    QWidget *buildColorTab();
    void onToneChanged();
    void onColorChanged();
    void resetCurrentTab();
    void updateValueLabels();
    void styleHueGroove(int hueIndex);   // tint a hue slider's line by its value
    void updateDismissTimer();

    static constexpr int kHueOffset = 5;   // m_color[5..12] are the hue sliders

    QTabWidget  *m_tabs   = nullptr;
    Row          m_tone[6];    // brightness, contrast, exposure, saturation, blacks, whites
    Row          m_color[13];  // temperature, tint, red, green, blue, then 8 hue bands
    QPushButton *m_resetBtn = nullptr;
    QTimer      *m_dismissTimer = nullptr;
    bool         m_mouseOver = false;
    bool         m_inhibitSignal = false;
};
