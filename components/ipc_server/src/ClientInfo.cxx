/*
 * ClientInfo.cxx
 *
 *  Created on: Apr 8, 2013
 *      Author: tom
 */

#include <QPoint>
#include <QRect>

#include "ClientInfo.hxx"
#include "ipc_server.hpp"

#include <cassert>

namespace LinksRouting
{

  static ClientInfo* a300_client = 0;

  /**
   * Get a cleaned string (remove special characters, unify umlauts, etc.)
   */
  QString cleanedString(QString const& str)
  {
    QString clean_str;
    for(int i = 0; i < str.length(); ++i)
    {
      QChar const& c = str.at(i).toLower();
      // TODO handle umlaute, etc. UTF-8/umlaute/whatever are not correctly
      //      reported by the xlib)
/*      if( c == 228 ) // ä
        clean_str.push_back("ae");
      else if( c == 246 ) // ö
        clean_str.push_back("oe");
      else if( c == 252 ) // ü
        clean_str.push_back("ue");
      else if( c == 223 ) // ß
        clean_str.push_back("sz");
      else*/ if(    ('a' <= c.unicode() && c.unicode() <= 'z')
               || ('0' <= c.unicode() && c.unicode() <= '9')
               || QString("@:").contains(c) )
        clean_str.push_back(c);
    }
    return clean_str;
  }

  //----------------------------------------------------------------------------
  ClientInfo::ClientInfo(QWebSocket* socket, IPCServer* ipc_server, WId wid):
    socket(socket),
    _pid(0),
    _dirty(~0),
    _ipc_server(ipc_server),
    _window_info(wid),
    _minimized_icon(std::make_shared<LinkDescription::Node>()),
    _covered_outline(std::make_shared<LinkDescription::Node>()),
    _avg_region_height(0)
  {
    _minimized_icon->set("filled", true);
    _minimized_icon->set("show-in-preview", false);
    _minimized_icon->set("always-route", true);
    _minimized_icon->set("type", "minimized-icon");
    _covered_outline->set("widen-end", false);
    _covered_outline->set("outline-only", true);
    _covered_outline->set("show-in-preview", false);
    _covered_outline->set("visible", false);
    _covered_outline->set("outline-title", true);
    _covered_outline->set("type", "covered-outline");
  }

