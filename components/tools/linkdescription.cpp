/*
 * linkdescription.cpp
 *
 *  Created on: 29.09.2012
 *      Author: tom
 */

#include <linkdescription.h>
#include <limits>

template<typename>
struct is_container:
  public std::false_type
{};

#define IS_CONTAINER(type)\
  template<>\
  struct is_container<type>:\
    public std::true_type\
  {};

#define IS_CONTAINER_T(type)\
  template<class T>\
  struct is_container<type<T>>:\
    public std::true_type\
  {};

IS_CONTAINER_T(std::vector)

IS_CONTAINER_T(QList)
IS_CONTAINER_T(QSet)
IS_CONTAINER_T(QVector)

IS_CONTAINER(QStringList)

namespace std
{
//  template<class T>
//    std::ostream& operator<<(std::ostream& strm, std::vector<T> const& list)
  template<class T>
  typename std::enable_if<is_container<T>::value, std::ostream&>::type
  operator<<(std::ostream& strm, T const& list)
  {
    strm << "[";

    bool first = true;
    for(auto const& str: list)
    {
      if( first )
        first = false;
      else
        strm << ", ";
      strm << '"' << str << '"';
    }

    return strm << "]";
  }

  std::ostream& operator<<(std::ostream& strm, QString const& str)
  {
    return strm << str.toStdString();
  }
}

namespace LinksRouting
{

  //----------------------------------------------------------------------------
  Direction dirFromNorm(const float2& normal)
  {
    if( normal.x == 0 )
      return normal.y > 0 ? Direction::UP : Direction::DOWN;
    else
      return normal.x > 0 ? Direction::RIGHT : Direction::LEFT;
  }

namespace LinkDescription
{

  //----------------------------------------------------------------------------
//  std::ostream& operator<<(std::ostream& strm, const PropertyMap& m)
//  {
//    m.print(strm);
//    return strm;
//  }

  //----------------------------------------------------------------------------
  void PropertyMap::print( std::ostream& strm,
                           std::string const& indent,
                           std::string const& indent_incr ) const
  {
    strm << indent << "{\n";
    for(auto& prop: _props)
      strm << indent << indent_incr << '"' << prop.first << "\": \"" << prop.second << "\"\n";
    strm << indent << "}\n";
  }

  //----------------------------------------------------------------------------
  Node::Node()
  {

  }

  //----------------------------------------------------------------------------
  Node::Node( const points_t& points,
              const PropertyMap& props ):
    PropertyElement( props ),
    _points( points )
  {

  }

  //----------------------------------------------------------------------------
  Node::Node( const points_t& points,
              const points_t& link_points,
              const PropertyMap& props ):
    PropertyElement( props ),
    _points( points ),
    _link_points( link_points )
  {

  }

  //----------------------------------------------------------------------------
  Node::Node( const points_t& points,
              const points_t& link_points,
              const points_t& link_points_children,
              const PropertyMap& props ):
    PropertyElement( props ),
    _points( points ),
    _link_points( link_points ),
    _link_points_children( link_points_children )
  {

  }

  //----------------------------------------------------------------------------
  Node::Node(HyperEdgePtr hedge)
  {
    _children.push_back(hedge);
    hedge->_parent = this;
  }

  //----------------------------------------------------------------------------
  Node::~Node()
  {
    clearChildren();
  }

  //----------------------------------------------------------------------------
  points_t& Node::getVertices()
  {
    return _points;
  }

  //----------------------------------------------------------------------------
  const points_t& Node::getVertices() const
  {
    return _points;
  }

  //----------------------------------------------------------------------------
  void Node::setLinkPoints(const points_t& points)
  {
    _link_points = points;
  }

  //----------------------------------------------------------------------------
  points_t& Node::getLinkPoints()
  {
    return _link_points.empty() ? _points : _link_points;
  }

  //----------------------------------------------------------------------------
  const points_t& Node::getLinkPoints() const
  {
    return const_cast<Node*>(this)->getLinkPoints();
  }

  //----------------------------------------------------------------------------
  float2 Node::getBestLinkPoint(const float2& from_pos, bool use_vertices) const
  {
    float2 min_vert;
    float min_dist = std::numeric_limits<float>::max();

    for(auto const& p: use_vertices ? getVertices() : getLinkPoints())
    {
      float dist = (p - from_pos).length();
      if( dist < min_dist )
      {
        min_vert = p;
        min_dist = dist;
      }
    }

    return min_vert;
  }

  //----------------------------------------------------------------------------
  points_t& Node::getLinkPointsChildren()
  {
    return _link_points_children.empty() ? getLinkPoints()
                                         : _link_points_children;
  }

