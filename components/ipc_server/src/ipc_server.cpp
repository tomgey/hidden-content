/*!
 * @file ipc_server.cpp
 * @brief
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 02.03.2012
 */

#include "ipc_server.hpp"
#include "log.hpp"
#include "ClientInfo.hxx"
#include "JSONParser.h"
#include "JSON.hpp"
#include "common/PreviewWindow.hpp"

#include <QHostInfo>
#include <QMutex>
#include <QWebSocket>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace LinksRouting
{
  const QString SAVE_FILE_EXT = "concept-local.json";

  QJsonArray to_json(ClientWeakList const& clients)
  {
    QJsonArray json;
    for(auto client_weak: clients)
    {
      ClientRef client = client_weak.lock();
      if( client )
        json.push_back(client->id());
    }

    return json;
  }

  QJsonArray to_json(StringList const& list)
  {
    QJsonArray json;
    for(auto const& s: list)
      json.push_back(QString::fromStdString(s));

    return json;
  }

  template<class T1, class T2>
  QJsonArray to_json(QPair<T1, T2> const& p)
  {
    QJsonArray json;
    json.push_back(to_json(p.first));
    json.push_back(to_json(p.second));

    return json;
  }

  QJsonValue to_json(Color const& color)
  {
    return QColor(
      color.r * 255 + .5f,
      color.g * 255 + .5f,
      color.b * 255 + .5f,
      color.a * 255 + .5f
    ).name();
  }

  using namespace std::placeholders;

  std::string to_string(const QRect& r)
  {
    std::stringstream strm;
    strm << r.left() << ' ' << r.top() << ' ' << r.right() << ' ' << r.bottom();
    return strm.str();
  }

  std::string to_string(const Rect& r)
  {
    return to_string( r.toQRect() );
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

  class IPCServer::TileHandler:
    public SlotType::TileHandler
  {
    public:
      TileHandler(IPCServer* ipc_server);

      virtual bool updateRegion( SlotType::TextPopup::Popup& popup,
                                 float2 center = float2(-9999, -9999),
                                 float2 rel_pos = float2() );
      virtual bool updateRegion( SlotType::XRayPopup::HoverRect& popup );
      virtual bool updateTileMap( const HierarchicTileMapPtr& tile_map,
                                  ClientRef client,
                                  const Rect& rect,
                                  int zoom );

    protected:
      friend class IPCServer;
      IPCServer* _ipc_server;

      struct TileRequest
      {
        QWebSocket* socket;
        HierarchicTileMapWeakPtr tile_map;
        int zoom;
        size_t x, y;
        float2 tile_size;
        clock::time_point time_stamp;
      };

      typedef std::map<uint8_t, TileRequest> TileRequests;
      TileRequests  _tile_requests;
      uint8_t       _tile_request_id;
      int           _new_request;
  };

  //----------------------------------------------------------------------------
  IPCServer::IPCServer( QMutex* mutex,
                        QWaitCondition* cond_data ):
    Configurable("QtWebsocketServer"),
    _server(nullptr),
    _status_server(nullptr),
    _window_monitor(std::bind(&IPCServer::regionsChanged, this, _1)),
    _mutex_slot_links(mutex),
    _cond_data_ready(cond_data),
    _dirty_flags(0)
  {
    assert(_mutex_slot_links);
    assert(_mutex_slot_links->isRecursive());

    registerArg("DebugRegions", _debug_regions);
    registerArg("DebugFullPreview", _debug_full_preview_path);
    registerArg("PreviewWidth", _preview_width = 800);
    registerArg("PreviewHeight", _preview_height = 400);
    registerArg("PreviewAutoWidth", _preview_auto_width = true);
    registerArg("OutsideSeeThrough", _outside_see_through = true);

    _msg_handlers["ABORT"] =
      std::bind(&IPCServer::onLinkAbort, this, _1, _2, _3);
    _msg_handlers["CMD"] =
      std::bind(&IPCServer::onClientCmd, this, _1, _2, _3);
    _msg_handlers["CONCEPT-NEW"] =
      std::bind(&IPCServer::onConceptNew, this, _1, _2);
    _msg_handlers["CONCEPT-UPDATE"] =
      std::bind(&IPCServer::onConceptUpdate, this, _1, _2);
    _msg_handlers["CONCEPT-DELETE"] =
      std::bind(&IPCServer::onConceptDelete, this, _1, _2);
    _msg_handlers["CONCEPT-UPDATE-REFS"] =
      std::bind(&IPCServer::onConceptUpdateRefs, this, _1, _2);
    _msg_handlers["CONCEPT-LINK-NEW"] =
      std::bind(&IPCServer::onConceptLinkNew, this, _1, _2);
    _msg_handlers["CONCEPT-LINK-UPDATE"] =
      std::bind(&IPCServer::onConceptLinkUpdate, this, _1, _2);
    _msg_handlers["CONCEPT-LINK-DELETE"] =
      std::bind(&IPCServer::onConceptLinkDelete, this, _1, _2);
    _msg_handlers["CONCEPT-LINK-UPDATE-REFS"] =
      std::bind(&IPCServer::onConceptLinkUpdateRefs, this, _1, _2);
    _msg_handlers["CONCEPT-SELECTION-UPDATE"] =
      std::bind(&IPCServer::onConceptSelectionUpdate, this, _1, _2);
    _msg_handlers["DUMP"] =
      std::bind(&IPCServer::dumpState, this, std::ref(std::cout));
    _msg_handlers["FOUND"] = // TODO remove and just use UPDATE?
      std::bind(&IPCServer::onLinkUpdate, this, _1, _2);
    _msg_handlers["GET"] =
      std::bind(&IPCServer::onValueGet, this, _1, _2);
    _msg_handlers["GET-FOUND"] =
      std::bind(&IPCServer::onValueGetFound, this, _1, _2);
    _msg_handlers["INITIATE"] =
      std::bind(&IPCServer::onLinkInitiate, this, _1, _2);
    _msg_handlers["LOAD-STATE"] =
      std::bind(&IPCServer::onLoadState, this, _1, _2);
    _msg_handlers["REGISTER"] =
      std::bind(&IPCServer::onClientRegister, this, _1, _2);
    _msg_handlers["RESIZE"] =
      std::bind(&IPCServer::onClientResize, this, _1, _2);
    _msg_handlers["SAVE-STATE"] =
      std::bind(&IPCServer::onSaveState, this, _1, _2);
    _msg_handlers["SEMANTIC-ZOOM"] =
      std::bind(&IPCServer::onClientSemanticZoom, this, _1, _2);
    _msg_handlers["SET"] =
      std::bind(&IPCServer::onValueSet, this, _1, _2);
    _msg_handlers["SYNC"] =
      std::bind(&IPCServer::onClientSync, this, _1, _2);
    _msg_handlers["UPDATE"] =
      std::bind(&IPCServer::onLinkUpdate, this, _1, _2);
    _msg_handlers["WM"] =
      std::bind(&IPCServer::onWindowManagementCommand, this, _1, _2);
  }

  //----------------------------------------------------------------------------
  IPCServer::~IPCServer()
  {

  }

  //----------------------------------------------------------------------------
  void IPCServer::publishSlots(SlotCollector& slot_collector)
  {
    _slot_links = slot_collector.create<LinkDescription::LinkList>("/links");
    _slot_xray = slot_collector.create<SlotType::XRayPopup>("/xray");
    _slot_outlines =
      slot_collector.create<SlotType::CoveredOutline>("/covered-outlines");
    _slot_tile_handler =
      slot_collector.create< SlotType::TileHandler,
                             TileHandler,
                             IPCServer*
                           >("/tile-handler", this);
    _tile_handler = dynamic_cast<TileHandler*>(_slot_tile_handler->_data.get());
    assert(_tile_handler);
  }

  //----------------------------------------------------------------------------
  void IPCServer::subscribeSlots(SlotSubscriber& slot_subscriber)
  {
    _subscribe_routing =
      slot_subscriber.getSlot<SlotType::ComponentSelection>("/routing");
    _subscribe_user_config =
      slot_subscriber.getSlot<LinksRouting::Config*>("/user-config");
    assert(_subscribe_user_config->_data.get());

    _subscribe_desktop_rect =
      slot_subscriber.getSlot<Rect>("/desktop/rect");

    _subscribe_mouse =
      slot_subscriber.getSlot<LinksRouting::SlotType::MouseEvent>("/mouse");
    _subscribe_popups =
      slot_subscriber.getSlot<SlotType::TextPopup>("/popups");
    _subscribe_previews =
      slot_subscriber.getSlot<SlotType::Preview>("/previews");

    _subscribe_mouse->_data->_click_callbacks.push_back
    (
      std::bind(&IPCServer::onClick, this, _1, _2)
    );
    _subscribe_mouse->_data->_move_callbacks.push_back
    (
      std::bind(&IPCServer::onMouseMove, this, _1, _2)
    );
    _subscribe_mouse->_data->_scroll_callbacks.push_back
    (
      std::bind(&IPCServer::onScrollWheel, this, _1, _2, _3)
    );
    _subscribe_mouse->_data->_drag_callbacks.push_back
    (
      std::bind(&IPCServer::onDrag, this, _1)
    );
  }


  //----------------------------------------------------------------------------
  bool IPCServer::setString(const std::string& name, const std::string& val)
  {
    if( name != "link-color" )
      return ComponentArguments::setString(name, val);

    char *end;
    long int color[3];
    color[0] = strtol(val.c_str(), &end, 10);
    color[1] = strtol(end,         &end, 10);
    color[2] = strtol(end,         0,    10);

    _colors.push_back( Color( color[0] / 255.f,
                              color[1] / 255.f,
                              color[2] / 255.f,
                              0.7f ) );
    //std::cout << "GlRenderer: Added color (" << val << ")" << std::endl;

    return true;
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
    int port = 4487,
        port_status = 4486;

    _server = new QWebSocketServer(
      QStringLiteral("Hidden Content Server"),
      QWebSocketServer::NonSecureMode,
      this
    );
    if( !_server->listen(QHostAddress::Any, port) )
    {
      LOG_ERROR("Failed to start WebSocket server on port " << port << ": "
                << _server->errorString() );
      return;
    }
    LOG_INFO("WebSocket server listening on port " << port);
    connect( _server, &QWebSocketServer::newConnection,
             this, &IPCServer::onClientConnection );

    _status_server = new QTcpServer(this);
    if( !_status_server->listen(QHostAddress::Any, port_status) )
    {
      LOG_ERROR("Failed to start status server on port " << port_status << ": "
                << _status_server->errorString() );
      return;
    }
    LOG_INFO("Status server listening on port " << port_status);
    connect( _status_server, &QTcpServer::newConnection,
             this, &IPCServer::onStatusClientConnect );
  }

  //----------------------------------------------------------------------------
  void IPCServer::shutdown()
  {

  }

  //----------------------------------------------------------------------------
  uint32_t IPCServer::process(unsigned int type)
  {
    if( !_debug_regions.empty() )
    {
      onTextReceived( QString::fromStdString(_debug_regions) );
      _debug_regions.clear();
    }

    if( !_debug_full_preview_path.empty() )
    {
      _full_preview_img =
        QGLWidget::convertToGLFormat
        (
          QImage( _debug_full_preview_path.c_str() ).mirrored(false, true)
        );
      _debug_full_preview_path.clear();
    }

    if( _tile_handler->_new_request > 0 )
      --_tile_handler->_new_request;
    else
    {
      int allowed_request = 6;
      for(auto req =  _tile_handler->_tile_requests.rbegin();
               req != _tile_handler->_tile_requests.rend();
             ++req )
      {
        if( !req->second.socket )
          continue;

        HierarchicTileMapPtr tile_map = req->second.tile_map.lock();
        if( !tile_map )
        {
          // TODO req = _tile_handler->_tile_requests.erase(req);
          continue;
        }

        float scale = 1/tile_map->getLayerScale(req->second.zoom);
        Rect src( float2( req->second.x * tile_map->getTileWidth(),
                          req->second.y * tile_map->getTileHeight() ),
                  req->second.tile_size );
        src *= scale;
        src.pos.x += tile_map->margin_left;

        std::cout << "request tile " << src.toString(true) << std::endl;
        req->second.socket->sendTextMessage(QString(
        "{"
          "\"task\": \"GET\","
          "\"id\": \"preview-tile\","
          "\"size\": [" + QString::number(req->second.tile_size.x)
                  + "," + QString::number(req->second.tile_size.y)
                  + "],"
          "\"sections_src\":" + to_string(tile_map->partitions_src).c_str() + ","
          "\"sections_dest\":" + to_string(tile_map->partitions_dest).c_str() + ","
          "\"src\": " + src.toString(true).c_str() + ","
          "\"req_id\": " + QString::number(req->first) +
        "}"));

        req->second.socket = 0;
        if( --allowed_request <= 0 )
          break;
      }
    }

    static clock::time_point last_time = clock::now();
    clock::time_point now = clock::now();
    auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>
    (
      now - last_time
    ).count();
    double dt = dur_us / 1000000.;
    last_time = now;

    auto updatePopup = [&](SlotType::AnimatedPopup& popup) -> uint32_t
    {
      uint32_t flags = 0;

      bool visible = popup.isVisible();
      if( popup.update(dt) )
        flags |= RENDER_DIRTY;

      double alpha = popup.getAlpha();

      if(    visible != popup.isVisible()
          || (alpha > 0 && alpha < 0.5) )
        flags |= MASK_DIRTY;

      return flags;
    };

    foreachPopup
    (
      [&](SlotType::TextPopup::Popup& popup, QWebSocket&, ClientInfo&) -> bool
      {
        if( popup.node )
        {
          if( popup.node->get<bool>("hidden") )
            popup.region.hide();
          else if( !popup.text.empty() )
            popup.region.show();
        }
        _dirty_flags |= updatePopup(popup.hover_region);

        if( popup.hover_region.isVisible() )
        {
          if( !popup.preview )
            popup.preview = _subscribe_previews->_data->getWindow(&popup);

          popup.preview->update(dt);
        }
        else
        {
          if( popup.preview )
          {
            popup.preview->release();
            popup.preview = nullptr;
          }
        }

        return false;
      }
    );

    WId preview_wid = 0;
    Rect preview_region;
    foreachPreview
    (
      [&](SlotType::XRayPopup::HoverRect& preview, QWebSocket&, ClientInfo&)
         -> bool
      {
        _dirty_flags |= updatePopup(preview);
        if( preview.getAlpha() > (preview.isFadeIn() ? 0.9 : 0.3) )
        {
          preview_wid = preview.node->get<WId>("client_wid");
          preview_region = preview.preview_region;
        }
        preview.node->set("alpha", preview.getAlpha());

        if( preview.isVisible() )
        {
          if( !preview.preview )
            preview.preview = _subscribe_previews->_data->getWindow(&preview);

          preview.preview->update(dt);
        }
        else
        {
          if( preview.preview )
          {
            preview.preview->release();
            preview.preview = nullptr;
          }
        }

        return false;
      }
    );

    bool changed = false;
    for(auto& cinfo: _clients)
      changed |= cinfo.second->updateHoverCovered(preview_wid, preview_region);

    if( changed )
      _dirty_flags |= LINKS_DIRTY;

    uint32_t flags = _dirty_flags;
    _dirty_flags = 0;
    return flags;
  }

  //----------------------------------------------------------------------------
  bool IPCServer::getOutsideSeeThrough() const
  {
    return routingActive() && _outside_see_through;
  }

  //----------------------------------------------------------------------------
  IPCServer::PopupIterator
  IPCServer::addPopup( const ClientInfo& client_info,
                       const SlotType::TextPopup::Popup& popup )
  {
    auto& popups = _subscribe_popups->_data->popups;
    auto popup_it = popups.insert(popups.end(), popup);

    auto client = std::find_if
    (
      _clients.begin(),
      _clients.end(),
      [&client_info](const ClientInfos::value_type& c)
      {
        return c.second->getWindowInfo().id == client_info.getWindowInfo().id;
      }
    );

    if( client == _clients.end() )
      return popup_it;

    popup_it->client_socket = client->first;
    return popup_it;
  }

  //----------------------------------------------------------------------------
  void IPCServer::removePopup(const PopupIterator& popup)
  {
    _subscribe_popups->_data->popups.erase(popup);
  }

  //----------------------------------------------------------------------------
  void IPCServer::removePopups(const std::list<PopupIterator>& popups_remove)
  {
    auto& popups = _subscribe_popups->_data->popups;
    for(auto const& it: popups_remove)
      popups.erase(it);
  }

  //----------------------------------------------------------------------------
  IPCServer::XRayIterator
  IPCServer::addCoveredPreview( const std::string& link_id,
                                const ClientInfo& client_info,
                                const LinkDescription::NodePtr& node,
                                const HierarchicTileMapWeakPtr& tile_map,
                                const QRect& viewport,
                                const QRect& scroll_region,
                                bool extend )
  {
    QRect preview_region =
      scroll_region.intersected(extend ? desktopRect().toQRect() : viewport);
    QRect source_region
    (
      preview_region.topLeft() - scroll_region.topLeft(),
      preview_region.size()
    );

    node->set("covered-region", to_string(viewport));
    node->set("covered-preview-region", to_string(preview_region));
    node->setOrClear("hover", false);

    Rect bb;
    for( auto vert = node->getVertices().begin();
              vert != node->getVertices().end();
            ++vert )
      bb.expand(*vert);

    SlotType::XRayPopup::HoverRect xray;
    xray.link_id = link_id;
    xray.region = bb;
    xray.preview_region = preview_region;
    xray.source_region = source_region;
    xray.node = node;
    xray.tile_map = tile_map;
    xray.client_socket = client_info.socket;

    auto& previews = _slot_xray->_data->popups;
    return previews.insert(previews.end(), xray);
  }

  //----------------------------------------------------------------------------
  void IPCServer::removeCoveredPreview(const XRayIterator& preview)
  {
    if( preview->node )
      preview->node->setOrClear("alpha", false);
    _slot_xray->_data->popups.erase(preview);
  }

  //----------------------------------------------------------------------------
  void IPCServer::removeCoveredPreviews(const std::list<XRayIterator>& previews)
  {
    auto& popups = _slot_xray->_data->popups;
    for(auto const& it: previews)
    {
      if( it->node )
        it->node->setOrClear("alpha", false);
      popups.erase(it);
    }
  }

  //----------------------------------------------------------------------------
  IPCServer::OutlineIterator
  IPCServer::addOutline(const ClientInfo& client_info)
  {
    auto const& winfo = client_info.getWindowInfo();
    SlotType::CoveredOutline::Outline outline;
    outline.title = to_string(winfo.title);
    outline.region_outline.pos = winfo.region.topLeft();
    outline.region_outline.size = winfo.region.size();
    outline.region_title.pos = winfo.region.topLeft() + float2(2, 5);
    outline.region_title.size.x = outline.title.size() * 7;
    outline.region_title.size.y = 20;
    outline.preview_valid = false;

    auto& outlines = _slot_outlines->_data->popups;
    return outlines.insert(outlines.end(), outline);
  }

  //----------------------------------------------------------------------------
  void IPCServer::removeOutline(const OutlineIterator& outline)
  {
    if( outline->preview_valid )
      removeCoveredPreview(outline->preview);
    _slot_outlines->_data->popups.erase(outline);
  }

  //----------------------------------------------------------------------------
  void IPCServer::removeOutlines(const std::list<OutlineIterator>& outlines_it)
  {
    auto& outlines = _slot_outlines->_data->popups;
    for(auto const& it: outlines_it)
    {
      if( it->preview_valid )
        removeCoveredPreview(it->preview);
      outlines.erase(it);
    }
  }

  //----------------------------------------------------------------------------
  bool IPCServer::routingActive() const
  {
    return _subscribe_routing->_data->active != "NoRouting";
  }

  //----------------------------------------------------------------------------
  void IPCServer::dumpState(std::ostream& strm) const
  {
    QMutexLocker lock_links(_mutex_slot_links);

    LinkDescription::printLinkList(*_slot_links->_data.get(), strm);
    strm << "<ClientList>\n";
    for(auto const& client: _clients)
      client.second->print(strm);
    strm << "</ClientList>" << std::endl;
  }

  //----------------------------------------------------------------------------
  void IPCServer::saveState(const QString& file)
  {
    _save_state_file = file;

    if( _save_state_file.isEmpty() )
    {
      QString dir = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
      if( dir.isEmpty() )
        qWarning() << "Could not get writable location for saving state.";

      _save_state_file = dir + "/"
                        + QHostInfo::localHostName()
                        + "-"
                        + QString::number(QDateTime::currentMSecsSinceEpoch())
                        + "." + SAVE_FILE_EXT;

    }

    qDebug() << "Going to save state to" << _save_state_file;
    onValueGet({}, {{"id", "/state/all"}});
  }

  //----------------------------------------------------------------------------
  void IPCServer::loadState(const QString& file_name)
  {
    qDebug() << "Loading state from" << file_name;

    QFile state_file(file_name);
    if( !state_file.open(QIODevice::ReadOnly | QIODevice::Text) )
    {
      qWarning() << "Failed to open state file" << file_name;
      return;
    }

    QJsonObject state = parseJson(state_file.readAll());
    if( !state.contains("concepts") )
    {
      qWarning() << "Invalid state: missing concept graph data.";
      return;
    }

    QJsonObject clients = state["clients"].toObject(),
                links = state["links"].toObject(),
                screen = state["screen"].toObject();

    loadConceptGraphFromJson(state["concepts"].toObject());
  }

  //----------------------------------------------------------------------------
  void IPCServer::clearConceptGraph()
  {
    distributeMessage(QJsonObject({
      {"task", "CONCEPT-SELECTION-UPDATE"},
      {"selection", QJsonArray()}
    }));
    _concept_selection.clear();

    for( auto rel = _concept_links.begin();
              rel != _concept_links.end();
            ++rel )
      distributeMessage(QJsonObject({
        {"task", "CONCEPT-LINK-DELETE"},
        {"id", rel.key()}
      }));
    _concept_links.clear();

    for( auto concept = _concept_nodes.begin();
              concept != _concept_nodes.end();
            ++concept )
      distributeMessage(QJsonObject({
        {"task", "CONCEPT-DELETE"},
        {"id", concept.key()}
      }));
    _concept_nodes.clear();

    _concept_layout.clear();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientConnection()
  {
    QWebSocket* client = _server->nextPendingConnection();

    connect(client, &QWebSocket::textMessageReceived, this, &IPCServer::onTextReceived);
    connect(client, &QWebSocket::binaryMessageReceived, this, &IPCServer::onBinaryReceived);
    connect(client, &QWebSocket::disconnected, this, &IPCServer::onClientDisconnection);

    _clients[ client ].reset(new ClientInfo(client, this));

    LOG_INFO( "Client connected: " << client->peerAddress().toString()
                                   << ":" << client->peerPort() );
  }

  //----------------------------------------------------------------------------
  void IPCServer::onTextReceived(QString data)
  {
    auto client = _clients.find(qobject_cast<QWebSocket*>(sender()));
    if( client == _clients.end() )
    {
      LOG_WARN("Received message from unknown client: " << data);
      return;
    }

    ClientRef client_info = client->second;
    std::cout << "Received (" << client_info->getWindowInfo().id << "): "
              << data.left(200) << std::endl;

    try
    {
      {
        QJsonObject msg = parseJson(data.toLocal8Bit());
        QString task = msg.value("task").toString();

        auto msg_handler = _msg_handlers.find(task);
        if( msg_handler == _msg_handlers.end() )
          LOG_WARN("Unknown message type: " << task.toStdString());
        else
          msg_handler->second(client_info, msg, data);
      }
    }
    catch(std::runtime_error& ex)
    {
      LOG_WARN("Failed to parse message: " << ex.what());
    }

    dirtyLinks();
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

    auto request = _tile_handler->_tile_requests.find(seq_id);
    if( request == _tile_handler->_tile_requests.end() )
    {
      LOG_WARN("Received unknown tile request.");
      return;
    }

    auto req_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>
      (
        clock::now() - request->second.time_stamp
      ).count();

    std::cout << "request #" << (int)seq_id << " " << req_ms << "ms"
              << std::endl;

    HierarchicTileMapPtr tile_map = request->second.tile_map.lock();
    if( !tile_map )
    {
      LOG_WARN("Received tile request for expired map.");
      _tile_handler->_tile_requests.erase(request);
      return;
    }

    tile_map->setTileData( request->second.x,
                           request->second.y,
                           request->second.zoom,
                           data.constData() + 2,
                           data.size() - 2 );

    QImage img( (const uchar*)data.constData() + 2,
                request->second.tile_size.x,
                request->second.tile_size.y,
                QImage::Format_ARGB32 );
    img.save( QString("tile-%1-%2").arg(request->second.x).arg(request->second.y));

    dirtyRender();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientDisconnection()
  {
    QWebSocket * socket = qobject_cast<QWebSocket*>(sender());
    if( !socket )
      return;

    auto client = _clients.find(socket);
    if( client == _clients.end() )
    {
      qDebug() << "Unknown client disconnected";
      return;
    }

    urlDec( client->second->url() );
    sendUrlUpdate();

    _clients.erase(client);
    socket->deleteLater();

    LOG_INFO("Client disconnected");
  }

  //----------------------------------------------------------------------------
  void IPCServer::onStatusClientConnect()
  {
    QTcpSocket* client = _status_server->nextPendingConnection();

    qDebug() << "status client connected"
             << client->peerAddress()
             << "port:"
             << client->peerPort();

    connect( client, &QTcpSocket::disconnected,
             client, &QTcpSocket::deleteLater );
    connect( client, &QTcpSocket::readyRead,
             this, &IPCServer::onStatusClientReadyRead );
  }

  //----------------------------------------------------------------------------
  void IPCServer::onStatusClientReadyRead()
  {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());

    qDebug() << client->readAll();
    QString response(
      "HTTP/1.0 200 OK\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Size: 0\r\n"
      "\r\n"
    );
    client->write(response.toLocal8Bit());
    client->disconnectFromHost();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientRegister(ClientRef client, QJsonObject const& msg)
  {
    const QString title = from_json<QString>(msg.value("title"));
    const WindowRegions& windows = _window_monitor.getWindows();

    // Identify window and get its id

    WId wid = 0;
    if( msg.contains("wid") )
    {
      LOG_DEBUG("Use window id from message.");
      wid = from_json<quintptr>(msg.value("wid"));
    }
    else if(    msg.contains("pos")
             || msg.contains("pid")
             || !title.isEmpty() )
    {
      if( msg.contains("pos") )
        wid = windows.windowIdAt( from_json<QPoint>(msg.value("pos")) );
      else
      {
        WindowInfoIterators possible_windows;
        if( msg.contains("pid") )
          possible_windows = windows.find_all(
            from_json<uint32_t>(msg.value("pid")),
            title
          );
        else
          possible_windows = windows.find_all(title);

        LOG_DEBUG("Found " << possible_windows.size() << " matches");

        if( !possible_windows.empty() )
        {
          QRect geom = from_json<QRect>( msg.value("geom"),
                                         possible_windows.front()->region );

          for(auto w: possible_windows)
          {
            if(    w->region.x() == geom.x()
                && w->region.y() == geom.y()
                && w->region.width() == geom.width()
                   // ignore title bar of maximized windows, because
                   // they are integrated into the global menubar
                && std::abs(w->region.height() - geom.height()) < 30 )
            {
              LOG_DEBUG(" - region match");
              wid = w->id;
            }
            else
              LOG_DEBUG(" - region mismatch " << to_string(w->region));
          }
        }
      }
    }

    if( wid )
      client->setWindowId(wid);
    else
      LOG_DEBUG("Can not get window id for client.");

    // Supported commands

    if( msg.contains("cmds") )
    {
      for(QJsonValue cmd: msg.value("cmds").toArray())
        client->addCommand( from_json<QString>(cmd) );
    }

    // Unique client identifier

    if( !msg.contains("client-id") )
      LOG_WARN("REGISTER: Missing 'client-id'.");
    else
      client->setId( from_json<QString>(msg.value("client-id")) );

    QUrl url = from_json<QUrl>(msg.value("url"));
    if( !url.isValid() )
    {
      LOG_WARN("REGISTER: Missing or invalid 'url'.");
      sendUrlUpdate(client);
    }
    else
    {
      client->setUrl(url);
      urlInc(url);
      sendUrlUpdate();
    }

    // Viewport/Scroll region

    client->parseView(msg);

    // Connect to all active links

    for(auto const& link: *_slot_links->_data)
    {
      QJsonObject req;
      req["task"] = "REQUEST";
      req["id"] = link._id;
      req["stamp"] = qint64(link._stamp);
      req["data"] = QJsonObject::fromVariantMap(link._props);

      distributeMessage(link, req, {client});
    }

    QMutexLocker lock_links(_mutex_slot_links);
    client->update(windows);

    qDebug() << "connected"
             << "\n  region:" << client->getWindowInfo().region
             << "\n  viewport:" << client->getViewportAbs();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientResize(ClientRef client, QJsonObject const& msg)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    client->parseView(msg);
    client->update(_window_monitor.getWindows());
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientSync(ClientRef client, QJsonObject const& msg)
  {
    QString const type = from_json<QString>(msg.value("type"));

    if( type == "SCROLL" )
    {
      if( from_json<QString>(msg.value("item"), "CONTENT") != "CONTENT" )
        // TODO handle ELEMENT scroll?
        return;

      QMutexLocker lock_links(_mutex_slot_links);

      client->setScrollPos( from_json<QPoint>(msg.value("pos")) );
      client->update(_window_monitor.getWindows());

      // Forward scroll events to clients which support this (eg. for
      // synchronized scrolling)
  //    for(auto const& socket: _clients)
  //    {
  //      if(    sender() != socket.first
  //          && socket.second->supportsCommand("scroll") )
  //        socket.first->sendTextMessage(data);
  //    }

      dirtyLinks();
    }
    else if( type == "URL" )
    {
      QUrl old_url = client->url();
      client->setUrl( from_json<QUrl>(msg.value("url")) );

      if( old_url != client->url() )
      {
        urlDec(old_url);
        urlInc(client->url());
        sendUrlUpdate();
      }
    }
    else
      qWarning() << "Unknown SYNC message." << msg;
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientCmd( ClientRef client,
                               QJsonObject const& msg,
                               QString const& msg_raw )
  {
    QString cmd = from_json<QString>(msg.value("cmd"));
    for(auto socket: _clients)
    {
      if(    client != socket.second
          && socket.second->supportsCommand(cmd) )
        socket.first->sendTextMessage(msg_raw);
    }
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClientSemanticZoom(ClientRef client, QJsonObject const& msg)
  {
    int step = from_json<int>(msg.value("step"));
    qreal fac = exp2(step);
    qDebug() << "zoom" << step << fac;

    QRect geom = client->getWindowInfo().queryGeometry();
    qDebug() << "geom" << geom;

    if( step < 0 && geom.width() <= 400 && geom.height() <= 300 )
    {
      //client->tile_map_uncompressed;
      //addOutline(*client_info);
      client->iconifyWindow();
      fac = 1;

      if( !client->hasSemanticPreview() )
        client->setSemanticPreview(
          _subscribe_previews->_data->getWindow(client)
        );
    }

    geom.setSize(geom.size() * fac);

    QSize desktop_size = _subscribe_desktop_rect->_data->size.toQSize();
    if(    geom.width() > 1.8 * desktop_size.width()
        || geom.height() > 1.8 * desktop_size.height() )
    {
      qDebug() << "no larger anymore" << geom << desktop_size;
      // Don't let the window size go crazy
      return;
    }

    QPoint center = from_json<QPoint>(msg.value("center"), geom.center()),
           offset = geom.topLeft() - center,
           new_pos = center + fac * offset;

    geom.moveTo( std::max(0, new_pos.x()),
                 std::max(0, new_pos.y()) );

    qDebug() << "moveResize" << geom;

    client->moveResizeWindow(geom);
  }

  //----------------------------------------------------------------------------
  QPair<PropertyObjectMap::iterator, bool>
  IPCServer::getConcept( QJsonObject const& msg,
                         bool create,
                         QString const& error_desc )
  {
    QString id = from_json<QString>(msg.value("id")).trimmed().toLower();
    if( id.isEmpty() )
    {
      qWarning() << "Empty concept id" << error_desc << msg;
      return {_concept_nodes.end(), false};
    }
    id.replace(':', '_'); // colon is reserved for link ids

    auto it = _concept_nodes.find(id);
    if( it == _concept_nodes.end() )
    {
      if( create )
        return {_concept_nodes.insert(id, {{"id", id}}), true};
      else if( !error_desc.isEmpty() )
        qWarning() << "Can not" << error_desc << "not existing concept" << msg;
    }

    return {it, false};
  }

  //----------------------------------------------------------------------------
  QPair<PropertyObjectMap::iterator, bool>
  IPCServer::getConceptLink( QJsonObject const& msg,
                             bool create,
                             QString const& error_desc )
  {
    QStringList node_ids = from_json<QStringList>(msg.value("nodes"));
    if( node_ids.isEmpty() )
      node_ids = from_json<QString>(msg.value("id")).split(':');

    if( node_ids.size() != 2 )
    {
      qWarning() << error_desc
                 << "Edge with != 2 nodes not supported. Got:" << node_ids;
      return {_concept_links.end(), false};
    }

    for(QString const& id: node_ids)
    {
      if( id.isEmpty() )
      {
        qWarning() << error_desc << "Empty node id." << node_ids;
        return {_concept_links.end(), false};
      }
    }

    node_ids.sort();
    QString link_id = node_ids.join(':');

    auto it = _concept_links.find(link_id);
    if( it == _concept_links.end() )
    {
      if( create )
        return {_concept_links.insert(link_id, {{"nodes", node_ids}}), true};
      else if( !error_desc.isEmpty() )
        qWarning() << "Can not" << error_desc << "not existing edge" << msg;
    }

    return {it, false};
  }

  //----------------------------------------------------------------------------
  void IPCServer::updateRefs( QVariantMap& props,
                              QJsonObject const& msg )
  {
    QJsonObject new_ref = msg.value("ref").toObject();
    QUrl url = from_json<QUrl>( !new_ref.isEmpty() ? new_ref.value("url")
                                                   : msg.value("url") );

    if( !url.isValid() )
    {
      qWarning() << "Missing or invalid url for updating reference." << msg;
      return;
    }

    auto refs = props.value("refs").toMap();
    auto ref_it = refs.find(url.toString());

    QString const cmd = from_json<QString>(msg.value("cmd"));
    if( cmd == "delete" )
    {
      if( ref_it == refs.end() )
      {
        qWarning() << "Can not remove not existing reference" << msg;
        return;
      }

      qDebug() << "Removing reference." << url;
      refs.erase(ref_it);
    }
    else
    {
      QVariantList new_selections =
        new_ref.value("selections").toArray().toVariantList();
      if( ref_it == refs.end() )
      {
        qDebug() << "Adding new reference." << url;

        refs[ url.toString() ] = QVariantMap({
          {"icon", from_json<QString>(new_ref.value("icon"))},
          {"title", from_json<QString>(new_ref.value("title"))},
          {"selections", new_selections}
        });
      }
      else
      {
        qDebug() << "Adding selection to existing reference." << url;

        auto ref = ref_it.value().toMap();

        if( new_ref.contains("icon") )
          ref["icon"] = from_json<QString>(new_ref.value("icon"));
        if( new_ref.contains("title") )
          ref["title"] = from_json<QString>(new_ref.value("title"));

        ref["selections"] = ref.value("selections").toList() + new_selections;
        ref_it.value() = ref;
      }
    }

    props["refs"] = refs;
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-NEW',
  //    'id':   <concept-id>,
  //    'name': <concept-name> // (optional)
  //    <any optional addional object properties>
  void IPCServer::onConceptNew(ClientRef client, QJsonObject const& msg)
  {
    auto insert_pair = getConcept(msg, true, "create concept");
    if( insert_pair.first == _concept_nodes.end() )
      return;

    auto it = insert_pair.first;
    QString const id = it->value("id").toString();
    QString const name = msg.contains("name")
                       ? from_json<QString>(msg.value("name")).trimmed()
                       : from_json<QString>(msg.value("id")).trimmed();

    if( !insert_pair.second )
    {
      qWarning() << "Concept" << name << "with id" << id << "already exists.";
      return;
    }
    qDebug() << "add concept:" << name << "with id" << id;

    QJsonObject msg_ret(msg);
    msg_ret.remove("task");
    msg_ret["id"] = id;
    msg_ret["name"] = name;

    it.value() = msg_ret.toVariantMap();

    msg_ret["task"] = "CONCEPT-NEW";
    distributeMessage(msg_ret);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-UPDATE',
  //    'id':   <concpet-id>,
  //    <new values for all properties that should be changed>
  void IPCServer::onConceptUpdate(ClientRef client, QJsonObject const& msg)
  {
    auto insert_pair = getConcept(msg, false, "update concept");
    if( insert_pair.first == _concept_nodes.end() )
      return;

    const QStringList ignore_props{
      "id",
      "task"
    };
    auto& props = *insert_pair.first;

    bool changed = false;
    for(auto it = msg.begin(); it != msg.end(); ++it)
    {
      if( ignore_props.contains(it.key()) )
        continue;

      QString const val = from_json<QString>(it.value()).trimmed(),
                    old_val = props.value(it.key()).toString();

      if( val == old_val )
      {
        qDebug() << "Ignoring unchanged value of" << it.key();
        continue;
      }

      changed = true;
      props[it.key()] = val;

      qDebug() << "Changing property" << it.key() << "of concept"
               << props.value("name")
               << "from" << old_val
               << "to" << val;
    }

    if( !changed )
      return;

    QJsonObject msg_ret = QJsonObject::fromVariantMap(props);
    msg_ret["task"] = "CONCEPT-UPDATE";

    distributeMessage(msg_ret);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-DELETE',
  //    'id':   <concept-id>
  void IPCServer::onConceptDelete(ClientRef client, QJsonObject const& msg)
  {
    auto insert_pair = getConcept(msg, false, "delete concept");
    if( insert_pair.first == _concept_nodes.end() )
      return;

    auto it = insert_pair.first;
    QString const id = it->value("id").toString();

    qDebug() << "remove concept:" << it->value("name") << "with id" << id;
    _concept_nodes.erase(it);

    distributeMessage(msg);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-UPDATE-REFS',
  //    'cmd': 'add' | 'delete',
  //    'id': <concept-id>,
  //    'ref': {
  //      'url': <url>,
  //      'icon': <img_data>,
  //      'selections': [[<range>, ...], ...]
  //    }
  void IPCServer::onConceptUpdateRefs(ClientRef, QJsonObject const& msg)
  {
    auto insert_pair = getConcept(msg, false, "update references");
    if( insert_pair.first == _concept_nodes.end() )
      return;

    updateRefs(*insert_pair.first, msg);

    QJsonObject msg_ret = QJsonObject::fromVariantMap(*insert_pair.first);
    msg_ret["task"] = "CONCEPT-UPDATE";

    distributeMessage(msg_ret);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-LINK-NEW',
  //    'nodes': [<node ids>]
  //    'id': <node id>:<node id> (optional instead of 'nodes', sorted
  //                               alphabetically ascending)
  void IPCServer::onConceptLinkNew(ClientRef client, QJsonObject const& msg)
  {
    QString const cmd = from_json<QString>(msg.value("cmd"));
    auto insert_pair = getConceptLink(msg, true, "create concept link");
    if( insert_pair.first == _concept_links.end() )
      return;

    auto node_ids = insert_pair.first.key();
    if( !insert_pair.second )
    {
      qWarning() << "Concept link" << node_ids << "already exists.";
      return;
    }
    qDebug() << "add concept link:" << node_ids;

    distributeMessage(QJsonObject({
      {"task", "CONCEPT-LINK-NEW"},
      {"nodes", to_json(node_ids.split(':'))}
    }));
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-LINK-UPDATE',
  //    'nodes': [<node ids>]
  //    'id': <node id>:<node id> (optional instead of 'nodes', sorted
  //                               alphabetically ascending)
  void IPCServer::onConceptLinkUpdate(ClientRef client, QJsonObject const& msg)
  {
    auto insert_pair = getConceptLink(msg, false, "update concept link");
    if( insert_pair.first == _concept_links.end() )
      return;

    const QStringList ignore_props{
      "id",
      "nodes",
      "task"
    };
    auto& props = *insert_pair.first;

    bool changed = false;
    for(auto it = msg.begin(); it != msg.end(); ++it)
    {
      if( ignore_props.contains(it.key()) )
        continue;

      QString const val = from_json<QString>(it.value()).trimmed(),
                    old_val = props.value(it.key()).toString();

      if( val == old_val )
      {
        qDebug() << "Ignoring unchanged value of" << it.key();
        continue;
      }

      changed = true;
      props[it.key()] = val;

      qDebug() << "Changing property" << it.key() << "of concept link"
               << insert_pair.first.key()
               << "from" << old_val
               << "to" << val;
    }

    if( !changed )
      return;

    QJsonObject msg_ret = QJsonObject::fromVariantMap(props);
    msg_ret["task"] = "CONCEPT-LINK-UPDATE";
    msg_ret["id"] = insert_pair.first.key();

    distributeMessage(msg_ret);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-LINK-DELETE',
  //    'nodes': [<node ids>]
  //    'id': <node id>:<node id> (optional instead of 'nodes', sorted
  //                               alphabetically ascending)
  void IPCServer::onConceptLinkDelete(ClientRef client, QJsonObject const& msg)
  {
    auto insert_pair = getConceptLink(msg, false, "delete concept link");
    if( insert_pair.first == _concept_links.end() )
      return;

    auto it = insert_pair.first;
    auto node_ids = it.key();

    qDebug() << "remove concept link:" << node_ids;
    _concept_links.erase(it);

    distributeMessage(QJsonObject({
      {"task", "CONCEPT-LINK-DELETE"},
      {"id", it.key()}
    }));
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-LINK-UPDATE-REFS'
  //    'cmd': 'add' | 'delete'
  //    'nodes': [<node ids>]
  //    'ref': {
  //      'url': <url>,
  //      'icon': <img_data>,
  //      'selections': [[<range>, ...], ...]
  //    }
  void IPCServer::onConceptLinkUpdateRefs(ClientRef, QJsonObject const& msg)
  {
    auto insert_pair = getConceptLink(msg, false, "update link references");
    if( insert_pair.first == _concept_links.end() )
      return;

    updateRefs(*insert_pair.first, msg);
    qDebug() << "link refs" << _concept_links;

    QJsonObject msg_ret = QJsonObject::fromVariantMap(*insert_pair.first);
    msg_ret["task"] = "CONCEPT-LINK-UPDATE";
    msg_ret["nodes"] = to_json(insert_pair.first.key().split(':'));

    distributeMessage(msg_ret);
  }

  //----------------------------------------------------------------------------
  //    'task': 'CONCEPT-SELECTION-UPDATE',
  //    'selection': [<list of concept and relation ids>]
  void IPCServer::onConceptSelectionUpdate( ClientRef client,
                                            QJsonObject const& msg )
  {
    _concept_selection = from_json<StringSet>(msg.value("selection"));
    distributeMessage(msg, client);
  }

  //----------------------------------------------------------------------------
  void IPCServer::onValueGet(ClientRef client, QJsonObject const& msg)
  {
    QString const id = from_json<QString>(msg.value("id"));

    QJsonObject msg_ret;
    msg_ret["task"] = "GET-FOUND";
    msg_ret["id"] = id;

    if( id == "/routing" )
    {
      QJsonObject routing;
      routing["active"] = _subscribe_routing->_data->active.c_str();
      routing["default"] = _subscribe_routing->_data->getDefault().c_str();

      QJsonArray available;
      for(auto comp: _subscribe_routing->_data->available)
        available << QJsonArray::fromStringList({ comp.first.c_str(),
                                                  comp.second ? "1" : "0" });
      routing["available"] = available;
      msg_ret["val"] = routing;
    }
    else if( id == "/search-history" )
    {
      std::string history;
      (*_subscribe_user_config->_data)->getString("QtWebsocketServer:SearchHistory", history);
      msg_ret["val"] = QString::fromStdString(history);
    }
    else if( id == "/state/all" ) // get state of all connected applications
                                  // and active links
    {
      _save_state_client = client;

      QJsonObject msg;
      msg["task"] = "GET";
      msg["id"] = "/state/all";

      QString const msg_str =
        QString::fromLocal8Bit(
          QJsonDocument(msg).toJson(QJsonDocument::Compact)
        );

      bool need_to_wait = false;
      for(auto const& client: _clients)
      {
        if( !client.second->supportsCommand("save-state") )
          continue;

        client.second->setStateData(QJsonObject());
        client.first->sendTextMessage(msg_str);

        qDebug() << "request state"
                 << client.second->getWindowInfo().title
                 << client.second->getWindowInfo().region;

        need_to_wait = true;
      }

      if( !need_to_wait )
        checkStateData();

      return;
    }
    else if( id == "/concepts/all" )
    {
      QJsonObject concept_graph = conceptGraphToJson();
      for(auto it = concept_graph.begin(); it != concept_graph.end(); ++it)
        msg_ret[it.key()] = it.value();
    }
    else if( id == "/desktop/size" )
    {
      msg_ret["val"] = to_json(desktopRect().bottomRight().toQSize());
    }
    else
    {
      std::string val_std;
      if( !(*_subscribe_user_config->_data)
           ->getParameter
            (
              to_string(id),
              val_std,
              to_string(from_json<QString>(msg.value("type")))
            ) )
      {
        LOG_WARN("Requesting unknown value: " << id);
        return;
      }

      msg_ret["val"] = QString::fromStdString(val_std);
    }

    client->socket->sendTextMessage(
      QJsonDocument(msg_ret).toJson(QJsonDocument::Compact)
    );
  }

  //----------------------------------------------------------------------------
  void IPCServer::onValueGetFound(ClientRef client, QJsonObject const& msg)
  {
    QString const id = from_json<QString>(msg.value("id"));
    if( id != "/state/all" )
    {
      qWarning() << "Unknown value for GET-FOUND" << msg;
      return;
    }

    client->setStateData(msg.value("data").toObject());
    QJsonValue layout = client->stateData().value("concept-layout");
    if( layout.isObject() )
      _concept_layout = from_json<PropertyObjectMap>(layout);

    checkStateData();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onValueSet(ClientRef client, QJsonObject const& msg)
  {
    QString id = from_json<QString>(msg.value("id"));
    if( id == "/routing" )
    {
      _subscribe_routing->_data->request =
        to_string(from_json<QString>(msg.value("val")));

      for( auto link = _slot_links->_data->begin();
               link != _slot_links->_data->end();
               ++link )
        link->_stamp += 1;
      LOG_INFO("Trigger reroute -> routing algorithm changed");
    }
    else
    {
      if( !(*_subscribe_user_config->_data)
           ->setParameter
            (
              to_string(id),
              to_string(from_json<QString>(msg.value("val"))),
              to_string(from_json<QString>(msg.value("type")))
            ) )
      {
        LOG_WARN("Request setting unknown value: " << id);
        return;
      }
    }

    dirtyLinks();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onWindowManagementCommand(ClientRef, QJsonObject const& msg)
  {
    QString const cmd = from_json<QString>(msg.value("cmd"));
    if( cmd == "activate-window" )
    {
      QUrl const url = from_json<QUrl>(msg.value("url"));
      if( !url.isValid() )
      {
        qWarning() << "WM command: Missing or invalid 'url'.";
        return;
      }

      for(auto client: _clients)
      {
        if( client.second->url() == url )
          client.second->activateWindow();
      }
    }
    else
      qWarning() << "Unknown window management command:" << msg;
  }

  //----------------------------------------------------------------------------
  void IPCServer::onSaveState(ClientRef, QJsonObject const&)
  {
    saveState();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onLoadState(ClientRef, QJsonObject const& msg)
  {
    QString const file_name = from_json<QString>(msg.value("path")).trimmed();
    if( !file_name.isEmpty() )
      loadState(file_name);
    else
      qWarning() << "Missing 'path' for loading state!";
  }

  //----------------------------------------------------------------------------
  void IPCServer::onLinkInitiate(ClientRef client, QJsonObject const& msg)
  {
    QString const id = from_json<QString>(msg.value("id")).trimmed();
    if( id.length() > 256 )
    {
      LOG_WARN("Search identifier too long!");
      return;
    }

    qDebug() << "INITATE" << id;

#if 0
    std::string history;
    (*_subscribe_user_config->_data)->getString("QtWebsocketServer:SearchHistory", history);

    QString clean_id = QString(id).replace(",","") + ",";
    QString new_history = clean_id
                        + QString::fromStdString(history).replace(clean_id, "");

    (*_subscribe_user_config->_data)
      ->setString("QtWebsocketServer:SearchHistory", to_string(new_history));
#endif

    auto window_list = _window_monitor.getWindows();
    StringList client_whitelist,
               client_blacklist;
    for( QString const& entry:
           from_json<QStringList>(msg.value("whitelist"), {}) )
    {
      if( entry == "this" )
      {
        client_whitelist.push_back(client->id().toStdString());
        continue;
      }

      QStringList path_parts = entry.split('/', QString::SkipEmptyParts);
      if( path_parts.empty() )
      {
        LOG_WARN("INITIATE: skip empty whitelist entry.");
        continue;
      }

      // window-at/<pos-x>|<pos-y>
      if( path_parts[0] == "window-at" )
      {
        QStringList pos_parts;
        if( path_parts.size() == 2 )
          pos_parts = path_parts[1].split('|');

        if( pos_parts.size() != 2 )
        {
          LOG_WARN( "INITIATE: invalid whitelist entry,"
                    " should be 'window-at/<pos-x>|<pos-y>'" );
          continue;
        }

        QPoint pos(pos_parts[0].toInt(), pos_parts[1].toInt());
        auto window = window_list.windowAt(pos);
        if( window == window_list.rend() )
        {
          LOG_WARN("INITIATE: whitelist: no match for '" << entry << "'");
          return;
        }

        auto other_client = findClientInfo(window->id);
        if( other_client == _clients.end() )
        {
          LOG_WARN("INITIATE: can not link to not connected window");
          return;
        }

        client_whitelist.push_back(other_client->second->id().toStdString());
      }
      else
      {
        client_whitelist.push_back(entry.toStdString());
      }
    }

    Color link_color = from_json<Color>(msg.value("color"));

    // Remove eventually existing search for same id
    auto link = findLink(id);
    if( link != _slot_links->_data->end() )
    {
      if( !link_color.isVisible() )
        // keep color if none is requested
        link_color = link->_color;

      qDebug() << "Replacing search for" << link->_id;
      abortLinking(link, client->getWindowInfo().id);
    }
    else if( !link_color.isVisible() )
    {
      // get first unused color
      for(Color const& color: _colors)
      {
        if( std::find_if(
              _slot_links->_data->begin(),
              _slot_links->_data->end(),
              [color](const LinkDescription::LinkDescription& desc)
              {
                return desc._color == color;
              }
            ) == _slot_links->_data->end() )
        {
          link_color = color;
          break;
        }
      }

      if( !link_color.isVisible() )
        // No unused color available -> need to reuse, so use a "random" one.
        link_color = _colors[ _slot_links->_data->size() % _colors.size() ];
    }

    auto hedge = LinkDescription::HyperEdge::make_shared();
    hedge->set("link-id", to_string(id));
    hedge->addNode( client->parseRegions(msg) );
    client->update(window_list);
    updateCenter(hedge.get());

    QJsonObject link_props(msg);
    link_props.remove("color"); // TODO sync color? store it here?
    link_props.remove("id");
    link_props.remove("stamp");
    link_props.remove("task");
    link_props.remove("whitelist");

    uint32_t stamp = from_json<uint32_t>(msg.value("stamp"));
    _slot_links->_data->push_back(
      LinkDescription::LinkDescription(
        id,
        stamp,
        hedge,
        link_color,
        client_whitelist,
        client_blacklist,
        link_props.toVariantMap()
      )
    );
    _slot_links->setValid(true);
    auto const& new_link = _slot_links->_data->back();

    QJsonObject req;
    req["task"] = "REQUEST";
    req["id"] = id;
    req["stamp"] = qint64(stamp);
    req["data"] = link_props;

    distributeMessage(new_link, req);
  }

  //----------------------------------------------------------------------------
  void IPCServer::onLinkUpdate(ClientRef client, QJsonObject const& msg)
  {
    QMutexLocker lock_links(_mutex_slot_links);
    LinkDescription::LinkList::iterator link;
    if( !requireLink(msg, link) )
      return;

    QString const task = from_json<QString>(msg.value("task"));
    qDebug() << "Handling link update:" << task;

    QString new_id = from_json<QString>(msg.value("new-id"), "");
    if( !new_id.isEmpty() )
    {
      qDebug() << "Renaming link" << link->_id << "==>" << new_id;
      link->_id = new_id;

      QJsonObject msg_fwd;
      msg_fwd["task"] = "UPDATE";
      msg_fwd["id"] = link->_id;
      msg_fwd["stamp"] = qint64(link->_stamp);
      msg_fwd["new-id"] = new_id;

      QString const msg_fwd_str =
        QString::fromLocal8Bit(
          QJsonDocument(msg_fwd).toJson(QJsonDocument::Compact)
        );

      for(auto const& socket: _clients)
      {
        if( sender() != socket.first )
          socket.first->sendTextMessage(msg_fwd_str);
      }
    }

    // Check for existing regions and remove them before parsing the new ones
    LinkDescription::NodePtr node;
    for(auto& n: link->_link->getNodes())
    {
      if( n->getChildren().empty() )
        continue;

      auto& hedge = n->getChildren().front();
      if( hedge->get<WId>("client_wid") == client->getWindowInfo().id )
      {
        node = n;
        break;
      }
    }

    if( node )
      client->parseRegions(msg, node);
    else
      link->_link->addNode(client->parseRegions(msg));

    client->update(_window_monitor.getWindows());
    updateCenter(link->_link.get());

    dirtyLinks();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onLinkAbort( ClientRef client,
                               QJsonObject const& msg,
                               QString const& msg_raw )
  {
    QString const id = from_json<QString>(msg.value("id"));
    uint32_t stamp = from_json<uint32_t>(msg.value("stamp"));

    if( id.isEmpty() && stamp == (uint32_t)-1 )
      return abortAll();

    QMutexLocker lock_links(_mutex_slot_links);
    LinkDescription::LinkList::iterator link;
    if( !requireLink(msg, link) )
      return;

    WId abort_wid = client->getWindowInfo().id;
    QString const scope = from_json<QString>(msg.value("scope"), "all");

    if( scope == "all" )
    {
      // Abort from all windows
      abort_wid = 0;

      // Forward to clients
      for(auto socket = _clients.begin(); socket != _clients.end(); ++socket)
        if( sender() != socket->first )
          socket->first->sendTextMessage(msg_raw);
    }
    else if( scope == "this" )
    {
      std::remove( link->_client_whitelist.begin(),
                   link->_client_whitelist.end(),
                   client->id().toStdString() );
    }

    abortLinking(link, abort_wid);
  }

  //----------------------------------------------------------------------------
  void IPCServer::regionsChanged(const WindowRegions& regions)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    _window_monitor.setDesktopRect( desktopRect().toQRect() );

    bool need_update = true; // TODO update checks to also detect eg. changes in
                             //      outside icons.
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

    if( !need_update )
      return;

    LOG_INFO("Windows changed -> trigger reroute");
    dirtyLinks();
  }

  //----------------------------------------------------------------------------
  class CoverWindows
  {
    public:
      CoverWindows( const WindowInfos::const_iterator& begin,
                    const WindowInfos::const_iterator& end ):
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
        size_t min_index = 0;

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
        const QRect& reg = getCoverRegion(p_cov);
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
      const WindowInfos::const_iterator& _begin,
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
  IPCServer::ClientInfos::iterator IPCServer::findClientInfo(WId wid)
  {
    return std::find_if
    (
      _clients.begin(),
      _clients.end(),
      [wid](const ClientInfos::value_type& it)
      {
        return it.second->getWindowInfo().id == wid;
      }
    );
  }

  //----------------------------------------------------------------------------
  IPCServer::ClientInfos::iterator
  IPCServer::findClientInfoById(QString const& id)
  {
    return std::find_if
    (
      _clients.begin(),
      _clients.end(),
      [id](const ClientInfos::value_type& it)
      {
        return it.second->id() == id;
      }
    );
  }

  //----------------------------------------------------------------------------
  LinkDescription::LinkList::iterator IPCServer::findLink(const QString& id)
  {
    return std::find_if
    (
      _slot_links->_data->begin(),
      _slot_links->_data->end(),
      [&id](const LinkDescription::LinkDescription& desc)
      {
        return desc._id.compare(id, Qt::CaseInsensitive) == 0;
      }
    );
  }

  //----------------------------------------------------------------------------
  bool IPCServer::requireLink( const QJsonObject& msg,
                               LinkDescription::LinkList::iterator& link )
  {
    link = findLink( from_json<QString>(msg.value("id")).trimmed() );
    if( link == _slot_links->_data->end() )
    {
      qWarning() << "No matching link for message:" << msg;
      return false;
    }

//    if( link->_stamp != stamp )
//    {
//      LOG_WARN("Received FOUND for wrong REQUEST (different stamp)");
//      return false;
//    }

    return true;
  }

  //----------------------------------------------------------------------------
  bool IPCServer::updateHedge( const WindowRegions& regions,
                               LinkDescription::HyperEdge* hedge )
  {
    bool modified = false;
    hedge->resetNodeParents();
    WId client_wid = hedge->get<WId>("client_wid", 0);

    QRect region, scroll_region;
    auto client_info = findClientInfo(client_wid);
    if( client_info != _clients.end() )
      modified |= client_info->second->update(regions);

    const LinkDescription::nodes_t nodes = hedge->getNodes();
    for( auto node = hedge->getNodes().begin();
              node != hedge->getNodes().end();
            ++node )
    {
      for(auto child = (*node)->getChildren().begin();
               child != (*node)->getChildren().end();
             ++child )
        modified |= updateHedge(regions, child->get());
    }

    return modified;
  }

  //----------------------------------------------------------------------------
  bool IPCServer::updateCenter(LinkDescription::HyperEdge* hedge)
  {
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
//    std::cout << " - center = " << hedge_center << hedge_center/num_visible << std::endl;

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
      if( client_wid != regions.windowIdAt(QPoint(vert->x, vert->y)) )
        ++hidden_count;
    }

    LinkDescription::PropertyMap& props = node->getProps();
    bool was_hidden = props.get<bool>("hidden"),
         hidden = hidden_count > node->getVertices().size() / 2;

    if( hidden != was_hidden )
    {
      props.set("hidden", hidden);
      return true;
    }

    return false;
  }

  //----------------------------------------------------------------------------
  void IPCServer::onClick(int x, int y)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    bool changed = foreachPopup([&]( SlotType::TextPopup::Popup& popup,
                                     QWebSocket& socket,
                                     ClientInfo& client_info ) -> bool
    {
      if( popup.region.contains(x, y) )
      {
        client_info.activateWindow();
        popup.hover_region.fadeIn();
        _tile_handler->updateRegion(popup);
        return true;
      }

      if(    !popup.hover_region.isVisible()
          || !popup.hover_region.contains(x, y) )
        return false;

      popup.hover_region.fadeOut();

      QSize preview_size = client_info.preview_size;
      float scale = (preview_size.height()
                    / pow(2.0f, static_cast<float>(popup.hover_region.zoom))
                    )
                  / _preview_height;
      float dy = y - popup.hover_region.region.pos.y,
            scroll_y = scale * dy + popup.hover_region.src_region.pos.y;

      for( auto part_src = client_info.tile_map->partitions_src.begin(),
                part_dst = client_info.tile_map->partitions_dest.begin();
                part_src != client_info.tile_map->partitions_src.end()
             && part_dst != client_info.tile_map->partitions_dest.end();
              ++part_src,
              ++part_dst )
      {
        if( scroll_y <= part_dst->y )
        {
          scroll_y = part_src->x + (scroll_y - part_dst->x);
          break;
        }
      }

      socket.sendTextMessage(QString(
      "{"
        "\"task\": \"SET\","
        "\"id\": \"scroll-y\","
        "\"val\": " + QString("%1").arg(scroll_y - 35) +
      "}"));
      client_info.activateWindow();
      return true;
    });

    changed |= foreachPreview([&]( SlotType::XRayPopup::HoverRect& preview,
                                   QWebSocket& socket,
                                   ClientInfo& client_info ) -> bool
    {
      float2 offset = preview.node->getParent()->get<float2>("screen-offset");
      if(    !preview.region.contains(float2(x, y) - offset)
          && !(preview.isVisible() && preview.preview_region.contains(x, y)) )
        return false;

      preview.node->getParent()->setOrClear("hover", false);
      preview.node->setOrClear("hover", false);
      preview.fadeOut();
      client_info.activateWindow();

      if( !preview.node->get<bool>("outside") )
      {
        // We need to scroll only for regions outside the viewport
        return true;
      }

      QRect client_region = client_info.getViewportAbs();

      const bool scroll_center = true;
      int scroll_y = y;

      if( scroll_center )
      {
        scroll_y -= (client_region.top() + client_region.bottom()) / 2;
      }
      else
      {
        scroll_y -= client_region.top();
        if( scroll_y > 0 )
          // if below scroll up until region is visible
          scroll_y = y - client_region.bottom() + 35;
        else
          // if above scroll down until region is visible
          scroll_y -= 35;
      }

      scroll_y -= client_info.scroll_region.top();

      socket.sendTextMessage(QString(
        "{"
          "\"task\": \"SET\","
          "\"id\": \"scroll-y\","
          "\"val\": " + QString("%1").arg(scroll_y) +
        "}"));

      return true;
    });

    if( changed )
      return dirtyRender();
  }

  void print( const LinkDescription::Node& node,
              const std::string& key,
              const std::string& indent = "" );

  void print( const LinkDescription::HyperEdge& hedge,
              const std::string& key,
              const std::string& indent = "" )
  {
    std::cout << indent << "HyperEdge(" << &hedge << ") "
              << key << " = '" << hedge.getProps().get<std::string>(key) << "'"
              << std::endl;

    for(auto const& c: hedge.getNodes())
      print(*c, key, indent + " ");
  }

  void print( const LinkDescription::Node& node,
              const std::string& key,
              const std::string& indent )
  {
    std::cout << indent << "Node(" << &node << ") "
              << key << " = '" << node.getProps().get<std::string>(key) << "'"
              << std::endl;

    for(auto const& c: node.getChildren())
      print(*c, key, indent + " ");
  }

  //----------------------------------------------------------------------------
  void IPCServer::onMouseMove(int x, int y)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    bool popup_visible = false;
    SlotType::TextPopup::Popup* hide_popup = nullptr;
    bool changed = foreachPopup([&]( SlotType::TextPopup::Popup& popup,
                                     QWebSocket& socket,
                                     ClientInfo& client_info ) -> bool
    {
      auto& reg = popup.hover_region;
      if(    !popup_visible // Only one popup at the same time
          && (  popup.region.contains(x, y)
             || (reg.isVisible() && reg.contains(x,y))
             ) )
      {
//        if( hide_popup )
//          hide_popup->hover_region.fadeOut();

        popup_visible = true;
        if( reg.isVisible() && !reg.isFadeOut() )
          return false;

        if( !reg.isFadeOut() )
          reg.delayedFadeIn();

        _tile_handler->updateRegion(popup);
        return true;
      }
      else if( reg.isVisible() )
      {
        if( !popup.hover_region.isFadeOut() )
          popup.hover_region.delayedFadeOut();
        else
          // timeout already started, so store to be able hiding if other
          // popup is shown before hiding this one.
          hide_popup = &popup;
      }
      else if( reg.isFadeIn() )
      {
        reg.hide();
      }

      return false;
    });

    bool preview_visible = false;
    changed |= foreachPreview([&]( SlotType::XRayPopup::HoverRect& preview,
                                   QWebSocket& socket,
                                   ClientInfo& client_info ) -> bool
    {
      auto const& p = preview.node->getParent();

      if(   !preview_visible // Only one preview at the same time
          && ( preview.region.contains( float2(x, y)
                                      - p->get<float2>("screen-offset"))
            || (preview.isVisible() && preview.preview_region.contains(x,y))
             )
        )
      {
        preview_visible = true;
        if( preview.isVisible() && !preview.isFadeOut() )
          return false;

        if( preview.isFadeOut() )
          preview.fadeIn();
        else
          preview.delayedFadeIn();

        _tile_handler->updateRegion(preview);
        return true;
      }
      else if( preview.isVisible() )
      {
        if( !preview.isFadeOut() && !preview_visible )
          preview.delayedFadeOut();
      }
      else if( preview.isFadeIn() )
      {
        preview.hide();
      }

      return false;
    });

    if( changed )
      dirtyRender();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onScrollWheel(int delta, const float2& pos, uint32_t mod)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    float preview_aspect = _preview_width
                         / static_cast<float>(_preview_height);

    if( foreachPopup([&]( SlotType::TextPopup::Popup& popup,
                      QWebSocket& socket,
                      ClientInfo& client_info ) -> bool
    {
      if( !popup.hover_region.isVisible() )
        return false;

      bool changed = false;
      const float2& scroll_size = popup.hover_region.scroll_region.size;
      float step_y = scroll_size.y
                   / pow(2.0f, static_cast<float>(popup.hover_region.zoom))
                   / _preview_height;

      if( mod & SlotType::MouseEvent::ShiftModifier )
      {
        popup.hover_region.src_region.pos.y -=
          delta / fabs(static_cast<float>(delta)) * 20 * step_y;
        changed |= _tile_handler->updateRegion(popup);
      }
      else if( mod & SlotType::MouseEvent::ControlModifier )
      {
        float step_x = step_y * preview_aspect;
        popup.hover_region.src_region.pos.x -=
          delta / fabs(static_cast<float>(delta)) * 20 * step_x;
        changed |= _tile_handler->updateRegion(popup);
      }
      else
      {
        //pow(2.0f, static_cast<float>(popup.hover_region.zoom)) = scroll_size.y / _preview_height;
        int max_zoom = log2(scroll_size.y / _preview_height / 0.9) + 0.7;
        int old_zoom = popup.hover_region.zoom;
        clampedStep(popup.hover_region.zoom, delta, 0, max_zoom, 1);

        if( popup.hover_region.zoom != old_zoom )
        {
          float scale = (scroll_size.y / pow(2.0f, static_cast<float>(old_zoom)))
                      / _preview_height;
          Rect const old_region = popup.hover_region.region;

          float2 d = pos - popup.hover_region.region.pos,
                 mouse_pos = scale * d + popup.hover_region.src_region.pos;
          _tile_handler->updateRegion
          (
            popup,
            mouse_pos,
            d / popup.hover_region.region.size
          );

          if( old_region != popup.hover_region.region )
            _dirty_flags |= MASK_DIRTY;

          changed = true;
        }
      }

      return changed;
    }) )
      dirtyRender();
  }

  //----------------------------------------------------------------------------
  void IPCServer::onDrag(const float2& delta)
  {
    QMutexLocker lock_links(_mutex_slot_links);

    float preview_aspect = _preview_width
                         / static_cast<float>(_preview_height);

    if( foreachPopup([&]( SlotType::TextPopup::Popup& popup,
                          QWebSocket& socket,
                          ClientInfo& client_info ) -> bool
    {
      if( !popup.hover_region.isVisible() )
        return false;

      const float2& scroll_size = popup.hover_region.scroll_region.size;
      float step_y = scroll_size.y
                   / pow(2.0f, static_cast<float>(popup.hover_region.zoom))
                   / _preview_height;

      popup.hover_region.src_region.pos.y -= delta.y * step_y;
      float step_x = step_y * preview_aspect;
      popup.hover_region.src_region.pos.x -= delta.x * step_x;

      return _tile_handler->updateRegion(popup);
    }) )
      dirtyRender();
  }

  //----------------------------------------------------------------------------
  bool IPCServer::foreachPopup(const IPCServer::popup_callback_t& cb)
  {
    bool changed = false;
    for( auto popup = _subscribe_popups->_data->popups.begin();
              popup != _subscribe_popups->_data->popups.end(); )
    {
      if( !popup->client_socket )
      {
        ++popup;
        continue;
      }

      QWebSocket* client_socket = static_cast<QWebSocket*>(popup->client_socket);
      ClientInfos::iterator client = _clients.find(client_socket);

      if( client == _clients.end() )
      {
        LOG_WARN("Popup without valid client_socket");
        popup->client_socket = 0;
        // deleting would result in invalid iterators inside ClientInfo, so
        // just let it be and wait for the according ClientInfo to remove it.
        //popup = _subscribe_popups->_data->popups.erase(popup);
        ++popup;
        changed = true;
        continue;
      }

      changed |= cb(*popup, *client_socket, *client->second);
      ++popup;
    }

    return changed;
  }


  //----------------------------------------------------------------------------
  bool IPCServer::foreachPreview(const IPCServer::preview_callback_t& cb)
  {
    bool changed = false;
    for( auto preview = _slot_xray->_data->popups.begin();
              preview != _slot_xray->_data->popups.end(); )
    {
      if( !preview->client_socket )
      {
        ++preview;
        continue;
      }

      auto const& p = preview->node->getParent();
      if( !p )
      {
        LOG_WARN("xray preview: parent lost");

        // Hide preview if parent has somehow been lost
        changed |= preview->node->setOrClear("hover", false);

        // deleting would result in invalid iterators inside ClientInfo, so
        // just let it be and wait for the according ClientInfo to remove it.
        //preview = _slot_xray->_data->popups.erase(preview);
        ++preview;
        continue;
      }

      QWebSocket* client_socket = static_cast<QWebSocket*>(preview->client_socket);
      ClientInfos::iterator client = _clients.find(client_socket);

      if( client == _clients.end() )
      {
        LOG_WARN("Preview without valid client_socket");
        preview->client_socket = 0;

        // Hide preview if socket has somehow been lost
        changed |= preview->node->setOrClear("hover", false);

        // deleting would result in invalid iterators inside ClientInfo, so
        // just let it be and wait for the according ClientInfo to remove it.
        //preview = _slot_xray->_data->popups.erase(preview);
        ++preview;
        continue;
      }

      changed |= cb(*preview, *client_socket, *client->second);
      ++preview;
    }

    return changed;
  }

  //----------------------------------------------------------------------------
  void IPCServer::distributeMessage(
      LinkDescription::LinkDescription const& link,
      const QJsonObject& msg
  ) const
  {
    distributeMessage(link, msg, clientList<ClientList>());
  }

  //----------------------------------------------------------------------------
  void IPCServer::distributeMessage(
    LinkDescription::LinkDescription const& link,
    const QJsonObject& msg,
    const ClientList& clients
  ) const
  {
    QByteArray msg_data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    std::cout << "message: "
              << QString::fromLocal8Bit(msg_data).toStdString() << std::endl;

    auto isOnList = []( ClientRef const& client,
                        StringList const& list )
    {
      return std::find_if(
        std::begin(list),
        std::end(list),
        [&](std::string const& client_id)
        {
          return client_id == client->id().toStdString();
        }
      ) != std::end(list);
    };

    StringList const& whitelist = link._client_whitelist,
                      blacklist = link._client_blacklist;

    for(auto client: clients)
    {
      LOG_DEBUG("check: " << client->getWindowInfo().title.toStdString());
      if( isOnList(client, blacklist) )
      {
        LOG_DEBUG("on blacklist");
        continue;
      }

      if( !whitelist.empty() && !isOnList(client, whitelist) )
      {
        LOG_DEBUG("not on whitelist");
        continue;
      }

      LOG_DEBUG("allowed");
      client->socket->sendTextMessage(msg_data);
    }
  }

  //----------------------------------------------------------------------------
  void IPCServer::distributeMessage( const QJsonObject& msg,
                                     const ClientList& clients,
                                     ClientRef sender ) const
  {
    QByteArray msg_data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    for(auto client: clients)
    {
      if( sender != client )
        client->socket->sendTextMessage(msg_data);
    }
  }

  //----------------------------------------------------------------------------
  void IPCServer::distributeMessage( const QJsonObject& msg,
                                     ClientRef sender ) const
  {
    distributeMessage(msg, clientList<ClientList>(), sender);
  }

  //----------------------------------------------------------------------------
  void IPCServer::dirtyLinks()
  {
    _dirty_flags |= LINKS_DIRTY | RENDER_DIRTY | MASK_DIRTY;
    _cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  void IPCServer::dirtyRender()
  {
    _dirty_flags |= RENDER_DIRTY;
    _cond_data_ready->wakeAll();
  }

  //----------------------------------------------------------------------------
  void IPCServer::checkStateData()
  {
    for(auto const& client: _clients)
      if(    client.second->supportsCommand("save-state")
          && client.second->stateData().isEmpty() )
      {
        qDebug() << "Missing state for" << client.second->getWindowInfo().title;
        return;
      }

    qDebug() << "state complete";

    QWebSocket* socket = nullptr;
    auto save_client = _save_state_client.lock();

    if( save_client )
    {
      socket = save_client->socket;
      if( !socket )
        qWarning() << "Failed to get socket for client";
    }

    if( !socket && _save_state_file.isEmpty() )
    {
      qWarning() << "Missing client or filename to save state.";
      return;
    }

    QJsonArray client_data;
    for(auto const& client: _clients)
    {
      if( !client.second->supportsCommand("save-state") )
        continue;

      QJsonObject client_state;
      client_state["client-id"] = client.second->id();
      client_state["data"] = client.second->stateData();
      client_state["region"] = to_json(client.second->getWindowInfo().region);
      client_data.append(client_state);
    }

    QJsonObject msg_state;
    msg_state["clients"] = client_data;
    msg_state["screen"] = to_json(desktopRect().bottomRight().toQSize());

    QJsonArray links;
    for(auto const& link: *_slot_links->_data)
    {
      QJsonObject link_data;
      link_data["id"] = link._id;
      link_data["color"] = to_json(link._color);
      link_data["whitelist"] = to_json(link._client_whitelist);
      link_data["blacklist"] = to_json(link._client_blacklist);
      links.push_back(link_data);
    }
    msg_state["links"] = links;
    msg_state["concepts"] = conceptGraphToJson();

    if( !_save_state_file.isEmpty() )
    {
      QFileInfo file_info(_save_state_file);
      QDir file_dir = file_info.absoluteDir();
      if( !file_dir.exists() )
      {
        if( !QDir("/").mkpath(file_dir.absolutePath()) )
        {
          qWarning() << "Creating directory" << file_dir.absolutePath() << "failed...";
          return;
        }
      }

      QFile state_file(_save_state_file);
      if( !state_file.open(QIODevice::Truncate | QIODevice::WriteOnly | QIODevice::Text) )
      {
        qWarning() << "Failed to open file" << _save_state_file;
        return;
      }

      state_file.write(QJsonDocument(msg_state).toJson());
      qDebug() << "Saved state to" << _save_state_file;

      _save_state_file.clear();
    }

    if( !socket )
      return;

    msg_state["task"] = "GET-FOUND";
    msg_state["id"] = "/state/all";

    socket->sendTextMessage(
      QJsonDocument(msg_state).toJson(QJsonDocument::Compact)
    );
  }

  //----------------------------------------------------------------------------
  void IPCServer::urlInc(const QUrl& url)
  {
    _opened_urls[url] += 1;
    _changed_urls[url] += 1;
  }

  //----------------------------------------------------------------------------
  void IPCServer::urlDec(const QUrl& url)
  {
    auto it = _opened_urls.find(url);
    if( it == _opened_urls.end() )
    {
      qWarning() << "Can not decrement counter for unknown url." << url;
      return;
    }

    it.value() -= 1;
    if( it.value() == 0 )
      _opened_urls.erase(it);

    _changed_urls[url] -= 1;
  }

  //----------------------------------------------------------------------------
  void IPCServer::sendUrlUpdate(ClientRef receiver)
  {
    ClientList clients = receiver
                       ? ClientList({receiver})
                       : clientList<ClientList>();

    distributeMessage(QJsonObject({
      {"task", "OPENED-URLS-UPDATE"},
      {"urls", to_json(_opened_urls)},
      {"changes", receiver ? to_json(_opened_urls) : to_json(_changed_urls)}
    }), clients);

    if( !receiver )
      _changed_urls.clear();
  }

  //----------------------------------------------------------------------------
  QJsonObject IPCServer::conceptGraphToJson() const
  {
    QJsonObject graph;
    graph["concepts"] = to_json(_concept_nodes);
    graph["relations"] = to_json(_concept_links);
    graph["selection"] = to_json(_concept_selection);
    graph["layout"] = to_json(_concept_layout);

    return graph;
  }

  //----------------------------------------------------------------------------
  void IPCServer::loadConceptGraphFromJson(const QJsonObject& graph)
  {
    clearConceptGraph();

    _concept_nodes = from_json<PropertyObjectMap>(graph["concepts"]);
    _concept_links = from_json<PropertyObjectMap>(graph["relations"]);
    _concept_selection = from_json<StringSet>(graph["selection"]);
    _concept_layout = from_json<PropertyObjectMap>(graph["layout"]);

    QJsonObject msg(graph);
    msg["id"] = "/concepts/all";
    msg["task"] = "GET-FOUND";

    distributeMessage(msg);
  }

  //----------------------------------------------------------------------------
  LinkDescription::LinkList::iterator
  IPCServer::abortLinking( const LinkDescription::LinkList::iterator& link,
                           WId wid )
  {
    size_t remove_count = 0;
    for(auto const& client: _clients)
    {
      if( !wid || client.second->getWindowInfo().id == wid )
      {
        qDebug() << "remove link for " << client.second->getWindowInfo().title;
        client.second->removeLink(link->_link.get());
        remove_count += 1;
      }
    }

    return remove_count == _clients.size() ? _slot_links->_data->erase(link)
                                           : link;
  }

  //----------------------------------------------------------------------------
  void IPCServer::abortAll()
  {
    QMutexLocker lock_links(_mutex_slot_links);
    for(auto link = _slot_links->_data->begin();
             link != _slot_links->_data->end(); )
      link = abortLinking(link);
    assert( _slot_links->_data->empty() );
  }

  //----------------------------------------------------------------------------
  IPCServer::TileHandler::TileHandler(IPCServer* ipc_server):
    _ipc_server(ipc_server),
    _tile_request_id(0),
    _new_request(0)
  {

  }

  //----------------------------------------------------------------------------
  bool IPCServer::TileHandler::updateRegion( SlotType::TextPopup::Popup& popup,
                                             float2 center,
                                             float2 rel_pos )
  {
    HierarchicTileMapPtr tile_map = popup.hover_region.tile_map.lock();
    float scale = 1/tile_map->getLayerScale(popup.hover_region.zoom);

    float preview_aspect = _ipc_server->_preview_width
                         / static_cast<float>(_ipc_server->_preview_height);
    const QRect reg = popup.hover_region.scroll_region.toQRect();
    float zoom_scale = pow(2.0f, static_cast<float>(popup.hover_region.zoom));

    int h = reg.height() / zoom_scale + 0.5f,
        w = h * preview_aspect + 0.5;

    Rect& src = popup.hover_region.src_region;
    //float2 old_pos = src.pos;

    src.size.x = w;
    src.size.y = h;

    if( popup.auto_resize )
    {
      int real_width = reg.width() / scale + 0.5f,
          popup_width = std::min(_ipc_server->_preview_width, real_width);

      Rect& popup_region = popup.hover_region.region;
      const Rect& icon_region = popup.region.region;
      float offset_l = popup_region.pos.x - icon_region.pos.x,
            offset_r = icon_region.size.x - popup_region.size.x - offset_l;

      if( std::signbit(offset_l) == std::signbit(offset_r) )
      {
        int center_x = popup_region.pos.x + 0.5 * popup_region.size.x;
        popup_region.size.x = popup_width;
        popup_region.pos.x = center_x - popup_width / 2;
      }
      else
      {
        if( icon_region.pos.x > popup_region.pos.x )
          popup_region.pos.x += popup_region.size.x - popup_width;
        popup_region.size.x = popup_width;
      }
    }

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

    if( !popup.client_socket )
    {
      LOG_WARN("Popup without active socket.");
      return false;
    }

    QWebSocket* socket = static_cast<QWebSocket*>(popup.client_socket);
    ClientInfos::iterator client = _ipc_server->_clients.find(socket);

    if( client == _ipc_server->_clients.end() )
    {
      LOG_WARN("Popup without valid client_socket");
      popup.client_socket = 0;

      return true;
    }

    return !updateTileMap(
      tile_map,
      client->second,
      src,
      popup.hover_region.zoom
    );
  }

  //----------------------------------------------------------------------------
  bool IPCServer::TileHandler::updateRegion(
    SlotType::XRayPopup::HoverRect& popup
  )
  {
    auto client = _ipc_server->_clients.find(
      static_cast<QWebSocket*>(popup.client_socket)
    );
    if( client == _ipc_server->_clients.end() )
      return false;

    return updateTileMap( popup.tile_map.lock(),
                          client->second,
                          popup.source_region,
                          -1 );
  }

  //----------------------------------------------------------------------------
  bool IPCServer::TileHandler::updateTileMap(
    const HierarchicTileMapPtr& tile_map,
    ClientRef client,
    const Rect& src,
    int zoom )
  {
    if( !tile_map )
    {
      LOG_WARN("Tilemap has expired!");
      return false;
    }

    if( !client )
    {
      LOG_WARN("Client has expired!");
      return false;
    }

    if( !client->socket )
    {
      LOG_WARN("Client has no socket!");
      return false;
    }

    bool sent = false;
    MapRect rect = tile_map->requestRect(src, zoom);
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
                && (tile_req.second.zoom == zoom)
                && (tile_req.second.x == x)
                && (tile_req.second.y == y);
          }
        );

        if( req != _tile_requests.end() )
          // already sent
          return;

        TileRequest tile_req = {
          client->socket,
          tile_map,
          zoom,
          x, y,
          float2(tile.width, tile.height),
          clock::now()
        };
        uint8_t req_id = ++_tile_request_id;
        _tile_requests[req_id] = tile_req;
        sent = true;
      }
    });
    if( sent )
      _new_request = 2;
    return sent;
  }

} // namespace LinksRouting
