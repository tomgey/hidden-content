/*!
 * @file ipc_server.cpp
 * @brief
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 02.03.2012
 */

#include "ipc_server.hpp"
#include "log.hpp"
#include "JSONParser.h"

#include <QMutex>

#include <algorithm>
#include <map>
#include <tuple>

namespace LinksRouting
{
  using namespace std::placeholders;

  WId windowAt( const WindowRegions& regions,
                const QPoint& pos );
  std::string to_string(const QRect& r)
  {
    std::stringstream strm;
    strm << r.left() << ' ' << r.top() << ' ' << r.right() << ' ' << r.bottom();
    return strm.str();
  }

  std::string WIdtoStr(WId wid)
  {
    return std::to_string(
#ifdef _WIN32
      reinterpret_cast<unsigned long long>(wid)
#else
      wid
#endif
    );
  }

  template<typename T>
  void clamp(T& val, T min, T max)
  {
    val = std::max(min, std::min(val, max));
  }

  template<typename T>
  void clampedStep( T& val,
                    T step,
                    T min,
                    T max,
                    T max_step = 1)
  {
    step = std::max(-max_step, std::min(step, max_step));
    clamp<T>(val += step, min, max);
  }

  //----------------------------------------------------------------------------
  IPCServer::IPCServer( QMutex* mutex,
                        QWaitCondition* cond_data,
                        QWidget* widget ):
    Configurable("QtWebsocketServer"),
    _window_monitor(widget, std::bind(&IPCServer::regionsChanged, this, _1)),
    _mutex_slot_links(mutex),
    _cond_data_ready(cond_data),
    _interaction_handler(this)
  {
    assert(widget);
    registerArg("DebugRegions", _debug_regions);
    registerArg("PreviewWidth", _preview_width = 320);
    registerArg("PreviewHeight", _preview_height = 384);

//    qRegisterMetaType<WindowRegions>("WindowRegions");
//    assert( connect( &_window_monitor, SIGNAL(regionsChanged(WindowRegions)),
//             this,             SLOT(regionsChanged(WindowRegions)) ) );
  }

  //----------------------------------------------------------------------------
  IPCServer::~IPCServer()
  {

  }

  //----------------------------------------------------------------------------
  void IPCServer::publishSlots(SlotCollector& slot_collector)
  {
    _slot_links = slot_collector.create<LinkDescription::LinkList>("/links");
  }

  //----------------------------------------------------------------------------
  void IPCServer::subscribeSlots(SlotSubscriber& slot_subscriber)
  {
    _subscribe_routing =
      slot_subscriber.getSlot<SlotType::ComponentSelection>("/routing");
    _subscribe_user_config =
      slot_subscriber.getSlot<LinksRouting::Config*>("/user-config");
    assert(_subscribe_user_config->_data.get());
    _subscribe_mouse =
      slot_subscriber.getSlot<LinksRouting::SlotType::MouseEvent>("/mouse");
    _subscribe_popups =
      slot_subscriber.getSlot<SlotType::TextPopup>("/popups");
  }

  //----------------------------------------------------------------------------
  bool IPCServer::startup(Core* core, unsigned int type)
  {
    _window_monitor.start();
    return true;
  }

  //----------------------------------------------------------------------------
  void IPCServer::init()
  {
    int port = 4487;
    _server = new QWsServer(this);
    if( !_server->listen(QHostAddress::Any, port) )
    {
      LOG_ERROR("Failed to start WebSocket server on port " << port << ": "
                << _server->errorString().toStdString() );
    }
    else
      LOG_INFO("WebSocket server listening on port " << port);

    connect(_server, SIGNAL(newConnection()), this, SLOT(onClientConnection()));
  }

  //----------------------------------------------------------------------------
  void IPCServer::shutdown()
  {

  }

