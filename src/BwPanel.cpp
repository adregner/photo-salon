#include "BwPanel.h"
#include "Const.h"
#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

static const char *kBandNames[6]    = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
static const char *kSwatchColors[6] = {"#FF3333", "#FFEE00", "#33CC33", "#00CCCC", "#3366FF", "#CC33CC"};

// The conversion "looks", in display order (a 4-wide grid: 4 then 3).
struct LookEntry { BwLook look; const char *tip; };
static const LookEntry kLooks[7] = {
    {BwLook::Neutral,      "Perceptual luminance — accurate and even. The natural baseline."},
    {BwLook::Photoshop,    "Photoshop's default Black & White mix — a familiar digital look."},
    {BwLook::IPhone,       "Punchy phone-camera look: deep blacks, protected highlights."},
    {BwLook::Monochrom,    "Panchromatic sensor (like a Leica Monochrom): true, wide luminance range."},
    {BwLook::ClassicLuma,  "Rec.601 luma — the classic digital grayscale."},
    {BwLook::Film,         "Tri-X / Ilford film curve: lifted blacks, gently rolled highlights."},
    {BwLook::HighContrast, "Dramatic S-curve: crushed blacks, bright whites."},
};

static const char *kBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 3px 7px; font-size: 12px; }"
    "QPushButton:hover { background: #555; }";

// Compare is a toggle: a lighter shade of grey marks the "showing original" state.
static const char *kCompareBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 3px 7px; font-size: 12px; }"
    "QPushButton:hover { background: #555; }"
    "QPushButton:checked { color: white; background: #888; border: 1px solid #aaa; }"
    "QPushButton:checked:hover { background: #999; }";

static const char *kLookBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 4px 6px; font-size: 12px; }"
    "QPushButton:hover { background: #555; }"
    "QPushButton:checked { background: #1c6cd6; border: 1px solid #2f8bff; }";

