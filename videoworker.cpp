#include "videoworker.h"
#include <gst/video/videooverlay.h>
#include <QDebug>

VideoWorker::VideoWorker(QObject *parent)
    : QThread(parent) {}

VideoWorker::~VideoWorker()
{
    stopStreaming();
    wait();
    qDebug() << "VideoWorker quit";
}

void VideoWorker::startStreaming(const QString &url, WId windowId)
{
    if (isRunning()) return;

    m_rtspUrl = url;
    m_winId = windowId;

    start();
}

void VideoWorker::stopStreaming()
{
    QMutexLocker locker(&m_mutex);

    if (m_busWatchId) {
        qDebug() << "Stop bus-watcher...";
        g_source_remove(m_busWatchId);
        m_busWatchId = 0;
    }

    if (m_pipeline) {
        qDebug() << "Stop pipeline...";
        // команда GST_STATE_NULL разрывает сетевое RTSP-соединение с камерой
        // и гасит внутренние потоки GStreamer
        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    }

    if (m_mainLoop && g_main_loop_is_running(m_mainLoop.get())) {
        qDebug() << "Stop GMainLoop...";
        g_main_loop_quit(m_mainLoop.get());
    }

    if (m_fpsTimerId) {
        qDebug() << "Stop FPC-counter...";
        g_source_remove(m_fpsTimerId);
        m_fpsTimerId = 0;
    }
}

bool VideoWorker::buildPipeline()
{
    // создаем сам контейнер-пайплайн
    m_pipeline.reset(gst_pipeline_new("rtsp-pipeline"));

    // создаем элементы через фабрики GStreamer
    m_source = gst_element_factory_make("rtspsrc", "source");
    m_decodebin = gst_element_factory_make("decodebin", "decoder");
    m_videosink = gst_element_factory_make("d3d11videosink", "sink");

    if (!m_pipeline || !m_source || !m_decodebin || !m_videosink) {
        qCritical() << "Error of creating pipeline's elements";
        return false;
    }

    g_object_set(G_OBJECT(m_source),
                 "location"  , m_rtspUrl.toUtf8().constData(), // устанавливаем url в элемент rtspsrc
                 "latency"   , 100,      // уменьшаем задержку rtspsrc до 100мс
                 "protocols" , 0x4,      // принудительно TCP
                 "retry"     , 1000,     // пытаться переподключаться при обрыве
                 "timeout"   , 5000000,  // таймаут (мкс)
                 nullptr);

    // добавляем элементы в пайплайн (владение передается)
    // GStreamer увеличивает внутренний счетчик ссылок
    // при уничтожении папйплайна счетчик уменьшится, а объекты очистятся через обертку
    gst_bin_add_many(GST_BIN(m_pipeline.get()), m_source, m_decodebin, m_videosink, nullptr);

    // у "холодного" плагина rtspsrc нет выхода, ему нечем связаться с декодером
    // когда rtspsrc примет входной поток, у него появится выход
    // подключаем си-сигнал "pad-added", который сработает, когда поток подключится к камере
    //g_signal_connect(m_source, "pad-added", G_CALLBACK(VideoWorker::onDecodebinPadAdded), m_decodebin);
    g_signal_connect(m_source, "pad-added", G_CALLBACK(VideoWorker::onRtspsrcPadAdded), this);

    // также и у decodebin нет статического выхода
    //g_signal_connect(m_decodebin, "pad-added", G_CALLBACK(VideoWorker::onRtspsrcPadAdded), m_videosink);
    g_signal_connect(m_decodebin, "pad-added", G_CALLBACK(VideoWorker::onDecodebinPadAdded), this);

    return true;
}


void VideoWorker::onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
    VideoWorker *self = static_cast<VideoWorker*>(data);
    GstPad *targetPad = gst_element_get_static_pad(self->m_decodebin, "sink");

    if (gst_pad_is_linked(targetPad)) {
        gst_object_unref(targetPad);
        return;
    }

    GstCaps *caps = gst_pad_query_caps(newPad, nullptr);
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);
    const gchar *mediaKey = gst_structure_get_name(capsStruct);

    // rtspsrc предоставляет поле "media" (video/audio/application)
    if (mediaKey && (g_str_has_prefix(mediaKey, "video") || g_str_has_prefix(mediaKey, "application"))) {
        //self->m_codec = self->extractCodecName(capsStruct);
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting rtspsrc to decodebin";
        } else {
            g_signal_connect(newPad, "notify::caps", G_CALLBACK(VideoWorker::onRtspsrcCapsChanged), self);
            qDebug() << "rtspsrc successfully linked to decodebin, codec:" << self->m_codec;
        }
    }

    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