  //----------------------------------------------------------------------------
  ClientInfo::~ClientInfo()
  {
    clear();
    for(auto& node: _nodes)
    {
      auto p = node->getParent();
      if( p )
        p->removeNode(node);
    }
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setWindowId(WId wid)
  {
    _window_info.id = wid;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setPointerId(uint16_t ptr_id)
  {
    _window_info.ptr_id = ptr_id;
  }

  //----------------------------------------------------------------------------
  const WindowInfo& ClientInfo::getWindowInfo() const
  {
    return _window_info;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setId(const QString& id)
  {
    _id = id;
  }

  //----------------------------------------------------------------------------
  const QString& ClientInfo::id() const
  {
    return _id;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setType(const QString& type)
  {
    _type = type;
    _dirty |= MATCH;
  }

  //----------------------------------------------------------------------------
  const QString& ClientInfo::type() const
  {
    return _type;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setTitle(const QString& title)
  {
    _title = title;
    _dirty |= MATCH;
  }

  //----------------------------------------------------------------------------
  const QString& ClientInfo::title() const
  {
    return _title;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setProcessId(uint32_t pid)
  {
    _pid = pid;
    _dirty |= MATCH;
  }

  //----------------------------------------------------------------------------
  uint32_t ClientInfo::processId() const
  {
    return _pid;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setUrl(const QUrl& url)
  {
    _url = url;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setReportedGeometry(const QRect& geom)
  {
    _geom = geom;
    _dirty |= MATCH;
  }

  //----------------------------------------------------------------------------
  const QRect& ClientInfo::reportedGeometry() const
  {
    return _geom;
  }

  //----------------------------------------------------------------------------
  const QUrl& ClientInfo::url() const
  {
    return _url;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setStateData(const QJsonObject& data)
  {
    _state_data = data;
  }

  //----------------------------------------------------------------------------
  const QJsonObject& ClientInfo::stateData() const
  {
    return _state_data;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::addCommand(const QString& type)
  {
    _cmds.insert(type);
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::supportsCommand(const QString& cmd) const
  {
    return _cmds.contains(cmd);
  }

  //----------------------------------------------------------------------------
  void ClientInfo::parseView(const QJsonObject& msg)
  {
    bool has_viewport = msg.contains("viewport"),
         has_scroll_region = msg.contains("scroll-region");

    if( !has_viewport && !has_scroll_region )
      return;

    if( has_viewport )
      viewport = from_json<QRect>(msg.value("viewport"));

    if( has_scroll_region )
      scroll_region = from_json<QRect>(msg.value("scroll-region"));
    else
      // If no scroll-region is given assume only content within viewport is
      // accessible
      scroll_region = QRect(QPoint(0,0), viewport.size());

    preview_size = scroll_region.size();
    _dirty |= SCROLL_POS | SCROLL_SIZE;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setScrollPos( const QPoint& offset )
  {
    const QSize size = scroll_region.size();
    scroll_region.setTopLeft(offset);
    scroll_region.setSize(size);

    _dirty |= SCROLL_POS;

    for(auto& popup: _popups)
      popup->hover_region.offset = -offset;
  }

  //----------------------------------------------------------------------------
  LinkDescription::NodePtr ClientInfo::parseRegions
  (
    const QJsonObject& msg,
    LinkDescription::NodePtr node
  )
  {
    qDebug() << "Parse regions of" << _window_info.title;

    _avg_region_height = 0;
    _dirty |= REGIONS;

    if( !msg.contains("regions") )
    {
      if( node )
        node->getChildren().front()->getNodes().clear();
      else
        node = std::make_shared<LinkDescription::Node>();

      node->set("display-num", 0);
      return node;
    }

    QVariantList regions = from_json<QVariantList>(msg.value("regions"));

    LinkDescription::PropertyMap node_props;
    for(auto region = regions.begin(); region != regions.end(); ++region)
    {
      if( region->type() != QVariant::Map )
        continue;

      auto map = region->toMap();
      for( auto it = map.constBegin(); it != map.constEnd(); ++it )
        node_props.set(to_string(it.key()), it->toString());
    }

    float2 top_left( getViewportAbs().topLeft() ),
           scroll_offset( scroll_region.topLeft() );
    LinkDescription::nodes_t nodes;

    bool const node_rel = node_props.get<bool>("rel", false);
    std::string const node_ref = node_props.get<std::string>("ref", "abs");

    for(auto region = regions.begin(); region != regions.end(); ++region)
    {
      if( region->type() != QVariant::List )
        continue;

      LinkDescription::points_t points;
      LinkDescription::PropertyMap region_props;

      float min_y = std::numeric_limits<float>::max(),
            max_y = std::numeric_limits<float>::lowest();

      //for(QVariant point: region.toList())
      auto regionlist = region->toList();
      for(auto point = regionlist.begin(); point != regionlist.end(); ++point)
      {
        if( point->type() == QVariant::Map )
        {
          auto map = point->toMap();
          for( auto it = map.constBegin(); it != map.constEnd(); ++it )
            region_props.set(to_string(it.key()), it->toString());
        }
        else
        {
          points.push_back( JSONNode(*point).getValue<float2>() );

          if( points.back().y > max_y )
            max_y = points.back().y;
          if( points.back().y < min_y )
            min_y = points.back().y;
        }
      }

      if( points.empty() )
        continue;

      _avg_region_height += max_y - min_y;
      float2 center;

      bool rel = region_props.get<bool>("rel", node_rel);
      std::string ref = region_props.get<std::string>("ref", node_ref);

      for(size_t i = 0; i < points.size(); ++i)
      {
        if( ref == "abs" )
        {
          // Transform to local coordinates within applications scrollable area
          if( !rel )
            points[i] -= top_left;
          points[i] -= scroll_offset;
        }
        center += points[i];
      }

      center /= points.size();
      LinkDescription::points_t link_points;
      //link_points.push_back(center); // TODO linkpoints...

      nodes.push_back
      (
        std::make_shared<LinkDescription::Node>(points, link_points, region_props)
      );
    }

    if( !nodes.empty() )
      _avg_region_height /= nodes.size();

    size_t num_regions = from_json<size_t>(msg.value("display-num"), 0);
    if( !num_regions )
      num_regions = nodes.size();

    auto hedge = LinkDescription::HyperEdge::make_shared(nodes, node_props);
    hedge->addNode(_minimized_icon);
    hedge->addNode(_covered_outline);
    //updateHedge(_window_monitor.getWindows(), hedge.get());

    if( !node )
    {
      node = std::make_shared<LinkDescription::Node>(hedge);
      _nodes.push_back(node);
    }
    else
    {
      node->clearChildren(); // TODO allow also adding regions?
      node->addChild(hedge);
    }

    node->set("display-num", num_regions);
    return node;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateWindowInfo(const WindowRegions& windows)
  {
    if( _window_info.isValid() && !(_dirty & MATCH) )
      return true;

    WindowInfoIterators possible_windows;
    if( processId() )
      // Only further check windows matching the given process id
      possible_windows = windows.find_all(processId(), "");
    else
      // Get all windows for further checks
      possible_windows = windows.find_all("");

    auto checkMatchOne = [&]( WindowInfoIterators const& w_its,
                              QString const& reason )
    {
      if( w_its.size() != 1 )
        return false;

      _dirty &= ~MATCH;

      WindowInfo const& winfo = *w_its.front();
      if( winfo.id == _window_info.id )
      {
#ifdef WINDOW_MATCH_DEBUG
        qDebug() << "Not updating already correct match";
#endif
        return true;
      }

      qDebug() << "Found one remaining matching window by" << reason
                                                           << winfo.title;

      if( _window_info.isValid() )
        qDebug() << "Replace with new match";

      _window_info = winfo;
      _dirty |= WINDOW;

      return true;
    };

    if( checkMatchOne(possible_windows, "process id") )
      return true;

    bool is_browser = type() == "Browser";
    const QRect& geom = reportedGeometry();

    QString clean_title = cleanedString(title());
    qDebug() << "check match region and/or title" << geom << title() << clean_title;

    WindowInfoIterators matching_windows;
    for(auto w: possible_windows)
    {
      if(    w->region.x() != geom.x()
          || w->region.y() != geom.y()
          || w->region.width() != geom.width()
             // ignore title bar of maximized windows, because
             // they are integrated into the global menubar
          || std::abs(w->region.height() - geom.height()) > 30 )
      {
#ifdef WINDOW_MATCH_DEBUG
        qDebug() << " - region mismatch" << w->region << w->title;
#endif
        continue;
      }

#ifdef WINDOW_MATCH_DEBUG
      qDebug() << " - region match" << w->region << w->title;
#endif

      if( is_browser && !w->title.contains("Mozilla Firefox")
                     && !w->title.contains("Aurora") )
      {
#ifdef WINDOW_MATCH_DEBUG
        qDebug() << " - region not a Firefox window";
#endif
        continue;
      }

      QString clean_window_title = cleanedString(w->title);
      qDebug() << "-" << w->title << "clean" << clean_window_title;

      if( !clean_window_title.startsWith(clean_title) )
      {
#ifdef WINDOW_MATCH_DEBUG
        qDebug() << " - title missmatch" << clean_window_title << clean_title;
#endif
        continue;
      }

#ifdef WINDOW_MATCH_DEBUG
      qDebug() << " - match";
#endif
      matching_windows.push_back(w);
    }

    if( matching_windows.size() > 1 )
    {
      qDebug() << "Found multiple matching windows -> selecting the top one";
      matching_windows.front() = matching_windows.back();
      matching_windows.resize(1);
    }

    // TODO handle cases where multiple clients match the same window
    //      recheck other matches if some match changes?

    return checkMatchOne(matching_windows, "geometry/title");
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::update(const WindowRegions& windows)
  {
    if( !updateWindowInfo(windows) )
    {
      qDebug() << "Failed to get a matching window info.";
      return false;
    }

    auto window_info = windows.find(_window_info.id);
    if( window_info == windows.end() )
      return false;

    if( _window_info != *window_info )
    {
      _window_info = *window_info;
      _dirty |= WINDOW;

      if( _window_info.title.contains("Airbus A300 - Wikipedia") )
        a300_client = this;
    }

    updateRegions(windows);

    if( !_dirty )
      return false;

    if( _dirty & (SCROLL_SIZE | REGIONS) )
      updateTileMap();

    for(auto& node: _nodes)
      updateHedges( node->getChildren() );

    _dirty = 0;
    return true;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateHoverCovered(WId hover_wid, const Rect& hover_region)
  {
    bool modified = false;
    for(auto& node: _nodes)
      for(auto& hedge: node->getChildren())
      {
        if( hedge->get<bool>("no-route") )
          continue;

        modified |= updateChildren(*hedge, hover_wid, hover_region);
      }

    return modified;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::removeLink(LinkDescription::HyperEdge* hedge)
  {
    for( auto node = _nodes.begin(); node != _nodes.end(); )
    {
      if( (*node)->getParent().get() == hedge )
      {
        hedge->removeNode(*node);
        node = _nodes.erase(node);
      }
      else
        ++node;
    }

    const std::string id = hedge->get<std::string>("link-id");
    for( auto popup = _popups.begin(); popup != _popups.end(); )
    {
      if( (*popup)->link_id == id )
      {
        _ipc_server->removePopup( *popup );
        popup = _popups.erase(popup);
      }
      else
        ++popup;
    }

    for( auto preview = _xray_previews.begin();
              preview != _xray_previews.end(); )
    {
      if( (*preview)->link_id == id )
      {
        _ipc_server->removeCoveredPreview( *preview );
        preview = _xray_previews.erase(preview);
      }
      else
        ++preview;
    }
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::hasLink(LinkDescription::HyperEdge* hedge) const
  {
    for(auto const& node: _nodes)
    {
      if( node->getParent().get() == hedge )
        return true;
    }

    return false;
  }

  //----------------------------------------------------------------------------
  QRect ClientInfo::getViewportAbs() const
  {
    return viewport.translated( _window_info.region.topLeft() );
  }

  //----------------------------------------------------------------------------
  QRect ClientInfo::getScrollRegionAbs() const
  {
    return scroll_region.translated( getViewportAbs().topLeft() );
  }

  //----------------------------------------------------------------------------
  void ClientInfo::activateWindow()
  {
    if( _window_info.isValid() )
      QxtWindowSystem::activeWindow(_window_info.id);
  }

  //----------------------------------------------------------------------------
  void ClientInfo::iconifyWindow()
  {
    if( _window_info.isValid() )
      QxtWindowSystem::iconifyWindow(_window_info.id);
  }

  //----------------------------------------------------------------------------
  void ClientInfo::moveResizeWindow(QRect const& geom)
  {
    if( !_window_info.isValid() )
      return;

    QxtWindowSystem::unmaxizeWindow(_window_info.id);
    QxtWindowSystem::moveResizeWindow(_window_info.id, geom);
  }

  //----------------------------------------------------------------------------
  void ClientInfo::setSemanticPreview(PreviewWindow* win)
  {
    _semantic_preview.reset(win);
  }

  //----------------------------------------------------------------------------
  PreviewWindow& ClientInfo::semanticPreview() const
  {
    return *_semantic_preview;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::hasSemanticPreview() const
  {
    return _semantic_preview.get() != nullptr;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::print( std::ostream& strm,
                          std::string const& indent,
                          std::string const& indent_incr ) const
  {
    strm << indent << "<ClientInfo address=\"" << this << "\">\n"
         << indent << indent_incr << "title: " << _window_info.title << "\n";

    for(auto const& node: _nodes)
      node->print(strm, indent + indent_incr, indent_incr);

    strm << indent << "</ClientInfo>\n";
  }

  //----------------------------------------------------------------------------
  float2 ClientInfo::getPreviewSize() const
  {
    return float2( _ipc_server->getPreviewWidth(),
                   _ipc_server->getPreviewHeight() );
  }

  //----------------------------------------------------------------------------
  void ClientInfo::createPopup( const float2& pos,
                                const float2& normal,
                                const std::string& text,
                                const LinkDescription::nodes_t& nodes,
                                const LinkDescription::NodePtr& node,
                                const std::string& link_id,
                                bool auto_resize,
                                ClientInfo* client,
                                LabelAlign align )
  {
    const QRect desktop_rect = _ipc_server->desktopRect().toQRect();

    float2 popup_pos, popup_size(text.length() * 10 + 6, 16);
    float2 hover_pos, hover_size = getPreviewSize();

    const size_t border_text = 4,
                 border_preview = 8;

    if( std::fabs(normal.y) > 0.5 )
    {
      popup_pos.x = pos.x - popup_size.x / 2;
      hover_pos.x = pos.x - hover_size.x / 2;

      if( normal.y < 0 )
      {
        popup_pos.y = pos.y - popup_size.y - border_text;
        hover_pos.y = popup_pos.y - hover_size.y + border_text
                                                 - 2 * border_preview;
      }
      else
      {
        popup_pos.y = pos.y + border_text;
        hover_pos.y = popup_pos.y + popup_size.y - border_text
                                                 + 2 * border_preview;
      }
    }
    else
    {
      switch( align )
      {
        case LabelAlign::RIGHT:
          popup_pos.x = pos.x - popup_size.x - border_text;
          break;
        default:
          popup_pos.x = pos.x + normal.x * border_text;
      }
      hover_pos.x = popup_pos.x + normal.x * (border_preview + border_text);
      if( normal.x > 0 )
        hover_pos.x += popup_size.x;
      else
      {
        popup_pos.x -= popup_size.x;
        hover_pos.x -= hover_size.x + popup_size.x;
      }

      popup_pos.y = pos.y - popup_size.y / 2;
      hover_pos.y = pos.y - hover_size.y / 2;

      clamp<float>( hover_pos.y,
                    desktop_rect.top() + border_preview,
                    desktop_rect.bottom() - border_preview - hover_size.y );
    }

    using SlotType::TextPopup;
    TextPopup::Popup popup = {
      text,
      link_id,
      nodes,
      node,
      nullptr,
      0,
      TextPopup::HoverRect(popup_pos, popup_size, border_text, !text.empty()),
      TextPopup::HoverRect(hover_pos, hover_size, border_preview, false),
      auto_resize && _ipc_server->getPreviewAutoWidth()
    };

    ClientInfo* ci = client ? client : this;
    popup.hover_region.offset = -ci->scroll_region.topLeft();
    popup.hover_region.dim = ci->viewport.size();
    popup.hover_region.scroll_region.size = ci->preview_size;
    popup.hover_region.tile_map = ci->tile_map;

    size_t height = ci->tile_map ? ci->tile_map->partitions_src.back().y
                                 - ci->tile_map->partitions_src.front().x
                                 : ci->scroll_region.height();

    if( height < 350 )
      popup.hover_region.zoom = 1;

    if( !ci->tile_map )
      qWarning() << "Missing tile_map for popup! title:" << ci->title()
                                               << "url:" << ci->url();

    _popups.push_back( _ipc_server->addPopup(*ci, popup) );
  }

  //----------------------------------------------------------------------------
  void ClientInfo::updateRegions(const WindowRegions& windows)
  {
    bool create_hidden_vis = _ipc_server->routingActive();

    clear();
    if( true ) //_dirty & WINDOW )
    {
      LinkDescription::points_t& icon = _minimized_icon->getVertices();
      icon.clear();

      if(    create_hidden_vis
          && _window_info.minimized
          && !_window_info.title.contains("Airbus A300 - Wikipedia") )
      {
        const QRect& region_launcher = _window_info.region_launcher;
        QPoint pos
        (
          region_launcher.right() + 11,
          (region_launcher.top() + region_launcher.bottom()) / 2
        );

        const int ICON_SIZE = 10;

        icon.push_back( pos );
        icon.push_back( pos + QPoint(ICON_SIZE, ICON_SIZE) );
        icon.push_back( pos + QPoint(ICON_SIZE,-ICON_SIZE) );

        LinkDescription::points_t link_points(1);
        link_points[0] = pos += QPoint(0.7 * ICON_SIZE, 0);
        _minimized_icon->setLinkPoints(link_points);

        if( !_nodes.empty() )
          createPopup
          (
            pos + QPoint(3, 0), // padding (also for border)
            float2(1,0),
            _nodes.front()->getChildren().front()->get<bool>("search-preview")
               ? ""
               : _nodes.front()->get<std::string>("display-num"),
            _nodes.front()->getChildren().front()->getNodes(),
            _minimized_icon,
            _nodes.front()->getParent()->get<std::string>("link-id")
          );
      }
    }

    auto first_above = windows.find(_window_info.id);
    if( first_above != windows.end() )
      first_above = first_above + 1;

    size_t num_covered = 0;
    bool modified = false;
    QRect desktop = _ipc_server->desktopRect().toQRect()
                                .translated( -getScrollRegionAbs().topLeft() ),
          local_view(-scroll_region.topLeft(), viewport.size());

    // Limit outside indicators to desktop region
    local_view = local_view.intersected(desktop);

    for(auto& node: _nodes)
    {
      modified |= node->set("minimized", _window_info.minimized);// || _window_info.covered);

      for(auto& hedge: node->getChildren())
      {
        if( hedge->get<bool>("no-route") )
          continue;

        const std::string& link_id = hedge->get<std::string>("link-id");

        /*
         * Check for regions scrolled away and not visualized by any see-
         * through visualization.
         */
        OutsideScroll outside_scroll[4] =
        {
          {float2(0, local_view.top()),    float2( 0, 1), 0},
          {float2(0, local_view.bottom()), float2( 0,-1), 0},
          {float2(local_view.left(),  0),  float2( 1, 0), 0},
          {float2(local_view.right(), 0),  float2(-1, 0), 0}
        };

        for( auto region = hedge->getNodes().begin();
                  region != hedge->getNodes().end(); )
        {
          if(    (*region)->get<std::string>("type") == "window-outline"
              || *region == _minimized_icon
              || *region == _covered_outline )
          {
            ++region;
            continue;
          }

          if( _window_info.minimized )// || _window_info.covered )
          {
            modified |= updateNode(**region);
            ++region;
            continue;
          }

          if( !(*region)->get<std::string>("outside-scroll").empty() )
          {
            region = hedge->getNodes().erase(region);
            continue;
          }

          LinkDescription::hedges_t& children = (*region)->getChildren();
          for(auto& child: children)
          {
            modified |= updateChildren( *child,
                                        desktop, local_view,
                                        windows, first_above );
          }

          modified |= updateNode( **region,
                                  desktop, local_view,
                                  windows, first_above );

          if( !(*region)->getVertices().empty() )
          {
            if( a300_client && (*region)->get<bool>("search-preview") )
            {
              LinkDescription::nodes_t a300_nodes;
              if( !a300_client->getNodes().empty() )
                a300_nodes = a300_client->getNodes().front()
                                        ->getChildren().front()
                                        ->getNodes();

              createPopup( getScrollRegionAbs().topLeft() + (*region)->getVertices().back(),
                           float2(1,0),
                           std::to_string(static_cast<unsigned long long>(a300_nodes.size())),
                           a300_nodes,
                           *region,
                           link_id,
                           true,
                           a300_client,
                           LabelAlign::RIGHT );
            }

            if(     (*region)->get<bool>("covered")
                && !(*region)->get<bool>("outside") )
              num_covered += 1;

            if(    /*(*region)->get<bool>("hidden")
                && */(*region)->get<bool>("outside") )
            {
              float2 center = (*region)->getCenter();
              for(auto& out: outside_scroll)
              {
                if( (center - out.pos) * out.normal > 0 )
                  continue;

                if( out.normal.x == 0 )
                  out.pos.x += center.x;
                else
                  out.pos.y += center.y;
                out.num_outside += 1;

                break;
              }
            }
          }

          if(   (*region)->get<bool>("covered")
             || (  (*region)->get<bool>("outside")
                && (*region)->get<bool>("on-screen")) )
          {
            if( create_hidden_vis )
              _xray_previews.push_back
              (
                _ipc_server->addCoveredPreview
                (
                  link_id,
                  *this,
                  *region,
                  tile_map_uncompressed,
                  getViewportAbs(),
                  getScrollRegionAbs(),
                  (*region)->get<bool>("outside")
                )
              );
          }

          ++region;
        }

        if( create_hidden_vis )
          for( size_t i = 0; i < 4; ++i )
          {
            OutsideScroll& out = outside_scroll[i];
            if( !out.num_outside )
              continue;

            if( out.normal.x == 0 )
            {
              out.pos.x /= out.num_outside;
              clamp<float>
              (
                out.pos.x,
                local_view.left() + 12,
                local_view.right() - 12
              );
            }
            else
            {
              out.pos.y /= out.num_outside;
              clamp<float>
              (
                out.pos.y,
                local_view.top() + 12,
                local_view.bottom() - 12
              );
            }

            float2 pos = out.pos;
            LinkDescription::points_t link_points, link_points_children;
            link_points_children.push_back(pos);
            link_points.push_back(pos += 7 * out.normal);

            LinkDescription::points_t points;
            points.push_back(pos +=  3 * out.normal + 12 * out.normal.normal());
            points.push_back(pos -= 10 * out.normal + 12 * out.normal.normal());
            points.push_back(pos += 10 * out.normal - 12 * out.normal.normal());

            auto new_node = std::make_shared<LinkDescription::Node>(points, link_points, link_points_children);
            new_node->set("outside-scroll", "side[" + std::to_string(static_cast<unsigned long long>(i)) + "]");
            new_node->set("filled", true);
            new_node->set("show-in-preview", false);
            new_node->set("type", "outside-scroll");
            new_node->set("is-icon", true);

            updateNode(*new_node, desktop, local_view, windows, first_above);
            hedge->addNode(new_node);

            if(     new_node->get<bool>("covered")
                && !new_node->get<bool>("outside") )
              num_covered += 1;

            createPopup( out.pos + getScrollRegionAbs().topLeft() + 13 * out.normal,
                         out.normal,
                         std::to_string(static_cast<unsigned long long>(out.num_outside)),
                         hedge->getNodes(),
                         new_node,
                         link_id );
          }
      }
    }

    LinkDescription::points_t& outline = _covered_outline->getVertices();
    outline.clear();

    if(    create_hidden_vis
        && (_window_info.covered || num_covered)
        && !_window_info.minimized )
    {
      _outlines.push_back( _ipc_server->addOutline(*this) );
      QPoint offset = _window_info.minimized
          ? QPoint()
          : getScrollRegionAbs().topLeft();

      Rect reg_title = _outlines.back()->region_title - offset;
      reg_title.size.x = std::min(150.f, reg_title.size.x);
      outline.push_back(reg_title.topLeft());
      outline.push_back(reg_title.topRight());
      outline.push_back(reg_title.bottomRight());
      outline.push_back(reg_title.bottomLeft());

//        for(auto& node: _nodes)
//          node->set("hidden", true);

      if( !_nodes.empty() )
      {
        _outlines.back()->preview = _ipc_server->addCoveredPreview
        (
          _nodes.front()->getParent()->get<std::string>("link-id"),
          *this,
          _covered_outline,
          tile_map_uncompressed,
          getViewportAbs(),
          getScrollRegionAbs()
        );
        _outlines.back()->preview_valid = true;
      }
    }

    if( modified )
      _dirty |= VISIBLITY;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateNode(
    LinkDescription::Node& node,
    const QRect& desktop, // in local
    const QRect& view,    // coordinates!
    const WindowRegions& windows,
    const WindowInfos::const_iterator& first_above
  )
  {
    if( node.getVertices().empty() )
      return false;

    bool modified = false,
         onscreen = true,
         covered = false,
         outside = false;
    Rect covering_region;
    WId covering_wid = 0;

    if( !node.get<bool>("is-window-outline", false) )
    {
      QPoint center_rel = node.getCenter().toQPoint(),
             center_abs = center_rel
                        + getScrollRegionAbs().topLeft();

      onscreen   = desktop.contains(center_rel),
      covered    = windows.hit( first_above,
                                center_abs,
                                &covering_region,
                                &covering_wid ),
      outside    = !view.contains(center_rel);
    }

    modified |= node.set("on-screen", onscreen);
    modified |= node.set("covered", covered);
    modified |= node.set("outside", outside);
    modified |= updateNode(node);

    node.set("covering-region", covering_region);
    node.set("covering-wid", covering_wid);

    return modified;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateChildren(
    LinkDescription::HyperEdge& hedge,
    const QRect& desktop,
    const QRect& view,
    const WindowRegions& windows,
    const WindowInfos::const_iterator& first_above
  )
  {
    bool modified = false;
    for(auto& node: hedge.getNodes())
      modified |= updateNode(*node, desktop, view, windows, first_above);
    return modified;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateNode( LinkDescription::Node& node,
                               WId hover_wid,
                               const Rect& preview_region )
  {
    if( node.getVertices().empty() || !node.get<bool>("on-screen") )
      return false;

    bool covered = false;
    if( !hover_wid )
    {
      // Reset covered if stop hovering
      covered = node.get<WId>("covering-wid") != 0;
    }
    else if( hover_wid != _window_info.id )
    {
      float2 center_abs = node.getCenter()
                        + getScrollRegionAbs().topLeft();
      covered = node.get<bool>("covered")
             || preview_region.contains(center_abs);
    }

    bool modified = node.set("covered", covered);
    modified |= updateNode(node);
    return modified;
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateChildren( LinkDescription::HyperEdge& hedge,
                                   WId hover_wid,
                                   const Rect& preview_region )
  {
    bool modified = false;
    for(auto& node: hedge.getNodes())
      modified |= updateNode(*node, hover_wid, preview_region);
    return modified;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::updateHedges( LinkDescription::hedges_t& hedges,
                                 bool first )
  {
    for(auto& hedge: hedges)
    {
      QPoint offset;
      if( hedge->get<std::string>("ref", "abs") == "viewport" )
        offset = getViewportAbs().topLeft();
      else if( !_window_info.minimized )
        offset = getScrollRegionAbs().topLeft();

      hedge->set("client_wid", _window_info.id);
      hedge->set("screen-offset", offset);

      if( first )
      {
        hedge->set("partitions_src", to_string(tile_map->partitions_src));
        hedge->set("partitions_dest", to_string(tile_map->partitions_dest));
      }

      for(auto& node: hedge->getNodes())
        updateHedges(node->getChildren(), false);
    }
  }

  //----------------------------------------------------------------------------
  bool ClientInfo::updateNode(LinkDescription::Node& node)
  {
    bool hidden =  node.get<bool>("minimized")
               || !node.get<bool>("on-screen")
               || ( !_ipc_server->getOutsideSeeThrough()
                 && node.get<bool>("outside") );

    return node.set("hidden", hidden || node.get<bool>("covered"));
  }

  //----------------------------------------------------------------------------
  void ClientInfo::updateTileMap()
  {
    assert(_ipc_server);

    const bool do_partitions = true;
    const bool partition_compress = true;
    const double preview_width = _ipc_server->getPreviewWidth(),
                 preview_height = _ipc_server->getPreviewHeight();

    preview_size = scroll_region.size();

    Partitions partitions_src,
               partitions_dest;
    size_t     margin_left = 0,
               margin_right = 0;

    if( do_partitions )
    {
      PartitionHelper part;
      for(auto const& node: _nodes)
        for(auto const& hedge: node->getChildren())
          for(auto const& region: hedge->getNodes())
          {
            if(     region->getVertices().empty()
                || !region->get<std::string>("outside-scroll").empty() )
              continue;

            const Rect& bb = region->getBoundingBox();
            part.add( float2( bb.t() - 3 * _avg_region_height,
                              bb.b() + 3 * _avg_region_height ) );
          }

      part.clip(0, scroll_region.height(), 2 * _avg_region_height);
      partitions_src = part.getPartitions();

      if( partition_compress && !partitions_src.empty() )
      {
        const int compress_size = _avg_region_height * 1.5;
        float cur_pos = 0;
        for( auto part = partitions_src.begin();
                  part != partitions_src.end();
                ++part )
        {
          if( part->x > cur_pos )
            cur_pos += compress_size;

          float2 target_part;
          target_part.x = cur_pos;

          cur_pos += part->y - part->x;
          target_part.y = cur_pos;

          partitions_dest.push_back(target_part);
        }

        if( partitions_src.back().y + 0.5 < scroll_region.height() )
          cur_pos += compress_size;

        int min_height = static_cast<float>(preview_height)
                       / preview_width
                       * scroll_region.width() + 0.5;
        cur_pos = std::max<int>(min_height, cur_pos);

        preview_size.setHeight(cur_pos);
//        margin_left = 170;
//        size_t width = preview_size.width() - margin_left;
//        margin_right = std::max<size_t>(width, 680) - 680;
//        preview_size.setWidth(width - margin_right);
      }
      else
        partitions_dest = partitions_src;
    }

    if( partitions_src.empty() )
    {
      partitions_src.push_back(float2(0, scroll_region.height()));
      partitions_dest = partitions_src;
    }

    tile_map =
      std::make_shared<HierarchicTileMap>( preview_size.width(),
                                           preview_size.height(),
                                           preview_width,
                                           preview_height );

    tile_map->partitions_src = partitions_src;
    tile_map->partitions_dest = partitions_dest;
    tile_map->margin_left = margin_left;
    tile_map->margin_right = margin_right;

    for(auto& popup: _popups)
    {
      popup->hover_region.tile_map = tile_map;
      popup->hover_region.scroll_region.size = preview_size;
    }

    const unsigned int TILE_SIZE = 512;
    tile_map_uncompressed =
      std::make_shared<HierarchicTileMap>( scroll_region.width(),
                                           scroll_region.height(),
                                           TILE_SIZE,
                                           TILE_SIZE );
    tile_map_uncompressed->partitions_src
      .push_back(float2(0, scroll_region.height()));
    tile_map_uncompressed->partitions_dest =
      tile_map_uncompressed->partitions_src;

    for(auto& preview: _xray_previews)
      preview->tile_map = tile_map_uncompressed;
  }

  //----------------------------------------------------------------------------
  void ClientInfo::clear()
  {
    if( _ipc_server )
    {
      _ipc_server->removePopups(_popups);
      _ipc_server->removeCoveredPreviews(_xray_previews);
      _ipc_server->removeOutlines(_outlines);
    }

    _popups.clear();
    _xray_previews.clear();
    _outlines.clear();
  }

} // namespace LinksRouting
