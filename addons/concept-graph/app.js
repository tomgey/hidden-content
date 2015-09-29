var links_socket = null;
var nodes = [],
    links = [];

// Node Selection (store node ids)
var selected_node_ids = new Set();
var active_node_id = null;

var request_queue = []; // Pending requests (if no node exists yet)
var queue_timeout = null;
var active_links = new Set();
var active_urls = new Set();

function $(id)
{
  return document.getElementById(id);
}

/**
 * Get the document/graph region relative to the application window
 *
 */
function getViewport()
{
  var posX = mozInnerScreenX - screenX,
      posY = mozInnerScreenY - screenY;
  var bb = $('graph-drawing-area').getBoundingClientRect();

  return [
    posX + bb.x,
    posY + bb.y,
    bb.width,
    bb.height
  ];
}

function start(check = true)
{
  if( check )
  {
    console.log("Check if server is alive...");
    httpPing(
      'http://localhost:4486/',
      function() {
        console.log("Server alive => connect");
        setTimeout(start, 0, false);
      },
      function() {
        console.log("Server not alive => wait and retry...");
        setTimeout(start, 3442, true);
      }
    );
    return;
  }
  else
    console.log("Going to connect to server...");

  console.log("Creating new WebSocket.");
  links_socket = new WebSocket('ws://localhost:4487', 'VLP');
  links_socket.binaryType = "arraybuffer";
  links_socket.onopen = function(event)
  {
    console.log("opened -> sending REGISTER" + links_socket + window.links_socket);
    setTimeout(sendMsgRegister, 10);
  };
  links_socket.onclose = function(event)
  {
    console.log("closed" + event);
    setTimeout(start, 2850, true);
  };
  links_socket.onerror = function(event)
  {
    console.log("error" + event);
  };
  links_socket.onmessage = function(event)
  {
    var msg = JSON.parse(event.data);
    if( msg.task == 'CONCEPT-NEW' )
    {
      delete msg.task;
      addNode(msg);
      restart();
    }
    else if( msg.task == 'CONCEPT-UPDATE' )
    {
      delete msg.task;
      updateNode(msg);
    }
    else if( msg.task == 'CONCEPT-DELETE' )
    {
      if( removeNodeById(msg.id) )
        restart();
    }
    else if( msg.task == 'CONCEPT-LINK-NEW' )
    {
      delete msg.task;
      addLink(msg);
      restart();
    }
    else if( msg.task == 'CONCEPT-SELECTION-UPDATE' )
    {
      updateNodeSelection('set', msg.concepts, msg.active, false);
      updateDetailDialogs();
      restart(false);
    }
    else if( msg.task == 'GET' )
    {
      if( msg.id == '/state/all' )
      {
        send({
          'task': 'GET-FOUND',
          'id': '/state/all',
          'data': {
            'type': 'concept-graph',
          }
        });
      }
      else
        console.log("Got unknown data: " + event.data);
    }
    else if( msg.task == 'GET-FOUND' )
    {
      if( msg.id == '/concepts/all' )
      {
        for(var n in msg.nodes)
        {
          var node = msg.nodes[n];
          node.id = n;
          addNode(node);
        }

        for(var i = 0; i < msg.links.length; ++i)
          addLink(msg.links[i]);

        updateNodeSelection('set', msg.selected, msg.active, false);
        updateDetailDialogs();
        restart();
      }
      else
        console.log("Got unknown data: " + event.data);
    }
    else if( msg.task == 'OPENED-URLS-UPDATE' )
    {
      active_urls = new Map();
      for(var url in msg.urls)
        active_urls.set(url, msg.urls[url]);

      updateDetailDialogs();
      updateLinks();

      svg.selectAll('g.ref')
         .classed('ref-open', function(d) { return active_urls.has(d.url); });
    }
    else if( msg.task == 'REQUEST' )
    {
      if( !handleRequest(msg) )
      {
        request_queue.push(msg);
        console.log("Storing link request for later handling: " + event.data);
      }
    }
    else if( msg.task == 'ABORT' )
    {
      active_links.delete(msg.id);
      console.log('ABORT', msg);
    }
    else
      console.log("Unknown message: " + event.data);
  }
}

