QT += widgets network

win32: LIBS += -lz
else: LIBS += -lz

CONFIG += c++17
TARGET = qtvnc
TEMPLATE = app

SOURCES += \
    main.cpp \
    connectdialog.cpp \
    vncconnection.cpp \
    vncviewerwidget.cpp \
    rfbconnection.cpp \
    rfbcrypto.cpp \
    d3des.c

HEADERS += \
    connectdialog.h \
    vncconnection.h \
    vncviewerwidget.h \
    rfbconnection.h \
    rfbprotocol.h \
    rfbcrypto.h \
    d3des.h
