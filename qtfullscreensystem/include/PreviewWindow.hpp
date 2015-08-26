/*
 * Window for showing out-of-viewport zoom- and panable preview
 *
 *  Created on: Oct 21, 2014
 *      Author: tom
 */

#ifndef QTF_PREVIEW_WINDOW_HPP_
#define QTF_PREVIEW_WINDOW_HPP_

#include <common/PreviewWindow.hpp>
#include <slots.hpp>
#include <slotdata/mouse_event.hpp>
#include <slotdata/text_popup.hpp>
#include <slotdata/TileHandler.hpp>

#include <QWindow>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>

namespace qtfullscreensystem
{
  namespace LR = LinksRouting;

  class QtPreviewWindow:
    public QWindow,
    protected QOpenGLFunctions,
    public LR::PreviewWindow
  {
    public:
      QtPreviewWindow();

      void subscribeSlots(LR::SlotSubscriber& slot_subscriber);

      virtual void initialize();

      /// Send geometry for preview (excluding space for margin/border)
      virtual void setPreviewGeometry(QRect const&);

      virtual void render(QPainter *painter) = 0;
      virtual void render();

      virtual void update(double dt);
      virtual void release();

    public slots:
      void renderLater();
      void renderNow();

      void onTileChanged(size_t x, size_t y, size_t zoom);

    protected:
      QRect     _geometry;
      int       _margin;

      bool    _do_drag;
      float2  _last_mouse_pos;

      GLImageCache _image_cache;
      unsigned int _tile_map_change_id;

      LR::slot_t<LR::SlotType::MouseEvent>::type  _subscribe_mouse;
      LR::slot_t<LR::SlotType::TextPopup>::type   _subscribe_popups;
      LR::slot_t<LR::SlotType::TileHandler>::type _subscribe_tile_handler;

      virtual QRect getGeometry() const = 0;
      virtual HierarchicTileMapPtr getTilemap() const = 0;

      virtual void onMouseMove(float2 const& delta) {}
      virtual void onMouseDrag(float2 const& delta) {}
      virtual void onMouseLeave() {}

      virtual bool event(QEvent*);
      virtual void exposeEvent(QExposeEvent*);

      virtual void mouseReleaseEvent(QMouseEvent*);
      virtual void mouseMoveEvent(QMouseEvent*);
      virtual void wheelEvent(QWheelEvent*);

    private:
      bool _update_pending;
      HierarchicTileMapWeakPtr _last_tilemap;

      QOpenGLContext     *_context;
      QOpenGLPaintDevice *_device;

      void init();
      void updateGeometry();
  };

  class PopupWindow:
    public QtPreviewWindow
  {
    public:
      explicit PopupWindow(Popup* popup);

      virtual void render(QPainter *painter);
      virtual void update(double dt);

      virtual void onMouseMove(float2 const& delta);
      virtual void onMouseDrag(float2 const& delta);
      virtual void onMouseLeave();

    protected:
      Popup *_popup;

      virtual QRect getGeometry() const;
      virtual HierarchicTileMapPtr getTilemap() const;
  };

  class SeeThroughWindow:
    public QtPreviewWindow
  {
    public:
      explicit SeeThroughWindow(SeeThrough* seethrough);

      virtual void render(QPainter *painter);

      virtual void onMouseMove(float2 const& delta);
      virtual void onMouseDrag(float2 const& delta);
      virtual void onMouseLeave();


    protected:
      SeeThrough *_see_through;

      virtual QRect getGeometry() const;
      virtual HierarchicTileMapPtr getTilemap() const;
  };

  class SemanticPreviewWindow:
    public QtPreviewWindow
  {
    public:
      explicit SemanticPreviewWindow(LR::ClientWeakRef client);

      virtual void render(QPainter *painter);

      virtual void onMouseMove(float2 const& delta);
      virtual void onMouseDrag(float2 const& delta);
      virtual void onMouseLeave();

      virtual void wheelEvent(QWheelEvent*);

    protected:
      LR::ClientWeakRef _client;

      virtual QRect getGeometry() const;
      virtual HierarchicTileMapPtr getTilemap() const;
  };

} // namespace qtfullscreensystem

#endif /* PREVIEW_WINDOW_HPP_ */
