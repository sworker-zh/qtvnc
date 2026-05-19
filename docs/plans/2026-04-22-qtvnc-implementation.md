# QtVNC Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a minimal Qt5 VNC Viewer (Qt6-compatible) that displays a remote desktop and forwards mouse events.

**Architecture:** Standalone CMake project linking against libvncclient. VncConnection (QThread subclass) runs the VNC message loop in a background thread. VncViewerWidget renders framebuffer and forwards mouse events. ConnectDialog provides connection UI.

**Tech Stack:** C++17, Qt5/6 Widgets, libvncclient (from LibVNCServer)

---

### Task 1: Project scaffolding (CMakeLists.txt + stub main.cpp)

**Files:**
- Create: `qtvnc/CMakeLists.txt`
- Create: `qtvnc/main.cpp`

**Step 1: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.14)
project(qtvnc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt5 or Qt6
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.12 COMPONENTS Widgets REQUIRED)
endif()
message(STATUS "Using Qt ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.${QT_VERSION_PATCH}")

# LibVNCServer (provides vncclient)
find_package(LibVNCServer QUIET COMPONENTS client)
if(LibVNCServer_FOUND)
    set(VNC_LIBRARIES LibVNCServer::vncclient)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VNCCLIENT REQUIRED IMPORTED_TARGET libvncclient)
    set(VNC_LIBRARIES PkgConfig::VNCCLIENT)
endif()

add_executable(qtvnc
    main.cpp
    connectdialog.cpp
    vncconnection.cpp
    vncviewerwidget.cpp
)

target_link_libraries(qtvnc PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${VNC_LIBRARIES}
)
```

**Step 2: Create stub main.cpp**

```cpp
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    return app.exec();
}
```

**Step 3: Build and verify Qt + libvncclient are found**

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Expected: Compiles without errors (will link since other .cpp are missing at this point — see note below).

Note: The other .cpp files will be created in subsequent tasks. For now, create empty stub files so CMake can proceed:
```bash
touch ../connectdialog.cpp ../vncconnection.cpp ../vncviewerwidget.cpp
```

**Step 4: Commit**

```bash
git add CMakeLists.txt main.cpp connectdialog.cpp vncconnection.cpp vncviewerwidget.cpp
git commit -m "qtvnc: add project scaffolding with CMake and Qt5/6 support"
```

---

### Task 2: VncConnection class

**Files:**
- Create: `qtvnc/vncconnection.h`
- Modify: `qtvnc/vncconnection.cpp` (replace stub)

**Step 1: Create vncconnection.h**

```cpp
#ifndef VNCCONNECTION_H
#define VNCCONNECTION_H

#include <QThread>
#include <QString>
#include <atomic>

struct rfbClient;

class VncConnection : public QThread
{
    Q_OBJECT

public:
    explicit VncConnection(QObject *parent = nullptr);
    ~VncConnection();

    void setHost(const QString &host);
    void setPort(int port);
    void setPassword(const QString &password);
    void stop();
    rfbClient *client() const;

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

protected:
    void run() override;

private:
    static void onFinishedFrameBufferUpdate(rfbClient *cl);
    static char *onGetPassword(rfbClient *cl);

    QString m_host;
    int m_port = 5900;
    QString m_password;
    rfbClient *m_client = nullptr;
    std::atomic<bool> m_running{false};
};

#endif // VNCCONNECTION_H
```

**Step 2: Create vncconnection.cpp**

```cpp
#include "vncconnection.h"
#include <rfb/rfbclient.h>
#include <cstring>

VncConnection::VncConnection(QObject *parent)
    : QThread(parent)
{
}

VncConnection::~VncConnection()
{
    stop();
    wait();
}

void VncConnection::setHost(const QString &host)
{
    m_host = host;
}

void VncConnection::setPort(int port)
{
    m_port = port;
}

void VncConnection::setPassword(const QString &password)
{
    m_password = password;
}

rfbClient *VncConnection::client() const
{
    return m_client;
}

void VncConnection::stop()
{
    m_running = false;
}

