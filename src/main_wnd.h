#ifndef MAIN_WND_H
#define MAIN_WND_H

#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>

// Forward declarations.
typedef struct _GtkWidget GtkWidget;
typedef union _GdkEvent GdkEvent;
typedef struct _GdkEventKey GdkEventKey;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreePath GtkTreePath;
typedef struct _GtkTreeViewColumn GtkTreeViewColumn;
typedef struct _cairo cairo_t;

class WindowRenderer : public rtc::VideoSinkInterface<webrtc::VideoFrame>
{
  GtkWidget* _window;     // Our main window.
  GtkWidget* _draw_area;  // The drawing surface for rendering video streams.

  // std::unique_ptr<VideoRenderer> remote_renderer_;
  
  int _width;
  int _height;

  std::unique_ptr<uint8_t[]> _draw_buffer;
  std::unique_ptr<uint8_t[]> _image;
  int _draw_buffer_size;

public:
  WindowRenderer();
  ~WindowRenderer();

  // Creates and shows the main window with the |Connect UI| enabled.
  bool create();

  // Destroys the window.  When the window is destroyed, it ends the
  // main message loop.
  bool destroy();

  // Callback for when the main window is destroyed.
  void on_destroyed(GtkWidget* widget, GdkEvent* event);

  void on_redraw();

  void draw(GtkWidget* widget, cairo_t* cr);

protected:
  void OnFrame(const webrtc::VideoFrame& frame) override;
};


#endif /* MAIN_WND_H */
