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
    // сигнал для отправки параметров потока
    void videoInfoUpdated(int width, int height, const QString &codec);

protected:
    void run() override;

private:
    // Си-функция обратного вызова для обработки динамических портов rtspsrc
    static void  onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    static void  onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    // функция измерения параметров потока
    static void onPadCapsChanged(GstPad *pad, GParamSpec *pspec, gpointer user_data);
    QString extractCodecName(const GstStructure *s);

    bool buildPipeline();
    void resetPtrs();

    GMainLoopPtr m_mainLoop;
    GstElementPtr m_pipeline;

    QMutex m_mutex;
    QRecursiveMutex m_diagMutex;

    QString m_rtspUrl;
    QString m_codec;
    QString m_lastCodec;
    int m_width = -1;
    int m_height = -1;
    WId m_winId = 0;

    GstElement* m_source = nullptr;
    GstElement* m_decodebin = nullptr;
    GstElement* m_videosink = nullptr;
};

#endif // VIDEOWORKER_H
