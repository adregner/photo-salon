#pragma once
#include <QMainWindow>
#include <QString>

class HelpOverlay;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    HelpOverlay *m_helpOverlay = nullptr;
};
