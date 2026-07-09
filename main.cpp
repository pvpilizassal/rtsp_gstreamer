#include "mainwindow.h"
#include "gst_types.h"
#include <QApplication>
#include <QDebug>
#include <iostream>

void runTest() {
    for (int i = 0; i < 100000; ++i) {
        GstElementPtr element(gst_element_factory_make("fakesink", nullptr));
        if (!element)
            return;
    }

    qDebug() << "Test complete";
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    runTest();

    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
