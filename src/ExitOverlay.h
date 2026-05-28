#pragma once
#include <QWidget>

class QPaintEvent;

class ExitOverlay : public QWidget {
public:
    explicit ExitOverlay(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
};
