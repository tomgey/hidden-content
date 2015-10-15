var search = {
  filter: function(nodes, links, query)
  {
    var filtered_nodes = [],
        filtered_links = [];

    // White space separated query parts (with support for quotes)
    var query_parts = query.match(/[^"\s]+|"(?:\\"|[^"])+"/g);

    if( query.startsWith('link:') )
    {
      var label_filter = query.substr(5);
      var node_map = new Map();

      filtered_links = links.filter(function(link)
      {
        if(    !link.label
            || !link.label.toLowerCase().includes(label_filter) )
          return false;

        node_map.set(link.source.id, link.source);
        node_map.set(link.target.id, link.target);
        return true;
      });

      filtered_nodes = [...node_map.values()];
    }
    else
    {
      var visible_nodes = new Set();
      filtered_nodes = nodes.filter(function(node)
      {
        if( filter.length && !node.name.toLowerCase().includes(filter) )
          return false;
  
        visible_nodes.add(node.id);
        return true;
      });
      filtered_links = links.filter(function(link)
      {
        return visible_nodes.has(link.nodes[0])
            && visible_nodes.has(link.nodes[1]);
      });
    }

    return [filtered_nodes, filtered_links];
  }
};