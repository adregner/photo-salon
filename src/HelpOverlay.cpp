#include "HelpOverlay.h"
#include <QFontDatabase>
#include <QPainter>

HelpOverlay::HelpOverlay(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void HelpOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 128));

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(24);
    p.setFont(font);
    p.setPen(Qt::white);

    const QString text =
        "Keyboard Shortcuts\n"
        "\n"
        "  →    Next image in folder\n"
        "  ←    Previous image in folder\n"
        "  +    Zoom in\n"
        "  -    Zoom out\n"
        "  0    Fit to window\n"
        "  ?    Show/hide this help\n"
        "\n"
        "Mouse & Other Controls\n"
        "\n"
        "  Scroll wheel    Zoom (anchored to cursor)";

    // Left-align at 1/3 width so monospace columns stay intact; center vertically
    QRect drawArea(width() / 3, 0, width() * 2 / 3, height());
    p.drawText(drawArea, Qt::AlignLeft | Qt::AlignVCenter, text);
}