function send(data)
{
  try
  {
    if( !links_socket )
      throw "No socket available!";

    if( links_socket.readyState != 1 )
    {
      console.log("Socket not ready for message: " + JSON.stringify(data));
      return;
    }

    links_socket.send(JSON.stringify(data));
  }
  catch(e)
  {
    console.log(e);
    links_socket = 0;
//    throw e;
  }
}

function sendMsgRegister()
{
  send({
    'task': 'REGISTER',
    'type': "Graph",
    'pid': localStorage.getItem('pid'),
    'cmds': ['create-concept', 'save-state'],
    "client-id": 'testing',
    'geom': [
      window.screenX, window.screenY,
      window.outerWidth, window.outerHeight
    ],
    'viewport': getViewport()
  });
  send({
    task: 'GET',
    id: '/concepts/all'
  });
}

function handleRequest(msg)
{
  var c = 'link://concept/';
  if( !msg.id.startsWith(c) )
  {
    console.log("Ignoring link request", msg);
    return true;
  }

  var node = getNodeById( msg.id.substring(c.length) );
  if( !node )
    return false; // Try again (eg. if new nodes arrive)

  active_links.add(msg.id);

  var do_circle = localStorage.getItem('link-circle') == 'true';
  var bbs = [];
  node_groups
    .filter(function(d){ return d.id == node.id; })
    .selectAll('.ref image')
    .each(function(d) {
      if( active_urls.has(d.url) )
        bbs.push(do_circle
          ? circlePoints({
              x: node.x + d.x + 8,
              y: node.y + d.y + 8
            })
          : [[ node.x + d.x + 8,
               node.y + d.y + 16
            ]]
        );
    });

  bbs.push({"ref": "viewport"});
  send({
    'task': 'FOUND',
    'title': document.title,
    'id': msg.id,
    'stamp': msg.stamp,
    'regions': bbs
  });

  return true;
}

function circlePoints(pos)
{
  var r = 10;
  var cnt = 10;

  var points = new Array(cnt + 1);
  var step = 2 * Math.PI / cnt;

  for(var i = 0; i < cnt; i += 1)
    points[i] = [ pos.x + r * Math.cos(i * step),
                  pos.y + r * Math.sin(i * step) ];
  points[cnt] = {rel: true};

  return points;
}

/**
 * Insert a new node in the graph.
 */
function addNode(node)
{
  if( getNodeById(node.id) )
  {
    console.warn("Duplicate node: " + node.id);
    return;
  }

  console.log("New concept: " + node.name, node);

  if( node.x == undefined ) node.x = force.size()[0] / 2 + (Math.random() - 0.5) * 100;
  if( node.y == undefined ) node.y = force.size()[1] / 2 + (Math.random() - 0.5) * 100;

  nodes.push(node);

  if( queue_timeout )
    clearTimeout(queue_timeout);
  queue_timeout = setTimeout(checkRequestQueue, 200);
}

/**
 * Update node information
 */
function updateNode(new_node)
{
  var node = getNodeById(new_node.id);
  if( !node )
  {
    console.warn("Unknown node: " + new_node.id);
    return;
  }

  console.log("Update node: " + node.name, new_node);
  for(var prop in new_node)
  {
    console.log('set ' + prop + ' to ' + new_node[prop]);
    node[prop] = new_node[prop];
  }

  updateDetailDialogs();
  restart();

  if( selected_node_ids.has(node.id) )
    sendInitiateForNode(node);
}

function checkRequestQueue()
{
  for(var i = request_queue.length - 1; i >= 0; --i)
  {
    if( handleRequest(request_queue[i]) )
    {
      request_queue.splice(i, 1);
      console.log("done", request_queue);
    }
  }

  queue_timeout = request_queue.size ? setTimeout(checkRequestQueue, 1111)
                                     : null;
}

/**
 * Connect two nodes with a link.
 */
function addLink(link)
{
  var first = getNodeById(link.nodes[0]),
      second = getNodeById(link.nodes[1]);

  if( !first)
  {
    console.warn("Unknown node: " + link.nodes[0]);
    return;
  }
  if( !second )
  {
    console.warn("Unknown node: " + link.nodes[1]);
    return;
  }

  link.source = first;
  link.target = second;
  console.log("add link", link);

  links.push(link);
}

