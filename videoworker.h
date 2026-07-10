#ifndef VIDEOWORKER_H
#define VIDEOWORKER_H
#include <QThread>
#include <QMutex>
#include "gst_types.h"

class VideoWorker : public QThread
{
    Q_OBJECT
public:
    explicit VideoWorker(QObject *parent = nullptr);
    ~VideoWorker() override;

    // запуск потока
    void startStreaming();

    // остановка потока
    void stopStreaming();

signals:
    // сигнал для отправки статуса в UI
    void statusChanged(const QString &status);

protected:
    void run() override;

private:
    GMainLoopPtr m_mainLoop;
    QMutex m_mutex;
};

#endif // VIDEOWORKER_H
