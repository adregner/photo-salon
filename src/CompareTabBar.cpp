#include "CompareTabBar.h"
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QPalette>
#include <QToolButton>

CompareTabBar::CompareTabBar(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1e));
    setPalette(pal);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 2, 4, 2);
    m_layout->setSpacing(4);
    m_layout->addStretch(1);   // tabs are left-aligned
}

void CompareTabBar::styleTab(int index, bool focused) {
    if (index < 0 || index >= m_tabs.size()) return;
    // Focused tab: lighter grey. Others: darker.
    const QString bg = focused ? QStringLiteral("#5a5a5a") : QStringLiteral("#2e2e2e");
    m_tabs.at(index)->setStyleSheet(QStringLiteral(
        "QFrame#compareTab { background-color: %1; border-radius: 3px; }"
        "QToolButton { color: #e6e6e6; border: none; background: transparent; padding: 2px 4px; }"
        "QToolButton:hover { color: #ffffff; }").arg(bg));
}

void CompareTabBar::setTabs(const QStringList &names, int focusedIndex) {
    // Rebuild from scratch. This is only ever called when the *set* of open
    // images changes (open / close / navigate), never from inside a tab's own
    // click handler, so deleting the old widgets immediately is safe — and it
    // keeps the index→widget mapping trivially correct for tests.
    for (QFrame *tab : m_tabs)
        delete tab;
    m_tabs.clear();

    for (int i = 0; i < names.size(); ++i) {
        auto *tab = new QFrame(this);
        tab->setObjectName(QStringLiteral("compareTab"));

        auto *row = new QHBoxLayout(tab);
        row->setContentsMargins(6, 2, 4, 2);
        row->setSpacing(2);

        auto *name = new QToolButton(tab);
        name->setObjectName(QStringLiteral("tabName"));
        name->setText(names.at(i));
        name->setCursor(Qt::PointingHandCursor);
        name->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(name, &QToolButton::clicked, this, [this, i] { emit tabSelected(i); });
        row->addWidget(name);

        auto *close = new QToolButton(tab);
        close->setObjectName(QStringLiteral("tabClose"));
        close->setText(QStringLiteral("✕"));
        close->setCursor(Qt::PointingHandCursor);
        close->setToolTip(QStringLiteral("Close this image"));
        connect(close, &QToolButton::clicked, this, [this, i] { emit tabClosed(i); });
        row->addWidget(close);

        m_layout->insertWidget(i, tab);
        m_tabs.append(tab);
    }

    setFocusedIndex(focusedIndex);
}

void CompareTabBar::setFocusedIndex(int focusedIndex) {
    m_focused = focusedIndex;
    for (int i = 0; i < m_tabs.size(); ++i)
        styleTab(i, i == focusedIndex);
}
