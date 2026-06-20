#include "AdjustPanel.h"
#include "Const.h"
#include "ImageAdjust.h"
#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const char *kToneNames[6]  = {"Brightness", "Contrast", "Exposure", "Saturation", "Blacks", "Whites"};
const char *kColorNames[5] = {"Temperature", "Tint", "Red", "Green", "Blue"};

// A slider that snaps back to its default (0) on a double-click, so any
// adjustment can be reset without dragging it precisely to centre.
class ResetSlider : public QSlider {
public:
    explicit ResetSlider(Qt::Orientation o, QWidget *parent = nullptr) : QSlider(o, parent) {}
protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            setValue(0);
            event->accept();
        } else {
            QSlider::mouseDoubleClickEvent(event);
        }
    }
};

const char *kBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 3px 7px; font-size: 12px; }"
    "QPushButton:hover { background: #555; }";

// A dark, compact tab bar that matches the translucent panel chrome.
const char *kTabStyle =
    "QTabWidget::pane { border: 1px solid #555; border-radius: 4px; top: -1px; }"
    "QTabBar::tab { color: #ccc; background: #3a3a3a; border: 1px solid #555;"
    " border-top-left-radius: 4px; border-top-right-radius: 4px;"
    " padding: 5px 16px; margin-right: 3px; font-size: 13px; }"
    "QTabBar::tab:selected { color: white; background: #1c6cd6; border-color: #2f8bff; }"
    "QTabBar::tab:hover:!selected { background: #4a4a4a; }";

const char *kSettingsKey = "adjustPanel/activeTab";
}  // namespace

AdjustPanel::AdjustPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(560);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 12);
    mainLayout->setSpacing(8);

    m_tabs = new QTabWidget(this);
    m_tabs->setStyleSheet(kTabStyle);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->addTab(buildToneTab(),  QStringLiteral("Light && Levels"));
    m_tabs->addTab(buildColorTab(), QStringLiteral("Color"));
    mainLayout->addWidget(m_tabs);

    // Bottom row: shortcut hint + per-tab reset.
    auto *bottomRow = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("C: show/hide   \\: compare"), this);
    hint->setStyleSheet("color: #888; font-size: 11px;");
    bottomRow->addWidget(hint);
    bottomRow->addStretch();

    m_resetBtn = new QPushButton(QStringLiteral("Reset Tab"), this);
    m_resetBtn->setStyleSheet(kBtnStyle);
    m_resetBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_resetBtn, &QPushButton::clicked, this, &AdjustPanel::resetCurrentTab);
    bottomRow->addWidget(m_resetBtn);
    mainLayout->addLayout(bottomRow);

    // Restore the last-used tab so reopening lands on the same controls.
    const int saved = QSettings().value(QLatin1String(kSettingsKey), 0).toInt();
    m_tabs->setCurrentIndex(qBound(0, saved, m_tabs->count() - 1));
    connect(m_tabs, &QTabWidget::currentChanged, this, [](int index) {
        QSettings().setValue(QLatin1String(kSettingsKey), index);
    });

    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(PANEL_DISMISS);
    connect(m_dismissTimer, &QTimer::timeout, this, &AdjustPanel::hide);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        if (isVisible()) updateDismissTimer();
    });
}

QWidget *AdjustPanel::buildToneTab() {
    auto *tab = new QWidget(this);
    auto *grid = new QGridLayout(tab);
    grid->setContentsMargins(6, 10, 6, 6);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    for (int i = 0; i < 6; ++i) {
        auto *name = new QLabel(QLatin1String(kToneNames[i]), tab);
        name->setFixedWidth(86);
        name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        name->setStyleSheet("color: white; font-size: 13px;");
        grid->addWidget(name, i, 0);

        auto *slider = new ResetSlider(Qt::Horizontal, tab);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setMinimumWidth(360);
        grid->addWidget(slider, i, 1);
        connect(slider, &QSlider::valueChanged, this, &AdjustPanel::onToneChanged);

        auto *value = new QLabel(QStringLiteral(" +0"), tab);
        value->setFixedWidth(38);
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet("color: white; font-size: 13px; font-family: monospace;");
        grid->addWidget(value, i, 2);

        m_tone[i] = {slider, value};
    }
    return tab;
}

QWidget *AdjustPanel::buildColorTab() {
    auto *tab = new QWidget(this);
    auto *grid = new QGridLayout(tab);
    grid->setContentsMargins(6, 10, 6, 6);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    // A row is [swatch?][name][slider][value]; only the per-hue rows get a swatch.
    auto addRow = [&](int row, const QString &name, const QString &swatchHex) -> Row {
        if (!swatchHex.isEmpty()) {
            auto *sw = new QLabel(tab);
            sw->setFixedSize(14, 14);
            sw->setStyleSheet(QString("background:%1; border:1px solid #888; border-radius:7px;").arg(swatchHex));
            grid->addWidget(sw, row, 0, Qt::AlignCenter);
        }
        auto *lbl = new QLabel(name, tab);
        lbl->setFixedWidth(78);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet("color: white; font-size: 13px;");
        grid->addWidget(lbl, row, 1);

        auto *slider = new ResetSlider(Qt::Horizontal, tab);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setMinimumWidth(330);
        grid->addWidget(slider, row, 2);
        connect(slider, &QSlider::valueChanged, this, &AdjustPanel::onColorChanged);

        auto *value = new QLabel(QStringLiteral(" +0"), tab);
        value->setFixedWidth(38);
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet("color: white; font-size: 13px; font-family: monospace;");
        grid->addWidget(value, row, 3);

        return Row{slider, value};
    };

    // The five colour-balance sliders (no swatch).
    for (int i = 0; i < 5; ++i)
        m_color[i] = addRow(i, QLatin1String(kColorNames[i]), QString());

    auto *sep = new QFrame(tab);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    grid->addWidget(sep, 5, 0, 1, 4);

    // The eight per-hue saturation sliders: swatch + a value-tinted groove.
    for (int b = 0; b < 8; ++b) {
        const auto &hb = ImageAdjust::hueBand(b);
        m_color[kHueOffset + b] = addRow(6 + b, QLatin1String(hb.name), QLatin1String(hb.swatch));
        styleHueGroove(b);
    }
    return tab;
}

