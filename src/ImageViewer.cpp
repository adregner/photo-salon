#include "ImageViewer.h"
#include "Const.h"
#include "ImageFormats.h"
#include "RotateGeometry.h"
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QPolygon>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// Crop overlay geometry. Grab targets are derived from the border line width so
// the catch area scales with the visual lines: the user can grab an edge from
// ±7.5x the line width away, and corners get a larger square catch radius.
constexpr qreal kCropLineWidth  = 2.0;                       // border stroke (viewport px)
constexpr int   kCropEdgeGrab   = int(7.5 * kCropLineWidth); // edge grab distance (= 15)
constexpr int   kCropCornerGrab = int(12  * kCropLineWidth); // corner grab distance (= 24)
constexpr int   kCropHandleSize = 12;                        // corner/edge handle square (px)

// --- Rotate cursor -------------------------------------------------------
// Qt has no rotate cursor, so draw one: a curved double-headed arrow whose two
// heads point along the two directions a drag on that corner can travel. The
// icon is drawn with its "outward" axis pointing up and then turned to face away
// from the image centre, which is what makes it read as belonging to the corner
// it is hovering over.
constexpr int kRotateCursorSize = 32;

void drawArrowHead(QPainter &p, const QPointF &tip, const QPointF &direction) {
    const double len = std::hypot(direction.x(), direction.y());
    if (len <= 0.0) return;
    const QPointF d(direction.x() / len, direction.y() / len);
    const QPointF n(-d.y(), d.x());          // unit normal
    constexpr double kLength = 6.5, kHalfWidth = 4.2;
    QPolygonF head;
    head << tip
         << tip - d * kLength + n * kHalfWidth
         << tip - d * kLength - n * kHalfWidth;
    p.drawPolygon(head);
}

QPixmap rotateCursorPixmap(double degrees) {
    QPixmap px(kRotateCursorSize, kRotateCursorSize);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    const double c = kRotateCursorSize / 2.0;
    p.translate(c, c);
    p.rotate(degrees);
    p.translate(-c, -c);

    // An arc bowing around a pivot below the icon — i.e. around the image centre
    // once the icon has been turned to face outward.
    constexpr double kPivotY = 30.0, kRadius = 18.0;
    constexpr double kStartDeg = 35.0, kEndDeg = 145.0;
    const QRectF arcBox(c - kRadius, kPivotY - kRadius, 2 * kRadius, 2 * kRadius);

    // Each end of the arc, and the tangent that continues past it — the two
    // directions a drag on this corner travels.
    auto endpoint = [&](double deg) {
        const double r = qDegreesToRadians(deg);
        return QPointF(c + kRadius * std::cos(r), kPivotY - kRadius * std::sin(r));
    };
    auto outwardTangent = [&](double deg, double sign) {
        const double r = qDegreesToRadians(deg);
        return QPointF(sign * std::sin(r), sign * std::cos(r));
    };

    // Draw a dark outline first, then the white icon on top, so the cursor stays
    // legible over both a bright photograph and the dimmed surround.
    for (int pass = 0; pass < 2; ++pass) {
        const QColor ink = pass == 0 ? QColor(0, 0, 0, 210) : QColor(Qt::white);
        p.setPen(QPen(ink, pass == 0 ? 6.0 : 3.2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(arcBox, int(kStartDeg * 16), int((kEndDeg - kStartDeg) * 16));
        p.setPen(QPen(ink, pass == 0 ? 3.0 : 0.0));
        p.setBrush(ink);
        drawArrowHead(p, endpoint(kStartDeg), outwardTangent(kStartDeg,  1.0));
        drawArrowHead(p, endpoint(kEndDeg),   outwardTangent(kEndDeg,   -1.0));
    }
    return px;
}

QCursor rotateCursor(double degrees) {
    // Quantize so small pointer movements along a corner don't rebuild the
    // pixmap on every mouse move.
    const int key = ((static_cast<int>(std::lround(degrees / 5.0)) * 5) % 360 + 360) % 360;
    static QHash<int, QCursor> cache;
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    return *cache.insert(key, QCursor(rotateCursorPixmap(key),
                                      kRotateCursorSize / 2, kRotateCursorSize / 2));
}
}  // namespace

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    // ScrollHandDrag would otherwise keep an open-hand cursor over the image at
    // all times. Keep a plain arrow at rest; the closed hand appears only while
    // actively dragging to pan (restored on release in mouseReleaseEvent).
    viewport()->setCursor(Qt::ArrowCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(Qt::black);
    // Tab/Backtab are intercepted by Qt's focus machinery at the viewport and never
    // forwarded to keyPressEvent via the normal QAbstractScrollArea path; handle them
    // here before the focus machinery sees the event.
    viewport()->installEventFilter(this);

    // ScrollHandDrag pans by moving the (hidden) scrollbars; mirror those into a
    // viewChanged() so side-by-side mode can keep the two images in sync.
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { emitViewChanged(); });
    connect(verticalScrollBar(),   &QScrollBar::valueChanged, this, [this] { emitViewChanged(); });

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    m_noticeTimer->setInterval(NOTICE_DURATION);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] {
        m_noticeVisible = false;
        viewport()->update();
    });

    loadImage(imagePath);
}

