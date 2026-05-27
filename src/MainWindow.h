#pragma once
#include <QMainWindow>
#include <QString>
#include <Qt>

class HelpOverlay;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

public slots:
    void toggleFullscreen();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    HelpOverlay *m_helpOverlay = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
};
