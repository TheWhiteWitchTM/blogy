QT += core gui widgets network
CONFIG += c++17
INCLUDEPATH += src
TARGET = blogy
TEMPLATE = app
RESOURCES += resources.qrc
QMAKE_CXXFLAGS += -Wall -Wextra -Wno-unused-parameter

# $ORIGIN/../lib is dist/lib when the binary lives in dist/bin. The Qt prefix
# is the fallback so ./blogy still runs from the build tree when Qt is installed.
unix:!macx {
    QMAKE_LFLAGS += -Wl,--disable-new-dtags
    QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../lib\''
    QMAKE_RPATHDIR += $$[QT_INSTALL_LIBS]
}

SOURCES += \
    src/main.cpp \
    src/config.cpp \
    src/settings.cpp \
    src/frontmatter.cpp \
    src/post.cpp \
    src/highlighter.cpp \
    src/editor.cpp \
    src/preview.cpp \
    src/git.cpp \
    src/bluesky.cpp \
    src/grok.cpp \
    src/selftest.cpp \
    src/ui/frontmatterform.cpp \
    src/ui/chatpanel.cpp \
    src/ui/dialogs.cpp \
    src/ui/mainwindow.cpp

HEADERS += \
    src/config.h \
    src/settings.h \
    src/frontmatter.h \
    src/post.h \
    src/highlighter.h \
    src/editor.h \
    src/preview.h \
    src/git.h \
    src/bluesky.h \
    src/grok.h \
    src/selftest.h \
    src/ui/frontmatterform.h \
    src/ui/chatpanel.h \
    src/ui/dialogs.h \
    src/ui/mainwindow.h
