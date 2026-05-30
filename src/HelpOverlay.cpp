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
    constexpr int maxFontSize = 24;
    font.setPointSize(maxFontSize);
    p.setFont(font);
    p.setPen(Qt::white);

    const QString text =
        "Keyboard Shortcuts\n"
        "\n"
        "  View:\n"
        "\n"
        "  +    Zoom in\n"
        "  -    Zoom out\n"
        "  0    Fit to window\n"
        "  F    Toggle fullscreen\n"
        "  B    Background color picker\n"
        "\n"
        "  Edit:\n"
        "\n"
        "  C    Crop mode\n"
        "  W    Black & white conversion\n"
        "  R    Rotate 90° clockwise\n"
        "  H    Flip horizontal\n"
        "  V    Flip vertical\n"
        "  S    Save current image\n"
        "  \\    Compare original image\n"
        "  E    EXIF metadata\n"
        "\n"
        "  Navigate:\n"
        "\n"
        "  →    Next image in folder\n"
        "  ←    Previous image in folder\n"
        "  O    Open file\n"
        "\n"
        "  Escape   Dismiss fullscreen / current panel\n"
        "  Q        Quit (press twice)\n"
        "  ?        Show/hide this help\n"
        "\n"
        "Mouse Controls\n"
        "\n"
        "  Scroll wheel    Zoom (anchored to cursor)";

    // Left-align at 1/3 width so monospace columns stay intact; center vertically
    QRect drawArea(width() / 3, 0, width() * 2 / 3, height());

    // Scale font down if all lines won't fit vertically at the max size
    QRect textBounds = p.boundingRect(drawArea, Qt::AlignLeft | Qt::AlignTop, text);
    if (textBounds.height() > height()) {
        int scaledSize = maxFontSize * height() / textBounds.height();
        font.setPointSize(qMax(1, scaledSize));
        p.setFont(font);
    }

    p.drawText(drawArea, Qt::AlignLeft | Qt::AlignVCenter, text);
}