void ImageViewer::loadImage(const QString &path) {
    m_scene->clear();
    m_pixmapItem = nullptr;
    m_imagePath = path;
    m_cropRect = QRectF();
    m_cropIsAuto = true;
    m_rotateAngle = 0.0;
    m_overlay = OverlayMode::None;
    m_activeHandle = CropHandle::None;
    m_rotating = false;
    m_cropBasePixmap = {};

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QPixmap pixmap = QPixmap::fromImage(reader.read());
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(path));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_nativeSize = pixmap.size();
        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        m_fitted = true;
        fitImage();
    }
    if (!path.isEmpty() && m_helpVisible) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }
    emit imagePathChanged(m_imagePath);
}

QPixmap ImageViewer::pixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

QPixmap ImageViewer::currentDisplayPixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

void ImageViewer::fitToWindow() {
    m_fitted = true;
    fitImage();
}

void ImageViewer::navigate(int delta) {
    if (m_imagePath.isEmpty()) return;
    QFileInfo info(m_imagePath);
    QDir dir = info.absoluteDir();
    QStringList exts = supportedExtensions();
    exts.removeAll("*.svg");
    QStringList files = dir.entryList(exts, QDir::Files, QDir::Name);
    int idx = files.indexOf(info.fileName());
    if (idx < 0 || files.isEmpty()) return;
    int next = ((idx + delta) % files.size() + files.size()) % files.size();
    loadImage(dir.absoluteFilePath(files[next]));
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_fitted) fitImage();
}

void ImageViewer::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (m_fitted && isVisible()) fitImage();
}

void ImageViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->ignore(); return; }
    emit focusRequested();   // interacting with a pane focuses it (side-by-side)
    applyZoom(std::pow(1.15, delta / 120.0));
    event->accept();
}

