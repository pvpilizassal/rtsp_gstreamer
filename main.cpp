#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    //playVideo();
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
