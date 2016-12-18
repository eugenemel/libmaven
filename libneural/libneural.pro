include(../libmaven.pri)
DESTDIR = $$OUTPUT_DIR/lib

TEMPLATE = lib
CONFIG += staticlib
TARGET = neural
SOURCES=neuron.cc nnwork.cc
HEADERS=neuron.h  nnwork.h
INCLUDEPATH += ./


contains(MEEGO_EDITION,harmattan) {
    target.path = /opt/libneural/lib
    INSTALLS += target
}
