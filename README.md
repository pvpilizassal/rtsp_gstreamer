# RTSP Viewer (Qt6 + GStreamer)

Минимальный десктоп-клиент для подключения к RTSP-камере с отображением видео и диагностической информации

## Возможности
- Подключение к RTSP-потоку по URL (с\без аутентификацией)
- Отображение видео в окне приложения
- Диагностика: разрешение видео (ширина х высота), примерный FPS (собственный подсчёт)
- Обработка ошибок: пустая строка URL, неверный URL, обрыв соединения, отказ сервера – с выводом понятного сообщения
- Разделение UI и логики GStreamer (отдельный поток, не блокирует Qt event loop)

## Зависимости для сборки
- **ОС**: Windows 10/11 (x86_64), тестировалось на Windows 10/11
- **CMake** >= 3.16
- **Qt 6.2+** (Widgets)
- **GStreamer 1.24+** (MinGW 64-bit runtime и development)
- Компилятор: MinGW-w64 (из состава Qt или MSYS2)

### Получение GStreamer (MinGW)
Рекомендуется официальный MinGW‑пакет с https://gstreamer.freedesktop.org/download/#windows (раздел «Windows (MinGW 64-bit)»).  
Также можно использовать пакеты MSYS2: 'mingw-w64-x86_64-gstreamer', 'mingw-w64-x86_64-gst-plugins-base', 
'mingw-w64-x86_64-gst-plugins-good', 'mingw-w64-x86_64-gst-plugins-bad', 
'mingw-w64-x86_64-gst-plugins-ugly', 'mingw-w64-x86_64-gst-libav'.

## Сборка из исходников
1. Установите Qt6 (с MinGW), CMake, GStreamer.
2. Откройте проект в Qt Creator или выполните в терминале:
   cmd
   mkdir build && cd build
   cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\mingw_64
   cmake --build . --config Release
3. Исполняемый файл появится в build\Release\rtsp_gstreamer.exe

## Использованные GStreamer‑элементы
- rtspsrc – приём RTSP‑потока
- decodebin – автоматическое декодирование
- d3d11videosink – отображение видео (Windows)
- Для тестового RTSP‑сервера: ksvideosrc, x264enc/x265enc, rtspclientsink

## Архитектура
- MainWindow (GUI) – только Qt Widgets, не содержит логики GStreamer
- VideoWorker – наследник QThread, содержит весь GStreamer‑пайплайн и GLib‑main‑loop
- Взаимодействие через сигналы/слоты (statusChanged, videoInfoUpdated, fpsUpdated), автоматическое QueuedConnection
- Отдельный поток гарантирует, что GStreamer не блокирует Qt event loop
- Подсчёт FPS: постоянный probe на sink‑пад видео‑sink’а увеличивает атомарный счётчик; раз в секунду таймер GLib сбрасывает счётчик и эмитит сигнал
- Определение кодека: кодек извлекается из RTP-структуры "encoding-name" в момент линковки rtspsrc с decodebin
- Разрешение видео захватывается одноразовым probe при первом буфере
- Ошибки обрабатываются через GstBus (watch), вызывают остановку цикла и смену статуса

## Тестирование
- Для тестирования RTSP‑поток создавался локально с помощью MediaMTX и команд GStreamer:
gst-launch-1.0 ksvideosrc device-index=0 ! videoconvert ! x264enc tune=zerolatency ! rtspclientsink location=rtsp://127.0.0.1:8554/cam