void ImageViewer::keyPressEvent(QKeyEvent *event) {
    if (m_helpVisible && event->key() != Qt::Key_Question) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }

    switch (event->key()) {
    case Qt::Key_Left:
        navigate(-1);
        event->accept();
        break;
    case Qt::Key_Right:
        navigate(1);
        event->accept();
        break;
    case Qt::Key_Question:
        m_helpVisible = !m_helpVisible;
        emit helpVisibilityChanged(m_helpVisible);
        event->accept();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        applyZoom(1.15);
        event->accept();
        break;
    case Qt::Key_Minus:
        applyZoom(1.0 / 1.15);
        event->accept();
        break;
    case Qt::Key_0:
        m_fitted = true;
        fitImage();
        event->accept();
        break;
    case Qt::Key_Tab:
        emit folderBrowseRequested();
        event->accept();
        break;
    case Qt::Key_F:
        emit fullscreenToggleRequested();
        event->accept();
        break;
    case Qt::Key_B:
        emit backgroundPickerRequested();
        event->accept();
        break;
    case Qt::Key_C:
        emit adjustPanelRequested();
        event->accept();
        break;
    case Qt::Key_X:
        setCropMode(!cropMode());
        event->accept();
        break;
    case Qt::Key_S:
        emit saveRequested();
        event->accept();
        break;
    case Qt::Key_W:
        emit bwPanelRequested();
        event->accept();
        break;
    case Qt::Key_R:
        setRotateMode(!rotateMode());
        event->accept();
        break;
    case Qt::Key_H:
        emit flipHorizontalRequested();
        event->accept();
        break;
    case Qt::Key_V:
        emit flipVerticalRequested();
        event->accept();
        break;
    case Qt::Key_Q:
        emit exitRequested();
        event->accept();
        break;
    case Qt::Key_Backslash:
        emit bwCompareRequested();
        event->accept();
        break;
    case Qt::Key_O:
        if (event->modifiers() & Qt::ShiftModifier)
            emit compareOpenRequested();
        else
            emit openFileRequested();
        event->accept();
        break;
    case Qt::Key_I:
        emit exifRequested();
        event->accept();
        break;
    case Qt::Key_P:
        // On macOS Qt maps physical Ctrl → MetaModifier; Cmd → ControlModifier.
        // Check MetaModifier on macOS so Ctrl+P means the physical Control key everywhere.
#ifdef Q_OS_MACOS
        if (event->modifiers() & Qt::MetaModifier)
#else
        if (event->modifiers() & Qt::ControlModifier)
#endif
            emit openExternalPickerRequested();
        else
            emit openExternalRequested(event->modifiers() & Qt::ShiftModifier);
        event->accept();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0) return;
    scale(factor, factor);
    m_fitted = false;
    emitViewChanged();
}

double ImageViewer::fitScale() const {
    const QRectF sr = m_scene->sceneRect();
    const QSize vp = viewport()->size();
    if (sr.width() <= 0 || sr.height() <= 0 || vp.isEmpty())
        return 1.0;
    return std::min(vp.width() / sr.width(), vp.height() / sr.height());
}

double ImageViewer::relativeZoom() const {
    const double fit = fitScale();
    return fit > 0 ? transform().m11() / fit : 1.0;
}

QPointF ImageViewer::relativeCenter() const {
    const QRectF sr = m_scene->sceneRect();
    if (sr.width() <= 0 || sr.height() <= 0)
        return QPointF(0.5, 0.5);
    const QPointF c = mapToScene(viewport()->rect().center());
    return QPointF((c.x() - sr.left()) / sr.width(),
                   (c.y() - sr.top())  / sr.height());
}

void ImageViewer::applyRelativeView(double relZoom, const QPointF &relCenter) {
    const QRectF sr = m_scene->sceneRect();
    if (sr.width() <= 0 || sr.height() <= 0)
        return;
    m_suppressViewChanged = true;
    const double target = relZoom * fitScale();
    QTransform t;
    t.scale(target, target);
    setTransform(t);
    centerOn(sr.left() + relCenter.x() * sr.width(),
             sr.top()  + relCenter.y() * sr.height());
    m_fitted = false;
    m_suppressViewChanged = false;
}

void ImageViewer::emitViewChanged() {
    if (!m_suppressViewChanged)
        emit viewChanged();
}

void ImageViewer::setHelpVisible(bool visible) {
    if (m_helpVisible == visible) return;
    m_helpVisible = visible;
    emit helpVisibilityChanged(visible);
}

void ImageViewer::closeHelp() {
    if (m_helpVisible) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }
}

bool ImageViewer::event(QEvent *event) {
    // QAbstractScrollArea::event() intercepts Tab and redirects it to the viewport
    // via a path that bypasses our keyPressEvent override. Intercept here first.
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return ke->isAccepted();
        }
    }
    return QGraphicsView::event(event);
}

bool ImageViewer::focusNextPrevChild(bool) {
    return false;
}

bool ImageViewer::eventFilter(QObject *obj, QEvent *event) {
    if (obj == viewport() && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return ke->isAccepted();
        }
    }
    return QGraphicsView::eventFilter(obj, event);
}

void ImageViewer::setCropMode(bool active) {
    setOverlayMode(active ? OverlayMode::Crop : OverlayMode::None);
}

void ImageViewer::setRotateMode(bool active) {
    setOverlayMode(active ? OverlayMode::Rotate : OverlayMode::None);
}

