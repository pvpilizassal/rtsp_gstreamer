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
}

bool VideoWorker::buildPipeline()
{
    // создаем сам контейнер-пайплайн
    m_pipeline.reset(gst_pipeline_new("rtsp-pipeline"));

    // создаем элементы через фабрики GStreamer
    m_source = gst_element_factory_make("rtspsrc", "source");
    m_decodebin = gst_element_factory_make("decodebin", "decoder");
    m_videosink = gst_element_factory_make("d3d11videosink", "sink");

    g_object_set(G_OBJECT(m_source),
                 "location"  , m_rtspUrl.toUtf8().constData(), // устанавливаем url в элемент rtspsrc
                 "latency"   , 100,      // уменьшаем задержку rtspsrc до 100мс
                 "protocols" , 0x4,      // принудительно TCP
                 "retry"     , 1000,     // пытаться переподключаться при обрыве
                 "timeout"   , 5000000,  // таймаут (мкс)
                 nullptr);

    if (!m_pipeline || !m_source || !m_decodebin || !m_videosink) {
        qCritical() << "Error of creating pipeline's elements";
        return false;
    }

    // добавляем элементы в пайплайн (владение передается)
    // GStreamer увеличивает внутренний счетчик ссылок
    // при уничтожении папйплайна счетчик уменьшится, а объекты очистятся через обертку
    gst_bin_add_many(GST_BIN(m_pipeline.get()), m_source, m_decodebin, m_videosink, nullptr);

    // у "холодного" плагина rtspsrc нет выхода, ему нечем связаться с декодером
    // когда rtspsrc примет входной поток, у него появится выход
    // подключаем си-сигнал "pad-added", который сработает, когда поток подключится к камере
    g_signal_connect(m_source, "pad-added", G_CALLBACK(VideoWorker::onDecodebinPadAdded), m_decodebin);
    //g_signal_connect(m_source, "pad-added", G_CALLBACK(VideoWorker::onRtspsrcPadAdded), m_decodebin);

    // также и у decodebin нет статического выхода
    g_signal_connect(m_decodebin, "pad-added", G_CALLBACK(VideoWorker::onRtspsrcPadAdded), m_videosink);
    //g_signal_connect(m_decodebin, "pad-added", G_CALLBACK(VideoWorker::onDecodebinPadAdded), m_videosink);

    return true;
}

// подключает к decodebin и сохраняет кодек
void VideoWorker::onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
    VideoWorker *self = static_cast<VideoWorker*>(data);
    auto *decodebin = GST_ELEMENT(data);
    GstPad *targetPad = gst_element_get_static_pad(decodebin, "sink");

    if (gst_pad_is_linked(targetPad)) {
        gst_object_unref(targetPad);
        return;
    }

    GstCaps *caps = gst_pad_query_caps(newPad, nullptr);
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);
    const gchar *mediaKey = gst_structure_get_name(capsStruct);

    // rtspsrc предоставляет поле "media" (video/audio/application)
    //if (mediaKey && g_str_has_prefix(mediaKey, "video")) {
    if (mediaKey && (g_str_has_prefix(mediaKey, "application/") || g_str_has_prefix(mediaKey, "video/"))) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting rtspsrc to decodebin";
        } else {
            qDebug() << "rtspsrc successfully linked to decodebin";

            // // подписываемся на изменение caps у этого пада
            // g_signal_connect(newPad, "notify::caps", G_CALLBACK(VideoWorker::onPadCapsChanged), self);

            // сохраняем кодек (извлекаем понятное имя)
            QMutexLocker lock(&self->m_diagMutex);
            self->m_codec = self->extractCodecName(capsStruct);
        }
    }

    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

// 2. Обработчик для decodebin (динамически подключает к videosink)
void VideoWorker::onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
    VideoWorker *self = static_cast<VideoWorker*>(data);
    auto *videosink = GST_ELEMENT(data);
    GstPad *targetPad = gst_element_get_static_pad(videosink, "sink");

    if (gst_pad_is_linked(targetPad)) {
        gst_object_unref(targetPad);
        return;
    }

    GstCaps *caps = gst_pad_query_caps(newPad, nullptr);
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(capsStruct);

    // decodebin выдает "video/x-raw..." после декодирования
    //if (name && g_str_has_prefix(name, "application/")) {
    if (name && (g_str_has_prefix(name, "application/") || g_str_has_prefix(name, "video/"))) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting decodebin to videosink";
        } else {
            qDebug() << "decodebin successfully linked to videosink";

            // // сохраняем кодек (извлекаем понятное имя)
            // QMutexLocker lock(&self->m_diagMutex);
            // self->m_codec = self->extractCodecName(capsStruct);

            // подписываемся на изменение caps у этого пада
            g_signal_connect(newPad, "notify::caps", G_CALLBACK(VideoWorker::onPadCapsChanged), self);
        }
    }

    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

void VideoWorker::onPadCapsChanged(GstPad *pad, GParamSpec *pspec, gpointer user_data)
{
    VideoWorker *self = static_cast<VideoWorker*>(user_data);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    if (!structure) {
        gst_caps_unref(caps);
        return;
    }

    gint width = 0, height = 0;
    if (gst_structure_get_int(structure, "width", &width) &&
        gst_structure_get_int(structure, "height", &height)) {

        QMutexLocker lock(&self->m_diagMutex);
        QString codec = self->m_codec;  // копируем под защитой
        lock.unlock();

        // Эмитируем сигнал ТОЛЬКО если данные изменились
        if (width != self->m_width || height != self->m_height || codec != self->m_lastCodec) {
            self->m_width = width;
            self->m_height = height;
            self->m_lastCodec = codec;
            // Эмитим из любого потока – Qt обеспечит QueuedConnection в главный поток
            emit self->videoInfoUpdated(width, height, codec);
        }
    }

    gst_caps_unref(caps);
}

QString VideoWorker::extractCodecName(const GstStructure *s)
{
    const gchar *name = gst_structure_get_name(s);
    if (!name) return "Unknown";

    if (strcmp(name, "video/x-h264") == 0) return "H.264";
    if (strcmp(name, "video/x-h265") == 0) return "H.265";
    // можно добавить другие известные кодеки
    return QString::fromUtf8(name).mid(6); // убираем "video/"
}

void VideoWorker::resetPtrs()
{
    QMutexLocker locker(&m_mutex);
    m_mainLoop.reset();
    m_pipeline.reset();
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

    g_main_loop_run(m_mainLoop.get());

    qDebug() << "GMainLoop stop. Quit from thread.";

    // освобождение ресурсов
    resetPtrs();

    emit statusChanged("Disconnected");
}
