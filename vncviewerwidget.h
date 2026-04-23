#ifndef VNCVIEWERWIDGET_H
#define VNCVIEWERWIDGET_H

#include <QWidget>
#include "vncconnection.h"

class VncViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VncViewerWidget(QWidget *parent = nullptr);
    ~VncViewerWidget();

    void startConnection(const QString &host, int port, const QString &password);

signals:
    void connectionLost();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onFrameUpdated();
    void onConnected();
    void onDisconnected();
    void onError(const QString &message);

private:
    void setupContextMenu();
    int mapMouseButton(Qt::MouseButton button) const;
    QPointF mousePos(QMouseEvent *event) const;
    QRect drawRect() const;
    QPointF mapToRemote(QPointF localPos) const;

    VncConnection *m_connection = nullptr;
    bool m_connected = false;
};

#endif // VNCVIEWERWIDGET_H