void VncConnection::run()
{
    m_client = rfbGetClient(8, 3, 4);

    // Request RGB565 pixel format for better performance
    m_client->format.depth = 16;
    m_client->format.bitsPerPixel = 16;
    m_client->format.redShift = 11;
    m_client->format.greenShift = 5;
    m_client->format.blueShift = 0;
    m_client->format.redMax = 0x1f;
    m_client->format.greenMax = 0x3f;
    m_client->format.blueMax = 0x1f;

    // Encoding preferences
    m_client->appData.encodingsString = "tight ultra hextile zlib corre rre raw";
    m_client->appData.compressLevel = 9;
    m_client->appData.qualityLevel = 5;
    m_client->appData.useRemoteCursor = TRUE;

    // Store 'this' pointer so static callbacks can access the instance
    rfbClientSetClientData(m_client, nullptr, this);

    // Set callbacks
    m_client->FinishedFrameBufferUpdate = onFinishedFrameBufferUpdate;
    if (!m_password.isEmpty()) {
        m_client->GetPassword = onGetPassword;
    }

    // Set connection parameters
    m_client->serverHost = strdup(m_host.toUtf8().constData());
    m_client->serverPort = m_port;

    m_running = true;

    if (!rfbInitClient(m_client, 0, nullptr)) {
        m_client = nullptr;
        emit errorOccurred(tr("Connection failed"));
        return;
    }

    emit connected();

    // Message loop
    while (m_running) {
        int result = WaitForMessage(m_client, 500);
        if (result < 0) {
            emit errorOccurred(tr("Connection lost"));
            break;
        }
        if (result > 0) {
            if (!HandleRFBServerMessage(m_client)) {
                emit errorOccurred(tr("Server disconnected"));
                break;
            }
        }
    }

    rfbClientCleanup(m_client);
    m_client = nullptr;
    m_running = false;
    emit disconnected();
}

void VncConnection::onFinishedFrameBufferUpdate(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (self) {
        emit self->frameUpdated();
    }
}

char *VncConnection::onGetPassword(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (!self || self->m_password.isEmpty()) {
        return nullptr;
    }
    return strdup(self->m_password.toUtf8().constData());
}
```

**Step 3: Build to verify compilation**

```bash
cd build && cmake --build .
```

Expected: Compiles without errors.

**Step 4: Commit**

```bash
git add vncconnection.h vncconnection.cpp
git commit -m "qtvnc: add VncConnection thread wrapper for libvncclient"
```

---

### Task 3: ConnectDialog class

**Files:**
- Create: `qtvnc/connectdialog.h`
- Modify: `qtvnc/connectdialog.cpp` (replace stub)

**Step 1: Create connectdialog.h**

```cpp
#ifndef CONNECTDIALOG_H
#define CONNECTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>

class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(QWidget *parent = nullptr);

    QString host() const;
    int port() const;
    QString password() const;

private:
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpin;
    QLineEdit *m_passwordEdit;
};

#endif // CONNECTDIALOG_H
```

**Step 2: Create connectdialog.cpp**

```cpp
#include "connectdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIntValidator>

ConnectDialog::ConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("VNC Connection"));

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("e.g. 192.168.1.100"));

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(5900);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *formLayout = new QFormLayout(this);
    formLayout->addRow(tr("Host:"), m_hostEdit);
    formLayout->addRow(tr("Port:"), m_portSpin);
    formLayout->addRow(tr("Password:"), m_passwordEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    formLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_hostEdit->text().trimmed().isEmpty()) {
            m_hostEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ConnectDialog::host() const
{
    return m_hostEdit->text().trimmed();
}

int ConnectDialog::port() const
{
    return m_portSpin->value();
}

QString ConnectDialog::password() const
{
    return m_passwordEdit->text();
}
```

**Step 3: Build to verify compilation**

```bash
cd build && cmake --build .
```

Expected: Compiles without errors.

**Step 4: Commit**

```bash
git add connectdialog.h connectdialog.cpp
git commit -m "qtvnc: add ConnectDialog for host/port/password input"
```

---

### Task 4: VncViewerWidget class

**Files:**
- Create: `qtvnc/vncviewerwidget.h`
- Modify: `qtvnc/vncviewerwidget.cpp` (replace stub)

**Step 1: Create vncviewerwidget.h**

```cpp
#ifndef VNCVIEWERWIDGET_H
#define VNCVIEWERWIDGET_H

#include <QWidget>
#include <QImage>
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
#else
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
#endif

private slots:
    void onFrameUpdated();
    void onConnected();
    void onDisconnected();
    void onError(const QString &message);

private:
    void setupContextMenu();
    int mapMouseButton(Qt::MouseButton button) const;
    QPointF mousePos(QMouseEvent *event) const;

    VncConnection *m_connection = nullptr;
    QThread *m_vncThread = nullptr;
    QImage m_image;
    bool m_connected = false;
};

