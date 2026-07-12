#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "version.h"
#include "videoworker.h"

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow),
                                          m_worker(new VideoWorker(this))
{
    ui->setupUi(this);
    QString windowTitle = QString("RTSP Viewer v%1 (Qt6 + GStreamer)")
                              .arg(PROJECT_VERSION_STR);
    setWindowTitle(windowTitle);
    resize(800, 600);

    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    auto* ctrlLayout = new QHBoxLayout();
    m_btnConnect = new QPushButton("Connect", this);
    m_btnDisconnect = new QPushButton("Disconnect", this);
    m_lblStatus = new QLabel("Status: Disconnected", this);
    m_txtUrl = new QLineEdit(this); // rtsp://127.0.0.1:8554/cam

    ctrlLayout->addWidget(new QLabel("RTSP URL:", this));
    // растяжка лайн-эдита на длину окна
    ctrlLayout->addWidget(m_txtUrl, 1);
    ctrlLayout->addWidget(m_btnConnect);
    ctrlLayout->addWidget(m_btnDisconnect);
    ctrlLayout->addWidget(m_lblStatus);
    ctrlLayout->addStretch();
    m_btnDisconnect->setEnabled(false);

    mainLayout->addLayout(ctrlLayout);

    auto* underLayout = new QHBoxLayout();
    m_lblResolution = new QLabel("Resolution: -", this);
    m_lblCodec = new QLabel("Codec: -", this);
    m_lblFPS = new QLabel("FPS: -", this);
    underLayout->addWidget(m_lblFPS);
    underLayout->addWidget(m_lblCodec);
    underLayout->addWidget(m_lblResolution);
    mainLayout->addLayout(underLayout);

    m_videoContainer = new QWidget(this);
    m_videoContainer->setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_videoContainer->setAttribute(Qt::WA_NoSystemBackground, true);

    // растяжка видео на все окно
    mainLayout->addWidget(m_videoContainer, 1);

    setCentralWidget(centralWidget);

    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

    // Qt::QueuedConnection выберется автоматически, тк сигналы идут из другого потока
    connect(m_worker, &VideoWorker::statusChanged, this, &MainWindow::onStatusChanged);

    connect(m_worker, &VideoWorker::videoInfoUpdated, this, &MainWindow::onVideoInfoUpdated);

    connect(m_worker, &VideoWorker::fpsUpdated, this, &MainWindow::onFpsUpdated);
}

// m_worker удалится сам, так как у него parent = this
MainWindow::~MainWindow() = default;

void MainWindow::onConnectClicked()
{
    QString url = m_txtUrl->text().trimmed();
    if (url.isEmpty()) {
        m_lblStatus->setText("Status: Error (Empty URL)");
        return;
    }

    m_btnConnect->setEnabled(false);
    m_txtUrl->setEnabled(false);

    m_worker->startStreaming(url, m_videoContainer->winId());
}

void MainWindow::onDisconnectClicked()
{
    m_btnDisconnect->setEnabled(false);
    m_worker->stopStreaming();
    m_lblResolution->setText(QString("Resolution: -"));
    m_lblCodec->setText(QString("Codec: -"));
    m_lblFPS->setText(QString("FPS: -"));
}

void MainWindow::onStatusChanged(const QString &status)
{
    m_lblStatus->setText(QString("Status: %1").arg(status));

    if (status.startsWith("Error:"))
        m_lblStatus->setStyleSheet("color: red;");
    else
        m_lblStatus->setStyleSheet("");

    // доступность кнопок в зависимости от состояния m_worker
    if (status == "Playing") {
        m_btnDisconnect->setEnabled(true);
        m_btnConnect->setEnabled(false);
    } else if (status == "Disconnected" || status.startsWith("Error")) {
        m_btnConnect->setEnabled(true);
        m_txtUrl->setEnabled(true);
        m_btnDisconnect->setEnabled(false);
    }
}

void MainWindow::onVideoInfoUpdated(int width, int height, const QString &codec)
{
    m_lblResolution->setText(QString("Resolution: %1x%2").arg(width).arg(height));
    m_lblCodec->setText(QString("Codec: %1").arg(codec));
}

void MainWindow::onFpsUpdated(double fps)
{
    m_lblFPS->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
}