var svg = d3.select('svg');

function nodeSize(node)
{
  return [
    node.icon ? 16 : node.name != undefined ? node.name.length * 4 + 12 : 16,
    16
  ];
}

function nodeIntersect(source, target)
{
  var node_size = nodeSize(source);
  var dx = target.x - source.x,
      dy = target.y - source.y;

  if( Math.abs(dx) < 2 )
  {
    var posX = source.x,
        posY = source.y + Math.sign(dy) * node_size[1];
  }
  else
  {
    // Intersect line with ellipse..
    var a = node_size[0],
        b = node_size[1],
        fac = a * b / Math.sqrt(a * a * dy * dy + b * b * dx * dx);

    var posX = source.x + fac * dx,
        posY = source.y + fac * dy;
  }

  return {
    x: posX,
    y: posY
  };
}

function getNodeById(id)
{
  if( !id || !id.length )
    return null;

  for(var i = 0; i < nodes.length; ++i)
    if( nodes[i].id == id )
      return nodes[i];

  return null;
}

/**
 * Remove node and all connected links
 */
function removeNodeById(id)
{
  if( !id || !id.length )
    return false;

  var node = null;
  for(var i = 0; i < nodes.length; ++i)
  {
    if( nodes[i].id == id )
    {
      node = nodes[i];
      nodes.splice(i, 1);
      break;
    }
  }

  if( !node )
    return false;

  updateNodeSelection('unset', node.id);
  updateDetailDialogs();

  // Remove matching links...
  for(var i = links.length - 1; i >= 0; --i)
  {
    var link = links[i];
    if( link.source.id == node.id || link.target.id == node.id )
    {
      send({
        'task': 'CONCEPT-LINK-UPDATE',
        'cmd': 'delete',
        'nodes': [link.source.id, link.target.id]
      });

      links.splice(i, 1);
    }
  }

  send({
    'task': 'CONCEPT-UPDATE',
    'cmd': 'delete',
    'id': node.id
  });
  return true;
}

// init D3 force layout
var force = d3.layout.force()
    .nodes(nodes)
    .links(links)
    .linkDistance(function(link){
      return Math.max(
        200,
        80 + (nodeSize(link.source)[0] + nodeSize(link.target)[0]) / 2
      );
    })
    .charge(function(node){
      return -10 * Math.min(400, Math.pow(Math.max(nodeSize(node)[0], nodeSize(node)[1]), 2));
    })
    .friction(0.6)
    .on('tick', tick);

// define arrow markers for graph links
svg.append('svg:defs').append('svg:marker')
    .attr('id', 'end-arrow')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 6)
    .attr('markerWidth', 3)
    .attr('markerHeight', 3)
    .attr('orient', 'auto')
  .append('svg:path')
    .attr('d', 'M0,-5L10,0L0,5')
    .attr('fill', '#000');

svg.append('svg:defs').append('svg:marker')
    .attr('id', 'start-arrow')
    .attr('viewBox', '0 -5 10 10')
    .attr('refX', 4)
    .attr('markerWidth', 3)
    .attr('markerHeight', 3)
    .attr('orient', 'auto')
  .append('svg:path')
    .attr('d', 'M10,-5L0,0L10,5')
    .attr('fill', '#000');

// handles to link and node element groups
var link_paths = svg.append('svg:g').selectAll('path'),
    node_groups = svg.append('svg:g').selectAll('g');

// mouse event vars
var selected_link = null,
    mousedown_link = null,
    mousedown_node = null,
    mouseup_node = null;

function resetMouseVars() {
  mousedown_node = null;
  mouseup_node = null;
  mousedown_link = null;
}

