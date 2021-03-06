#ifndef LR_LINKDESCRIPTION
#define LR_LINKDESCRIPTION

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QVariant>
#include <QVector>

#include "datatypes.h"
#include "float2.hpp"
#include "string_utils.h"

#include <functional>
#include <list>
#include <map>
#include <vector>
#include <memory>
#include <iostream>

typedef QVector<QStringList> FilterList; // Each filter consists of one or more parts
typedef QMap<QString, QVariantMap> PropertyObjectMap;

namespace LinksRouting
{
  enum class Direction { LEFT, UP, RIGHT, DOWN };
  Direction dirFromNorm(const float2& normal);

  typedef std::function<void()> ExitCallback;

  struct ClientInfo;
  typedef std::shared_ptr<ClientInfo> ClientRef;
  typedef std::weak_ptr<ClientInfo> ClientWeakRef;
  typedef std::vector<ClientRef> ClientList;
  typedef std::vector<ClientWeakRef> ClientWeakList;

namespace LinkDescription
{
  class HyperEdge;
  typedef std::shared_ptr<HyperEdge> HyperEdgePtr;
  typedef std::weak_ptr<HyperEdge> HyperEdgeWeakPtr;
  struct HyperEdgeDescriptionSegment;
  struct HyperEdgeDescriptionForkation;
  typedef std::shared_ptr<HyperEdgeDescriptionForkation>
          HyperEdgeDescriptionForkationPtr;
  typedef std::shared_ptr<const HyperEdgeDescriptionForkation>
          HyperEdgeDescriptionForkationConstPtr;

  typedef std::vector<float2> points_t;
  typedef std::map<std::string, std::string> props_t;
  typedef std::vector<HyperEdgePtr> hedges_t;

  class PropertyMap
  {
    public:

      PropertyMap( const props_t& props = props_t() ):
        _props(props)
      {}

      /**
       * Set a property
       *
       * @param key     Property key
       * @param val     New value
       * @return Whether the value has changed
       */
      template<typename T>
      bool set(const std::string& key, const T& val)
      {
//#if defined(WIN32) || defined(_WIN32)
        // Use stringstream because visual studio seems do not have the proper
        // std::to_string overloads.
        std::stringstream strm;
        strm << val;
        if( _props[key] == strm.str() )
          return false;

        _props[ key ] = strm.str();
        return true;
//#else
//      _props[ key ] = std::to_string(val);
//#endif
      }

      bool setOrClear(const std::string& key, bool val)
      {
        if( val )
        {
          if( convertFromString<bool>(_props[key]) )
            return false;
          else
            return set(key, val);
        }

        auto const& it = _props.find(key);
        if( it == _props.end() )
          return false;

        _props.erase(it);

        return true;
      }

      /**
       * Get a property
       *
       * @param key     Property key
       * @param def_val Default value if property doesn't exist
       * @return
       */
      template<typename T>
      T get(const std::string& key, const T& def_val = T()) const
      {
        props_t::const_iterator it = _props.find(key);
        if( it == _props.end() )
          return def_val;

        return convertFromString(it->second, def_val);
      }

      bool has(const std::string& key) const
      {
        return _props.find(key) != _props.end();
      }

      props_t&       getMap()       { return _props; }
      props_t const& getMap() const { return _props; }

      void print( std::ostream& strm = std::cout,
                  std::string const& indent = "",
                  std::string const& indent_incr = "  ") const;

    protected:
      props_t _props;
  };

  class PropertyElement
  {
    public:

      virtual ~PropertyElement(){}

      template<typename T>
      bool set(const std::string& key, const T& val)
      {
        return _props.set(key, val);
      }

      bool setOrClear(const std::string& key, bool val)
      {
        return _props.setOrClear(key, val);
      }

      template<typename T>
      T get(const std::string& key, const T& def_val = T()) const
      {
        const std::string& val = getImpl(key);
        if( val.empty() )
          return def_val;

        return convertFromString(val, def_val);
      }

      bool has(const std::string& key) const
      {
        return _props.has(key);
      }

      PropertyMap&       getProps()       { return _props; }
      PropertyMap const& getProps() const { return _props; }

    protected:
      PropertyMap _props;

      PropertyElement( const PropertyMap& props = PropertyMap() ):
        _props( props )
      {}

      virtual std::string getImpl(const std::string& key) const
      {
        return _props.get(key, std::string());
      }
  };