  //----------------------------------------------------------------------------
  void IPCServer::process(unsigned int type)
  {
    if( !_debug_regions.empty() )
    {
      onTextReceived( QString::fromStdString(_debug_regions) );
      _debug_regions.clear();
    }
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientConnection()
  {
    QWsSocket* client_socket = _server->nextPendingConnection();
    QObject* client_object = qobject_cast<QObject*>(client_socket);

    connect(client_object, SIGNAL(frameReceived(QString)), this, SLOT(onTextReceived(QString)));
    connect(client_object, SIGNAL(frameReceived(QByteArray)), this, SLOT(onBinaryReceived(QByteArray)));
    connect(client_object, SIGNAL(disconnected()), this, SLOT(onClientDisconnection()));
    connect(client_object, SIGNAL(pong(quint64)), this, SLOT(onPong(quint64)));

    _clients[ client_socket ] = ClientInfo();

    LOG_INFO(   "Client connected: "
             << client_socket->peerAddress().toString().toStdString()
             << ":" << client_socket->peerPort() );
  }

  //----------------------------------------------------------------------------
  void IPCServer::onTextReceived(QString data)
  {
    auto client = _clients.find(qobject_cast<QWsSocket*>(sender()));
    if( client == _clients.end() )
    {
      LOG_WARN("Received message from unknown client: " << data.toStdString());
      return;
    }

    ClientInfo& client_info = client->second;
    WId& client_wid = client_info.wid;

    std::cout << "Received (" << client_wid << "): "
              << data.toStdString()
              << std::endl;

    try
    {
      JSONParser msg(data);

      QString task = msg.getValue<QString>("task");
      if( task == "REGISTER" || task == "RESIZE" )
      {
        if( task == "REGISTER" )
        {
          QVariantList pos_list = msg.getValue<QVariantList>("pos");
          if( pos_list.length() != 2 )
          {
            LOG_WARN("Received invalid position (REGISTER)");
            return;
          }

          QPoint pos(pos_list.at(0).toInt(), pos_list.at(1).toInt());
          client_wid = windowAt(_window_monitor.getWindows(), pos);
        }

        client_info.region = msg.getValue<QRect>("region");

        return;
      }

      QString id = msg.getValue<QString>("id").toLower(),
              title = msg.getValue<QString>("title", "");
      const std::string& id_str = id.toStdString();

      if( task == "GET" )
      {
        QString val;
        if( id == "/routing" )
        {
          val = "{\"active\": \""
              + QString::fromStdString(_subscribe_routing->_data->active)
              + "\", \"default\": \""
              + QString::fromStdString(_subscribe_routing->_data->getDefault())
              + "\", \"available\":[";
          if( !_subscribe_routing->_data->available.empty() )
          {
            for( auto comp = _subscribe_routing->_data->available.begin();
                 comp != _subscribe_routing->_data->available.end();
                 ++comp )
              val += "[\"" + QString::fromStdString(comp->first) + "\","
                   + (comp->second ? '1' : '0') + "],";
            val.replace(val.length() - 1, 1, ']');
          }
          else
            val.push_back(']');
          val.push_back('}');
        }
        else if( id == "/search-history" )
        {
          std::string history;
          (*_subscribe_user_config->_data)->getString("QtWebsocketServer:SearchHistory", history);
          val = "\""
              + QString::fromStdString(history).replace('"', "\\\"")
              + "\"";
        }
        else
        {
          LOG_WARN("Requesting unknown value: " << id_str);
          return;
        }

        client->first->write
        ("{"
            "\"task\": \"GET-FOUND\","
            "\"id\": \"" + id.replace('"', "\\\"") + "\","
            "\"val\":" + val +
         "}");

        return;
      }
      else if( task == "SET" )
      {
        if( id == "/routing" )
        {
          _subscribe_routing->_data->request =
            msg.getValue<QString>("val").toStdString();
          for( auto link = _slot_links->_data->begin();
                   link != _slot_links->_data->end();
                   ++link )
            link->_stamp += 1;
          LOG_INFO("Trigger reroute -> routing algorithm changed");
          _cond_data_ready->wakeAll();
        }
        else if( id == "/config" )
        {
          (*_subscribe_user_config->_data)->setParameter
          (
            msg.getValue<QString>("var").toStdString(),
            msg.getValue<QString>("val").toStdString(),
            msg.getValue<QString>("type").toStdString()
          );
        }
        else
          LOG_WARN("Request setting unknown value: " << id_str);

        return;
      }

      uint32_t stamp = msg.getValue<uint32_t>("stamp");
      QMutexLocker lock_links(_mutex_slot_links);

      auto link = std::find_if
      (
        _slot_links->_data->begin(),
        _slot_links->_data->end(),
        [&id_str](const LinkDescription::LinkDescription& desc)
        {
          return desc._id == id_str;
        }
      );

      if( task == "INITIATE" )
        onInitiate(link, id, stamp, msg, client_info);
      else if( task == "FOUND" )
      {
        if( link == _slot_links->_data->end() )
        {
          LOG_WARN("Received FOUND for none existing REQUEST");
          return;
        }

        if( link->_stamp != stamp )
        {
          LOG_WARN("Received FOUND for wrong REQUEST (different stamp)");
          return;
        }

        LOG_INFO("Received FOUND: " << id_str);

        updateScrollRegion(msg, client_info);
        link->_link->addNode( parseRegions(msg, client_info) );
        updateCenter(link->_link.get());
      }
      else if( task == "ABORT" )
      {
        if( id.isEmpty() && stamp == (uint32_t)-1 )
          _slot_links->_data->clear();
        else if( link != _slot_links->_data->end() )
          _slot_links->_data->erase(link);
        else
          LOG_WARN("Received ABORT for none existing REQUEST");

        for(auto socket = _clients.begin(); socket != _clients.end(); ++socket)
          if( sender() != socket->first )
            socket->first->write(data);
      }
      else
      {
        LOG_WARN("Unknown task: " << task.toStdString());
        return;
      }
    }
    catch(std::runtime_error& ex)
    {
      LOG_WARN("Failed to parse message: " << ex.what());
    }

//    std::map<std::string, std::tuple<std::string>> types
//    {
//      {"x", std::make_tuple("int")},
//      {"y", std::make_tuple("int")},
//      {"key", std::make_tuple("string")}
//    };
    _cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onBinaryReceived(QByteArray data)
  {
    if( data.size() < 3 )
    {
      LOG_WARN("Binary message too small (" << data.size() << "byte)");
      return;
    }

    uint8_t type = data.at(0),
            seq_id = data.at(1);

    std::cout << "Binary data received: "
              << ((data.size() / 1024) / 1024.f) << "MB"
              << " type=" << (int)type
              << " seq=" << (int)seq_id << std::endl;

    QMutexLocker lock_links(_mutex_slot_links);

    if( type != 1 )
    {
      LOG_WARN("Invalid binary data!");
      return;
    }

    std::cout << "request #" << (int)seq_id << std::endl;

    auto request = _interaction_handler._tile_requests.find(seq_id);
    if( request == _interaction_handler._tile_requests.end() )
    {
      LOG_WARN("Received unknown tile request.");
      return;
    }

    HierarchicTileMapPtr tile_map = request->second.tile_map.lock();
    if( !tile_map )
    {
      LOG_WARN("Received tile request for expired map.");
      _interaction_handler._tile_requests.erase(request);
      return;
    }

    tile_map->setTileData( request->second.x,
                           request->second.y,
                           request->second.zoom,
                           data.constData() + 2,
                           data.size() - 2 );

    _cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onPong(quint64 elapsedTime)
  {

  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientDisconnection()
  {
    QWsSocket * socket = qobject_cast<QWsSocket*>(sender());
    if( !socket )
      return;

    _clients.erase(socket);
    socket->deleteLater();

    LOG_INFO("Client disconnected");
  }

  //----------------------------------------------------------------------------
  void IPCServer::onInitiate( LinkDescription::LinkList::iterator& link,
                              const QString& id,
                              uint32_t stamp,
                              const JSONParser& msg,
                              ClientInfo& client_info )
  {
    const std::string id_str = id.toStdString();
    LOG_INFO("Received INITIATE: " << id_str);
    uint32_t color_id = 0;

    std::string history;
    (*_subscribe_user_config->_data)->getString("QtWebsocketServer:SearchHistory", history);

    QString clean_id = QString(id).replace(",","") + ",";
    QString new_history = clean_id
                        + QString::fromStdString(history).replace(clean_id, "");

    (*_subscribe_user_config->_data)->setString("QtWebsocketServer:SearchHistory",
                                                new_history.toStdString());

    updateScrollRegion(msg, client_info);

    // TODO keep working for multiple links at the same time
    _subscribe_mouse->_data->clear();
    _subscribe_popups->_data->popups.clear();

    // Remove eventually existing search for same id
    if( link != _slot_links->_data->end() )
    {
      // keep color
      color_id = link->_color_id;

      LOG_INFO("Replacing search for " << link->_id);
      _slot_links->_data->erase(link);
    }
    else
    {
      // get first unused color
      while
      (
        std::find_if
        (
          _slot_links->_data->begin(),
          _slot_links->_data->end(),
          [color_id](const LinkDescription::LinkDescription& desc)
          {
            return desc._color_id == color_id;
          }
        ) != _slot_links->_data->end()
      )
        ++color_id;
    }
    auto hedge = std::make_shared<LinkDescription::HyperEdge>();
    hedge->addNode( parseRegions(msg, client_info) );
    updateCenter(hedge.get());

    _slot_links->_data->push_back(
      LinkDescription::LinkDescription
      (
        id.toStdString(),
        stamp,
        hedge, // TODO add props?
        color_id
      )
    );
    _slot_links->setValid(true);

    // Send request to all clients
    QString request(
      "{"
        "\"task\": \"REQUEST\","
        "\"id\": \"" + QString(id).replace('"', "\\\"") + "\","
        "\"stamp\": " + QString::number(stamp) +
      "}"
    );

//        for(QWsSocket* socket: _clients)
//          socket->write(request);
    for(auto socket = _clients.begin(); socket != _clients.end(); ++socket)
      socket->first->write(request);
  }

  //----------------------------------------------------------------------------
  void IPCServer::regionsChanged(const WindowRegions& regions)
  {
    _mutex_slot_links->lock();
    _subscribe_mouse->_data->clear();
    _subscribe_popups->_data->popups.clear();

    bool need_update = false;
    for( auto link = _slot_links->_data->begin();
         link != _slot_links->_data->end();
         ++link )
    {
      if( updateHedge(regions, link->_link.get()) )
      {
        link->_stamp += 1;
        need_update = true;
      }
    }
    _mutex_slot_links->unlock();

    if( !need_update )
      return;

    LOG_INFO("Windows changed -> trigger reroute");
    _cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  class CoverWindows
  {
    public:
      CoverWindows( const WindowRegions::const_iterator& begin,
                    const WindowRegions::const_iterator& end ):
        _begin(begin),
        _end(end)
      {}

      bool valid() const
      {
        return _begin != _end;
      }

      bool hit(const QPoint& point)
      {
        for(auto it = _begin; it != _end; ++it)
          if( it->region.contains(point) )
            return true;
        return false;
      }

      QRect getCoverRegion(const float2& point)
      {
        QPoint p(point.x, point.y);
        for(auto it = _begin; it != _end; ++it)
          if( it->region.contains(p) )
            return it->region;
        return QRect();
      }

      LinkDescription::NodePtr getNearestVisible( const float2& point,
                                                  bool triangle = false )
      {
        const QRect reg = getCoverRegion(point);
        float dist[4] = {
          point.x - reg.left(),
          reg.right() - point.x,
          point.y - reg.top(),
          reg.bottom() - point.y
        };

        float min_dist = dist[0];
        int min_index = 0;

        for( size_t i = 1; i < 4; ++i )
          if( dist[i] < min_dist )
          {
            min_dist = dist[i];
            min_index = i;
          }

        float2 new_center = point,
               normal(0,0);
        int sign = (min_index & 1) ? 1 : -1;
        min_dist *= sign;

        if( min_index < 2 )
        {
          new_center.x += min_dist;
          normal.x = sign;
        }
        else
        {
          new_center.y += min_dist;
          normal.y = sign;
        }

        return buildIcon(new_center, normal, triangle);
      }

      /**
       *
       * @param p_out       Outside point
       * @param p_cov       Covered point
       * @param triangle
       * @return
       */
      LinkDescription::NodePtr getVisibleIntersect( const float2& p_out,
                                                    const float2& p_cov,
                                                    bool triangle = false )
      {
        const QRect reg = getCoverRegion(p_cov);
        float2 dir = (p_out - p_cov).normalize();

        float2 normal;
        float fac = -1;

        if( std::fabs(dir.x) > 0.01 )
        {
          // if not too close to vertical try intersecting left or right
          if( dir.x > 0 )
          {
            fac = (reg.right() - p_cov.x) / dir.x;
            normal.x = 1;
          }
          else
          {
            fac = (reg.left() - p_cov.x) / dir.x;
            normal.x = -1;
          }

          // check if vertically inside the region
          float y = p_cov.y + fac * dir.y;
          if( y < reg.top() || y > reg.bottom() )
            fac = -1;
        }

        if( fac < 0 )
        {
          // nearly vertical or horizontal hit outside of vertical region
          // boundaries -> intersect with top or bottom
          normal.x = 0;

          if( dir.y < 0 )
          {
            fac = (reg.top() - p_cov.y) / dir.y;
            normal.y = -1;
          }
          else
          {
            fac = (reg.bottom() - p_cov.y) / dir.y;
            normal.y = 1;
          }
        }

        float2 new_center = p_cov + fac * dir;
        return buildIcon(new_center, normal, triangle);
      }

    protected:
      const WindowRegions::const_iterator& _begin,
                                           _end;

      LinkDescription::NodePtr buildIcon( float2 center,
                                          const float2& normal,
                                          bool triangle ) const
      {
        LinkDescription::points_t link_points, link_points_children;
        link_points_children.push_back(center);
        link_points.push_back(center += 12 * normal);

        LinkDescription::points_t points;

        if( triangle )
        {
          points.push_back(center += 6 * normal.normal());
          points.push_back(center -= 6 * normal.normal() + 12 * normal);
          points.push_back(center -= 6 * normal.normal() - 12 * normal);
        }
        else
        {
          points.push_back(center += 0.5 * normal.normal());
          points.push_back(center -= 9 * normal);

          points.push_back(center += 2.5 * normal.normal());
          points.push_back(center += 6   * normal.normal() + 3 * normal);
          points.push_back(center += 1.5 * normal.normal() - 3 * normal);
          points.push_back(center -= 6   * normal.normal() + 3 * normal);
          points.push_back(center -= 9   * normal.normal());
          points.push_back(center -= 6   * normal.normal() - 3 * normal);
          points.push_back(center += 1.5 * normal.normal() + 3 * normal);
          points.push_back(center += 6   * normal.normal() - 3 * normal);
          points.push_back(center += 2.5 * normal.normal());

          points.push_back(center += 9 * normal);
        }

        auto node = std::make_shared<LinkDescription::Node>(
          points,
          link_points,
          link_points_children
        );
        node->set("virtual-covered", true);
        //node->set("filled", true);

        return node;
      }
  };

  //----------------------------------------------------------------------------
  bool IPCServer::updateHedge( const WindowRegions& regions,
                               LinkDescription::HyperEdge* hedge )
  {
    hedge->resetNodeParents();
    WId client_wid =
#ifdef _WIN32
      reinterpret_cast<WId>
      (
#endif
        hedge->get<unsigned long>("client_wid", 0)
#ifdef _WIN32
      )
#endif
    ;

    auto client_info = std::find_if
    (
      _clients.begin(),
      _clients.end(),
      [&client_wid](const ClientInfos::value_type& it)
      {
        return it.second.wid == client_wid;
      }
    );
    QRect region;
    WindowRegions::const_iterator first_above = regions.end();
    if( client_info != _clients.end() )
    {
      region = client_info->second.region;

      if( client_wid )
      {
        auto window_info = std::find_if
        (
          regions.begin(),
          regions.end(),
          [&client_wid](const WindowInfo& winfo)
          {
            return winfo.id == client_wid;
          }
        );
        region.translate( window_info->region.topLeft() );
        first_above = window_info + 1;
      }
    }

    bool modified = false;

    struct OutsideScroll
    {
      float2 pos;
      float2 normal;
      size_t num_outside;
    } outside_scroll[] = {
      {float2(0, region.top()),    float2( 0, 1), 0},
      {float2(0, region.bottom()), float2( 0,-1), 0},
      {float2(region.left(),  0),  float2( 1, 0), 0},
      {float2(region.right(), 0),  float2(-1, 0), 0}
    };
    const size_t num_outside_scroll = sizeof(outside_scroll)
                                    / sizeof(outside_scroll[0]);

//    std::vector<LinkDescription::nodes_t::iterator> old_outside_nodes;

    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
      if( (*node)->get<bool>("virtual-covered", false) )
        node = hedge->getNodes().erase(node);
    // TODO check why still sometimes regions don't get removed immediately

    float2 hedge_center;
    size_t num_visible = 0;

    const LinkDescription::nodes_t nodes = hedge->getNodes();
    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
    {
      if(    !(*node)->get<std::string>("virtual-outside").empty()
          || (*node)->get<bool>("virtual-covered", false) )
      {
        //old_outside_nodes.push_back(node);
        node = hedge->getNodes().erase(node);
        modified = true;
        continue;
      }

      if( (*node)->get<bool>("outside", false) )
      {
        (*node)->set("hidden", true);

        float2 center = (*node)->getCenter();
        if( center != float2(0,0) )
        {
          for( size_t i = 0; i < num_outside_scroll; ++i )
          {
            OutsideScroll& out = outside_scroll[i];
            if( (center - out.pos) * out.normal > 0 )
              continue;

            if( out.normal.x == 0 )
              out.pos.x += center.x;
            else
              out.pos.y += center.y;
            out.num_outside += 1;
          }
        }
        continue;
      }

      for(auto child = (*node)->getChildren().begin();
               child != (*node)->getChildren().end();
             ++child )
      {
        if( updateHedge(regions, child->get()) )
          modified = true;

        float2 center = (*child)->getCenter();
        if( center != float2(0,0) )
        {
          hedge_center += center;
          num_visible += 1;
        }
      }

      if( !client_wid )
        continue;

      if( updateRegion(regions, node->get(), client_wid) )
        modified = true;
    }

    for( size_t i = 0; i < num_outside_scroll; ++i )
    {
      OutsideScroll& out = outside_scroll[i];
      if( !out.num_outside )
        continue;

      if( out.normal.x == 0 )
        out.pos.x /= out.num_outside;
      else
        out.pos.y /= out.num_outside;

      LinkDescription::points_t link_points;
      link_points.push_back(out.pos += 7 * out.normal);

      LinkDescription::points_t points;
      points.push_back(out.pos +=  3 * out.normal + 12 * out.normal.normal());
      points.push_back(out.pos -= 10 * out.normal + 12 * out.normal.normal());
      points.push_back(out.pos += 10 * out.normal - 12 * out.normal.normal());

      auto node = std::make_shared<LinkDescription::Node>(points, link_points);
      node->set("virtual-outside", "side[" + std::to_string(static_cast<unsigned long long>(i)) + "]");
      node->set("filled", true);
      updateRegion(regions, node.get(), client_wid);

      QRect reg(points[0].x, points[0].y, 0, 0);
      const int border = 3;
      for(size_t j = 1; j < points.size(); ++j)
      {
        int x = points[j].x,
            y = points[j].y;

        if( x - border < reg.left() )
          reg.setLeft(x - border);
        if( x + border > reg.right() )
          reg.setRight(x + border);

        if( y - border < reg.top() )
          reg.setTop(y - border);
        else if( y + border > reg.bottom() )
          reg.setBottom(y + border);
      }

      {
        std::string text = std::to_string(static_cast<unsigned long long>(out.num_outside));
        float2 popup_pos, popup_size(text.length() * 10 + 6, 16);
        float2 hover_pos, hover_size(_preview_width, _preview_height);

        const size_t border_text = 4,
                     border_preview = 8;

        if( std::fabs(out.normal.y) > 0.5 )
        {
          popup_pos.x = reg.center().x() - popup_size.x / 2;
          hover_pos.x = reg.center().x() - hover_size.x / 2;

          if( out.normal.y < 0 )
          {
            popup_pos.y = reg.top() - popup_size.y - border_text;
            hover_pos.y = popup_pos.y - hover_size.y + border_text
                                                     - 2 * border_preview;
          }
          else
          {
            popup_pos.y = reg.bottom() + border_text;
            hover_pos.y = popup_pos.y + popup_size.y - border_text
                                                     + 2 * border_preview;
          }
        }

        using SlotType::TextPopup;
        TextPopup::Popup popup = {
          text,
          nodes,
          TextPopup::HoverRect(popup_pos, popup_size, border_text, true),
          TextPopup::HoverRect(hover_pos, hover_size, border_preview, false)
        };
        popup.hover_region.offset.x = region.left();
        popup.hover_region.offset.y = region.top();
        popup.hover_region.dim.x = region.width();
        popup.hover_region.dim.y = region.height();
        popup.hover_region.scroll_region = client_info->second.scroll_region;
        popup.hover_region.tile_map = client_info->second.tile_map;

        size_t index = _subscribe_popups->_data->popups.size();
        _subscribe_popups->_data->popups.push_back(popup);

        /**
         * Mouse move callback
         */
        _subscribe_mouse->_data->_move_callbacks.push_back(
          [&,index,client_info](int x, int y)
          {
            auto& popup = _subscribe_popups->_data->popups[index];
            if(  popup.region.contains(x, y)
              || (   popup.hover_region.visible
                  && popup.hover_region.contains(x, y)
                 )
              )
            {
              if( popup.hover_region.visible )
                return;

              popup.hover_region.visible = true;

              _interaction_handler.updateRegion(*client_info, popup);

            }
            else if( !popup.hover_region.visible )
              return;
            else
              popup.hover_region.visible = false;

            _cond_data_ready->wakeAll();
          }
        );

        /**
         * Mouse click callback
         */
        _subscribe_mouse->_data->_click_callbacks.push_back(
        [&,index,client_info](int x, int y)
        {
          auto& popup = _subscribe_popups->_data->popups[index];
          if( !popup.hover_region.visible ||
              !popup.hover_region.contains(x, y) )
            return;

          popup.hover_region.visible = false;

          QRect reg = client_info->second.scroll_region;
          float scale = (reg.height() / pow(2.0f, static_cast<float>(popup.hover_region.zoom)))
                      / _preview_height;
          float dy = y - popup.hover_region.region.pos.y,
                scroll_y = scale * dy + popup.hover_region.src_region.pos.y;

          client_info->first->write(QString(
          "{"
            "\"task\": \"SET\","
            "\"id\": \"scroll-y\","
            "\"val\": " + QString("%1").arg(scroll_y - 15) +
          "}"));

          _cond_data_ready->wakeAll();
        });

        /**
         * Scroll callback
         */
        _subscribe_mouse->_data->_scroll_callbacks.push_back(
        [&,index,client_info](int delta, const float2& pos, uint32_t mod)
        {float preview_aspect = _preview_width
          / static_cast<float>(_preview_height);
          auto& popup = _subscribe_popups->_data->popups[index];
          if( !popup.hover_region.visible )
              return;

          QRect reg = client_info->second.scroll_region;
          float step_y = reg.height() / pow(2.0f, static_cast<float>(popup.hover_region.zoom))
                       / _preview_height;

          if( mod & SlotType::MouseEvent::ShiftModifier )
          {
            popup.hover_region.src_region.pos.y -= delta / fabs(static_cast<float>(delta)) * 20 * step_y;
            _interaction_handler.updateRegion(*client_info, popup);
          }
          else if( mod & SlotType::MouseEvent::ControlModifier )
          {
            float step_x = step_y * preview_aspect;
            popup.hover_region.src_region.pos.x -= delta / fabs(static_cast<float>(delta)) * 20 * step_x;
            _interaction_handler.updateRegion(*client_info, popup);
          }
          else
          {
            int old_zoom = popup.hover_region.zoom;
            clampedStep(popup.hover_region.zoom, delta, 0, 6, 1);

            if( popup.hover_region.zoom != old_zoom )
            {
              float scale = (reg.height() / pow(2.0f, static_cast<float>(old_zoom)))
                          / _preview_height;

              float2 d = pos - popup.hover_region.region.pos,
                     mouse_pos = scale * d + popup.hover_region.src_region.pos;
              _interaction_handler.updateRegion(
                *client_info,
                popup,
                mouse_pos,
                d / popup.hover_region.region.size
              );
            }
          }
        });

        /**
         * Drag callback
         */
        _subscribe_mouse->_data->_drag_callbacks.push_back(
        [&,index,client_info](const float2& delta)
        {float preview_aspect = _preview_width
          / static_cast<float>(_preview_height);
          auto& popup = _subscribe_popups->_data->popups[index];
          if( !popup.hover_region.visible )
              return;

          QRect reg = client_info->second.scroll_region;
          float step_y = reg.height() / pow(2.0f, static_cast<float>(popup.hover_region.zoom))
                       / _preview_height;

          popup.hover_region.src_region.pos.y -= delta.y * step_y;
          float step_x = step_y * preview_aspect;
          popup.hover_region.src_region.pos.x -= delta.x * step_x;

          _interaction_handler.updateRegion(*client_info, popup);
        });
      }

      hedge->addNode( node );
    }

    LinkDescription::nodes_t covered_nodes;
    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
      if( (*node)->get<bool>("virtual-covered", false) )
        node = hedge->getNodes().erase(node);

    CoverWindows cover_windows(first_above, regions.end());
    if( cover_windows.valid() )
    {
      // Show regions covered by other windows
      for( auto node = hedge->getNodes().begin();
                node != hedge->getNodes().end();
              ++node )
      {
        bool covered = !(*node)->get<bool>("outside", false)
                    &&  (*node)->get<bool>("hidden", false);
        (*node)->set("covered", covered);

        if( !covered )
        {
          (*node)->set("covered-region", std::string());
          continue;
        }

        (*node)->set("covered-region", to_string(region));
        (*node)->set("hover", false);

        Rect bb;
        for( auto vert = (*node)->getVertices().begin();
                  vert != (*node)->getVertices().end();
                ++vert )
          bb.expand(*vert);

        /**
         * Mouse move callback
         */
        _subscribe_mouse->_data->_move_callbacks.push_back(
          [&,bb,node](int x, int y)
          {
            bool hover = (*node)->get<bool>("hover");
            if( bb.contains(x, y) )
            {
              if( hover )
                return;
              hover = true;
            }
            else if( hover )
            {
              hover = false;
            }
            else
              return;

            (*node)->set("hover", hover);
            _cond_data_ready->wakeAll();
          }
        );

        covered_nodes.push_back(*node);

//        hedge->getNodes().push_back(
//          cover_windows.getNearestVisible((*node)->getCenter())
//        );
      }
    }

    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
      if(    !(*node)->get<bool>("hidden", false)
          /*||  (*node)->get<bool>("covered", false) */)
      {
        float2 center = (*node)->getCenter();
        if( center != float2(0,0) )
        {
          hedge_center += center;
          num_visible += 1;
        }
      }

    if( num_visible > 0 )
      hedge_center /= num_visible;

    hedge->setCenter(hedge_center);

    if( cover_windows.valid() )
    {
      for( auto node = covered_nodes.begin();
                node != covered_nodes.end();
              ++node )
      {
        LinkDescription::NodePtr new_node =
          cover_windows.getVisibleIntersect(hedge_center, (*node)->getCenter());
        auto new_hedge = std::make_shared<LinkDescription::HyperEdge>();
        new_hedge->set("client_wid", WIdtoStr(client_wid));
        new_hedge->set("no-parent-route", true);
        new_hedge->set("covered", true);
        new_hedge->addNode(*node);
        new_hedge->setCenter(new_node->getLinkPointsChildren().front());
        new_node->getChildren().push_back(new_hedge);
        hedge->addNode(new_node);

        float2 center = new_hedge->getCenter();

        /**
         * Mouse click callback
         */
        _subscribe_mouse->_data->_click_callbacks.push_back(
        [center,client_wid](int x, int y)
        {
//          if( (center - float2(x,y)).length() > 10 )
//            return;

          std::cout << "raise: "
                    << QxtWindowSystem::activeWindow(client_wid)
                    << std::endl;
        });
      }
    }

    return modified;
  }

  //----------------------------------------------------------------------------
  bool IPCServer::updateCenter(LinkDescription::HyperEdge* hedge)
  {
    const LinkDescription::Node* p = hedge->getParent();
    const LinkDescription::HyperEdge* pp = p ? p->getParent() : 0;
    std::cout << hedge << ", p=" << p << ", pp=" << pp << std::endl;
    float2 hedge_center;
    size_t num_visible = 0;

    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
    {
      for(auto child = (*node)->getChildren().begin();
               child != (*node)->getChildren().end();
             ++child )
      {
        float2 center = (*child)->getCenter();
        if( center != float2(0,0) )
        {
          hedge_center += center;
          num_visible += 1;
        }
      }
    }

    if( !num_visible )
      return false;

    hedge->setCenter(hedge_center / num_visible);
    std::cout << " - center = " << hedge_center << hedge_center/num_visible << std::endl;

    return true;
  }

  //----------------------------------------------------------------------------
  bool IPCServer::updateRegion( const WindowRegions& regions,
                                LinkDescription::Node* node,
                                WId client_wid )
  {
    size_t hidden_count = 0;
    for( auto vert = node->getVertices().begin();
              vert != node->getVertices().end();
            ++vert )
    {
      if( client_wid != windowAt(regions, QPoint(vert->x, vert->y)) )
        ++hidden_count;
    }

    LinkDescription::props_t& props = node->getProps();
    LinkDescription::props_t::iterator hidden = props.find("hidden");
    if( hidden_count > node->getVertices().size() / 2 )
    {
      if( hidden == props.end() )
      {
        // if more than half of the points are in another window don't show
        // region
        props["hidden"] = "true";
        return true;
      }
    }
    else
    {
      if( hidden != props.end() )
      {
        props.erase(hidden);
        return true;
      }
    }
    return false;
  }

  //----------------------------------------------------------------------------
  LinkDescription::NodePtr IPCServer::parseRegions( const JSONParser& json,
                                                    ClientInfo& client_info )
  {
    std::cout << "Parse regions (" << client_info.wid << ")" << std::endl;

    if( !json.isSet("regions") )
      return std::make_shared<LinkDescription::Node>();

    const WindowRegions windows = _window_monitor.getWindows();
    auto window_info = std::find_if
    (
      windows.begin(),
      windows.end(),
      [&client_info](const WindowInfo& winfo)
      {
        return winfo.id == client_info.wid;
      }
    );
    float2 top_left( client_info.region.left(),
                     client_info.region.top() );
    if( window_info == windows.end() )
    {
      LOG_WARN("No such window!");
    }
    else
    {
      top_left.x += window_info->region.left();
      top_left.y += window_info->region.top();
    }

    LinkDescription::nodes_t nodes;
    float avg_height = 0;

    QVariantList regions = json.getValue<QVariantList>("regions");
    for(auto region = regions.begin(); region != regions.end(); ++region)
    {
      LinkDescription::points_t points;
      LinkDescription::props_t props;

      float min_y = 0,
            max_y = 0;

      //for(QVariant point: region.toList())
      auto regionlist = region->toList();
      for(auto point = regionlist.begin(); point != regionlist.end(); ++point)
      {
        if( point->type() == QVariant::List )
        {
          QVariantList coords = point->toList();
          if( coords.size() != 2 )
          {
            LOG_WARN("Wrong coord count for point.");
            continue;
          }

          points.push_back(float2(
            coords.at(0).toInt(),
            coords.at(1).toInt()
          ));

          if( points.size() == 1 )
          {
            min_y = max_y = points.back().y;
          }
          else
          {
            if( points.back().y > max_y )
              max_y = points.back().y;
            else if( points.back().y < min_y )
              min_y = points.back().y;
          }
        }
        else if( point->type() == QVariant::Map )
        {
          auto map = point->toMap();
          for( auto it = map.constBegin(); it != map.constEnd(); ++it )
            props[ it.key().toStdString() ] = it->toString().toStdString();
        }
        else
          LOG_WARN("Wrong data type: " << point->typeName());
      }

      if( points.empty() )
        continue;

      avg_height += max_y - min_y;
      float2 center;

      auto rel = props.find("rel");
      if( rel != props.end() && rel->second == "true" )
      {
        for(size_t i = 0; i < points.size(); ++i)
          center += (points[i] += top_left);
      }
      else
      {
        for(size_t i = 0; i < points.size(); ++i)
          center += points[i];
      }

      center /= points.size();
      LinkDescription::points_t link_points;
      link_points.push_back(center);

      nodes.push_back( std::make_shared<LinkDescription::Node>(points, link_points, props) );
    }

    if( !nodes.empty() )
      avg_height /= nodes.size();

    LinkDescription::HyperEdgePtr hedge =
      std::make_shared<LinkDescription::HyperEdge>(nodes);
    hedge->set("client_wid", WIdtoStr(client_info.wid));

    hedge->set("partitions_src", to_string(client_info.tile_map->partitions_src));
    hedge->set("partitions_dest", to_string(client_info.tile_map->partitions_dest));
    client_info.partitions_src = client_info.tile_map->partitions_src;
    client_info.partitions_dest = client_info.tile_map->partitions_dest;

    updateHedge(windows, hedge.get());
    return std::make_shared<LinkDescription::Node>(hedge);
  }

  //----------------------------------------------------------------------------
  void IPCServer::updateScrollRegion( const JSONParser& msg,
                                      ClientInfo& client_info )
  {
    if( msg.isSet("scroll-region") )
      client_info.scroll_region = msg.getValue<QRect>("scroll-region");
    else
      client_info.scroll_region = client_info.region;

    const WindowRegions windows = _window_monitor.getWindows();
    auto window_info = std::find_if
    (
      windows.begin(),
      windows.end(),
      [&client_info](const WindowInfo& winfo)
      {
        return winfo.id == client_info.wid;
      }
    );
    float2 top_left( client_info.region.left(),
                     client_info.region.top() );
    if( window_info == windows.end() )
    {
      LOG_WARN("No such window!");
    }
    else
    {
      top_left.x += window_info->region.left();
      top_left.y += window_info->region.top();
    }

    LinkDescription::nodes_t nodes;
    float avg_height = 0;

    QVariantList regions = msg.getValue<QVariantList>("regions");
    for(auto region = regions.begin(); region != regions.end(); ++region)
    {
      LinkDescription::points_t points;
      LinkDescription::props_t props;

      float min_y = 0,
            max_y = 0;

      //for(QVariant point: region.toList())
      auto regionlist = region->toList();
      for(auto point = regionlist.begin(); point != regionlist.end(); ++point)
      {
        if( point->type() == QVariant::List )
        {
          QVariantList coords = point->toList();
          if( coords.size() != 2 )
          {
            LOG_WARN("Wrong coord count for point.");
            continue;
          }

          points.push_back(float2(
            coords.at(0).toInt(),
            coords.at(1).toInt()
          ));

          if( points.size() == 1 )
          {
            min_y = max_y = points.back().y;
          }
          else
          {
            if( points.back().y > max_y )
              max_y = points.back().y;
            else if( points.back().y < min_y )
              min_y = points.back().y;
          }
        }
        else if( point->type() == QVariant::Map )
        {
          auto map = point->toMap();
          for( auto it = map.constBegin(); it != map.constEnd(); ++it )
            props[ it.key().toStdString() ] = it->toString().toStdString();
        }
        else
          LOG_WARN("Wrong data type: " << point->typeName());
      }

      if( points.empty() )
        continue;

      avg_height += max_y - min_y;
      float2 center;

      auto rel = props.find("rel");
      if( rel != props.end() && rel->second == "true" )
      {
        for(size_t i = 0; i < points.size(); ++i)
          center += (points[i] += top_left);
      }
      else
      {
        for(size_t i = 0; i < points.size(); ++i)
          center += points[i];
      }

      center /= points.size();
      LinkDescription::points_t link_points;
      link_points.push_back(center);

      nodes.push_back( std::make_shared<LinkDescription::Node>(points, link_points, props) );
    }

    if( !nodes.empty() )
      avg_height /= nodes.size();

    const bool do_partitions = false;
    const bool partition_compress = false;

    float offset = client_info.scroll_region.top() - top_left.y;
    Partitions partitions_src,
               partitions_dest;

    if( do_partitions )
    {
      PartitionHelper part;
      for(auto node = nodes.begin(); node != nodes.end(); ++node)
      {
        const Rect& bb = (*node)->getBoundingBox();
        part.add( float2( offset + bb.t() - 3 * avg_height,
                          offset + bb.b() + 3 * avg_height ) );
      }
      part.clip(0, client_info.scroll_region.height(), 2 * avg_height);
      partitions_src = part.getPartitions();

      if( partition_compress )
      {
        const int compress_size = avg_height * 1.5;
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

        if( partitions_src.back().y + 0.5 < client_info.scroll_region.height() )
          cur_pos += compress_size;

        client_info.scroll_region.setHeight(cur_pos);
      }
      else
        partitions_dest = partitions_src;
    }
    else
    {
      partitions_src.push_back(float2(0, client_info.scroll_region.height()));
      partitions_dest.push_back(partitions_src.back());
    }

    client_info.tile_map =
      std::make_shared<HierarchicTileMap>( client_info.scroll_region.width(),
                                           client_info.scroll_region.height(),
                                           _preview_width,
                                           _preview_height );

    client_info.tile_map->partitions_src = partitions_src;
    client_info.tile_map->partitions_dest = partitions_dest;
  }

  //----------------------------------------------------------------------------
  IPCServer::InteractionHandler::InteractionHandler(IPCServer* server):
    _server(server),
    _tile_request_id(0)
  {

  }

  //----------------------------------------------------------------------------
  void IPCServer::InteractionHandler::updateRegion(
    const ClientInfos::value_type& client_info,
    SlotType::TextPopup::Popup& popup,
    float2 center,
    float2 rel_pos )
  {
    float preview_aspect = _server->_preview_width
                         / static_cast<float>(_server->_preview_height);
    const QRect& reg = client_info.second.scroll_region;
    int h = reg.height() / pow(2.0f, static_cast<float>(popup.hover_region.zoom)) + 0.5f,
        w = h * preview_aspect + 0.5;

    Rect& src = popup.hover_region.src_region;
    float2 old_pos = src.pos;

    src.size.x = w;
    src.size.y = h;

    if( center.x > -9999 && center.y > -9999 )
    {
      // Center source region around mouse cursor
      src.pos.x = center.x - rel_pos.x * w;
      src.pos.y = center.y - rel_pos.y * h;
    }

    clamp<float>(src.pos.x, 0, reg.width() - w);
    clamp<float>(src.pos.y, 0, reg.height() - h);

//    if( old_pos == src.pos )
//      return;

    const HierarchicTileMapPtr& tile_map = client_info.second.tile_map;
    bool sent = false;


    float scale = 1/tile_map->getLayerScale(popup.hover_region.zoom);
    MapRect rect = tile_map->requestRect(src, popup.hover_region.zoom);
    rect.foreachTile([&](Tile& tile, size_t x, size_t y)
    {
      if( tile.type == Tile::NONE )
      {
        auto req = std::find_if
        (
          _tile_requests.begin(),
          _tile_requests.end(),
          //TODO VS FIX
          [&](const std::map<uint8_t, TileRequest>::value_type& tile_req)
          //[&](const TileRequests::value_type& tile_req)
          {
            return (tile_req.second.tile_map.lock() == tile_map)
                && (tile_req.second.zoom == popup.hover_region.zoom)
                && (tile_req.second.x == x)
                && (tile_req.second.y == y);
          }
        );

        if( req != _tile_requests.end() )
          // already sent
          return;

        Rect src( float2( x * tile_map->getTileWidth(),
                          y * tile_map->getTileHeight() ),
                  float2(tile.width, tile.height) );
        src *= scale;

        TileRequest tile_req = {
          tile_map,
          popup.hover_region.zoom,
          x, y
        };
        uint8_t req_id = ++_tile_request_id;
        _tile_requests[req_id] = tile_req;

        client_info.first->write(QString(
        "{"
          "\"task\": \"GET\","
          "\"id\": \"preview-tile\","
          "\"size\": [" + QString::number(tile.width)
                  + "," + QString::number(tile.height)
                  + "],"
          "\"sections_src\":" + to_string(client_info.second.partitions_src).c_str() + ","
          "\"sections_dest\":" + to_string(client_info.second.partitions_dest).c_str() + ","
          "\"src\": " + src.toString(true).c_str() + ","
          "\"req_id\": " + QString::number(req_id) +
        "}"));
        sent = true;
      }
    });

    if( !sent )
      _server->_cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  WId windowAt( const WindowRegions& regions,
                const QPoint& pos )
  {
    for( auto reg = regions.rbegin(); reg != regions.rend(); ++reg )
    {
      if( reg->region.contains(pos) )
        return reg->id;
    }
    return 0;
  }

} // namespace LinksRouting
