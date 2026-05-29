#pragma once
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QPixmap>
#include <QString>
#include <Qt>

class BackgroundColorPicker;
class BwPanel;
class HelpOverlay;
class ExitOverlay;
class ImageViewer;
class QResizeEvent;
class QTimer;

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
    void onBwPanelRequested();
    void applyBwConversion();
    void toggleBwCompare();
    void deactivateBw();
    void exitApplication();
    void openFile();

    ImageViewer *m_viewer = nullptr;
    HelpOverlay *m_helpOverlay = nullptr;
    ExitOverlay *m_exitOverlay = nullptr;
    BackgroundColorPicker *m_colorPicker = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
    bool m_forwardingKeyEvent = false;

    BwPanel                *m_bwPanel       = nullptr;
    QPixmap                 m_diskPixmap;   // image as loaded from disk; never modified by crop or BW
    QPixmap                 m_basePixmap;   // disk image with current crop applied; BW source
    QImage                  m_originalImage; // = m_basePixmap.toImage(), cached for BW conversion
    QImage                  m_lastBwImage;
    QPixmap                 m_lastBwPixmap;
    bool                    m_bwActive      = false;
    bool                    m_bwComparing   = false;
    QFutureWatcher<QImage> *m_bwWatcher     = nullptr;
    QTimer                 *m_bwDebounce    = nullptr;
    QTimer                 *m_exitDebounce  = nullptr;
};
