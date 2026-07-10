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
    m_source.reset(gst_element_factory_make("rtspsrc", "source"));
    m_decodebin.reset(gst_element_factory_make("decodebin", "decoder"));
    m_videosink.reset(gst_element_factory_make("d3d11videosink", "sink"));
    // autovideosink - приёмник видео, автоматически выбирает видеодрайвер системы.
    // для более сложной интеграции с возможностью наложения виджетов на видео
    // стоит использовать другие плагины

    if (!m_pipeline || !m_source || !m_decodebin || !m_videosink) {
        qCritical() << "Error of creating pipeline's elements";
        return false;
    }

    // устанавливаем url в элемент rtspsrc, nullptr заканчивает список свойств
    g_object_set(G_OBJECT(m_source.get()), "location", m_rtspUrl.toUtf8().constData(), nullptr);

    // уменьшаем задержку rtspsrc до 100мс (по дефолту 2000мс)
    g_object_set(G_OBJECT(m_source.get()), "latency", 100, nullptr);

    // добавляем элементы в пайплайн (владение не передается)
    // GStreamer увеличивает внутренний счетчик ссылок
    // при уничтожении папйплайна счетчик уменьшится, а объекты очистятся через обертку
    gst_bin_add_many(GST_BIN(m_pipeline.get()), m_source.get(), m_decodebin.get(), m_videosink.get(), nullptr);

    // у "холодного" плагина rtspsrc нет выхода, ему нечем связаться с декодером
    // когда rtspsrc примет входной поток, у него появится выход
    // подключаем си-сигнал "pad-added", который сработает, когда поток подключится к камере
    g_signal_connect(m_source.get(), "pad-added", G_CALLBACK(VideoWorker::onPadAdded), m_decodebin.get());

    // также и у decodebin нет статического выхода
    g_signal_connect(m_decodebin.get(), "pad-added", G_CALLBACK(VideoWorker::onPadAdded), m_videosink.get());

    return true;
}

// связка sink-пэда rtspsrc с src-пэдом decodebin
// GMainLoop передаст параметры в onPadAdded, когда поток будет принят в m_source
// src - указатель на rtspsrc, newPad - указатель на созданный пэд, data - указатель на m_decodebin
void VideoWorker::onPadAdded(GstElement *src, GstPad *newPad, gpointer data)
{
    auto *targetElement = GST_ELEMENT(data); // указатель на объект m_decodebin

    // указатель на src-порт дкодера, к которому будет подключиться sink rtspsrc
    GstPad *targetPad = gst_element_get_static_pad(targetElement, "sink");
    if (gst_pad_is_linked(targetPad)) {
        gst_object_unref(targetPad);
        return; // проверка, если src-порт уже занят
    }

    // параметры созданного пэда
    GstCapsPtr caps(gst_pad_query_caps(newPad, nullptr));
    // упаковка в словарь параметров с 0 страницы
    GstStructure *mapKeys = gst_caps_get_structure(caps.get(), 0);
    // доступ к ключу media словаря mapKeys
    const gchar *mediaKey = gst_structure_get_string(mapKeys, "media");
    if (!mediaKey) {
        mediaKey = gst_structure_get_name(mapKeys);
    }

    // если это видео-поток — связывает порты rtspsrc и decodebin
    if (mediaKey && g_str_has_prefix(mediaKey, "video")) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting rtspsrc to decodebin";
        } else {
            qDebug() << "rtspsrc successfully linked to decodebin";
        }
    }
    else if (mediaKey && g_str_has_prefix(mediaKey, "video/")) {
        if (gst_pad_link(newPad, targetPad) != GST_PAD_LINK_OK) {
            qCritical() << "Error connecting decodebin to videosink";
        } else {
            qDebug() << "decodebin successfully linked to videosink";
        }
    }

    // освобождение ресурса для указателя на sink-порт декодера
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
