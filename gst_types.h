#ifndef GST_TYPES_H
#define GST_TYPES_H

#include <gst/gst.h>
#include "gst_ptr.h"

// void* = gpointer
namespace GstDetails {
// элементы
inline void element_unref(GstElement* p) { if (p) gst_object_unref(p); }
// шины
inline void bus_unref(GstBus* p)         { if (p) gst_object_unref(p); }
// параметры потока
inline void caps_unref(GstCaps* p)       { if (p) gst_caps_unref(p); }
// сообщения
inline void message_unref(GstMessage* p) { if (p) gst_message_unref(p); }
// тип символов GLib
inline void g_free_char(gchar* p)        { if (p) g_free(p); }
// поток
inline void main_loop_unref(GMainLoop* p) { if (p) g_main_loop_unref(p); }
}

// псевдонимы элементов и их деструкторов с использованием обертки
using GstElementPtr = GstPtr<GstElement, GstDetails::element_unref>;
using GstBusPtr     = GstPtr<GstBus,     GstDetails::bus_unref>;
using GstCapsPtr    = GstPtr<GstCaps,    GstDetails::caps_unref>;
using GstMessagePtr = GstPtr<GstMessage, GstDetails::message_unref>;
using GcharPtr      = GstPtr<gchar,      GstDetails::g_free_char>;
using GMainLoopPtr = GstPtr<GMainLoop, GstDetails::main_loop_unref>;

#endif // GST_TYPES_H
