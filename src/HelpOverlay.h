#pragma once
#include <QString>
#include <QWidget>

class QPaintEvent;

class HelpOverlay : public QWidget {
public:
    explicit HelpOverlay(QWidget *parent = nullptr);
    void setExternalEditorName(const QString &name);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QString m_externalEditorName;
};
