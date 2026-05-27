#include "BackgroundColorPicker.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QTimer>

BackgroundColorPicker::BackgroundColorPicker(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(48);

    m_label = new QLabel("Background: 0", this);
    m_label->setStyleSheet("color: white; font-size: 13px; padding: 0 8px;");

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 255);
    m_slider->setValue(0);
    m_slider->setMinimumWidth(180);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->addWidget(m_label);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_label->setText(QString("Background: %1").arg(v));
        emit greyChanged(v);
    });

    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(15000);
    connect(m_dismissTimer, &QTimer::timeout, this, &BackgroundColorPicker::hide);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        if (isVisible()) updateDismissTimer();
    });
}

void BackgroundColorPicker::updateDismissTimer() {
    QWidget *fw = QApplication::focusWidget();
    bool hasFocus = fw && (fw == this || isAncestorOf(fw));
    if (m_mouseOver || hasFocus)
        m_dismissTimer->stop();
    else
        m_dismissTimer->start();
}

void BackgroundColorPicker::setCurrentValue(int grey) {
    m_slider->setValue(qBound(0, grey, 255));
}

void BackgroundColorPicker::enterEvent(QEnterEvent *event) {
    m_mouseOver = true;
    m_dismissTimer->stop();
    QWidget::enterEvent(event);
}

void BackgroundColorPicker::leaveEvent(QEvent *event) {
    m_mouseOver = false;
    updateDismissTimer();
    QWidget::leaveEvent(event);
}

void BackgroundColorPicker::hideEvent(QHideEvent *event) {
    m_dismissTimer->stop();
    m_mouseOver = false;
    QWidget::hideEvent(event);
}

void BackgroundColorPicker::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 30, 30, 220));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);
}