void ImageViewer::setOverlayMode(OverlayMode mode) {
    if (m_overlay == mode) return;
    const bool wasCrop     = cropMode();
    const bool wasRotate   = rotateMode();
    const OverlayMode from = m_overlay;
    m_overlay = mode;

    if (from == OverlayMode::None) {
        enterOverlay();
    } else if (mode == OverlayMode::None) {
        exitOverlay();
    } else {
        // Crop ⇄ rotate: same box over the same image, only the meaning of a
        // drag changes. Nothing is applied, so nothing is re-derived.
        m_activeHandle = CropHandle::None;
        m_rotating = false;
        viewport()->setCursor(Qt::ArrowCursor);
        viewport()->update();
    }

    if (mode != OverlayMode::None) {
        showOverlayNotice();
    } else {
        m_noticeTimer->stop();
        m_noticeVisible = false;
    }

    if (cropMode()   != wasCrop)   emit cropModeChanged(cropMode());
    if (rotateMode() != wasRotate) emit rotateModeChanged(rotateMode());
}

void ImageViewer::enterOverlay() {
    if (!m_pixmapItem) return;

    // Work from the in-memory base when we have one (it already carries the
    // orientation edit); fall back to the file only when nothing has set it.
    if (m_cropBasePixmap.isNull()) {
        QPixmap disk(m_imagePath);
        if (!disk.isNull()) m_cropBasePixmap = disk;
    }
    const QPixmap base = !m_cropBasePixmap.isNull() ? m_cropBasePixmap
                                                    : m_pixmapItem->pixmap();
    if (!base.isNull()) {
        m_pixmapItem->setPixmap(base);
        m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    }

    syncOverlayGeometry();

    // Arm the selection: an untouched image starts at the largest rectangle the
    // current angle allows (the whole frame when it isn't rotated), a stored one
    // is carried in and pulled inside the tilted bounds.
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) {
        m_cropRect = RotateGeometry::largestInscribedRect(overlayBaseSize(), m_rotateAngle);
        m_cropIsAuto = true;
    } else {
        const QRectF fitted = RotateGeometry::shrinkToFit(
            m_cropRect.intersected(rotatedBoundsRect()), rotateBounds());
        if (fitted.width() >= 1.0 && fitted.height() >= 1.0)
            m_cropRect = fitted;
    }

    m_fitted = true;
    fitImage();

    m_activeHandle = CropHandle::None;
    m_rotating = false;
    setDragMode(QGraphicsView::NoDrag);
    viewport()->setCursor(Qt::ArrowCursor);
}

void ImageViewer::exitOverlay() {
    m_noticeTimer->stop();
    m_noticeVisible = false;

    // Bake the overlay's rotation and selection into the shown pixmap. MainWindow
    // re-derives the same result from the manifest at full quality straight
    // after; doing it here keeps the viewer correct on its own.
    if (m_pixmapItem) {
        const QPixmap base = m_pixmapItem->pixmap();
        m_pixmapItem->setTransform(QTransform());

        QPixmap rotated = base;
        if (!base.isNull() && !RotateGeometry::isZeroAngle(m_rotateAngle))
            rotated = base.transformed(QTransform().rotate(m_rotateAngle),
                                       Qt::SmoothTransformation);

        const QRectF bounds(QPointF(0, 0), QSizeF(rotated.size()));
        const QRectF selection = (m_cropRect.isValid() && !m_cropRect.isEmpty())
            ? m_cropRect.intersected(bounds)
            : bounds;
        const QRect selectionPx = selection.toAlignedRect().intersected(rotated.rect());
        m_pixmapItem->setPixmap(selectionPx.isEmpty() ? rotated : rotated.copy(selectionPx));

        m_scene->setSceneRect(m_pixmapItem->pixmap().rect());
        m_fitted = true;
        fitImage();
    }

    m_activeHandle = CropHandle::None;
    m_rotating = false;
    setDragMode(QGraphicsView::ScrollHandDrag);
    viewport()->setCursor(Qt::ArrowCursor);
}