#endif // VNCVIEWERWIDGET_H
```

**Step 2: Create vncviewerwidget.cpp**

```cpp
#include "vncviewerwidget.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <rfb/rfbclient.h>

VncViewerWidget::VncViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setupContextMenu();
}

VncViewerWidget::~VncViewerWidget()
{
    if (m_connection) {
        m_connection->stop();
        m_connection->wait();
        delete m_connection;
    }
}

void VncViewerWidget::setupContextMenu()
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *fullScreenAction = menu.addAction(
            isFullScreen() ? tr("Exit Fullscreen") : tr("Fullscreen"));
        menu.addSeparator();
        QAction *exitAction = menu.addAction(tr("Exit"));

        QAction *chosen = menu.exec(mapToGlobal(pos));
        if (chosen == fullScreenAction) {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
        } else if (chosen == exitAction) {
            close();
        }
    });
}

QPointF VncViewerWidget::mousePos(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

int VncViewerWidget::mapMouseButton(Qt::MouseButton button) const
{
    if (button == Qt::LeftButton)   return rfbButton1Mask;
    if (button == Qt::MiddleButton) return rfbButton2Mask;
    if (button == Qt::RightButton)  return rfbButton3Mask;
    return 0;
}

void VncViewerWidget::startConnection(const QString &host, int port, const QString &password)
{
    m_connection = new VncConnection();
    m_vncThread = new QThread(this);

    m_connection->moveToThread(m_vncThread);

    connect(m_vncThread, &QThread::started, m_connection, [this, host, port, password]() {
        m_connection->setHost(host);
        m_connection->setPort(port);
        m_connection->setPassword(password);
        m_connection->start();
    });

    connect(m_connection, &VncConnection::frameUpdated, this, &VncViewerWidget::onFrameUpdated, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::connected, this, &VncViewerWidget::onConnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::disconnected, this, &VncViewerWidget::onDisconnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::errorOccurred, this, &VncViewerWidget::onError, Qt::QueuedConnection);

    m_vncThread->start();
}

void VncViewerWidget::onFrameUpdated()
{
    update();
}

void VncViewerWidget::onConnected()
{
    m_connected = true;
    if (m_connection && m_connection->client()) {
        resize(m_connection->client()->width, m_connection->client()->height);
    }
}

void VncViewerWidget::onDisconnected()
{
    m_connected = false;
    emit connectionLost();
}

void VncViewerWidget::onError(const QString &message)
{
    m_connected = false;
    QMessageBox::warning(this, tr("VNC Error"), message);
    emit connectionLost();
}

void VncViewerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (!m_connection || !m_connection->client()) {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, tr("Connecting..."));
        return;
    }

    rfbClient *cl = m_connection->client();
    QImage image(cl->frameBuffer, cl->width, cl->height,
                 cl->width * 2, QImage::Format_RGB16);

    QPainter painter(this);
    painter.drawImage(this->rect(), image);
}

void VncViewerWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_connected || !m_connection || !m_connection->client()) return;
    rfbClient *cl = m_connection->client();
    QPointF pos = mousePos(event);
    int x = static_cast<int>(pos.x() / width() * cl->width);
    int y = static_cast<int>(pos.y() / height() * cl->height);
    SendPointerEvent(cl, x, y, mapMouseButton(event->button()));
}

void VncViewerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_connected || !m_connection || !m_connection->client()) return;
    rfbClient *cl = m_connection->client();
    QPointF pos = mousePos(event);
    int x = static_cast<int>(pos.x() / width() * cl->width);
    int y = static_cast<int>(pos.y() / height() * cl->height);
    SendPointerEvent(cl, x, y, 0);
}

void VncViewerWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_connected || !m_connection || !m_connection->client()) return;
    rfbClient *cl = m_connection->client();
    QPointF pos = mousePos(event);
    int x = static_cast<int>(pos.x() / width() * cl->width);
    int y = static_cast<int>(pos.y() / height() * cl->height);

    int buttonMask = 0;
    if (event->buttons() & Qt::LeftButton)   buttonMask |= rfbButton1Mask;
    if (event->buttons() & Qt::MiddleButton) buttonMask |= rfbButton2Mask;
    if (event->buttons() & Qt::RightButton)  buttonMask |= rfbButton3Mask;

    SendPointerEvent(cl, x, y, buttonMask);
}

void VncViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_connected || !m_connection || !m_connection->client()) return;
    rfbClient *cl = m_connection->client();
    QPointF pos = event->position();
    int x = static_cast<int>(pos.x() / width() * cl->width);
    int y = static_cast<int>(pos.y() / height() * cl->height);

    int mask = event->angleDelta().y() > 0 ? rfbButton4Mask : rfbButton5Mask;
    SendPointerEvent(cl, x, y, mask);
    SendPointerEvent(cl, x, y, 0);
}
```

**Step 3: Build to verify compilation**

```bash
cd build && cmake --build .
```

Expected: Compiles without errors.

**Step 4: Commit**

```bash
git add vncviewerwidget.h vncviewerwidget.cpp
git commit -m "qtvnc: add VncViewerWidget with framebuffer rendering and mouse forwarding"
```

---

### Task 5: Main integration (wire everything together)

**Files:**
- Modify: `qtvnc/main.cpp`

**Step 1: Write the integration main.cpp**

```cpp
#include <QApplication>
#include "connectdialog.h"
#include "vncviewerwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QtVNC");

    while (true) {
        ConnectDialog dlg;
        if (dlg.exec() != QDialog::Accepted) {
            break;
        }

        VncViewerWidget viewer;
        QObject::connect(&viewer, &VncViewerWidget::connectionLost, &viewer, [&viewer]() {
            viewer.close();
        });

        viewer.showMaximized();
        viewer.startConnection(dlg.host(), dlg.port(), dlg.password());

        app.exec();

        // If connection was lost (not user closing), show dialog again
        // For simplicity, always show dialog after viewer closes
        // User can cancel to exit
    }

    return 0;
}
```

**Step 2: Build the complete project**

```bash
cd build && cmake --build .
```

Expected: Compiles without errors, produces `qtvnc` executable.

**Step 3: Functional test (requires a running VNC server)**

Run: `./qtvnc` (or `./qtvnc.exe` on Windows)
Expected:
1. ConnectDialog appears with Host/Port/Password fields
2. Enter VNC server address and click Connect
3. Viewer window opens showing remote desktop
4. Mouse click/move/scroll forwarded to remote
5. Right-click shows context menu with Fullscreen/Exit
6. On disconnect, dialog reappears

**Step 4: Commit**

```bash
git add main.cpp
git commit -m "qtvnc: wire main.cpp with ConnectDialog and VncViewerWidget loop"
```

---

### Task 6: Fix threading issue — VncConnection should not be a QThread

The initial design has VncConnection inheriting QThread but using `start()` internally. This creates confusion because `QThread::start()` starts the thread's `run()`, and we're also calling `moveToThread()` on it in VncViewerWidget. Fix this by making VncConnection a simple QObject that manages its own `std::thread`.

**Files:**
- Modify: `qtvnc/vncconnection.h`
- Modify: `qtvnc/vncconnection.cpp`
- Modify: `qtvnc/vncviewerwidget.cpp`

**Step 1: Rewrite vncconnection.h — replace QThread with QObject + std::thread**

```cpp
#ifndef VNCCONNECTION_H
#define VNCCONNECTION_H

#include <QObject>
#include <QString>
#include <thread>
#include <atomic>

struct rfbClient;

class VncConnection : public QObject
{
    Q_OBJECT

public:
    explicit VncConnection(QObject *parent = nullptr);
    ~VncConnection();

    void setHost(const QString &host);
    void setPort(int port);
    void setPassword(const QString &password);
    void start();
    void stop();
    rfbClient *client() const;

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

private:
    void run();

    static void onFinishedFrameBufferUpdate(rfbClient *cl);
    static char *onGetPassword(rfbClient *cl);

