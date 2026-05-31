#pragma once
#include "ExifReader.h"
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QPixmap>
#include <QString>
#include <QTransform>
#include <Qt>

class BackgroundColorPicker;
class BwPanel;
class ExifOverlay;
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
    void applyOrientationTransform(const QTransform &t);
    void exitApplication();
    void openFile();
    ExifReader::ExifData imageStateData() const;

    ImageViewer *m_viewer = nullptr;
    HelpOverlay *m_helpOverlay = nullptr;
    ExifOverlay *m_exifOverlay = nullptr;
    ExitOverlay *m_exitOverlay = nullptr;
    BackgroundColorPicker *m_colorPicker = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
    bool m_forwardingKeyEvent = false;

    int                     m_rotationAngle = 0;
    bool                    m_flippedH      = false;
    bool                    m_flippedV      = false;
    bool                    m_cropApplied   = false;

    BwPanel                *m_bwPanel       = nullptr;
    QPixmap                 m_diskPixmap;        // image exactly as loaded from disk; never modified
    QPixmap                 m_orientedDiskPixmap; // m_diskPixmap with current rotation/flip applied; always the full-size crop base
    QPixmap                 m_basePixmap;        // m_orientedDiskPixmap with current crop applied; BW source
    QImage                  m_originalImage; // = m_basePixmap.toImage(), cached for BW conversion
    QImage                  m_lastBwImage;
    QPixmap                 m_lastBwPixmap;
    bool                    m_bwActive      = false;
    bool                    m_bwComparing   = false;
    QFutureWatcher<QImage> *m_bwWatcher     = nullptr;
    QTimer                 *m_bwDebounce    = nullptr;
    QTimer                 *m_exitDebounce  = nullptr;
};
