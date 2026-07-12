cmake_minimum_required(VERSION 3.16)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets)

# ищем GStreamer так же, как в корневом проекте
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Исходники теста + реализация VideoWorker
add_executable(test_gst_leaks
    tst_test_gst_leaks.cpp
    ../videoworker.cpp
)

target_include_directories(test_gst_leaks PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(test_gst_leaks PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    ${GSTREAMER_LIBRARIES}
)

target_link_directories(test_gst_leaks PRIVATE ${GSTREAMER_LIBRARY_DIRS})

target_link_libraries(test_gst_leaks PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    ${GSTREAMER_LIBRARIES}
)

# регистрируем тест с именем "gst_leaks"
add_test(NAME gst_leaks COMMAND test_gst_leaks)

# назначаем переменную окружения для обнаружения утечек
set_tests_properties(gst_leaks PROPERTIES ENVIRONMENT "GST_TRACERS=leaks")