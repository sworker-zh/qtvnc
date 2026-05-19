# QtVNC - Qt5 VNC Viewer 设计方案

## 概述

基于 libvncclient 开发一个极简的 Qt5 VNC Viewer，向后兼容 Qt6。核心功能：远程画面显示 + 鼠标控制。

## 项目结构

```
qtvnc/
  CMakeLists.txt
  main.cpp
  connectdialog.h/cpp
  vncconnection.h/cpp
  vncviewerwidget.h/cpp
```

## 类设计

### VncConnection (QObject, 运行在 QThread)

封装 rfbClient 生命周期：创建、初始化、消息循环、断开清理。

- 通过信号通知主线程：帧更新、连接状态变化、错误信息
- 唯一直接调用 libvncclient C API 的类
- 使用 moveToThread() 移入 QThread
- 消息循环：WaitForMessage() + HandleRFBServerMessage()
- 无锁帧读取：WaitForMessage 阻塞时 VNC 线程不写 framebuffer

公共接口：
- void connectToHost(const QString &host, int port)
- void disconnect()
- signals: connected(), disconnected(), frameUpdated(rfbClient*), errorOccurred(QString)

### VncViewerWidget (QWidget)

主窗口，持有 VncConnection 实例和 QThread。

- 接收 frameUpdated 信号后在 paintEvent 中绘制 QImage
- 自动缩放填充窗口区域
- 鼠标事件按比例映射坐标后调用 SendPointerEvent
- 右键上下文菜单：断开连接 / 全屏切换 / 退出

### ConnectDialog (QDialog)

启动时弹出的连接对话框。

- 主机 (QLineEdit, 默认空)
- 端口 (QSpinBox, 默认 5900)
- 密码 (QLineEdit, Password 模式)
- 取消 / 连接 按钮

### main.cpp

程序入口。先弹出 ConnectDialog，确认后创建 VncViewerWidget 并启动连接。

## 线程模型

```
主线程 (Qt GUI)                VNC 线程 (QThread)
┌──────────────────┐           ┌─────────────────────┐
│ VncViewerWidget  │  启动连接  │  rfbGetClient()     │
│ ConnectDialog    │───────────►│  rfbInitClient()    │
│                  │◄──信号────│  connected()        │
│ 鼠标事件         │  直接调用  │  WaitForMessage()   │
│ SendPointerEvent │───────────►│  HandleRFBServer()  │
│                  │◄──信号────│  frameUpdated()     │
│ 断开连接         │───────────►│  退出循环+清理      │
│                  │◄──信号────│  disconnected()     │
└──────────────────┘           └─────────────────────┘
```

- SendPointerEvent 线程安全，可直接从主线程调用
- 断开时设置 m_running=false，QThread::wait() 等待线程结束
- 无锁帧读取：VNC 线程在 WaitForMessage 阻塞时不写 framebuffer

## UI 交互

### ConnectDialog

- 密码框 QLineEdit::Password 模式
- 端口默认 5900
- 点连接后关闭对话框，进入 VncViewerWidget

### VncViewerWidget

- 默认 showMaximized()
- 自动缩放填充：remote_x = local_x / widget_width * remote_width
- 鼠标左/右/中键转发 (button mask 1/4/2)
- 滚轮映射为 button 4/5 的 Press+Release
- 右键菜单：断开连接 / 全屏 / 退出全屏 / 退出

### 错误处理

- 连接失败 → QMessageBox 提示，重新弹出 ConnectDialog
- 连接中断 → 信号通知，弹提示后回到 ConnectDialog
- 密码通过 GetPassword 回调从 ConnectDialog 获取

## Qt5/6 兼容策略

- 使用 QT_VERSION_CHECK 宏处理 API 差异
- Qt6 中 QMouseEvent::localPos() 变为 position()
- 不依赖任何 Qt6-only 模块
- CMake 使用 find_package(QtX ...) 自动检测