    QString m_host;
    int m_port = 5900;
    QString m_password;
    rfbClient *m_client = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

#endif // VNCCONNECTION_H
```

**Step 2: Rewrite vncconnection.cpp**

```cpp
#include "vncconnection.h"
#include <rfb/rfbclient.h>
#include <cstring>

VncConnection::VncConnection(QObject *parent)
    : QObject(parent)
{
}

VncConnection::~VncConnection()
{
    stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void VncConnection::setHost(const QString &host)
{
    m_host = host;
}

void VncConnection::setPort(int port)
{
    m_port = port;
}

void VncConnection::setPassword(const QString &password)
{
    m_password = password;
}

rfbClient *VncConnection::client() const
{
    return m_client;
}

void VncConnection::start()
{
    m_thread = std::thread(&VncConnection::run, this);
}

void VncConnection::stop()
{
    m_running = false;
}

void VncConnection::run()
{
    m_client = rfbGetClient(8, 3, 4);

    m_client->format.depth = 16;
    m_client->format.bitsPerPixel = 16;
    m_client->format.redShift = 11;
    m_client->format.greenShift = 5;
    m_client->format.blueShift = 0;
    m_client->format.redMax = 0x1f;
    m_client->format.greenMax = 0x3f;
    m_client->format.blueMax = 0x1f;

    m_client->appData.encodingsString = "tight ultra hextile zlib corre rre raw";
    m_client->appData.compressLevel = 9;
    m_client->appData.qualityLevel = 5;
    m_client->appData.useRemoteCursor = TRUE;

    rfbClientSetClientData(m_client, nullptr, this);
    m_client->FinishedFrameBufferUpdate = onFinishedFrameBufferUpdate;
    if (!m_password.isEmpty()) {
        m_client->GetPassword = onGetPassword;
    }

    m_client->serverHost = strdup(m_host.toUtf8().constData());
    m_client->serverPort = m_port;

    m_running = true;

    if (!rfbInitClient(m_client, 0, nullptr)) {
        m_client = nullptr;
        emit errorOccurred(tr("Connection failed"));
        return;
    }

    emit connected();

    while (m_running) {
        int result = WaitForMessage(m_client, 500);
        if (result < 0) {
            emit errorOccurred(tr("Connection lost"));
            break;
        }
        if (result > 0) {
            if (!HandleRFBServerMessage(m_client)) {
                emit errorOccurred(tr("Server disconnected"));
                break;
            }
        }
    }

    rfbClientCleanup(m_client);
    m_client = nullptr;
    m_running = false;
    emit disconnected();
}

void VncConnection::onFinishedFrameBufferUpdate(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (self) {
        emit self->frameUpdated();
    }
}

char *VncConnection::onGetPassword(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (!self || self->m_password.isEmpty()) {
        return nullptr;
    }
    return strdup(self->m_password.toUtf8().constData());
}
```

**Step 3: Update vncviewerwidget.cpp — remove moveToThread, simplify startConnection**

In `startConnection`, replace the QThread/moveToThread setup with direct calls:

```cpp
void VncViewerWidget::startConnection(const QString &host, int port, const QString &password)
{
    m_connection = new VncConnection();

    connect(m_connection, &VncConnection::frameUpdated, this, &VncViewerWidget::onFrameUpdated, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::connected, this, &VncViewerWidget::onConnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::disconnected, this, &VncViewerWidget::onDisconnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::errorOccurred, this, &VncViewerWidget::onError, Qt::QueuedConnection);

    m_connection->setHost(host);
    m_connection->setPort(port);
    m_connection->setPassword(password);
    m_connection->start();
}
```

Remove `m_vncThread` from the header and destructor.

**Step 4: Build and verify**

```bash
cd build && cmake --build .
```

**Step 5: Commit**

```bash
git add vncconnection.h vncconnection.cpp vncviewerwidget.h vncviewerwidget.cpp
git commit -m "qtvnc: simplify VncConnection to QObject+std::thread, remove QThread dependency"
```

---

### Task 7: Polish — reconnection loop and cleanup

**Files:**
- Modify: `qtvnc/main.cpp`
- Modify: `qtvnc/vncviewerwidget.cpp`

**Step 1: Improve main.cpp reconnection logic**

Track whether the user closed the window vs connection was lost, to avoid re-showing dialog on intentional close.

```cpp
#include <QApplication>
#include "connectdialog.h"
#include "vncviewerwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QtVNC");

    while (true) {
        ConnectDialog dlg;
        if (dlg.exec() != QDialog::Accepted) {
            break;
        }

        VncViewerWidget viewer;
        bool connectionLost = false;

        QObject::connect(&viewer, &VncViewerWidget::connectionLost, &viewer, [&]() {
            connectionLost = true;
            viewer.close();
        });

        viewer.showMaximized();
        viewer.startConnection(dlg.host(), dlg.port(), dlg.password());

        int ret = app.exec();

        // If not connectionLost, user closed window intentionally -> exit
        if (!connectionLost) {
            break;
        }
        // Otherwise loop back to ConnectDialog
    }

    return 0;
}
```

**Step 2: Build, test, commit**

```bash
cd build && cmake --build .
git add main.cpp vncviewerwidget.cpp
git commit -m "qtvnc: improve reconnection loop with connection-lost tracking"
```
