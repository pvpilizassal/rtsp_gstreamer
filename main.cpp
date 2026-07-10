#include <QApplication>
#include <QDebug>
#include <QThread>
#include <iostream>
#include "mainwindow.h"
#include "gst_types.h"

void playVideo() {
    GstElementPtr pipeline(gst_element_factory_make("playbin", "my_player"));

    if (!pipeline) {
        return;
    }

    const char* url = "rtsp://localhost:8554/webcam";
    g_object_set(pipeline.get(), "uri", url, nullptr);

    g_object_set(pipeline.get(), "flags", 0x00000001, nullptr);

    qDebug() << "Loading stream...";

    // запуск потока
    gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);

    GstBus* bus = gst_element_get_bus(pipeline.get());

    QThread::msleep(200);

    GstElement* rtspsrc = gst_bin_get_by_name(GST_BIN(pipeline.get()), "source");
    if (rtspsrc) {
        g_object_set(rtspsrc, "latency", 0, nullptr);
        gst_object_unref(rtspsrc);
        qDebug() << "RTSP Latency successfully set to 0!";
    } else {
        qDebug() << "Warning: Could not find internal rtspsrc to set latency.";
    }
    gst_object_unref(bus);

    QThread::sleep(30);

    qDebug() << "Ending stream...";
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);

}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    //playVideo();
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
