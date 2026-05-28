#include "ExitOverlay.h"
#include <QFontDatabase>
#include <QPainter>

ExitOverlay::ExitOverlay(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void ExitOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 128));

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(36);
    p.setFont(font);
    p.setPen(Qt::white);

    const QString text = "Press \"Q\" again to exit";

    QRect drawArea(width() / 3, 0, width() * 2 / 3, height());
    p.drawText(drawArea, Qt::AlignLeft | Qt::AlignVCenter, text);
}
