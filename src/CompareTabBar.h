#pragma once
#include <QList>
#include <QStringList>
#include <QWidget>

class QFrame;
class QHBoxLayout;

// ---------------------------------------------------------------------------
// CompareTabBar — the minimal tab strip shown at the top of the window while in
// side-by-side compare mode. One tab per open image, labelled with the file
// name. The focused image's tab is drawn a lighter grey; every tab has an "✕"
// to close it (which drops back to single-image mode with the other image).
// Hidden entirely when only one image is open.
// ---------------------------------------------------------------------------
class CompareTabBar : public QWidget {
    Q_OBJECT

public:
    explicit CompareTabBar(QWidget *parent = nullptr);

    // Rebuild the strip: one tab per name, with `focusedIndex` highlighted.
    void setTabs(const QStringList &names, int focusedIndex);
    // Restyle which tab reads as focused, without rebuilding the widgets (safe to
    // call from within a tab's own click handler).
    void setFocusedIndex(int focusedIndex);

signals:
    void tabSelected(int index);   // a tab's name was clicked
    void tabClosed(int index);     // a tab's "✕" was clicked

private:
    void styleTab(int index, bool focused);

    QHBoxLayout    *m_layout = nullptr;
    QList<QFrame *> m_tabs;
    int             m_focused = 0;
};