void ImageViewer::syncOverlayGeometry() {
    if (!m_pixmapItem || !overlayActive()) return;
    const QSize base = m_pixmapItem->pixmap().size();
    if (base.isEmpty()) return;

    if (RotateGeometry::isZeroAngle(m_rotateAngle)) {
        m_pixmapItem->setTransform(QTransform());
        m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(base)));
    } else {
        // trueMatrix is the transform QImage/QPixmap use to rotate: it includes
        // the shift that lands the result's top-left on the origin, so the item
        // and the selection share one coordinate space.
        m_pixmapItem->setTransform(QImage::trueMatrix(QTransform().rotate(m_rotateAngle),
                                                      base.width(), base.height()));
        m_scene->setSceneRect(QRectF(QPointF(0, 0),
                                     RotateGeometry::boundingSize(base, m_rotateAngle)));
    }
    if (m_fitted) fitImage();
}

void ImageViewer::showOverlayNotice() {
    bool &shown = cropMode() ? m_cropNoticeShown : m_rotateNoticeShown;
    if (shown) {
        m_noticeTimer->stop();
        m_noticeVisible = false;
        return;
    }
    shown = true;
    m_noticeVisible = true;
    m_noticeTimer->start();
}

QSize ImageViewer::overlayBaseSize() const {
    if (!m_cropBasePixmap.isNull()) return m_cropBasePixmap.size();
    return m_pixmapItem ? m_pixmapItem->pixmap().size() : QSize();
}

QRectF ImageViewer::rotatedBoundsRect() const {
    return QRectF(QPointF(0, 0), RotateGeometry::boundingSize(overlayBaseSize(), m_rotateAngle));
}

QPolygonF ImageViewer::rotateBounds() const {
    return RotateGeometry::rotatedBounds(overlayBaseSize(), m_rotateAngle);
}

void ImageViewer::setRotateAngle(double degrees) {
    const double angle = RotateGeometry::clampAngle(degrees);
    if (qFuzzyCompare(angle + 1.0, m_rotateAngle + 1.0)) return;
    const double from = m_rotateAngle;
    m_rotateAngle = angle;

    // The selection is expressed in the rotated image's coordinates, so it has
    // to follow the angle — and must never end up over a blank corner.
    const QSize base = overlayBaseSize();
    if (!base.isEmpty()) {
        if (m_cropIsAuto || !m_cropRect.isValid() || m_cropRect.isEmpty()) {
            m_cropRect = RotateGeometry::largestInscribedRect(base, m_rotateAngle);
            m_cropIsAuto = true;
        } else {
            const QRectF moved = RotateGeometry::remapBetweenAngles(m_cropRect, base, from, m_rotateAngle);
            if (moved.width() >= 1.0 && moved.height() >= 1.0)
                m_cropRect = moved;
        }
    }

    syncOverlayGeometry();
    if (overlayActive()) viewport()->update();
    emit rotateAngleChanged(m_rotateAngle);
}

void ImageViewer::setCropRect(const QRectF &rect) {
    if (!m_pixmapItem) return;
    // Clamp against the rotated image's bounds — which is the full oriented
    // original when nothing is rotated. This is also how MainWindow stores a
    // transformed selection while no overlay is open and the item shows only
    // the already-cropped display image.
    QRectF clamped = rect.intersected(rotatedBoundsRect());
    if (!RotateGeometry::isZeroAngle(m_rotateAngle))
        clamped = RotateGeometry::shrinkToFit(clamped, rotateBounds());
    m_cropRect = clamped;
    m_cropIsAuto = false;
    if (overlayActive()) viewport()->update();
}

