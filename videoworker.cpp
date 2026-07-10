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
    m_pipeline.reset(GST_ELEMENT(gst_pipeline_new("udp-receiver-pipeline")));

    // Создаем элементы для приема «сырого» RTP видео по UDP
    GstElement *src     = gst_element_factory_make("udpsrc", "src");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "filter");
    GstElement *depay   = gst_element_factory_make("rtph264depay", "depay");
    GstElement *parser  = gst_element_factory_make("h264parse", "parser");
    GstElement *decoder = gst_element_factory_make("avdec_h264", "decoder");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *m_videosink = gst_element_factory_make("d3d11videosink", "sink");

    if (!m_pipeline || !src || !capsfilter || !depay || !parser || !decoder || !convert || !m_videosink) {
        qCritical() << "Error creating UDP pipeline elements";
        return false;
    }

    // 1. Настраиваем UDP порт
    g_object_set(G_OBJECT(src), "port", 5000, nullptr);

    // 2. ВАЖНО: Указываем udpsrc, что мы ждем именно RTP H.264 видео (иначе он не поймет байты)
    GstCaps *caps = gst_caps_from_string("application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96");
    g_object_set(G_OBJECT(capsfilter), "caps", caps, nullptr);
    gst_caps_unref(caps);

    // 3. Добавляем все элементы в пайплайн (владение переходит к m_pipeline)
    gst_bin_add_many(GST_BIN(m_pipeline.get()), src, capsfilter, depay, parser, decoder, convert, m_videosink, nullptr);

    // 4. Связываем элементы в один жесткий поезд (цепочку)
    // Больше никаких динамических сигналов g_signal_connect!
    if (!gst_element_link_many(src, capsfilter, depay, parser, decoder, convert, m_videosink, nullptr)) {
        qCritical() << "Failed to link elements in UDP pipeline";
        return false;
    }

    qDebug() << "UDP Receiver Pipeline successfully built and linked!";
    return true;
}

// bool VideoWorker::buildPipeline()
// {
//     // создаем сам контейнер-пайплайн
//     m_pipeline.reset(gst_pipeline_new("rtsp-pipeline"));

//     // создаем элементы через фабрики GStreamer
//     m_source.reset(gst_element_factory_make("rtspsrc", "source"));
//     m_decodebin.reset(gst_element_factory_make("decodebin", "decoder"));
//     m_videosink.reset(gst_element_factory_make("d3d11videosink", "sink"));
//     // autovideosink - приёмник видео, автоматически выбирает видеодрайвер системы.
//     // для более сложной интеграции с возможностью наложения виджетов на видео
//     // стоит использовать другие плагины

//     if (!m_pipeline || !m_source || !m_decodebin || !m_videosink) {
//         qCritical() << "Error of creating pipeline's elements";
//         return false;
//     }

//     // устанавливаем url в элемент rtspsrc, nullptr заканчивает список свойств
//     g_object_set(G_OBJECT(m_source.get()), "location", m_rtspUrl.toUtf8().constData(), nullptr);

//     // уменьшаем задержку rtspsrc до 100мс (по дефолту 2000мс)
//     g_object_set(G_OBJECT(m_source.get()), "latency", 100, nullptr);

//     // добавляем элементы в пайплайн (владение не передается)
//     // GStreamer увеличивает внутренний счетчик ссылок
//     // при уничтожении папйплайна счетчик уменьшится, а объекты очистятся через обертку
//     gst_bin_add_many(GST_BIN(m_pipeline.get()), m_source.get(), m_decodebin.get(), m_videosink.get(), nullptr);

//     // у "холодного" плагина rtspsrc нет выхода, ему нечем связаться с декодером
//     // когда rtspsrc примет входной поток, у него появится выход
//     // подключаем си-сигнал "pad-added", который сработает, когда поток подключится к камере
//     g_signal_connect(m_source.get(), "pad-added", G_CALLBACK(VideoWorker::onDecodebinPadAdded), m_decodebin.get());

//     // также и у decodebin нет статического выхода
//     g_signal_connect(m_decodebin.get(), "pad-added", G_CALLBACK(VideoWorker::onRtspsrcPadAdded), m_videosink.get());

//     return true;
// }

void VideoWorker::onRtspsrcPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
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
    if (mediaKey && g_str_has_prefix(mediaKey, "video")) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting rtspsrc to decodebin";
        } else {
            qDebug() << "rtspsrc successfully linked to decodebin";
        }
    }
    else {
        qDebug() << "Error";
    }
    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

// 2. Обработчик для decodebin (динамически подключает к videosink)
void VideoWorker::onDecodebinPadAdded(GstElement *src, GstPad *newPad, gpointer data) {
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
    if (name && g_str_has_prefix(name, "application/")) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting decodebin to videosink";
        } else {
            qDebug() << "decodebin successfully linked to videosink";
        }
    }
    else {
        qDebug() << "Error";
    }
    gst_caps_unref(caps);
    gst_object_unref(targetPad);
}

void VideoWorker::resetPtrs()
{
    QMutexLocker locker(&m_mutex);
    m_mainLoop.reset();
    m_pipeline.reset();
    //m_source.reset();
    //m_decodebin.reset();
    //m_videosink.reset();
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
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_videosink.get()), m_winId);

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