  //----------------------------------------------------------------------------
  const points_t& Node::getLinkPointsChildren() const
  {
    return const_cast<Node*>(this)->getLinkPointsChildren();
  }

  //----------------------------------------------------------------------------
  float2 Node::getCenter() const
  {
    const points_t& points = getLinkPoints();
    if( points.empty() )
      return float2();

    return points.front();

    float2 center;
    for( auto p = points.begin(); p != points.end(); ++p )
      center += *p;

    if( !points.empty() )
      center /= points.size();

    return center;
  }

  //----------------------------------------------------------------------------
  Rect Node::getBoundingBox() const
  {
    if( _points.empty() )
      return Rect();

    float2 min = _points.front(),
           max = _points.front();
    for( auto p = _points.begin(); p != _points.end(); ++p )
    {
      if( p->x < min.x )
        min.x = p->x;
      else if( p->x > max.x )
        max.x = p->x;

      if( p->y < min.y )
        min.y = p->y;
      else if( p->y > max.y )
        max.y = p->y;
    }

    return Rect(min, max - min);
  }

  //----------------------------------------------------------------------------
  HyperEdgePtr Node::getParent()
  {
    return _parent.lock();
  }

  //----------------------------------------------------------------------------
  const HyperEdgePtr Node::getParent() const
  {
    return _parent.lock();
  }

  //----------------------------------------------------------------------------
  const hedges_t& Node::getChildren() const
  {
    return _children;
  }

  //----------------------------------------------------------------------------
  hedges_t& Node::getChildren()
  {
    return _children;
  }

  //----------------------------------------------------------------------------
  void Node::addChildren(const hedges_t& edges)
  {
    for(auto it = edges.begin(); it != edges.end(); ++it)
      (*it)->_parent = this;
    _children.insert(_children.end(), edges.begin(), edges.end());
  }

  //----------------------------------------------------------------------------
  void Node::addChild(const HyperEdgePtr& hedge)
  {
    hedge->_parent = this;
    _children.push_back(hedge);
  }

  //----------------------------------------------------------------------------
  void Node::clearChildren()
  {
    for(auto& node: _children)
      node->_parent = 0;
    _children.clear();
  }

  //----------------------------------------------------------------------------
  void Node::print( std::ostream& strm,
                    std::string const& indent,
                    std::string const& indent_incr ) const
  {
    strm << indent << "<Node address=\"" << this << "\">\n"/*
         << indent << indent_incr << "center: " << _center << "\n"
         << indent << indent_incr << "revision: " << _revision << "\n"*/;

    _props.print(strm, indent + indent_incr, indent_incr);

    strm << indent << indent_incr << "points: " << _points << "\n"
         << indent << indent_incr << "link_points: " << _link_points << "\n"
         << indent << indent_incr << "link_points_children: " << _link_points_children << "\n";

    for(auto const& child: _children)
      child->print(strm, indent + indent_incr, indent_incr);

    strm << indent << "</Node>\n";
  }

  //----------------------------------------------------------------------------
  std::string Node::getImpl(const std::string& key) const
  {
    props_t::const_iterator prop = _props.getMap().find(key);
    if( prop != _props.getMap().end() && !prop->second.empty() )
      return prop->second;

    HyperEdgePtr p = _parent.lock();
    if( p )
      return p->get<std::string>(key);

    return std::string();
  }

  //----------------------------------------------------------------------------
  nodes_t& HyperEdge::getNodes()
  {
    return _nodes;
  }

  //----------------------------------------------------------------------------
  const nodes_t& HyperEdge::getNodes() const
  {
    return _nodes;
  }

  //----------------------------------------------------------------------------
  void HyperEdge::setCenter(const float2& center)
  {
    _center = center;
  }

  //----------------------------------------------------------------------------
  const float2& HyperEdge::getCenter() const
  {
    return _center;
  }

  //----------------------------------------------------------------------------
  const float2 HyperEdge::getCenterAbs() const
  {
    return _center + get<float2>("screen-offset");
  }

  //----------------------------------------------------------------------------
  uint32_t HyperEdge::getRevision() const
  {
    return _revision;
  }

  //----------------------------------------------------------------------------
  void HyperEdge::addNodes(const nodes_t& nodes)
  {
    if( nodes.empty() )
      return;
    //_nodes.reserve( _nodes.size() + nodes.size() );
    for(auto it = nodes.begin(); it != nodes.end(); ++it)
    {
      _nodes.push_back(*it);
      _nodes.back()->_parent = _self;
    }
    ++_revision;
  }