ImageViewer::CropHandle ImageViewer::hitTestHandle(const QPoint &vp) const {
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) return CropHandle::None;

    QRect vr = mapFromScene(m_cropRect).boundingRect();
    const int E = kCropEdgeGrab;   // edge grab distance in viewport pixels
    const int C = kCropCornerGrab; // corner grab distance in viewport pixels

    // Corners take priority
    if (qAbs(vp.x() - vr.left())  <= C && qAbs(vp.y() - vr.top())    <= C) return CropHandle::TopLeft;
    if (qAbs(vp.x() - vr.right()) <= C && qAbs(vp.y() - vr.top())    <= C) return CropHandle::TopRight;
    if (qAbs(vp.x() - vr.left())  <= C && qAbs(vp.y() - vr.bottom()) <= C) return CropHandle::BottomLeft;
    if (qAbs(vp.x() - vr.right()) <= C && qAbs(vp.y() - vr.bottom()) <= C) return CropHandle::BottomRight;

    // Edges (grab anywhere along the edge, not just the midpoint handle)
    if (qAbs(vp.y() - vr.top())    <= E && vp.x() > vr.left() && vp.x() < vr.right()) return CropHandle::Top;
    if (qAbs(vp.y() - vr.bottom()) <= E && vp.x() > vr.left() && vp.x() < vr.right()) return CropHandle::Bottom;
    if (qAbs(vp.x() - vr.left())   <= E && vp.y() > vr.top()  && vp.y() < vr.bottom()) return CropHandle::Left;
    if (qAbs(vp.x() - vr.right())  <= E && vp.y() > vr.top()  && vp.y() < vr.bottom()) return CropHandle::Right;

    // Interior → move
    if (vr.contains(vp)) return CropHandle::Move;

    return CropHandle::None;
}

bool ImageViewer::isCorner(CropHandle h) {
    return h == CropHandle::TopLeft || h == CropHandle::TopRight
        || h == CropHandle::BottomLeft || h == CropHandle::BottomRight;
}

double ImageViewer::pointerAngle(const QPoint &viewportPos) const {
    const QPointF pivot = mapFromScene(sceneRect().center());
    const QPointF v = QPointF(viewportPos) - pivot;
    if (qFuzzyIsNull(v.x()) && qFuzzyIsNull(v.y())) return 0.0;
    return qRadiansToDegrees(std::atan2(v.y(), v.x()));
}

QCursor ImageViewer::rotateCursorFor(CropHandle handle) const {
    const QRect box = mapFromScene(m_cropRect).boundingRect();
    QPoint corner;
    switch (handle) {
    case CropHandle::TopLeft:     corner = box.topLeft();     break;
    case CropHandle::TopRight:    corner = box.topRight();    break;
    case CropHandle::BottomLeft:  corner = box.bottomLeft();  break;
    case CropHandle::BottomRight: corner = box.bottomRight(); break;
    default: return QCursor(Qt::ArrowCursor);
    }
    // Turn the icon so its arc faces away from the centre; its two arrowheads
    // then lie along the two ways this corner can travel.
    const QPointF outward = QPointF(corner) - QPointF(box.center());
    return rotateCursor(qRadiansToDegrees(std::atan2(outward.x(), -outward.y())));
}

void ImageViewer::updateOverlayCursor(const QPoint &viewportPos) {
    const CropHandle handle = hitTestHandle(viewportPos);

    // In rotate mode only the corners do anything, and they turn the image.
    if (rotateMode()) {
        viewport()->setCursor(isCorner(handle) ? rotateCursorFor(handle)
                                               : QCursor(Qt::ArrowCursor));
        return;
    }

    switch (handle) {
    case CropHandle::None:        viewport()->setCursor(Qt::ArrowCursor); break;
    case CropHandle::Move:        viewport()->setCursor(Qt::SizeAllCursor); break;
    case CropHandle::TopLeft:
    case CropHandle::BottomRight: viewport()->setCursor(Qt::SizeFDiagCursor); break;
    case CropHandle::TopRight:
    case CropHandle::BottomLeft:  viewport()->setCursor(Qt::SizeBDiagCursor); break;
    case CropHandle::Top:
    case CropHandle::Bottom:      viewport()->setCursor(Qt::SizeVerCursor); break;
    case CropHandle::Left:
    case CropHandle::Right:       viewport()->setCursor(Qt::SizeHorCursor); break;
    }
}

