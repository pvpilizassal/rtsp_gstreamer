#include "videoworker.h"
#include <QDebug>

VideoWorker::VideoWorker(QObject *parent)
    : QThread(parent) {}

VideoWorker::~VideoWorker()
{
    stopStreaming();
    wait();
    qDebug() << "VideoWorker quit";
}

void VideoWorker::startStreaming()
{
    // переделать под коннект на кнопку в мейн-окне
    if (isRunning()) {
        qWarning() << "Thread is ON!";
        return;
    }

    start();
}

void VideoWorker::stopStreaming()
{
    QMutexLocker locker(&m_mutex);

    if (m_mainLoop && g_main_loop_is_running(m_mainLoop.get())) {
        qDebug() << "Stop GMainLoop...";
        g_main_loop_quit(m_mainLoop.get());
    }
}

void VideoWorker::run()
{
    qDebug() << "Thread GStreamer successfuly run. ID:" << QThread::currentThreadId();
    emit statusChanged("Connecting");

    m_mainLoop.reset(g_main_loop_new(nullptr, FALSE));

    if (!m_mainLoop)
    {
        qCritical() << "Error create GMainLoop!";
        emit statusChanged("Error");
        return;
    }

    emit statusChanged("Playing");

    g_main_loop_run(m_mainLoop.get());

    qDebug() << "GMainLoop stop. Quit from thread.";

    m_mainLoop.reset();

    emit statusChanged("Disconnected");
}