BwPanel::BwPanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(680);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 12);
    mainLayout->setSpacing(6);

    // Row 1: look buttons (4-wide grid), single-selection.
    m_lookGroup = new QButtonGroup(this);
    m_lookGroup->setExclusive(true);

    auto *lookGrid = new QGridLayout;
    lookGrid->setSpacing(4);
    for (int i = 0; i < 7; ++i) {
        auto *btn = new QPushButton(BwConverter::lookName(kLooks[i].look), this);
        btn->setStyleSheet(kLookBtnStyle);
        btn->setCheckable(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setToolTip(kLooks[i].tip);
        lookGrid->addWidget(btn, i / 4, i % 4);
        m_lookGroup->addButton(btn, (int)kLooks[i].look);
    }
    connect(m_lookGroup, &QButtonGroup::idClicked, this,
            [this](int id) { selectLook((BwLook)id); });
    mainLayout->addLayout(lookGrid);

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

    // Rows 3-8: hue-band sliders, then a contrast slider.
    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    auto addValueLabel = [&](int row) {
        auto *valLabel = new QLabel(" +0", this);
        valLabel->setFixedWidth(38);
        valLabel->setAlignment(Qt::AlignCenter);
        valLabel->setStyleSheet("color: white; font-size: 13px; font-family: monospace;");
        grid->addWidget(valLabel, row, 3);
        return valLabel;
    };
    auto makeSlider = [&](int row) {
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setMinimumWidth(400);
        grid->addWidget(slider, row, 2);
        connect(slider, &QSlider::valueChanged, this, &BwPanel::onAnySliderChanged);
        return slider;
    };

    for (int i = 0; i < 6; ++i) {
        auto *swatch = new QLabel(this);
        swatch->setFixedSize(14, 14);
        swatch->setAutoFillBackground(true);
        swatch->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(kSwatchColors[i]));
        grid->addWidget(swatch, i, 0, Qt::AlignVCenter);

        auto *nameLabel = new QLabel(kBandNames[i], this);
        nameLabel->setFixedWidth(68);
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        nameLabel->setStyleSheet("color: white; font-size: 13px;");
        grid->addWidget(nameLabel, i, 1);

        m_bands[i] = {makeSlider(i), addValueLabel(i)};
    }

    // Separator above the contrast row.
    auto *grpSep = new QFrame(this);
    grpSep->setFrameShape(QFrame::HLine);
    grpSep->setFrameShadow(QFrame::Sunken);
    grid->addWidget(grpSep, 6, 0, 1, 4);

    // Contrast row (a black→white gradient swatch distinguishes it from the colour bands).
    auto *contrastSwatch = new QLabel(this);
    contrastSwatch->setFixedSize(14, 14);
    contrastSwatch->setStyleSheet(
        "border: 1px solid #888; background: qlineargradient("
        "x1:0, y1:0, x2:1, y2:0, stop:0 black, stop:1 white);");
    grid->addWidget(contrastSwatch, 7, 0, Qt::AlignVCenter);

    auto *contrastName = new QLabel("Contrast", this);
    contrastName->setFixedWidth(68);
    contrastName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    contrastName->setStyleSheet("color: white; font-size: 13px;");
    grid->addWidget(contrastName, 7, 1);

    m_contrast = {makeSlider(7), addValueLabel(7)};

    mainLayout->addLayout(grid);

    // Bottom row
    auto *bottomRow = new QHBoxLayout;
    auto *hintLabel = new QLabel("W: show/hide   \\: compare", this);
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
    m_compareBtn->setStyleSheet(kCompareBtnStyle);
    m_compareBtn->setCheckable(true);
    m_compareBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_compareBtn, &QPushButton::toggled, this, [this](bool checked) {
        emit compareToggled(checked);
    });
    bottomRow->addWidget(m_compareBtn);
    mainLayout->addLayout(bottomRow);

    // Start on Neutral.
    if (auto *b = m_lookGroup->button((int)BwLook::Neutral))
        b->setChecked(true);

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
    p.look     = m_look;
    p.reds     = m_bands[0].slider->value();
    p.yellows  = m_bands[1].slider->value();
    p.greens   = m_bands[2].slider->value();
    p.cyans    = m_bands[3].slider->value();
    p.blues    = m_bands[4].slider->value();
    p.magentas = m_bands[5].slider->value();
    p.contrast = m_contrast.slider->value();
    return p;
}

void BwPanel::setParams(const BwParams &p) {
    m_inhibitSignal = true;
    m_look = p.look;
    if (auto *b = m_lookGroup->button((int)p.look)) {
        QSignalBlocker blocker(m_lookGroup);
        b->setChecked(true);
    }
    const int vals[6] = {p.reds, p.yellows, p.greens, p.cyans, p.blues, p.magentas};
    for (int i = 0; i < 6; ++i)
        m_bands[i].slider->setValue(vals[i]);
    m_contrast.slider->setValue(p.contrast);
    m_inhibitSignal = false;

    updateValueLabels();
    emit paramsChanged(params());
}

void BwPanel::selectLook(BwLook look) {
    // Picking a look loads its defaults (hue bands zeroed, the look's own contrast).
    setParams(BwConverter::lookPreset(look));
}

void BwPanel::setComparing(bool comparing) {
    // Keep the button enabled so it toggles back and forth; reflect the current
    // state in its label and (via the :checked style) its shade of grey.
    QSignalBlocker blocker(*m_compareBtn);
    m_compareBtn->setChecked(comparing);
    m_compareBtn->setText(comparing ? "Back to B&W" : "Compare");
}

void BwPanel::updateValueLabels() {
    for (int i = 0; i < 6; ++i) {
        int v = m_bands[i].slider->value();
        m_bands[i].valueLabel->setText(QString("%1%2").arg(v >= 0 ? " +" : " ").arg(v));
    }
    int c = m_contrast.slider->value();
    m_contrast.valueLabel->setText(QString("%1%2").arg(c >= 0 ? " +" : " ").arg(c));
}

void BwPanel::onAnySliderChanged() {
    updateValueLabels();
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