  //----------------------------------------------------------------------------
  void HyperEdge::addNode(const NodePtr& node)
  {
    _nodes.push_back(node);
    _nodes.back()->_parent = _self;
  }

  //----------------------------------------------------------------------------
  nodes_t::iterator HyperEdge::removeNode(const nodes_t::iterator& node)
  {
    (*node)->_parent.reset();
    return _nodes.erase(node);
  }

  //----------------------------------------------------------------------------
  void HyperEdge::removeNode(const NodePtr& node)
  {
    _nodes.remove(node);
  }

  //----------------------------------------------------------------------------
  void HyperEdge::resetNodeParents()
  {
    for(auto& node: _nodes)
      node->_parent = _self;
  }

  //----------------------------------------------------------------------------
  Node* HyperEdge::getParent()
  {
    return _parent;
  }

  //----------------------------------------------------------------------------
  const Node* HyperEdge::getParent() const
  {
    return _parent;
  }

  //----------------------------------------------------------------------------
  void HyperEdge::setHyperEdgeDescription(HyperEdgeDescriptionForkationPtr desc)
  {
    _fork = desc;
  }

  //----------------------------------------------------------------------------
  HyperEdgeDescriptionForkationPtr HyperEdge::getHyperEdgeDescription()
  {
    return _fork;
  }

  //----------------------------------------------------------------------------
  HyperEdgeDescriptionForkationConstPtr HyperEdge::getHyperEdgeDescription() const
  {
    return _fork;
  }

  //----------------------------------------------------------------------------
  void HyperEdge::removeRoutingInformation()
  {
    _fork.reset();
  }

  //----------------------------------------------------------------------------
  void HyperEdge::print( std::ostream& strm,
                         std::string const& indent,
                         std::string const& indent_incr ) const
  {
    strm << indent << "<HyperEdge address=\"" << this << "\">\n"
         << indent << indent_incr << "center: " << _center << "\n"
         << indent << indent_incr << "revision: " << _revision << "\n";

    _props.print(strm, indent + indent_incr, indent_incr);
    printNodeList(_nodes, strm, indent + indent_incr, indent_incr);

//    HyperEdgeDescriptionForkationPtr _fork;

    strm << indent << "</HyperEdge>\n";
  }

  //----------------------------------------------------------------------------
  HyperEdge::HyperEdge():
    _parent(0),
    _revision( 0 )
  {

  }

  //----------------------------------------------------------------------------
  HyperEdge::HyperEdge( const nodes_t& nodes,
                        const PropertyMap& props ):
    PropertyElement( props ),
    _parent(0),
    _nodes( nodes ),
    _revision( 0 ),
    _fork( 0 )
  {

  }

  //----------------------------------------------------------------------------
  std::string HyperEdge::getImpl(const std::string& key) const
  {
    props_t::const_iterator prop = _props.getMap().find(key);
    if( prop != _props.getMap().end() && !prop->second.empty() )
      return prop->second;

    if( _parent )
      return _parent->get<std::string>(key);

    return std::string();
  }

  //----------------------------------------------------------------------------
  void LinkDescription::print( std::ostream& strm,
                               std::string const& indent,
                               std::string const& indent_incr ) const
  {
    strm << indent << "<LinkDescription>\n"
         << indent << indent_incr << "id: \"" << _id.toLocal8Bit().data() << "\"\n"
         << indent << indent_incr << "stamp: " << _stamp << "\n"
         << indent << indent_incr << "color: " << _color.name() << " alpha=" << _color.alpha() << "\n"
         << indent << indent_incr << "whitelist: " << _client_whitelist << "\n"
         << indent << indent_incr << "blacklist: " << _client_blacklist << "\n";

    _link->print(strm, indent + indent_incr, indent_incr);

    strm << indent << "</LinkDescription>\n";
  }

  //----------------------------------------------------------------------------
  void printLinkList( LinkList const& links,
                      std::ostream& strm,
                      std::string const& indent,
                      std::string const& indent_incr )
  {
    strm << indent << "<LinkList>\n";
    for(auto const& link: links)
      link.print(strm, indent + indent_incr, indent_incr);
    strm << indent << "</LinkList>\n";
  }

  //----------------------------------------------------------------------------
  void printNodeList( nodes_t const& nodes,
                      std::ostream& strm,
                      std::string const& indent,
                      std::string const& indent_incr )
  {
    strm << indent << "<NodeList>\n";
    for(auto const& node: nodes)
      node->print(strm, indent + indent_incr, indent_incr);
    strm << indent << "</NodeList>\n";
  }

} // namespace LinkDescription
} // namespace LinksRouting