// update force layout (called automatically each iteration)
function tick()
{
  [w, h] = force.size();
  node_groups.attr('transform', function(d)
  {
    // Keep nodes within visible region
    [node_w, node_h] = nodeSize(d);
    node_w += 5;
    node_h += 10;

    d.x = Math.max(node_w, Math.min(d.x, w - node_w));
    d.y = Math.max(node_h, Math.min(d.y, h - node_h - 20));

    return 'translate(' + d.x + ',' + d.y + ')';
  });

  // draw directed edges with proper padding from node centers
  link_paths.attr('d', function(d)
  {
    var pos_src = nodeIntersect(d.source, d.target),
        pos_tgt = nodeIntersect(d.target, d.source);

    var dx = pos_tgt.x - pos_src.x,
        dy = pos_tgt.y - pos_src.y,
        dist = Math.sqrt(dx * dx + dy * dy),
        normX = dx / dist,
        normY = dy / dist;

    var sourcePadding = d.left ? 6 : 2,
        targetPadding = d.right ? 6 : 2;

    var sourceX = pos_src.x + (sourcePadding * normX),
        sourceY = pos_src.y + (sourcePadding * normY),
        targetX = pos_tgt.x - (targetPadding * normX),
        targetY = pos_tgt.y - (targetPadding * normY);

    return 'M' + sourceX + ',' + sourceY + 'L' + targetX + ',' + targetY;
  });

  if( force.alpha() < 0.02 || nodes.length == 0 )
    force.stop();

  updateLinksThrottled();
}

// force layout finished updating
function updateLinks()
{
  for(var link_id of active_links)
    handleRequest({'id': link_id, 'stamp': 0});
}
var updateLinksThrottled = throttle(updateLinks, 500);

function resize()
{
  var width  = parseInt(svg.style('width'), 10),
      height = parseInt(svg.style('height'), 10);
  force.size([width, height]).resume();

  send({
    'task': 'RESIZE',
    'viewport': getViewport()
  });
}

