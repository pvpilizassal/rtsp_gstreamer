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
    void videoInfoUpdated(int width, int height, const QString &codec);
    void errorOccurred(const QString &message);

protected:
    void run() override;

private:
    // Си-функция обратного вызова для обработки динамических портов rtspsrc
    static void  onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    static void  onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data);

    static GstPadProbeReturn onVideoSinkProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static void onRtspsrcCapsChanged(GstPad *pad, GParamSpec *, gpointer user_data);

    static gboolean onBusMessage(GstBus *bus, GstMessage *msg, gpointer user_data);

    QString extractCodecName(const GstStructure *s);

    bool buildPipeline();
    void resetPtrs();

    GMainLoopPtr m_mainLoop;
    GstElementPtr m_pipeline;

    QMutex m_mutex;

    QString m_rtspUrl;
    QString m_codec;
    QString m_lastCodec;
    int m_width = -1;
    int m_height = -1;

    WId m_winId = 0;

    GstElement* m_source = nullptr;
    GstElement* m_decodebin = nullptr;
    GstElement* m_videosink = nullptr;

    gulong m_busWatchId = 0;

    bool m_errorOccurred = false;
    bool m_hadWarning = false;

    QString m_lastError;
};

#endif // VIDEOWORKER_H