void VideoWorker::onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
    VideoWorker *self = static_cast<VideoWorker*>(data);
    GstPad *targetPad = gst_element_get_static_pad(self->m_videosink, "sink");

    if (gst_pad_is_linked(targetPad)) {
        gst_object_unref(targetPad);
        return;
    }

    GstCaps *caps = gst_pad_query_caps(newPad, nullptr);
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);
    const gchar *mediaKey = gst_structure_get_name(capsStruct);

    if (mediaKey && g_str_has_prefix(mediaKey, "video/")) {
        if (gst_pad_link(newPad, targetPad) == GST_PAD_LINK_OK) {
            qDebug() << "decodebin video pad linked to videosink";
            // probe на sink-пад videosink для захвата разрешения
            gst_pad_add_probe(targetPad,
                              GST_PAD_PROBE_TYPE_BUFFER,
                              VideoWorker::onVideoSinkProbe,
                              self,
                              nullptr);

            // probe для подсчёта кадров
            gst_pad_add_probe(targetPad,
                              GST_PAD_PROBE_TYPE_BUFFER,
                              VideoWorker::onFpsProbe,
                              self,
                              nullptr);
        } else {
            qCritical() << "Error linking decodebin to videosink";
        }
    }
    // application/x-rtp и прочие – просто линкуем без подписки
    else if (mediaKey && g_str_has_prefix(mediaKey, "application/")) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error linking decodebin to videosink (rtp)";
        }
    }

    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

QString VideoWorker::extractCodecName(const GstStructure *s)
{
    const gchar *name = gst_structure_get_name(s);
    if (!name) return "Unknown";
    if (strcmp(name, "video/x-h264") == 0) return "H.264";
    if (strcmp(name, "video/x-h265") == 0) return "H.265";
    // На всякий случай другие варианты:
    if (strcmp(name, "video/x-vp8") == 0)  return "VP8";
    if (strcmp(name, "video/x-vp9") == 0)  return "VP9";
    // fallback – убираем "video/"
    return QString::fromUtf8(name).mid(6);
}

GstPadProbeReturn VideoWorker::onFpsProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    VideoWorker *self = static_cast<VideoWorker*>(data);
    self->m_frameCount.fetch_add(1, std::memory_order_relaxed);
    return GST_PAD_PROBE_OK;   // не удаляем probe
}

gboolean VideoWorker::onFpsTimer(gpointer data)
{
    VideoWorker *self = static_cast<VideoWorker*>(data);
    uint64_t count = self->m_frameCount.exchange(0, std::memory_order_relaxed);
    // кол-во кадров за последнюю секунду
    double fps = static_cast<double>(count);
    self->m_currentFps.store(fps, std::memory_order_relaxed);
    emit self->fpsUpdated(fps);
    return TRUE;   // продолжать таймер
}

GstPadProbeReturn VideoWorker::onVideoSinkProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    VideoWorker *self = static_cast<VideoWorker*>(data);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) return GST_PAD_PROBE_PASS;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    if (structure) {
        gint width = 0, height = 0;
        if (gst_structure_get_int(structure, "width", &width) &&
            gst_structure_get_int(structure, "height", &height)) {
            QString codec = self->m_codec;
            if (codec.isEmpty()) codec = "Unknown";
            if (width != self->m_width || height != self->m_height || codec != self->m_lastCodec) {
                self->m_width = width;
                self->m_height = height;
                self->m_lastCodec = codec;
                emit self->videoInfoUpdated(width, height, codec);
            }
        }
    }
    gst_caps_unref(caps);
    return GST_PAD_PROBE_REMOVE;   // больше не вызываем
}

void VideoWorker::onRtspsrcCapsChanged(GstPad *pad, GParamSpec *, gpointer data)
{
    VideoWorker *self = static_cast<VideoWorker*>(data);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure *s = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(s);
    if (name && g_str_has_prefix(name, "video/")) {
        self->m_codec = self->extractCodecName(s);
        qDebug() << "Codec detected:" << self->m_codec;

        // Отключаем обработчик, чтобы не срабатывал повторно
        g_signal_handlers_disconnect_by_func(pad, (gpointer)VideoWorker::onRtspsrcCapsChanged, self);
    }
    gst_caps_unref(caps);
}

