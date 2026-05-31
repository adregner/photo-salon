#include "ExifOverlay.h"
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>

ExifOverlay::ExifOverlay(QWidget *parent)
    : QWidget(parent)
    , m_template(defaultTemplate())
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

// static
QStringList ExifOverlay::defaultTemplate()
{
    return {
        "{Camera}",
        "{LensModel}",
        "{DateTime}",
        "",
        "{Exposure}",
        "{FocalLength}",
        "{ExposureBias}",
        "Program: {ExposureProgram}",
        "Metering: {MeteringMode}",
        "Flash: {Flash}",
        "",
        "{GPS}",
        "",
        "{Software}",
        "",
        "{Dimensions}",
        "{FileName}  ·  {FileSize}",
    };
}

void ExifOverlay::setData(const ExifReader::ExifData &data)
{
    m_data = data;
    update();
}

void ExifOverlay::setTemplate(const QStringList &lines)
{
    m_template = lines;
    update();
}

std::optional<QString> ExifOverlay::renderLine(const QString &tmpl) const
{
    if (tmpl.isEmpty())
        return QString{};  // blank separator — always kept

    static const QRegularExpression re(R"(\{([^}]+)\})");

    bool hasPlaceholders = false;
    bool anyResolved     = false;
    QString result       = tmpl;

    // Collect matches first so replacements don't shift indices
    QList<QPair<QString, QString>> subs;
    auto it = re.globalMatch(tmpl);
    while (it.hasNext()) {
        auto m  = it.next();
        hasPlaceholders = true;
        QString val = m_data.value(m.captured(1));
        if (!val.isEmpty()) anyResolved = true;
        subs.append({m.captured(0), val});
    }

    for (const auto &[token, val] : subs)
        result.replace(token, val);

    result = result.trimmed();

    // Omit lines that had placeholders but none resolved to a value
    if (hasPlaceholders && !anyResolved)
        return std::nullopt;

    return result;
}

QStringList ExifOverlay::renderLines() const
{
    QStringList out;
    for (const QString &line : m_template) {
        auto rendered = renderLine(line);
        if (rendered.has_value())
            out << *rendered;
    }
    return out;
}

void ExifOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 180));

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    constexpr int maxFontSize = 18;
    font.setPointSize(maxFontSize);
    p.setFont(font);

    const QString title    = "Metadata";
    const QStringList body = renderLines();

    QRect drawArea(width() / 3, 20, width() * 2 / 3 - 20, height() - 40);

    // Scale font down if the full text block won't fit
    QString fullText = title + '\n' + QString(title.length(), '-') + '\n' + body.join('\n');
    QRect bounds = p.boundingRect(drawArea, Qt::AlignLeft | Qt::AlignTop, fullText);
    if (bounds.height() > drawArea.height()) {
        int scaled = maxFontSize * drawArea.height() / bounds.height();
        font.setPointSize(qMax(8, scaled));
        p.setFont(font);
    }

    // Title row
    p.setPen(QColor(180, 220, 255));
    QFontMetrics fm(p.font());
    int lineH = fm.lineSpacing();
    p.drawText(drawArea, Qt::AlignLeft | Qt::AlignTop,
               title + '\n' + QString(title.length(), '-'));

    // Body
    p.setPen(Qt::white);
    QRect bodyRect = drawArea.adjusted(0, lineH * 2, 0, 0);
    p.drawText(bodyRect, Qt::AlignLeft | Qt::AlignTop, body.join('\n'));
}
