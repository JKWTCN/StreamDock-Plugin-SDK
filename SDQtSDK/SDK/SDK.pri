# websocket
QT += core websockets
QT -= gui

CONFIG += c++11

HEADERS += \
    $$PWD/KnobFeedback.h \
    $$PWD/Action.h \
    $$PWD/BasePlugin.h \
    $$PWD/ConnectionManager.h \
    $$PWD/CustomMain.h \
    $$PWD/Localizer.h \
    $$PWD/Logger.h \
    $$PWD/Plugin.h \
    $$PWD/SDKDefines.h

SOURCES += \
    $$PWD/KnobFeedback.cpp \
    $$PWD/Action.cpp \
    $$PWD/BasePlugin.cpp \
    $$PWD/ConnectionManager.cpp \
    $$PWD/CustomMain.cpp \
    $$PWD/Localizer.cpp \
    $$PWD/Logger.cpp \
    $$PWD/Plugin.cpp

INCLUDEPATH += $$PWD
win32:DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN
win32 {
    SOURCES += $$PWD/KnobFeedbackWindows.cpp
    LIBS += -lgdiplus -lole32
}
macx {
    SOURCES += $$PWD/KnobFeedbackMac.cpp
    LIBS += -framework CoreGraphics -framework CoreText -framework ImageIO -framework CoreFoundation
}
