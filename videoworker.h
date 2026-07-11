#ifndef VIDEOWORKER_H
#define VIDEOWORKER_H
#include <QThread>
#include <QMutex>
#include <QWidget>
#include "gst_types.h"

class VideoWorker : public QThread
{
    Q_OBJECT
public:
    explicit VideoWorker(QObject *parent = nullptr);
    ~VideoWorker() override;

    // запуск потока
    void startStreaming(const QString &url, WId windowId);

    // остановка потока
    void stopStreaming();

signals:
    // сигнал для отправки статуса в UI
    void statusChanged(const QString &status);

protected:
    void run() override;

private:
    // Си-функция обратного вызова для обработки динамических портов rtspsrc
    //static void onPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    static void  onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    static void  onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    bool buildPipeline();
    void resetPtrs();

    GMainLoopPtr m_mainLoop;
    GstElementPtr m_pipeline;

    QMutex m_mutex;

    QString m_rtspUrl;
    WId m_winId = 0;

    GstElement* m_source = nullptr;
    GstElement* m_decodebin = nullptr;
    GstElement* m_videosink = nullptr;
};

#endif // VIDEOWORKER_H
