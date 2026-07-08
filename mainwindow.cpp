#include "mainwindow.h"
#include "version.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    QString windowTitle = QString("RTSP Viewer v%1 (Qt6 + GStreamer)")
                              .arg(PROJECT_VERSION_STR);
    setWindowTitle(windowTitle);
    resize(800, 600);
}