void AdjustPanel::styleHueGroove(int hueIndex) {
    QSlider *s = m_color[kHueOffset + hueIndex].slider;
    const double center = ImageAdjust::hueBand(hueIndex).center;
    // Map the slider value onto the groove's vividness: −100 → grey, +100 → the
    // band's full hue, so the line itself reflects the current value.
    const double sat = (s->value() + 100) / 200.0;
    const QColor groove = QColor::fromHslF(center / 360.0, sat, 0.5);
    s->setStyleSheet(QString(
        "QSlider::groove:horizontal { height:6px; border-radius:3px; background:%1; }"
        "QSlider::handle:horizontal { background:#f0f0f0; border:1px solid #555;"
        " width:12px; height:12px; margin:-4px 0; border-radius:6px; }")
        .arg(groove.name()));
}

AdjustParams AdjustPanel::adjustParams() const {
    AdjustParams p;
    p.brightness = m_tone[0].slider->value();
    p.contrast   = m_tone[1].slider->value();
    p.exposure   = m_tone[2].slider->value();
    p.saturation = m_tone[3].slider->value();
    p.blacks     = m_tone[4].slider->value();
    p.whites     = m_tone[5].slider->value();
    return p;
}

ColorParams AdjustPanel::colorParams() const {
    ColorParams p;
    p.temperature = m_color[0].slider->value();
    p.tint        = m_color[1].slider->value();
    p.red         = m_color[2].slider->value();
    p.green       = m_color[3].slider->value();
    p.blue        = m_color[4].slider->value();
    for (int b = 0; b < 8; ++b)
        p.hues[b] = m_color[kHueOffset + b].slider->value();
    return p;
}

void AdjustPanel::setAdjustParams(const AdjustParams &p) {
    const int vals[6] = {p.brightness, p.contrast, p.exposure, p.saturation, p.blacks, p.whites};
    m_inhibitSignal = true;
    for (int i = 0; i < 6; ++i) {
        QSignalBlocker block(m_tone[i].slider);
        m_tone[i].slider->setValue(vals[i]);
    }
    m_inhibitSignal = false;
    updateValueLabels();
}

void AdjustPanel::setColorParams(const ColorParams &p) {
    const int vals[5] = {p.temperature, p.tint, p.red, p.green, p.blue};
    m_inhibitSignal = true;
    for (int i = 0; i < 5; ++i) {
        QSignalBlocker block(m_color[i].slider);
        m_color[i].slider->setValue(vals[i]);
    }
    for (int b = 0; b < 8; ++b) {
        QSignalBlocker block(m_color[kHueOffset + b].slider);
        m_color[kHueOffset + b].slider->setValue(p.hues[b]);
    }
    m_inhibitSignal = false;
    updateValueLabels();
    for (int b = 0; b < 8; ++b)
        styleHueGroove(b);
}

int  AdjustPanel::activeTab() const         { return m_tabs->currentIndex(); }
void AdjustPanel::setActiveTab(int index)   { m_tabs->setCurrentIndex(index); }

void AdjustPanel::onToneChanged() {
    updateValueLabels();
    if (!m_inhibitSignal) emit adjustParamsChanged(adjustParams());
}

void AdjustPanel::onColorChanged() {
    updateValueLabels();
    for (int b = 0; b < 8; ++b)
        styleHueGroove(b);
    if (!m_inhibitSignal) emit colorParamsChanged(colorParams());
}

void AdjustPanel::resetCurrentTab() {
    if (m_tabs->currentIndex() == 0)
        setAdjustParams(AdjustParams{});
    else
        setColorParams(ColorParams{});
    // setX did not emit (signals inhibited); emit once for the reset tab.
    if (m_tabs->currentIndex() == 0)
        emit adjustParamsChanged(adjustParams());
    else
        emit colorParamsChanged(colorParams());
}

void AdjustPanel::updateValueLabels() {
    auto label = [](const Row &r) {
        const int v = r.slider->value();
        r.value->setText(QStringLiteral("%1%2").arg(v >= 0 ? " +" : " ").arg(v));
    };
    for (auto &r : m_tone)  label(r);
    for (auto &r : m_color) label(r);
}

void AdjustPanel::updateDismissTimer() {
    QWidget *fw = QApplication::focusWidget();
    const bool hasFocus = fw && (fw == this || isAncestorOf(fw));
    if (m_mouseOver || hasFocus)
        m_dismissTimer->stop();
    else
        m_dismissTimer->start();
}

void AdjustPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 30, 30, 220));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);
}

void AdjustPanel::enterEvent(QEnterEvent *event) {
    m_mouseOver = true;
    m_dismissTimer->stop();
    QWidget::enterEvent(event);
}

void AdjustPanel::leaveEvent(QEvent *event) {
    m_mouseOver = false;
    updateDismissTimer();
    QWidget::leaveEvent(event);
}

void AdjustPanel::hideEvent(QHideEvent *event) {
    m_dismissTimer->stop();
    m_mouseOver = false;
    QWidget::hideEvent(event);
}