// update graph (called when needed)
function restart(update_layout = true)
{
  link_paths = link_paths.data(links);

  // add new links
  link_paths.enter().append('svg:path')
    .attr('class', 'link')
    .on('mousedown', function(d) {
      if(d3.event.ctrlKey) return;

      // select link
      mousedown_link = d;
      if(mousedown_link === selected_link) selected_link = null;
      else selected_link = mousedown_link;
      // TODO clear node selection?

      d3.select('.card-concept-details')
        .style('display', 'none');

      restart(false);
    });

  // remove old links
  link_paths.exit().remove();

  // update links
  link_paths
    .classed('selected', function(d) { return d === selected_link; })
    .style('marker-start', function(d) { return d.left ? 'url(#start-arrow)' : ''; })
    .style('marker-end', function(d) { return d.right ? 'url(#end-arrow)' : ''; });

  // circle (node) group
  // NB: the function arg is crucial here! nodes are known by id, not by index!
  node_groups = node_groups.data(nodes, function(d) { return d.id; });

  var nodes_enter =
    node_groups.enter().append('svg:g')
                       .attr('class', 'node');
  node_groups.exit().remove();

  node_groups
    .call(d3.behavior.drag()
      .on("dragstart", function()
      {

      })
      .on("drag", function()
      {
        for(var id of selected_node_ids)
        {
          var node = getNodeById(id);
          node.px = (node.x += d3.event.dx);
          node.py = (node.y += d3.event.dy);
          node.fixed = true;
        }
        force.resume();
      })
    );

  nodes_enter.append('svg:ellipse')
    // --------------------
    // Node mousedown/click
    .on('mousedown', function(d)
    {
      mousedown_node = d;

      if( selected_node_ids.has(d.id) )
      {
        d3.select(this).classed('new-selection', false);

        // Do not change selection on mousedown, as it could be the start of a
        // drag/move gesture.
        return;
      }

      updateNodeSelection(d3.event.ctrlKey ? 'toggle' : 'set', d.id);
      d3.select(this).classed('new-selection', true);

      restart(false);

//      selected_link = null;
//      link_paths
//        .classed('selected', function(d) { return d === selected_link; });
//
//      // Hide tool area while dragging (eg. new link)
//      $('tool-area').style.display = 'none';
//      restart(false);
    })
    .on('click', function(d)
    {
      if( d3.event.defaultPrevented )
        return;

      if( d3.select(this).classed('new-selection') )
        return;

      updateNodeSelection(d3.event.ctrlKey ? 'toggle' : 'set', d.id);
      restart(false);
    })
    .on('mouseup', function(d)
    {
      if( !mousedown_node )
        return;

      // check for drag-to-self
      mouseup_node = d;
      if(mouseup_node === mousedown_node) { resetMouseVars(); return; }

      // unenlarge target node
      d3.select(this).attr('transform', '');

      // add link to graph (update if exists)
      // NB: links are strictly source < target; arrows separately specified by booleans
      var source, target, direction;
      if(mousedown_node.id < mouseup_node.id) {
        source = mousedown_node;
        target = mouseup_node;
        direction = 'right';
      } else {
        source = mouseup_node;
        target = mousedown_node;
        direction = 'left';
      }

      var link;
      link = links.filter(function(l)
      {
        return (l.source === source && l.target === target)
            || (l.source === target && l.target === source);
      })[0];

      if( !link )
      {
        send({
          'task': 'CONCEPT-LINK-UPDATE',
          'cmd': 'new',
          'nodes': [source.id, target.id]
        });

//        link = {source: source, target: target, left: false, right: false};
//        links.push(link);
      }

      // select new link
//      selected_link = link;
//      selected_node = null;
//      restart();
    });

  nodes_enter
    .append('text')
    .attr('x', 0)
    .attr('y', 4)
    .attr('class', 'id');

  node_groups
    .classed('selected', function(d) { return selected_node_ids.has(d.id); });
  node_groups.select('ellipse')
    .attr('rx', function(d) { return nodeSize(d)[0]; })
    .attr('ry', function(d) { return nodeSize(d)[1]; });
  node_groups.selectAll('text')
    .text(function(d) { return d.name; });

  node_groups.selectAll('g.ref')
    .remove();

  var ref_icons =
    node_groups
      .selectAll('g.ref')
      .data(function(d)
      {
        if( !d.refs )
          return [];

        var refs = [];
        for(var url in d.refs)
          refs[ refs.length ] = {
            url: url,
            data: d.refs[url]
          };

        var num_cols = Math.min(4, refs.length);
        var pad = 4, w = 16, h = 16;
        var x = -num_cols / 2 * w - (num_cols - 1) / 2 * pad,
            y = 24;

        for(var i = 0; i < refs.length; ++i)
        {
          var col = i % num_cols;
          refs[i]['x'] = x + (i % num_cols) * (w + pad);
          refs[i]['y'] = y + Math.floor(i / num_cols) * (h + pad);
        }
        return refs;
      });

  var ref_enter =
    ref_icons.enter()
      .append('g')
      .classed('ref', true)
      .classed('ref-open', function(d) { return active_urls.has(d.url); })
      .on('mousedown', function()
      {
        d3.event.stopPropagation();
      });

  ref_enter
    .append('image')
    .attr('width', 16)
    .attr('height', 16);
    //.attr('clip-path', 'url(#clipCircle)');
  ref_enter
    .append('circle')
    .classed('ref-highlight', true)
    .attr('r', 10);

  ref_enter.selectAll('image')
    .attr('xlink:href', function(d) { return d.data.icon; })
    .attr('title', function(d) { return d.url; })
    .attr('x', function(d) { return d.x; })
    .attr('y', function(d) { return d.y; });
  ref_enter.selectAll('circle')
    .attr('cx', function(d) { return d.x + 8; })
    .attr('cy', function(d) { return d.y + 8; });

  if( update_layout )
  {
    // set the graph in motion
    resize();
    force.start();
  }
}

function mousemove()
{

}

function mouseup()
{
  resetMouseVars();
  $('tool-area').style.display = 'inline';
}

function spliceLinksForNode(node) {
  var toSplice = links.filter(function(l) {
    return (l.source === node || l.target === node);
  });
  toSplice.map(function(l) {
    links.splice(links.indexOf(l), 1);
  });
}

// only respond once per keydown
var lastKeyDown = -1;

