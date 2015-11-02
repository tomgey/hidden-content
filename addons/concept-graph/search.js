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

      links.forEach(function(link, id)
      {
        if(    !link.label
            || !link.label.toLowerCase().includes(label_filter) )
          return;

        node_map.set(link.source.id, link.source);
        node_map.set(link.target.id, link.target);
        filtered_links.push(link);
      });

      filtered_nodes = [...node_map.values()];
    }
    else
    {
      var visible_nodes = new Set();
      nodes.forEach(function(concept, id)
      {
        if( filter.length && !concept.name.toLowerCase().includes(filter) )
          return;

        visible_nodes.add(id);
        filtered_nodes.push(concept);
      });
      links.forEach(function(link)
      {
        if(    visible_nodes.has(link.nodes[0])
            && visible_nodes.has(link.nodes[1]) )
          filtered_links.push(link);
      });
    }

    return [filtered_nodes, filtered_links];
  }
};