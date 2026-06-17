// Tests for the Black & White panel's controls — in particular that the
// "Compare" button behaves as a proper toggle (regression test).
#include <QtTest/QtTest>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include "BwPanel.h"

class BwPanelTest : public QObject {
    Q_OBJECT

private slots:
    void compareButton_isCheckableToggle();
    void setComparing_keepsButtonEnabled();
    void setComparing_updatesLabel();
    void compareButton_togglesBackAndForth();
};

void BwPanelTest::compareButton_isCheckableToggle() {
    BwPanel panel;
    QPushButton *btn = panel.compareButton();
    QVERIFY(btn);
    QVERIFY(btn->isCheckable());
    QVERIFY(!btn->isChecked());
    QVERIFY(btn->isEnabled());
}

// Regression: the button used to be disabled while comparing, so it could not
// be clicked a second time to toggle back to B&W.
void BwPanelTest::setComparing_keepsButtonEnabled() {
    BwPanel panel;
    QPushButton *btn = panel.compareButton();

    panel.setComparing(true);
    QVERIFY(btn->isChecked());
    QVERIFY(btn->isEnabled());

    panel.setComparing(false);
    QVERIFY(!btn->isChecked());
    QVERIFY(btn->isEnabled());
}

void BwPanelTest::setComparing_updatesLabel() {
    BwPanel panel;
    QPushButton *btn = panel.compareButton();

    const QString offLabel = btn->text();
    panel.setComparing(true);
    QVERIFY(btn->text() != offLabel);   // label changes to reflect the state
    panel.setComparing(false);
    QCOMPARE(btn->text(), offLabel);    // and reverts
}

// End-to-end toggle: mimic MainWindow by feeding compareToggled back into
// setComparing, then verify two clicks produce two alternating signals.
void BwPanelTest::compareButton_togglesBackAndForth() {
    BwPanel panel;
    QPushButton *btn = panel.compareButton();
    connect(&panel, &BwPanel::compareToggled, &panel,
            [&panel](bool showOriginal) { panel.setComparing(showOriginal); });

    QSignalSpy spy(&panel, &BwPanel::compareToggled);

    btn->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    btn->click();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    BwPanelTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bw_panel.moc"
