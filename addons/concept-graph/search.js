var search = {
  filter: function(nodes, links, query)
  {
    // White space separated query parts (with support for quotes)
    var regex = /([^"\s]+)|"((?:\\"|[^"])+)"/g;
    var part = null,
        link_query = null,
        node_query = null;

    while( (part = regex.exec(query)) != null )
    {
      var p = part[1] || part[2];
      if( p.startsWith('link:') )
        link_query = p.slice('link:'.length);
      else
        node_query = p;
    }

    if( node_query )
    {
      var visible_nodes = new Map();

      nodes.forEach(function(concept, id)
      {
        if( concept.name.toLowerCase().includes(node_query) )
          visible_nodes.set(id, concept);
      });
    }
    else
      var visible_nodes = new Map(nodes);

    var filtered_links = [];
    if( link_query )
    {
      var node_canditates = visible_nodes;
      visible_nodes = new Map();

      links.forEach(function(link, id)
      {
        if(    !link.label
            || !link.label.toLowerCase().includes(link_query)
            || (  !node_canditates.has(link.source.id)
               && !node_canditates.has(link.target.id)) )
          return;

        // if at least on node of the link is visible show both nodes
        visible_nodes.set(link.source.id, link.source);
        visible_nodes.set(link.target.id, link.target);
        filtered_links.push(link);
      });
    }
    else
    {
      links.forEach(function(link)
      {
        if(    visible_nodes.has(link.source.id)
            && visible_nodes.has(link.target.id) )
          filtered_links.push(link);
      });
    }

    return [[...visible_nodes.values()], filtered_links];
  }
};