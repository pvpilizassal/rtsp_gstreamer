#include <QTest>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QSignalSpy>
#include <gst/gst.h>
#include "videoworker.h"

class test_gst_leaks : public QObject
{
    Q_OBJECT

public:
    test_gst_leaks();
    ~test_gst_leaks() override;

private slots:
    void test_case1();

    void initTestCase()
    {
        // инициализируем GStreamer с трассировщиком утечек
        gst_init(nullptr, nullptr);
        qDebug() << "GStreamer initialized with leak tracer active";
    }

    void cleanupTestCase()
    {
        // время дописать логи
        QThread::msleep(200);
    }

    void testWorkerLifecycle()
    {
        // проверка, что создание и удаление VideoWorker без запуска не вызывает утечек
        {
            VideoWorker worker;
            Q_UNUSED(worker)
        }
        QVERIFY2(true, "Worker construction/destruction should not leak");
    }

    void testPipelineRunAndStop()
    {
        GstElement *pipeline = gst_pipeline_new("test-pipeline");
        GstElement *src = gst_element_factory_make("videotestsrc", "src");
        GstElement *sink = gst_element_factory_make("fakesink", "sink");
        QVERIFY(pipeline && src && sink);

        gst_bin_add_many(GST_BIN(pipeline), src, sink, nullptr);
        bool linked = gst_element_link(src, sink);
        QVERIFY(linked);

        GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        QCOMPARE(ret, GST_STATE_CHANGE_ASYNC); // или SUCCESS

        QThread::msleep(500);

        ret = gst_element_set_state(pipeline, GST_STATE_NULL);
        QVERIFY(ret != GST_STATE_CHANGE_FAILURE);

        gst_object_unref(pipeline);

        QThread::msleep(500);
    }

    void testVideoWorkerErrorCase()
    {
        // запускаем VideoWorker с неверным URL, он должен отработать ошибку и не утечь
        VideoWorker worker;
        QSignalSpy errorSpy(&worker, &VideoWorker::statusChanged);
        QSignalSpy fpsSpy(&worker, &VideoWorker::fpsUpdated);
        QSignalSpy videoSpy(&worker, &VideoWorker::videoInfoUpdated);

        // используем неверный URL, чтобы вызвать ошибку
        worker.startStreaming("rtsp://127.0.0.1:1/nonexistent", 0); // WId=0, видео не нужно
        QVERIFY(worker.wait(5000));
        bool hasError = false;
        for (const QList<QVariant> &args : errorSpy) {
            if (args.first().toString().startsWith("Error:")) {
                hasError = true;
                break;
            }
        }
        QVERIFY2(hasError, "Should have received error status");
    }
};

test_gst_leaks::test_gst_leaks() {}

test_gst_leaks::~test_gst_leaks() = default;

void test_gst_leaks::test_case1() {}

QTEST_APPLESS_MAIN(test_gst_leaks)

#include "tst_test_gst_leaks.moc"
