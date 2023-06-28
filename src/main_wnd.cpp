#include "main_wnd.h"

#include <iostream>

#include <cairo.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib-object.h>
#include <glib.h>
#include <gobject/gclosure.h>
#include <gtk/gtk.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "api/video/video_source_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "libyuv/convert.h"
#include "libyuv/convert_from.h"


gboolean on_destroyed_callback(GtkWidget* widget,
                             GdkEvent* event,
                             gpointer data) {
  reinterpret_cast<WindowRenderer*>(data)->on_destroyed(widget, event);
  return FALSE;
}

gboolean redraw(gpointer data)
{
  WindowRenderer* wnd = reinterpret_cast<WindowRenderer*>(data);
  wnd->on_redraw();
  return false;
}

gboolean draw(GtkWidget* widget, cairo_t* cr, gpointer data)
{
  WindowRenderer* wnd = reinterpret_cast<WindowRenderer*>(data);
  wnd->draw(widget, cr);
  return false;
}

WindowRenderer::WindowRenderer()
{

}

WindowRenderer::~WindowRenderer()
{

}

bool WindowRenderer::create()
{
  RTC_DCHECK(_window == NULL);

  _window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (_window) {    
    gtk_window_set_position(GTK_WINDOW(_window), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(_window), 640, 480);
    gtk_window_set_title(GTK_WINDOW(_window), "Tunnel Player");
    g_signal_connect(G_OBJECT(_window), "delete-event",
                     G_CALLBACK(&on_destroyed_callback), this);

    gtk_container_set_border_width(GTK_CONTAINER(_window), 0);
    
    _draw_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(_window), _draw_area);
    g_signal_connect(G_OBJECT(_draw_area), "draw", G_CALLBACK(&::draw), this);

    gtk_widget_show_all(_window);
    gtk_window_present((GtkWindow*)_window);
  }

  return _window != NULL;
}

bool WindowRenderer::destroy()
{
  gtk_widget_destroy(_window);
  _window = NULL;

  return true;

}

void WindowRenderer::on_destroyed(GtkWidget* widget, GdkEvent* event)
{
  _window = NULL;
  _draw_area = NULL;
}

void WindowRenderer::on_redraw()
{
  gdk_threads_enter();

  if (_image != NULL && _draw_area != NULL) {

    if (!_draw_buffer.get()) {
      _draw_buffer_size = (_width * _height * 4) * 4;
      _draw_buffer.reset(new uint8_t[_draw_buffer_size]);
      gtk_widget_set_size_request(_draw_area, _width * 2, _height * 2);
    }

    const uint32_t* image =
      reinterpret_cast<const uint32_t*>(_image.get());
    uint32_t* scaled = reinterpret_cast<uint32_t*>(_draw_buffer.get());
    for (int r = 0; r < _height; ++r) {
      for (int c = 0; c < _width; ++c) {
        int x = c * 2;
        scaled[x] = scaled[x + 1] = image[c];
      }

      uint32_t* prev_line = scaled;
      scaled += _width * 2;
      memcpy(scaled, prev_line, (_width * 2) * 4);

      image += _width;
      scaled += _width * 2;
    }

    gtk_widget_queue_draw(_draw_area);
  }

  gdk_threads_leave();
}

void WindowRenderer::draw(GtkWidget* widget, cairo_t* cr)
{  
  cairo_format_t format = CAIRO_FORMAT_ARGB32;
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
								 _draw_buffer.get(), format, _width * 2, _height * 2,
								 cairo_format_stride_for_width(format, _width * 2));
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_rectangle(cr, 0, 0, _width * 2, _height * 2);
  cairo_fill(cr);
  cairo_surface_destroy(surface);
}

void WindowRenderer::OnFrame(const webrtc::VideoFrame& frame)
{
  gdk_threads_enter();
  
  rtc::scoped_refptr<webrtc::I420BufferInterface> buffer(
      frame.video_frame_buffer()->ToI420());
  if (frame.rotation() != webrtc::kVideoRotation_0) {
    buffer = webrtc::I420Buffer::Rotate(*buffer, frame.rotation());
  }

  if (_width != frame.width() || _height != frame.height()) {
    _width = frame.width();
    _height = frame.height();
    _image.reset(new uint8_t[_width * _height * 4]);
  }


  libyuv::I420ToARGB(buffer->DataY(), buffer->StrideY(), buffer->DataU(),
                     buffer->StrideU(), buffer->DataV(), buffer->StrideV(),
                     _image.get(), _width * 4, buffer->width(),
                     buffer->height());

  gdk_threads_leave();

  g_idle_add(redraw, this);
}
