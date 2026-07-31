#include "RotatePanel.h"
#include "RotateGeometry.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
const char *kBtnStyle =
    "QPushButton { color: white; background: #444; border: 1px solid #666;"
    " border-radius: 3px; padding: 5px 11px; font-size: 13px; }"
    "QPushButton:hover { background: #555; }";

// A slider that snaps back to straight on a double-click, matching the reset
// gesture the adjustment sliders and the crop box already use.
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
}  // namespace

RotatePanel::RotatePanel(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedWidth(430);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(10);

    // The two lossless quarter turns. These compose into the orientation edit
    // and cost nothing in quality, so they stay separate from the free angle.
    auto *turnRow = new QHBoxLayout;
    turnRow->setSpacing(8);
    m_rotateLeft = new QPushButton(QStringLiteral("⟲  Rotate Left"), this);
    m_rotateLeft->setStyleSheet(kBtnStyle);
    m_rotateLeft->setFocusPolicy(Qt::NoFocus);
    m_rotateLeft->setToolTip(QStringLiteral("Rotate 90° counter-clockwise"));
    connect(m_rotateLeft, &QPushButton::clicked, this, &RotatePanel::rotateLeftRequested);
    turnRow->addWidget(m_rotateLeft);

    m_rotateRight = new QPushButton(QStringLiteral("Rotate Right  ⟳"), this);
    m_rotateRight->setStyleSheet(kBtnStyle);
    m_rotateRight->setFocusPolicy(Qt::NoFocus);
    m_rotateRight->setToolTip(QStringLiteral("Rotate 90° clockwise"));
    connect(m_rotateRight, &QPushButton::clicked, this, &RotatePanel::rotateRightRequested);
    turnRow->addWidget(m_rotateRight);
    mainLayout->addLayout(turnRow);

    // The free angle: the same value the corner handles drag out.
    auto *angleRow = new QHBoxLayout;
    angleRow->setSpacing(10);
    auto *name = new QLabel(QStringLiteral("Straighten"), this);
    name->setFixedWidth(74);
    name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    name->setStyleSheet("color: white; font-size: 13px;");
    angleRow->addWidget(name);

    m_slider = new ResetSlider(Qt::Horizontal, this);
    const int limit = static_cast<int>(RotateGeometry::kMaxAngle) * kStepsPerDegree;
    m_slider->setRange(-limit, limit);
    m_slider->setValue(0);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(kStepsPerDegree);
    m_slider->setMinimumWidth(240);
    connect(m_slider, &QSlider::valueChanged, this, &RotatePanel::onSliderChanged);
    angleRow->addWidget(m_slider);

    m_value = new QLabel(this);
    m_value->setFixedWidth(56);
    m_value->setAlignment(Qt::AlignCenter);
    m_value->setStyleSheet("color: white; font-size: 13px; font-family: monospace;");
    angleRow->addWidget(m_value);
    mainLayout->addLayout(angleRow);

    auto *bottomRow = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("R: show/hide   drag a corner to rotate"), this);
    hint->setStyleSheet("color: #888; font-size: 11px;");
    bottomRow->addWidget(hint);
    bottomRow->addStretch();

    m_resetBtn = new QPushButton(QStringLiteral("Reset"), this);
    m_resetBtn->setStyleSheet(kBtnStyle);
    m_resetBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_resetBtn, &QPushButton::clicked, this, [this] { m_slider->setValue(0); });
    bottomRow->addWidget(m_resetBtn);
    mainLayout->addLayout(bottomRow);

    updateValueLabel();
}

double RotatePanel::angle() const {
    return static_cast<double>(m_slider->value()) / kStepsPerDegree;
}

void RotatePanel::setAngle(double degrees) {
    const int steps = static_cast<int>(std::lround(RotateGeometry::clampAngle(degrees) * kStepsPerDegree));
    m_inhibitSignal = true;
    {
        QSignalBlocker block(m_slider);
        m_slider->setValue(steps);
    }
    m_inhibitSignal = false;
    updateValueLabel();
}

void RotatePanel::onSliderChanged() {
    updateValueLabel();
    if (!m_inhibitSignal) emit angleChanged(angle());
}

void RotatePanel::updateValueLabel() {
    const double a = angle();
    m_value->setText(QStringLiteral("%1%2°")
                         .arg(a >= 0 ? QStringLiteral("+") : QString())
                         .arg(a, 0, 'f', 1));
}

void RotatePanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 30, 30, 220));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);
}