void ImageViewer::mousePressEvent(QMouseEvent *event) {
    emit focusRequested();   // interacting with a pane focuses it (side-by-side)
    if (!overlayActive()) { QGraphicsView::mousePressEvent(event); return; }
    if (event->button() == Qt::LeftButton) {
        const CropHandle handle = hitTestHandle(event->pos());
        if (rotateMode()) {
            // Only a corner starts a rotation; the angle is tracked as the
            // pointer's own angle around the image centre.
            m_rotating = isCorner(handle);
            m_activeHandle = CropHandle::None;
            if (m_rotating) {
                m_rotateDragStartAngle   = m_rotateAngle;
                m_rotateDragStartPointer = pointerAngle(event->pos());
            }
        } else {
            m_activeHandle = handle;
            m_dragStartScene = mapToScene(event->pos());
            m_dragStartCropRect = m_cropRect;
        }
    }
    event->accept();
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event) {
    if (!overlayActive()) { QGraphicsView::mouseMoveEvent(event); return; }

    if (m_rotating && (event->buttons() & Qt::LeftButton)) {
        double delta = pointerAngle(event->pos()) - m_rotateDragStartPointer;
        while (delta >  180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        setRotateAngle(m_rotateDragStartAngle + delta);
        event->accept();
        return;
    }

    if ((event->buttons() & Qt::LeftButton) && m_activeHandle != CropHandle::None) {
        const QPointF delta = mapToScene(event->pos()) - m_dragStartScene;
        const QRectF start = m_dragStartCropRect;
        // The box may only ever cover the photograph itself, never the blank
        // corners a rotation opened up. On an upright image this polygon is the
        // image's own rectangle, so the same code clamps against its sides.
        const QPolygonF bounds = rotateBounds();
        QRectF r = start;

        if (m_activeHandle == CropHandle::Move) {
            // Moving never resizes: the box keeps its size and just stops when
            // it reaches an edge.
            r = RotateGeometry::slideInside(start, delta, bounds);
        } else {
            // Resizing moves only the sides meeting the dragged handle, so the
            // opposite corner is the anchor and stays exactly where it is.
            QPointF anchor, corner;
            bool freeX = true, freeY = true;
            switch (m_activeHandle) {
            case CropHandle::TopLeft:     anchor = start.bottomRight(); corner = start.topLeft();     break;
            case CropHandle::TopRight:    anchor = start.bottomLeft();  corner = start.topRight();    break;
            case CropHandle::BottomLeft:  anchor = start.topRight();    corner = start.bottomLeft();  break;
            case CropHandle::BottomRight: anchor = start.topLeft();     corner = start.bottomRight(); break;
            case CropHandle::Left:        anchor = start.bottomRight(); corner = start.topLeft();     freeY = false; break;
            case CropHandle::Right:       anchor = start.topLeft();     corner = start.bottomRight(); freeY = false; break;
            case CropHandle::Top:         anchor = start.bottomRight(); corner = start.topLeft();     freeX = false; break;
            case CropHandle::Bottom:      anchor = start.topLeft();     corner = start.bottomRight(); freeX = false; break;
            default: break;
            }
            r = RotateGeometry::resizeInside(anchor, corner, corner + delta, freeX, freeY, bounds);
        }

        if (r.width() >= 1.0 && r.height() >= 1.0) {
            m_cropRect = r;
            m_cropIsAuto = false;
        }

        viewport()->update();
    } else {
        updateOverlayCursor(event->pos());
    }

    event->accept();
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event) {
    if (!overlayActive()) {
        QGraphicsView::mouseReleaseEvent(event);
        // ScrollHandDrag restores an open-hand cursor here; force it back to a
        // plain arrow so the hand is only ever shown mid-pan.
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_activeHandle = CropHandle::None;
        m_rotating = false;
        updateOverlayCursor(event->pos());
    }
    event->accept();
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!overlayActive()) { QGraphicsView::mouseDoubleClickEvent(event); return; }

    if (event->button() == Qt::LeftButton && m_pixmapItem
        && hitTestHandle(event->pos()) != CropHandle::None) {
        if (rotateMode()) {
            // Back to square: no tilt, and the whole frame selected again.
            m_cropIsAuto = true;
            setRotateAngle(0.0);
            m_cropRect = RotateGeometry::largestInscribedRect(overlayBaseSize(), 0.0);
        } else {
            m_cropRect = RotateGeometry::largestInscribedRect(overlayBaseSize(), m_rotateAngle);
            m_cropIsAuto = true;
        }
        m_noticeVisible = false;
        m_noticeTimer->stop();
        viewport()->update();
    }
    event->accept();
}

