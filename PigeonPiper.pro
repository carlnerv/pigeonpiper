QT       += core gui widgets network
TARGET = PigeonPiper
TEMPLATE = app

# RHEL7 GCC 4.8.5 支持C++11
CONFIG += c++11

SOURCES += main.cpp \
           mainwindow.cpp \
           settingsdialog.cpp \
           transfermanager.cpp

HEADERS += mainwindow.h \
           settingsdialog.h \
           transfermanager.h \
           configmanager.h

TRANSLATIONS += app_zh_CN.ts \
                app_en.ts

DISTFILES += \
    images/icon.png

RESOURCES += \
    resources.qrc
