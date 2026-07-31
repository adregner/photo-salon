#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// ---------------------------------------------------------------------------
// RotatePanel — the pop-up that accompanies rotate mode (`R`). Styled like the
// other edit panels (a translucent, frameless Tool window), it carries the two
// lossless quarter-turn buttons that used to live on the `R` key, plus a fine
// straightening slider mirroring the angle the corner handles drag out.
//
// Unlike the adjustment panels it never dismisses itself: it is the visible face
// of a mode, so MainWindow shows and hides it with rotate mode.
//
// The panel only reports intent: MainWindow folds a quarter turn into the
// manifest's OrientationEdit and the free angle into its RotateEdit.
// ---------------------------------------------------------------------------
class RotatePanel : public QWidget {
    Q_OBJECT
public:
    explicit RotatePanel(QWidget *parent = nullptr);

    double angle() const;
    // Reflect an angle dragged out on the image without echoing it back.
    void setAngle(double degrees);

    // Exposed so the quarter turns can be driven directly in tests.
    QPushButton *rotateLeftButton()  const { return m_rotateLeft; }
    QPushButton *rotateRightButton() const { return m_rotateRight; }

signals:
    void angleChanged(double degrees);
    void rotateLeftRequested();    // 90° counter-clockwise, lossless
    void rotateRightRequested();   // 90° clockwise, lossless

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void onSliderChanged();
    void updateValueLabel();

    // The slider works in tenths of a degree so it can be dragged finely with
    // integer steps.
    static constexpr int kStepsPerDegree = 10;

    QSlider     *m_slider      = nullptr;
    QLabel      *m_value       = nullptr;
    QPushButton *m_rotateLeft  = nullptr;
    QPushButton *m_rotateRight = nullptr;
    QPushButton *m_resetBtn    = nullptr;
    bool         m_inhibitSignal = false;
};
