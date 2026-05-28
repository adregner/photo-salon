#include "BwPanel.h"
#include "Const.h"
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

static const char *kBandNames[6]   = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
static const char *kSwatchColors[6] = {"#FF3333", "#FFEE00", "#33CC33", "#00CCCC", "#3366FF", "#CC33CC"};

static const char *kBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 3px 7px; font-size: 12px; }"
    "QPushButton:hover { background: #555; }";

BwPanel::BwPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(680);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 12);
    mainLayout->setSpacing(6);

    // Row 1: preset buttons
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(4);

    auto makeBtn = [&](const char *text) {
        auto *btn = new QPushButton(text, this);
        btn->setStyleSheet(kBtnStyle);
        btn->setFocusPolicy(Qt::NoFocus);
        presetRow->addWidget(btn);
        return btn;
    };

    auto *btnNeutral  = makeBtn("Neutral");
    auto *btnYellow   = makeBtn("Yellow #8");
    auto *btnOrange   = makeBtn("Orange #15");
    auto *btnRedA     = makeBtn("Red A #25");
    auto *btnDeepRed  = makeBtn("Deep Red #29");
    auto *btnGreen    = makeBtn("Green #58");
    auto *btnInfrared = makeBtn("Infrared");
    auto *btnAuto     = makeBtn("Auto");

    connect(btnNeutral,  &QPushButton::clicked, this, [this]{ applyPreset({0,0,0,0,0,0}); });
    connect(btnYellow,   &QPushButton::clicked, this, [this]{ applyPreset({20,50,20,-10,-50,0}); });
    connect(btnOrange,   &QPushButton::clicked, this, [this]{ applyPreset({40,60,10,-30,-70,20}); });
    connect(btnRedA,     &QPushButton::clicked, this, [this]{ applyPreset({80,40,-40,-80,-90,50}); });
    connect(btnDeepRed,  &QPushButton::clicked, this, [this]{ applyPreset({100,60,-60,-100,-100,70}); });
    connect(btnGreen,    &QPushButton::clicked, this, [this]{ applyPreset({-70,-10,80,30,-50,-80}); });
    connect(btnInfrared, &QPushButton::clicked, this, [this]{ applyPreset({60,50,100,30,-80,20}); });
    connect(btnAuto,     &QPushButton::clicked, this, [this]{ emit autoRequested(); });

    mainLayout->addLayout(presetRow);

    // Row 2: separator
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    {
        auto *sepMargin = new QVBoxLayout;
        sepMargin->setContentsMargins(0, 4, 0, 4);
        sepMargin->addWidget(sep);
        mainLayout->addLayout(sepMargin);
    }

    // Rows 3-8: slider grid
    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    for (int i = 0; i < 6; ++i) {
        // Swatch
        auto *swatch = new QLabel(this);
        swatch->setFixedSize(14, 14);
        swatch->setAutoFillBackground(true);
        swatch->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(kSwatchColors[i]));
        grid->addWidget(swatch, i, 0, Qt::AlignVCenter);

        // Band name
        auto *nameLabel = new QLabel(kBandNames[i], this);
        nameLabel->setFixedWidth(68);
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        nameLabel->setStyleSheet("color: white; font-size: 13px;");
        grid->addWidget(nameLabel, i, 1);

        // Slider
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setMinimumWidth(400);
        grid->addWidget(slider, i, 2);

        // Value label
        auto *valLabel = new QLabel(" +0", this);
        valLabel->setFixedWidth(38);
        valLabel->setAlignment(Qt::AlignCenter);
        valLabel->setStyleSheet("color: white; font-size: 13px; font-family: monospace;");
        grid->addWidget(valLabel, i, 3);

        m_bands[i] = {slider, valLabel};

        connect(slider, &QSlider::valueChanged, this, &BwPanel::onAnySliderChanged);
    }

    mainLayout->addLayout(grid);

    // Row 9: bottom row
    auto *bottomRow = new QHBoxLayout;
    auto *hintLabel = new QLabel("W: show/hide  \\: compare", this);
    hintLabel->setStyleSheet("color: #888; font-size: 11px;");
    bottomRow->addWidget(hintLabel);
    bottomRow->addStretch();

    m_resetBtn = new QPushButton("Reset to Color", this);
    m_resetBtn->setStyleSheet(kBtnStyle);
    m_resetBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        emit resetToColorRequested();
    });
    bottomRow->addWidget(m_resetBtn);

    m_compareBtn = new QPushButton("Compare", this);
    m_compareBtn->setStyleSheet(kBtnStyle);
    m_compareBtn->setCheckable(true);
    m_compareBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_compareBtn, &QPushButton::toggled, this, [this](bool checked) {
        emit compareToggled(checked);
    });
    bottomRow->addWidget(m_compareBtn);
    mainLayout->addLayout(bottomRow);

    // Dismiss timer
    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(PANEL_DISMISS);
    connect(m_dismissTimer, &QTimer::timeout, this, &BwPanel::hide);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        if (isVisible()) updateDismissTimer();
    });
}

BwParams BwPanel::params() const {
    BwParams p;
    p.reds     = m_bands[0].slider->value();
    p.yellows  = m_bands[1].slider->value();
    p.greens   = m_bands[2].slider->value();
    p.cyans    = m_bands[3].slider->value();
    p.blues    = m_bands[4].slider->value();
    p.magentas = m_bands[5].slider->value();
    return p;
}

void BwPanel::setParams(const BwParams &p) {
    m_inhibitSignal = true;
    const int vals[6] = {p.reds, p.yellows, p.greens, p.cyans, p.blues, p.magentas};
    for (int i = 0; i < 6; ++i) {
        m_bands[i].slider->setValue(vals[i]);
        int v = vals[i];
        m_bands[i].valueLabel->setText(QString("%1%2").arg(v >= 0 ? " +" : " ").arg(v));
    }
    m_inhibitSignal = false;
    emit paramsChanged(params());
}

void BwPanel::applyPreset(const BwParams &p) {
    setParams(p);
}

void BwPanel::setComparing(bool comparing) {
    QSignalBlocker blocker(*m_compareBtn);
    m_compareBtn->setChecked(comparing);
    m_compareBtn->setEnabled(!comparing);
}

void BwPanel::onAnySliderChanged() {
    // Update all value labels
    for (int i = 0; i < 6; ++i) {
        int v = m_bands[i].slider->value();
        m_bands[i].valueLabel->setText(QString("%1%2").arg(v >= 0 ? " +" : " ").arg(v));
    }
    if (!m_inhibitSignal)
        emit paramsChanged(params());
}

void BwPanel::updateDismissTimer() {
    QWidget *fw = QApplication::focusWidget();
    bool hasFocus = fw && (fw == this || isAncestorOf(fw));
    if (m_mouseOver || hasFocus)
        m_dismissTimer->stop();
    else
        m_dismissTimer->start();
}

void BwPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 30, 30, 220));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);
}

void BwPanel::enterEvent(QEnterEvent *event) {
    m_mouseOver = true;
    m_dismissTimer->stop();
    QWidget::enterEvent(event);
}

void BwPanel::leaveEvent(QEvent *event) {
    m_mouseOver = false;
    updateDismissTimer();
    QWidget::leaveEvent(event);
}

void BwPanel::hideEvent(QHideEvent *event) {
    m_dismissTimer->stop();
    m_mouseOver = false;
    QWidget::hideEvent(event);
}
