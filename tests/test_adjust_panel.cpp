// Tests for the AdjustPanel: parameter round-trips, the two-tab layout, that the
// active tab is remembered across a hide/show cycle, and that slider edits emit
// the matching per-tab signal.
#include <QtTest/QtTest>
#include <QApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include "AdjustPanel.h"

class AdjustPanelTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }

    void params_roundTrip();
    void hasTwoTabs();
    void remembersActiveTab_acrossHideShow();
    void toneEdit_emitsAdjustSignal();
    void colorEdit_emitsColorSignal();
};

void AdjustPanelTest::params_roundTrip() {
    AdjustPanel panel;
    AdjustParams a; a.brightness = 12; a.contrast = -34; a.exposure = 5;
    a.saturation = 60; a.blacks = -10; a.whites = 25;
    panel.setAdjustParams(a);
    AdjustParams got = panel.adjustParams();
    QCOMPARE(got.brightness, 12);
    QCOMPARE(got.contrast, -34);
    QCOMPARE(got.whites, 25);

    ColorParams c; c.temperature = -20; c.tint = 40; c.red = 5; c.green = -5; c.blue = 30;
    c.hues[2] = 55; c.hues[6] = -35;
    panel.setColorParams(c);
    ColorParams gotC = panel.colorParams();
    QCOMPARE(gotC.temperature, -20);
    QCOMPARE(gotC.blue, 30);
    QCOMPARE(gotC.hues[2], 55);
    QCOMPARE(gotC.hues[6], -35);
}

void AdjustPanelTest::hasTwoTabs() {
    AdjustPanel panel;
    panel.setActiveTab(0);
    QCOMPARE(panel.activeTab(), 0);
    panel.setActiveTab(1);
    QCOMPARE(panel.activeTab(), 1);
}

// The headline requirement: the panel reopens on whichever tab was last shown.
void AdjustPanelTest::remembersActiveTab_acrossHideShow() {
    {
        AdjustPanel panel;
        panel.show();
        panel.setActiveTab(1);   // switch to the Color tab
        panel.hide();
        QCOMPARE(panel.activeTab(), 1);   // retained on the live widget
    }
    // …and across a fresh panel (persisted), as when reopened after dismissal.
    AdjustPanel reopened;
    QCOMPARE(reopened.activeTab(), 1);
}

void AdjustPanelTest::toneEdit_emitsAdjustSignal() {
    AdjustPanel panel;
    auto *tabs = panel.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto toneSliders = tabs->widget(0)->findChildren<QSlider *>();
    QCOMPARE(toneSliders.size(), 6);

    QSignalSpy adjustSpy(&panel, &AdjustPanel::adjustParamsChanged);
    QSignalSpy colorSpy(&panel, &AdjustPanel::colorParamsChanged);

    // The silent setter must not emit; a direct slider move must.
    panel.setAdjustParams(AdjustParams{});
    QCOMPARE(adjustSpy.count(), 0);

    toneSliders.first()->setValue(45);   // brightness
    QCOMPARE(adjustSpy.count(), 1);
    QCOMPARE(colorSpy.count(), 0);
    QCOMPARE(adjustSpy.at(0).at(0).value<AdjustParams>().brightness, 45);
}

void AdjustPanelTest::colorEdit_emitsColorSignal() {
    AdjustPanel panel;
    auto *tabs = panel.findChild<QTabWidget *>();
    QVERIFY(tabs);
    auto colorSliders = tabs->widget(1)->findChildren<QSlider *>();
    QCOMPARE(colorSliders.size(), 13);   // 5 balance + 8 hue bands

    QSignalSpy adjustSpy(&panel, &AdjustPanel::adjustParamsChanged);
    QSignalSpy colorSpy(&panel, &AdjustPanel::colorParamsChanged);

    colorSliders.first()->setValue(-30);   // temperature
    QCOMPARE(colorSpy.count(), 1);
    QCOMPARE(adjustSpy.count(), 0);
    QCOMPARE(colorSpy.at(0).at(0).value<ColorParams>().temperature, -30);

    // A hue-band slider (last row) emits the same signal and lands in hues[].
    colorSliders.last()->setValue(70);
    QCOMPARE(colorSpy.count(), 2);
    QCOMPARE(colorSpy.at(1).at(0).value<ColorParams>().hues[7], 70);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    qRegisterMetaType<AdjustParams>();
    qRegisterMetaType<ColorParams>();
    AdjustPanelTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_adjust_panel.moc"
