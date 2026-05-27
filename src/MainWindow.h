#pragma once
#include <QMainWindow>
#include <QString>
#include <Qt>

class BackgroundColorPicker;
class HelpOverlay;
class ImageViewer;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

public slots:
    void toggleFullscreen();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    ImageViewer *m_viewer = nullptr;
    HelpOverlay *m_helpOverlay = nullptr;
    BackgroundColorPicker *m_colorPicker = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
};
