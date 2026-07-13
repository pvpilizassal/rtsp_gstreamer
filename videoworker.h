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
    void fpsUpdated(double fps);

protected:
    void run() override;

private:
    // Си-функция обратного вызова для обработки динамических портов rtspsrc
    static void  onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data);
    static void  onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data);

    static GstPadProbeReturn onVideoSinkProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static void onRtspsrcCapsChanged(GstPad *pad, GParamSpec *, gpointer data);
    //static void onDecodebinElementAdded(GstBin *bin, GstElement *element, gpointer data);

    static gboolean onBusMessage(GstBus *bus, GstMessage *msg, gpointer data);

    QString extractCodecName(const GstStructure *s);

    static GstPadProbeReturn onFpsProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static gboolean onFpsTimer(gpointer data);

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

    std::atomic<uint64_t> m_frameCount{0};
    std::atomic<double> m_currentFps{0.0};
    guint m_fpsTimerId = 0;
};

#endif // VIDEOWORKER_H