function keydown() {
  var tag = d3.event.target.tagName;
  if( tag == 'TEXTAREA' || tag == 'INPUT' )
    return;

  var drawer = d3.select('.mdl-layout__drawer');
  switch( d3.event.keyCode )
  {
    case 27: // Escape
      if( dlgActive )
        dlgActive.hide();
      else
      {
        if( drawer.classed('is-visible') )
          drawer.classed('is-visible', false);
      }
      break;
    case 8:  // Backspace
    case 46: // Delete
      if( dlgActive || drawer.classed('is-visible') )
        return;

      for(var node_id of selected_node_ids)
        removeNodeById(node_id);

      if( selected_link )
        links.splice(links.indexOf(selected_link), 1);
      selected_link = null;

      restart();
      break;
  }
  return;

  d3.event.preventDefault();

  if(lastKeyDown !== -1) return;
  lastKeyDown = d3.event.keyCode;

  if(!selected_node && !selected_link) return;
  switch(d3.event.keyCode) {
    case 8: // backspace
    case 46: // delete
      if(selected_node) {
        nodes.splice(nodes.indexOf(selected_node), 1);
        spliceLinksForNode(selected_node);
      } else if(selected_link) {
        links.splice(links.indexOf(selected_link), 1);
      }
      selected_link = null;
      selected_node = null;
      restart();
      break;
  }
}

function keyup() {
  lastKeyDown = -1;
}

function sendInitiateForNode(n)
{
  send({
    'task': 'INITIATE',
    'id': 'link://concept/' + n.id,
    'refs': n.refs || {}
  });
  active_links.add('link://concept/' + n.id);
}

function abortLink(id)
{
  send({
    'task': 'ABORT',
    'id': id,
    'stamp': 0,
    'scope': 'all'
  });
  active_links.delete(id);
}

function abortAllLinks()
{
  for(var id of active_links)
    abortLink(id);
}

function updateNodeSelection(action, node_id, active_id, send_msg = true)
{
  var previous_selection = new Set(selected_node_ids);

  if( action == 'set' )
  {
    if( typeof node_id == 'object' )
    {
      selected_node_ids = new Set(node_id);
      active_node_id = active_id;
    }
    else
    {
      selected_node_ids.clear();
      if( node_id )
        selected_node_ids.add(node_id);

      active_node_id = node_id;
    }
  }
  else if( action == 'toggle' )
  {
    if( selected_node_ids.has(node_id) )
    {
      selected_node_ids.delete(node_id);
      if( active_node_id == node_id )
        active_node_id = null;
    }
    else
    {
      selected_node_ids.add(node_id);
      active_node_id = node_id;
    }
  }
  else if( action == 'unset' )
  {
    selected_node_ids.delete(node_id);
    if( active_node_id == node_id )
      active_node_id = null;
  }
  else
    console.warn('Unknown action: ' + action);

  if( send_msg )
    send({
      'task': 'CONCEPT-SELECTION-UPDATE',
      'concepts': [...selected_node_ids],
      'active': active_node_id
    });

  // Automatically link selected nodes
  for(var prev_id of previous_selection)
    if( !selected_node_ids.has(prev_id) )
      abortLink('link://concept/' + prev_id);

  for(var cur_id of selected_node_ids)
    if( !previous_selection.has(cur_id) )
      sendInitiateForNode(getNodeById(cur_id));

  updateDetailDialogs();
}