gboolean VideoWorker::onBusMessage(GstBus *bus, GstMessage *msg, gpointer data)
{
    VideoWorker *self = static_cast<VideoWorker*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    // неверная ссылка
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug_info = nullptr;
        QString errorMsg = "";
        QString errorStr = "";

        gst_message_parse_error(msg, &err, &debug_info);
        errorMsg = QString::fromUtf8(err->message);
        qCritical() << "GStreamer error:" << err->message;
        if (debug_info) {
            qDebug() << "Debug info:" << debug_info;
            g_free(debug_info);
        }

        g_error_free(err);

        if (errorMsg.contains("Not found", Qt::CaseInsensitive))
            errorStr = "Stream not found (404) — check URL";
        else if (errorMsg.contains("Unauthorized", Qt::CaseInsensitive))
            errorStr = "Authentication failed (401)";
        else if (errorMsg.contains("Could not connect", Qt::CaseInsensitive))
            errorStr = "Connection refused — server unreachable";

        self->m_errorOccurred = true;
        self->m_lastError = errorStr;
        emit self->statusChanged(QString("Error: %1").arg(errorStr));

        // Останавливаем главный цикл, чтобы выйти из run()
        if (self->m_mainLoop && g_main_loop_is_running(self->m_mainLoop.get())) {
            g_main_loop_quit(self->m_mainLoop.get());
        }
        break;
    }
    // спонтанный разрыв связи с потоком
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        if (self->m_hadWarning) {
            self->m_errorOccurred = true;
            emit self->statusChanged("Error: Stream interrupted");
        } else {
            // штатное завершение
            emit self->statusChanged("Disconnected");
        }
        if (self->m_mainLoop && g_main_loop_is_running(self->m_mainLoop.get())) {
            g_main_loop_quit(self->m_mainLoop.get());
        }
        break;
    case GST_MESSAGE_WARNING: {
        GError *warn = nullptr;
        gchar *debug_info = nullptr;
        gst_message_parse_warning(msg, &warn, &debug_info);
        QString warnMsg = QString::fromUtf8(warn->message);
        qWarning() << "GStreamer warning:" << warnMsg;

        // если это предупреждение о потере соединения – запоминаем
        if (warnMsg.contains("Could not read from resource")) {
            self->m_hadWarning = true;
        }

        g_error_free(warn);
        if (debug_info) g_free(debug_info);
        break;
    }
    // добавить обработку других сообщений
    default:
        break;
    }
    return TRUE;
}

void VideoWorker::resetPtrs()
{
    QMutexLocker locker(&m_mutex);
    if (m_pipeline) {
        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    }

    m_mainLoop.reset();
    m_pipeline.reset();

    m_width = -1;
    m_height = -1;
    m_codec.clear();
    m_lastCodec.clear();

    m_lastError.clear();
    m_errorOccurred = false;
    m_hadWarning = false;
    m_errorOccurred = false;

    m_frameCount = 0;
    m_currentFps = 0.0;
}

void VideoWorker::run()
{
    qDebug() << "Thread GStreamer successfuly run. ID:" << QThread::currentThreadId();
    emit statusChanged("Connecting");

    // создание пайплайна
    if (!buildPipeline()) {
        emit statusChanged("Error");
        return;
    }

    // обработчик сообщений шины
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get()));
    m_busWatchId = gst_bus_add_watch(bus, VideoWorker::onBusMessage, this);
    gst_object_unref(bus);

    // передача дескриптора окна (winId) в элемент отображения видео
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_videosink), m_winId);

    m_mainLoop.reset(g_main_loop_new(nullptr, FALSE));

    if (!m_mainLoop)
    {
        qCritical() << "Error create GMainLoop!";
        emit statusChanged("Error");
        return;
    }

    // запуск пайплайна
    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);

    emit statusChanged("Playing");

    m_fpsTimerId = g_timeout_add_seconds(1, VideoWorker::onFpsTimer, this);

    g_main_loop_run(m_mainLoop.get());

    // выход из цикла GMainLoop
    qDebug() << "GMainLoop stop. Quit from thread.";

    if (!m_errorOccurred) {
        emit statusChanged("Disconnected");
    }

    if (m_busWatchId) {
        g_source_remove(m_busWatchId);
        m_busWatchId = 0;
    }

    if (m_fpsTimerId) {
        g_source_remove(m_fpsTimerId);
        m_fpsTimerId = 0;
    }

    // освобождение ресурсов
    resetPtrs();

}
