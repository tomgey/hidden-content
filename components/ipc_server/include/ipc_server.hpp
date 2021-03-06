/*!
 * @file ipc_server.hpp
 * @brief
 * @details
 * @author Thomas Geymayer <tomgey@gmail.com>
 * @date Date of Creation: 02.03.2012
 */

#ifndef _IPC_SERVER_HPP_
#define _IPC_SERVER_HPP_

#include <QtCore>
#include <QtOpenGL/qgl.h>
#include <QTcpServer>
#include <QWebSocketServer>
#include <qwindowdefs.h>

#include "common/componentarguments.h"
#include "config.h"
#include "linkdescription.h"
#include "slotdata/component_selection.hpp"
#include "slotdata/image.hpp"
#include "slotdata/mouse_event.hpp"
#include "slotdata/polygon.hpp"
#include "slotdata/Preview.hpp"
#include "slotdata/text_popup.hpp"
#include "slotdata/TileHandler.hpp"
#include "window_monitor.hpp"

#include "datatypes.h"
#include <stdint.h>

class QMutex;

namespace LinksRouting
{
  struct ClientInfo;

  typedef QSet<QString> StringSet;

  class IPCServer:
    public QObject,
    public Component,
    public ComponentArguments

  {
      Q_OBJECT

    public:

      IPCServer(QMutex* mutex, QWaitCondition* cond_data);
      virtual ~IPCServer();

      void publishSlots(SlotCollector& slot_collector);
      void subscribeSlots(SlotSubscriber& slot_subscriber);

      bool setString(const std::string& name, const std::string& val) override;

      bool startup(Core* core, unsigned int type);
      void init();
      void shutdown();
      bool supports(unsigned int type) const
      {
        return (type & Component::DataServer);
      }

      uint32_t process(unsigned int type) override;

      int getPreviewWidth() const { return _preview_width; }
      int getPreviewHeight() const { return _preview_height; }
      bool getPreviewAutoWidth() const { return _preview_auto_width; }
      bool getOutsideSeeThrough() const;
      Rect desktopRect() const;

      template<class Container>
      Container clientList() const
      {
        Container clients;
        std::transform( _clients.begin(), _clients.end(),
                        std::inserter(clients, clients.end()),
                        std::bind( &ClientInfos::value_type::second,
                                   std::placeholders::_1 ) );
        return clients;
      }

      typedef SlotType::TextPopup::Popups::iterator PopupIterator;

      PopupIterator addPopup( const ClientInfo& client_info,
                              const SlotType::TextPopup::Popup& popup );
      void removePopup(const PopupIterator& popup);
      void removePopups(const std::list<PopupIterator>& popups);

      typedef SlotType::XRayPopup::Popups::iterator XRayIterator;

      XRayIterator addCoveredPreview( const std::string& link_id,
                                      const ClientInfo& client_info,
                                      const LinkDescription::NodePtr& node,
                                      const HierarchicTileMapWeakPtr& tile_map,
                                      const QRect& viewport,
                                      const QRect& scroll_region,
                                      bool extend = false );
      void removeCoveredPreview(const XRayIterator& preview);
      void removeCoveredPreviews(const std::list<XRayIterator>& previews);

      typedef SlotType::CoveredOutline::List::iterator OutlineIterator;

      OutlineIterator addOutline(const ClientInfo& client_info);
      void removeOutline(const OutlineIterator& outline);
      void removeOutlines(const std::list<OutlineIterator>& outlines);

      bool routingActive() const;

      void dumpState(std::ostream& strm = std::cout) const;
      void saveState(const QString& file_name = "");
      void loadState(const QString& file_name);

      void replayLog(const QString& file_name);

      void clearConceptGraph();

    private slots:

      void onClientConnection();
      void onTextReceived(QString data);
      void onBinaryReceived(QByteArray data);
      void onClientDisconnection();

      void onStatusClientConnect();
      void onStatusClientReadyRead();

    protected:

      typedef std::map<QWebSocket*, ClientRef> ClientInfos;
      typedef std::function<void ( ClientRef client,
                                   QJsonObject const& msg,
                                   QString const& msg_raw )> MsgCallback;
      typedef std::map<QString, MsgCallback> MsgCallbackMap;

      void onClientRegister(ClientRef, QJsonObject const& msg);
      void onClientResize(ClientRef, QJsonObject const& msg);
      void onClientSync(ClientRef, QJsonObject const& msg);

      void onClientCmd( ClientRef, QJsonObject const& msg,
                                   QString const& msg_raw );
      void onClientSemanticZoom(ClientRef, QJsonObject const& msg);

      /**
       * Get an existing concept or add a new one.
       *
       * @param create      Create new concept if not exists
       * @param error_desc  Description added to error message if something
       *                    fails
       *
       * @return An iterator to the existing or newly created concept and a bool
       *         indicating whether a new concept has been inserted.
       */
      QPair<PropertyObjectMap::iterator, bool>
      getConcept( QJsonObject const& msg,
                  bool create = false,
                  QString const& error_desc = "" );

      /**
       * Get an existing edge/concept link or add a new one.
       *
       * @param create      Create new edge if not exists
       * @param error_desc  Description added to error message if something
       *                    fails
       *
       * @return An iterator to the existing or newly created edge and a bool
       *         indicating whether a new edge has been inserted.
       */
      QPair<PropertyObjectMap::iterator, bool>
      getConceptLink( QJsonObject const& msg,
                      bool create = false,
                      QString const& error_desc = "" );

      /**
       * Update refs in the given properties (of a node or an edge)
       */
      void updateRefs( QVariantMap& props,
                       QJsonObject const& msg );

      void onConceptNew(ClientRef, QJsonObject const& msg);
      void onConceptUpdate(ClientRef, QJsonObject const& msg);
      void onConceptDelete(ClientRef, QJsonObject const& msg);
      void onConceptUpdateRefs(ClientRef, QJsonObject const& msg);

      void onConceptLinkNew(ClientRef, QJsonObject const& msg);
      void onConceptLinkUpdate(ClientRef, QJsonObject const& msg);
      void onConceptLinkDelete(ClientRef, QJsonObject const& msg);
      void onConceptLinkUpdateRefs(ClientRef, QJsonObject const& msg);

      void onConceptSelectionUpdate(ClientRef, QJsonObject const& msg);

      void onValueGet(ClientRef, QJsonObject const& msg);
      void onValueGetFound(ClientRef, QJsonObject const& msg);
      void onValueSet(ClientRef, QJsonObject const& msg);

      void onWindowManagementCommand(ClientRef, QJsonObject const& msg);

      void onSaveState(ClientRef, QJsonObject const& msg);
      void onLoadState(ClientRef, QJsonObject const& msg);
      void onReplayLog(ClientRef, QJsonObject const& msg);

      void onLinkInitiate(ClientRef, QJsonObject const& msg);
      void onLinkUpdate(ClientRef, QJsonObject const& msg);
      void onLinkAbort( ClientRef, QJsonObject const& msg,
                                   QString const& msg_raw );

      FilterList parseFilterList( FilterList& filter_list,
                                  ClientRef client,
                                  const QStringList& filter_strings,
                                  const WindowRegions& windows );

      void regionsChanged(const WindowRegions& regions);
      ClientInfos::iterator findClientInfo(WId wid);
      ClientInfos::iterator findClientInfoById(QString const& cid);
      ClientRef getClientByWId(WId wid);

      LinkDescription::LinkList::iterator findLink(const QString& id);
      bool requireLink( const QJsonObject& msg,
                        LinkDescription::LinkList::iterator& link );


      bool updateHedge( const WindowRegions& regions,
                        LinkDescription::HyperEdge* hedge );
      bool updateCenter(LinkDescription::HyperEdge* hedge);
      bool updateRegion( const WindowRegions& regions,
                         LinkDescription::Node* node,
                         WId client_wid );

      void onClick(int x, int y);
      void onMouseMove(int x, int y);
      void onScrollWheel(int delta, const float2& pos, uint32_t mod);
      void onDrag(const float2& delta);

      typedef std::function<bool( SlotType::TextPopup::Popup&,
                                  QWebSocket&,
                                  ClientInfo& )> popup_callback_t;
      bool foreachPopup(const popup_callback_t& cb);

      typedef std::function<bool( SlotType::XRayPopup::HoverRect&,
                                  QWebSocket&,
                                  ClientInfo& )> preview_callback_t;
      bool foreachPreview(const preview_callback_t& cb);

      /** Send message to all clients (respecting white and black list) */
      void distributeMessage( LinkDescription::LinkDescription const& link,
                              const QJsonObject& msg ) const;

      /** Send message to listed clients (respecting white and black list) */
      void distributeMessage( LinkDescription::LinkDescription const& link,
                              const QJsonObject& msg,
                              const ClientList& clients ) const;

      /** Send message to all given clients except the sender */
      void distributeMessage( const QJsonObject& msg,
                              const ClientList& clients,
                              ClientRef sender = nullptr ) const;

      /** Send message to all clients except the sender */
      void distributeMessage( const QJsonObject& msg,
                              ClientRef sender = nullptr ) const;

      void sendMessage( const QJsonObject& msg,
                        ClientRef receiver ) const;
      void sendMessageToSupportedClient( const QJsonObject& msg,
                                         const QString& required_cmd,
                                         ClientRef sender ) const;

      void dirtyLinks();
      void dirtyRender();

      /** Check state save data and send to client if complete */
      void checkStateData(bool save_if_not_complete = false);

      /** Increment counter for the given url */
      void urlInc(const QUrl& url);

      /** Decrement counter for the given url */
      void urlDec(const QUrl& url);

      /** Send changes in opened urls after the last call to sendUrlupdate() */
      void sendUrlUpdate(ClientRef receiver = nullptr);

      /** Get concept graph as json object */
      QJsonObject conceptGraphToJson() const;

      /** Load concept graph from json object. (existing graph is deleted) */
      void loadConceptGraphFromJson(const QJsonObject& graph);

      /** Write to the session logfile */
      void logWrite(QJsonObject& msg);

      static QString dateTimeString();

    private:

      QWebSocketServer   *_server;
      QTcpServer         *_status_server;
      ClientInfos         _clients;
      WindowMonitor       _window_monitor;

      QMutex             *_mutex_slot_links;
      QWaitCondition     *_cond_data_ready;
      uint32_t            _dirty_flags;

      MsgCallbackMap      _msg_handlers;
      QVector<QColor>     _colors; //!< Available link colors

      ClientWeakRef       _save_state_client;
      QString             _save_state_file;

      QMap<QUrl, uint8_t> _opened_urls; //!< Urls of documents currently shown
                                        //   shown inside a connected
                                        //   application.
      QMap<QUrl, int8_t>  _changed_urls;

      PropertyObjectMap _concept_nodes,
                        _concept_links;
      StringSet         _concept_selection; //!< Selected concepts and relations

      PropertyObjectMap _concept_layout; //!< Node layout (in the browser
                                         //   concept graph)

      QString           _session_start_stamp,
                        _session_state_dir;
      QFile             _log_file;
      clock::time_point _last_autosave;

      slot_t<std::vector<Rect>>::type _slot_regions;

      /* List of all open searches */
      slot_t<LinkDescription::LinkList>::type _slot_links;

      /* Outside x-ray popup */
      slot_t<SlotType::XRayPopup>::type _slot_xray;
      slot_t<SlotType::CoveredOutline>::type _slot_outlines;

      slot_t<SlotType::TileHandler>::type _slot_tile_handler;

      /* List of available routing components */
      slot_t<SlotType::ComponentSelection>::type _subscribe_routing;

      /* Permanent configuration changeable at runtime */
      slot_t<LinksRouting::Config*>::type _subscribe_user_config;

      /* Drawable desktop region */
      slot_t<Rect>::type _subscribe_desktop_rect;

      /* Slot for registering mouse callback */
      slot_t<SlotType::MouseEvent>::type  _subscribe_mouse;
      slot_t<SlotType::TextPopup>::type   _subscribe_popups;
      slot_t<SlotType::Preview>::type     _subscribe_previews;


      QColor getLinkColor( uint8_t cursor_id,
                           const QColor& requested_color = {} );

      /**
       * Clear all data for the given link.
       *
       * If @a wid is given, aborts only the links to the regions belonging to
       * the according window.
       */
      LinkDescription::LinkList::iterator abortLinking(
        const LinkDescription::LinkList::iterator& link =
              LinkDescription::LinkList::iterator(),
        WId wid = 0
      );

      /**
       * Clear all links
       *
       * @ptr_id  If given clear only the links owned by the given pointer id
       */
      void abortAll(uint8_t ptr_id = 0);

      std::string   _debug_regions,
                    _debug_full_preview_path;
      QImage        _full_preview_img;
      int           _preview_width,
                    _preview_height,
                    _autosave_interval;
      bool          _preview_auto_width,
                    _outside_see_through;

      class TileHandler;
      TileHandler  *_tile_handler;
  };

} // namespace LinksRouting

#endif /* _IPC_SERVER_HPP_ */