function updateDetailDialogs()
{
  var card = d3.select('.card-concept-details');
  var active_node = getNodeById(active_node_id);

  if( !active_node )
  {
    card.style('display', 'none');
    return;
  }

  card.style('display', '');
  card.select('.mdl-card__title-text').text(active_node.name);
  card.select('.concept-raw-data').text(JSON.stringify(active_node, null, 1));

  var refs = [];
  for(var url in active_node.refs)
  {
    var ref = active_node.refs[url];
    ref['url'] = url;
    refs.push(ref);
  }

  card.select('.concept-references')
      .style('display', refs.length ? 'block' : 'none');

  var ul = card.select('.concept-references > ul');
  ul.selectAll('li').remove();
  var li = ul.selectAll('li')
             .data(refs)
             .enter()
             .append('li');

  li.classed('ref-open', function(d) { return active_urls.has(d.url); });
  li.append('a')
    .attr('class', 'ref-img mdl-badge')
    .attr('data-badge', function(d) { return active_urls.get(d.url); })
    .append('img')
      .attr('src', function(d){ return d.icon; });
  li.append('a')
    .attr('class', 'ref-url')
    .attr('href', function(d){ return d.url; })
    .text(function(d){ return d.title || d.url; });
  li.append('button')
    .attr('class', 'concept-ref-delete mdl-button mdl-button--icon mdl-js-button mdl-js-ripple-effect')
    .on('click', function(d){
      send({
        'task': 'CONCEPT-UPDATE-REFS',
        'cmd': 'delete',
        'url': d.url,
        'id': active_node.id
      });
    })
      .append('i')
      .attr('class', 'material-icons')
      .text('delete');

  card.select('.concept-edit')
      .on('click', function() {
        dlgConceptName.show(function(name){
            send({
              'task': 'CONCEPT-UPDATE',
              'cmd': 'update',
              'id': active_node_id,
              'name': name
            });
          },
          active_node.name
        );
      });
  card.select('.concept-delete')
     .on('click', function() {
       removeNodeById(active_node_id);
       restart();
     });
}

var dlgActive = null;
var overlay = {
  show: function() {
    d3.select('#overlay').style({
      'visibility': 'visible',
      'background-color': 'rgba(0,0,0,0.6)'
    });
  },
  hide: function() {
    d3.select('#overlay').style({
      'visibility': 'hidden',
      'background-color': 'rgba(0,0,0,0)'
    });
  },
  isVisible: function() {
    return d3.select('#overlay').style('visibility') == 'visible';
  }
};

var dlgConceptName = {
  show: function(cb, cur_val = '') {
    dlgConceptName._cb = cb;
    overlay.show();

    var n = d3.select('#dlg-concept-name-input')
              .property('value', cur_val)
              .node();
    n.focus();
    if( n.setSelectionRange )
      n.setSelectionRange(n.value.length, n.value.length);

    d3.select('#dlg-concept-name-field')
      .classed('is-dirty', cur_val && cur_val.length);

    dlgActive = dlgConceptName;
  },
  hide: function() {
    overlay.hide();

    d3.select('#dlg-concept-name-field')
      .classed('is-dirty is-invalid is-focused', false);

    dlgActive = null;
    dlgConceptName._cb = null;
  }
};

d3.select('#dlg-concept-name').on('submit', function() {
  d3.event.preventDefault();
  var name = d3.select('#dlg-concept-name-input')
               .property('value')
               .trim();

  if( !name.length )
  {
    d3.select('#dlg-concept-name-field')
      .classed('is-invalid', true);
    d3.select('#dlg-concept-name-input')
      .node().focus();
  }
  else
  {
    dlgConceptName._cb(name);
    dlgConceptName.hide();
  }
});

d3.select('#button-add-concept').on('click', function() {
  dlgConceptName.show(function(name){
    send({
      'task': 'CONCEPT-UPDATE',
      'cmd': 'new',
      'id': name
    });
  });
});

d3.select('#check-debug-mode').on('click', function(){
  localStorage.setItem('debug-mode', this.checked);
  d3.select('html').classed('debug-mode', this.checked);
});
d3.select('#check-link-circle').on('click', function(){
  localStorage.setItem('link-circle', this.checked);
  updateLinks();
});

// app starts here
svg
  .on('mousedown', function(d) {
    mousedown_node = null;

    if( d3.event.target.tagName != 'svg' )
      return;

    if( !d3.event.ctrlKey )
    {
      updateNodeSelection('set', null);
      restart(false);
    }
  })
  .on('mousemove', mousemove);
d3.select(window)
  .on('keydown', keydown)
  .on('keyup', keyup)
  .on('mouseup', mouseup)
  .on('resize', resize)
  .on('load', function(){
    restart();
    start();

    d3.select('html')
      .classed('debug-mode', localStorage.getItem('debug-mode') == 'true');
  })
  .on('visibilitychange', function() {
    if( document.hidden )
      abortAllLinks();
    else
      for(var cur_id of selected_node_ids)
        sendInitiateForNode(getNodeById(cur_id));
  })
  .on('beforeunload', function(){
    abortAllLinks();
  });