void ImageViewer::drawForeground(QPainter *painter, const QRectF &rect) {
    QGraphicsView::drawForeground(painter, rect);

    if (!overlayActive() || !m_pixmapItem) return;

    QRectF imageRect = rotatedBoundsRect();
    QRectF crop = (m_cropRect.isValid() && !m_cropRect.isEmpty()) ? m_cropRect : imageRect;

    // Overlay the four excluded regions in scene coordinates
    QColor overlay(0, 0, 0, 160);
    painter->setBrush(overlay);
    painter->setPen(Qt::NoPen);

    auto fill = [&](const QRectF &r) { if (r.width() > 0 && r.height() > 0) painter->drawRect(r); };
    fill({imageRect.left(),  imageRect.top(),  imageRect.width(), crop.top()    - imageRect.top()});
    fill({imageRect.left(),  crop.bottom(),    imageRect.width(), imageRect.bottom() - crop.bottom()});
    fill({imageRect.left(),  crop.top(),       crop.left()  - imageRect.left(), crop.height()});
    fill({crop.right(),      crop.top(),       imageRect.right() - crop.right(), crop.height()});

    // Outline the tilted photograph itself, so the limit the selection is being
    // held inside is visible rather than merely felt.
    if (!RotateGeometry::isZeroAngle(m_rotateAngle)) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 120), 0, Qt::DashLine));
        painter->drawPolygon(rotateBounds());
    }

    // Switch to viewport coordinates for the border and resize handles
    painter->save();
    painter->resetTransform();

    QRect vr = mapFromScene(crop).boundingRect();

    // Border
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::white, kCropLineWidth));
    painter->drawRect(vr);

    // Handles: crop offers corners and edge midpoints to drag; rotate turns on
    // the corners alone, drawn round to say so.
    const int H = kCropHandleSize, H2 = kCropHandleSize / 2;
    painter->setBrush(Qt::white);
    painter->setPen(QPen(Qt::black, 1));

    auto handle = [&](int cx, int cy) {
        if (rotateMode()) painter->drawEllipse(QPoint(cx, cy), H2, H2);
        else              painter->drawRect(cx - H2, cy - H2, H, H);
    };

    handle(vr.left(),  vr.top());
    handle(vr.right(), vr.top());
    handle(vr.left(),  vr.bottom());
    handle(vr.right(), vr.bottom());
    if (!rotateMode()) {
        handle((vr.left() + vr.right()) / 2, vr.top());
        handle((vr.left() + vr.right()) / 2, vr.bottom());
        handle(vr.left(),  (vr.top() + vr.bottom()) / 2);
        handle(vr.right(), (vr.top() + vr.bottom()) / 2);
    }

    if (m_noticeVisible && !vr.isEmpty()) {
        painter->setBrush(QColor(0, 0, 0, 90));
        painter->setPen(Qt::NoPen);
        painter->drawRect(vr);

        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(36);
        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(vr, Qt::AlignCenter,
                          rotateMode() ? QStringLiteral("Drag a corner to rotate")
                                       : QStringLiteral("Double-click to reset crop"));
    }

    painter->restore();
}

void ImageViewer::setDisplayPixmap(const QPixmap &px) {
    if (!m_pixmapItem) return;
    // A display pixmap is always already rendered, so any rotation the overlay
    // left on the item has to go.
    if (!overlayActive())
        m_pixmapItem->setTransform(QTransform());
    m_pixmapItem->setPixmap(px);
    if (px.size() != m_scene->sceneRect().size().toSize()) {
        m_scene->setSceneRect(px.rect());
        if (m_fitted) fitImage();
    }
}

void ImageViewer::setBasePixmapForCrop(const QPixmap &px) {
    m_cropBasePixmap = px;
    // A quarter turn or flip applied from inside the overlay swaps the base out
    // underneath it; show the new one straight away.
    if (overlayActive() && m_pixmapItem && !px.isNull()) {
        m_pixmapItem->setPixmap(px);
        syncOverlayGeometry();
        viewport()->update();
    }
}

void ImageViewer::setBackgroundGrey(int value) {
    m_backgroundGrey = qBound(0, value, 255);
    setBackgroundBrush(QColor(m_backgroundGrey, m_backgroundGrey, m_backgroundGrey));
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty()) {
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        emitViewChanged();
    }
}
