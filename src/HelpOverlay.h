#pragma once
#include <QWidget>

class QPaintEvent;

class HelpOverlay : public QWidget {
public:
    explicit HelpOverlay(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
};
