#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "./ui_mainwindow.h"
#include "videoworker.h"

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onStatusChanged(const QString &status);

private:
    std::unique_ptr<Ui::MainWindow> ui;
    // указатель на видео-поток
    VideoWorker* m_worker = nullptr;

    QPushButton* m_btnConnect = nullptr;
    QPushButton* m_btnDisconnect = nullptr;
    QLabel* m_lblStatus = nullptr;
};
#endif // MAINWINDOW_H