  class Node:
    public PropertyElement
  {
    friend class HyperEdge;
    public:

      Node();
      explicit Node( const points_t& points,
                     const PropertyMap& props = PropertyMap() );
      Node( const points_t& points,
            const points_t& link_points,
            const PropertyMap& props = PropertyMap() );
      Node( const points_t& points,
            const points_t& link_points,
            const points_t& link_points_children,
            const PropertyMap& props = PropertyMap() );
      explicit Node(HyperEdgePtr hedge);
      ~Node();

      points_t& getVertices();
      const points_t& getVertices() const;

      void setLinkPoints(const points_t& points);
      points_t& getLinkPoints();
      const points_t& getLinkPoints() const;
      float2 getBestLinkPoint( const float2& from_pos,
                               bool use_vertices = false ) const;

      points_t& getLinkPointsChildren();
      const points_t& getLinkPointsChildren() const;

      float2 getCenter() const;
      Rect getBoundingBox() const;

      HyperEdgePtr getParent();
      const HyperEdgePtr getParent() const;

      const hedges_t& getChildren() const;
      hedges_t& getChildren();

      void addChildren(const hedges_t& edges);
      void addChild(const HyperEdgePtr& hedge);
      void clearChildren();

      void print( std::ostream& strm = std::cout,
                  std::string const& indent = "",
                  std::string const& indent_incr = "  ") const;

    private:

      points_t _points;
      points_t _link_points;
      points_t _link_points_children;
      HyperEdgeWeakPtr _parent;
      hedges_t _children;

      virtual std::string getImpl(const std::string& key) const override;
  };

  typedef std::shared_ptr<Node> NodePtr;
  typedef std::weak_ptr<Node> NodeWeakPtr;
  typedef std::list<NodePtr> nodes_t;
  typedef std::vector<NodePtr> node_vec_t;

  class HyperEdge:
    public PropertyElement
  {
    friend class Node;
    public:

      template<typename... _Args>
      static HyperEdgePtr make_shared(_Args&&... __args)
      {
        HyperEdgePtr p( new HyperEdge(std::forward<_Args>(__args)...) );
        p->_self = p;
        p->resetNodeParents();
        return p;
      }

      nodes_t& getNodes();
      const nodes_t& getNodes() const;

      void setCenter(const float2& center);
      const float2& getCenter() const;
      const float2 getCenterAbs() const;

      uint32_t getRevision() const;

      void addNodes(const nodes_t& nodes);
      void addNode(const NodePtr& node);
      void resetNodeParents();
      nodes_t::iterator removeNode(const nodes_t::iterator& node);
      void removeNode(const NodePtr& node);

      Node* getParent();
      const Node* getParent() const;

      void setHyperEdgeDescription(HyperEdgeDescriptionForkationPtr desc);
      HyperEdgeDescriptionForkationPtr getHyperEdgeDescription();
      HyperEdgeDescriptionForkationConstPtr getHyperEdgeDescription() const;

      void removeRoutingInformation();

      void print( std::ostream& strm = std::cout,
                  std::string const& indent = "",
                  std::string const& indent_incr = "  ") const;

    private:

      HyperEdge();
      explicit HyperEdge( const nodes_t& nodes,
                          const PropertyMap& props = PropertyMap() );

      HyperEdge(const HyperEdge&) /* = delete */;
      HyperEdge& operator=(const HyperEdge&) /* = delete */;

      virtual std::string getImpl(const std::string& key) const override;

      HyperEdgeWeakPtr _self;
      Node* _parent;
      nodes_t _nodes;
      float2 _center;     ///!< Estimated center
      uint32_t _revision; ///!< Track modifications
      HyperEdgeDescriptionForkationPtr _fork;
  };


  struct HyperEdgeDescriptionSegment:
    public PropertyElement
  {
      nodes_t nodes;
      points_t trail;
  };
  typedef std::list<HyperEdgeDescriptionSegment> HedgeSegmentList;

  struct HyperEdgeDescriptionForkation
  {
    HyperEdgeDescriptionForkation()
    {
    }

    float2 position;

    HyperEdgeDescriptionSegment incoming;
    HedgeSegmentList outgoing;
  };

  struct LinkDescription
  {
    LinkDescription( const QString& id,
                     uint32_t stamp = 0,
                     const HyperEdgePtr& link = {},
                     const QColor& color = {},
                     const FilterList& client_whitelist = {},
                     const FilterList& client_blacklist = {},
                     const QJsonObject& props = {}):
      _id( id ),
      _stamp( stamp ),
      _link( link ),
      _color( color ),
      _client_whitelist( client_whitelist ),
      _client_blacklist( client_blacklist ),
      _props( props )
    {}

    void print( std::ostream& strm = std::cout,
                std::string const& indent = "",
                std::string const& indent_incr = "  ") const;

    QString       _id;
    uint32_t      _stamp;
    HyperEdgePtr  _link;
    QColor        _color;
    FilterList    _client_whitelist,
                  _client_blacklist;
    QJsonObject   _props;
  };

  typedef std::list<LinkDescription> LinkList;
  void printLinkList( LinkList const& links,
                      std::ostream& strm = std::cout,
                      std::string const& indent = "",
                      std::string const& indent_incr = "  " );

  void printNodeList( nodes_t const& nodes,
                      std::ostream& strm = std::cout,
                      std::string const& indent = "",
                      std::string const& indent_incr = "  " );

  //std::ostream& operator<<(std::ostream& strm, const PropertyMap& m);

} // namespace LinkDescription
} // namespace LinksRouting

#endif //LR_LINKDESCRIPTION
