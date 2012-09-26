#ifndef LR_LINKDESCRIPTION
#define LR_LINKDESCRIPTION

#include "color.h"
#include "datatypes.h"
#include "float2.hpp"
#include "string_utils.h"

#include <list>
#include <vector>
#include <memory>

namespace LinksRouting
{
namespace LinkDescription
{
  class Node;
  class HyperEdge;
  struct HyperEdgeDescriptionSegment;
  struct HyperEdgeDescriptionForkation;
  typedef std::shared_ptr<HyperEdgeDescriptionForkation>
          HyperEdgeDescriptionForkationPtr;
  typedef std::shared_ptr<const HyperEdgeDescriptionForkation>
          HyperEdgeDescriptionForkationConstPtr;

  typedef std::vector<float2> points_t;
  typedef std::map<std::string, std::string> props_t;
  typedef std::list<Node> nodes_t;

  class PropertyElement
  {
    public:
      props_t& getProps() { return _props; }
      const props_t& getProps() const { return _props; }

      /**
       * Set a property
       *
       * @param key     Property key
       * @param val     New value
       */
      template<typename T>
      void set(const std::string& key, const T val)
      {
#if defined(WIN32) || defined(_WIN32)
        // Use stringstream because visual studio seems do not have the proper
        // std::to_string overloads.
		std::stringstream strm;
		strm << val;
		_props[ key ] = strm.str();
#else
		_props[ key ] = std::to_string(val);
#endif
      }

      void set(const std::string& key, const std::string& val)
      {
        _props[ key ] = val;
      }

      void set(const std::string& key, const char* val)
      {
        _props[ key ] = val;
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

    protected:
      props_t   _props;

      PropertyElement( const props_t& props = props_t() ):
        _props( props )
      {}
  };

  class Node:
    public PropertyElement
  {
    friend class HyperEdge;
    public:

      Node():
        _parent(0)
      {}

      explicit Node( const points_t& points,
                     const props_t& props = props_t() ):
        PropertyElement( props ),
        _points( points ),
        _parent(0)
      {}

      explicit Node( const points_t& points,
                     const points_t& link_points,
                     const props_t& props = props_t() ):
        PropertyElement( props ),
        _points( points ),
        _link_points( link_points ),
        _parent(0)
      {}

      explicit Node( HyperEdge* hedge ):
        _parent(0)
      {
        _children.push_back(hedge);
      }

      points_t& getVertices() { return _points; }
      const points_t& getVertices() const { return _points; }

      points_t& getLinkPoints() { return _link_points.empty() ? _points
                                                              : _link_points; }
      const points_t& getLinkPoints() const { return getLinkPoints(); }

      float2 getCenter()
      {
        const points_t& points = getLinkPoints();
        float2 center;
        for( auto p = points.begin(); p != points.end(); ++p )
          center += *p;

        if( !points.empty() )
          center /= points.size();

        return center;
      }

      HyperEdge* getParent()
      {
        return _parent;
      }
      const HyperEdge* getParent() const
      {
        return _parent;
      }
      const std::vector<HyperEdge*>& getChildren() const
      {
        return _children;
      }
      std::vector<HyperEdge*>& getChildren()
      {
        return _children;
      }
      inline void addChildren(std::vector<HyperEdge*>& edges);


//        virtual float positionDistancePenalty() const = 0;
//        virtual bool hasFixedColor(Color &color) const = 0;
//
//        virtual void setComputedPosition(const float2& pos) = 0;
//        virtual float2 getComputedPosition() const = 0;

    private:

      points_t _points;
      points_t _link_points;
      HyperEdge* _parent;
      std::vector<HyperEdge*> _children;
  };

  class HyperEdge:
    public PropertyElement
  {
    friend class Node;
    public:

      HyperEdge():
        _parent(0),
        _revision( 0 )
      {}

      explicit HyperEdge( const nodes_t& nodes,
                          const props_t& props = props_t() ):
        PropertyElement( props ),
        _parent(0),
        _nodes( nodes ),
        _revision( 0 ),
        _fork( 0 )
      {
        for(auto it = _nodes.begin(); it != _nodes.end(); ++it)
          it->_parent = this;
      }

      nodes_t& getNodes() { return _nodes; }
      const nodes_t& getNodes() const { return _nodes; }

      uint32_t getRevision() const { return _revision; }

      void addNodes(const nodes_t& nodes)
      {
        if( nodes.empty() )
          return;
        //_nodes.reserve( _nodes.size() + nodes.size() );
        for(auto it = nodes.begin(); it != nodes.end(); ++it)
        {
          _nodes.push_back(*it);
          _nodes.back()._parent = this;
        }
        ++_revision;
      }

      void addNode(const LinkDescription::Node& node)
      {
        _nodes.push_back(node);
        _nodes.back()._parent = this;
      }

      const Node* getParent() const
      {
        return _parent;
      }

//      virtual bool hasFixedColor(Color &color) const = 0;
//      virtual const std::vector<Node*>& getConnections() const = 0;
//

      void setHyperEdgeDescription(HyperEdgeDescriptionForkationPtr desc)
      {
        _fork = desc;
      }
      HyperEdgeDescriptionForkationPtr getHyperEdgeDescription()
      {
        return _fork;
      }
      HyperEdgeDescriptionForkationConstPtr getHyperEdgeDescription() const
      {
        return _fork;
      }

      inline void removeRoutingInformation();

    private:

      Node* _parent;
      nodes_t _nodes;
      uint32_t _revision; ///!< Track modifications
      HyperEdgeDescriptionForkationPtr _fork;
  };


  struct HyperEdgeDescriptionSegment
  {
      nodes_t nodes;
      points_t trail;
  };

  struct HyperEdgeDescriptionForkation
  {
    HyperEdgeDescriptionForkation()
    {
    }

    float2 position;

    HyperEdgeDescriptionSegment incoming;
    std::list<HyperEdgeDescriptionSegment> outgoing;
  };

  struct LinkDescription
  {
    LinkDescription( const std::string& id,
                     uint32_t stamp,
                     const HyperEdge& link,
                     uint32_t color_id ):
      _id( id ),
      _stamp( stamp ),
      _link( link ),
      _color_id( color_id )
    {}

    const std::string   _id;
    uint32_t            _stamp;
    HyperEdge           _link;
    uint32_t            _color_id;
  };

  typedef std::list<LinkDescription> LinkList;


  void Node::addChildren(std::vector<HyperEdge*>& edges)
  {
    for(auto it = edges.begin(); it != edges.end(); ++it)
      (*it)->_parent = this;
    _children.insert(_children.end(), edges.begin(), edges.end());
  }

  void HyperEdge::removeRoutingInformation()
  {
    _fork.reset();
  }

} // namespace LinkDescription
} // namespace LinksRouting

#endif //LR_LINKDESCRIPTION
