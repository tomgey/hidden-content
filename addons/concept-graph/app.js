var links_socket = null;
var filtered_nodes = [],
    filtered_links = [];
var filter = '';
var try_connect = false;

var concept_graph =
  new ConceptGraph()
  .on('concept-new', function(id, node)
  {
    console.log("New concept: " + id, node);

    if( node.x == undefined )
      node.x = force.size()[0] / 2 + (Math.random() - 0.5) * 100;
    if( node.y == undefined )
      node.y = force.size()[1] / 2 + (Math.random() - 0.5) * 100;

    if( queue_timeout )
      clearTimeout(queue_timeout);
    queue_timeout = setTimeout(checkRequestQueue, 200);

    restart();
    updateDetailDialogs();
  })
  .on('concept-update', function(id, concept)
  {
    restart();

    if( SidePanel._active_concept && SidePanel._active_concept.id == id )
      SidePanel._active_concept = null;

    updateDetailDialogs();

    if( localBool('auto-link') && concept_graph.selection.has(concept.id) )
      sendInitiateForNode(concept);
  })
  .on('selection-add', function(id, type)
  {
    if( localBool('auto-link') && type == 'concept' )
      sendInitiateForNode( concept_graph.getConceptById(id) );
  })
  .on('selection-remove', function(id, type)
  {
    if( type == 'concept' )
      abortLink('link://concept/' + id);
  })
  .on([ 'concept-delete',
        'relation-new',
        'relation-update',
        'relation-delete',
        'selection-change' ], function(id, rel, type)
  {
    restart(type != 'selection-change');
    if( type == 'relation-update' )
    {
      if( SidePanel._active_relation && SidePanel._active_relation.id == id )
        SidePanel._active_relation = null;
    }

    updateDetailDialogs();
  });

var request_queue = []; // Pending requests (if no node exists yet)
var queue_timeout = null;
var active_links = new Set();
var active_urls = new Set();

var drag_start_pos = null,
    drag_start_selection = null;

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

function startWithCountdownMessage(timeout)
{
  if( !try_connect )
    return;

  if( timeout >= 1000 )
  {
    NotificationMessage.show(
      "<b>Not connected</b> Retry in " + Math.round(timeout / 1000) + "s...",
      false
    );

    setTimeout(startWithCountdownMessage, 1000, timeout - 1000);
  }
  else
  {
    NotificationMessage.show("Check if server is alive...", false);
    setTimeout(start, timeout, true);
  }
}

function start(check = true)
{
  if( check )
  {
    if( !try_connect )
    {
      NotificationMessage.show('<b>Standalone Mode</b> (Not connected)');
      return;
    }

    NotificationMessage.show("Check if server is alive...", false);
    httpPing(
      localStorage.getItem('server.ping-address'),
      function() {
        NotificationMessage.show("Connecting...");
        setTimeout(start, 0, false);
      },
      function() {
        startWithCountdownMessage(5442);
      }
    );
    return;
  }
  else
    NotificationMessage.show("Connecting...");

  console.log("Creating new WebSocket.");
  links_socket = new WebSocket(localStorage.getItem('server.websocket-address'), 'VLP');
  links_socket.binaryType = "arraybuffer";
  links_socket.onopen = function(event)
  {
    console.log("opened -> sending REGISTER" + links_socket + window.links_socket);
    NotificationMessage.hide();
    setTimeout(sendMsgRegister, 10);
  };
  links_socket.onclose = function(event)
  {
    NotificationMessage.show("<b>Connection closed</b>");
    setTimeout(start, 2850, true);
  };
  links_socket.onerror = function(event)
  {
    NotificationMessage.show("<b>Connection error!</b>");
    console.log("error" + event);
  };
  links_socket.onmessage = function(event)
  {
    var msg = JSON.parse(event.data);
    if( concept_graph.handleMessage(msg) )
      return;
    else if( msg.task == 'GET' )
    {
      if( msg.id == '/state/all' )
      {
        var layout = {};
        for(var [id, concept] of concept_graph.concepts)
          layout[id] = {
            x: concept.x,
            y: concept.y,
            fixed: concept.fixed || false
          };

        send({
          'task': 'GET-FOUND',
          'id': '/state/all',
          'data': {
            'type': 'concept-graph',
            'concept-layout': layout
          }
        });
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

function stop()
{
  try_connect = false;

  if( links_socket )
  {
    links_socket.close();
    links_socket = null;
  }
}

function send(data)
{
  if( !try_connect )
    return;

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

function localBool(key)
{
  return localStorage.getItem(key) == 'true';
}

function updateConcept(concept, key, val)
{
  var old_val = concept[key];
  if(     (old_val && old_val == val)
      || (!old_val && !val) )
    return false;

  var new_cfg = {
    id: concept.id
  };
  new_cfg[key] = val;

  concept_graph.updateConcept(new_cfg);
  return true;
}

function updateRelation(relation, key, val)
{
  var old_val = relation[key];
  if(     (old_val && old_val == val)
      || (!old_val && !val) )
    return false;

  var msg = {
    'task': 'CONCEPT-LINK-UPDATE',
    'cmd': 'update',
    'id': relation.id,
  };
  msg[key] = val;

  send(msg);
  return true;
}

function handleRequest(msg)
{
  var c = 'link://concept/';
  if( msg.id.startsWith(c) )
  {
    var node = concept_graph.getConceptById( msg.id.substring(c.length) );
    if( !node )
      return false; // Try again (eg. if new nodes arrive)
  }
  else
  {
    var node = concept_graph.getConceptById(msg.id);
    if( !node )
      return true; // Just ignore keyword links not matching any concept
  }

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

function getElementById(list, id)
{
  if( !id || !id.length )
    return null;

  for(var i = 0; i < list.length; ++i)
    if( list[i].id == id )
      return list[i];

  return null;
}

// init D3 force layout
var force = d3.layout.force()
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

var links_group = svg.append('svg:g'),
    nodes_group = svg.append('svg:g');

// handles to link and node element groups
var link_groups = null,
    node_groups = null;

var drag_rect =
  svg.append('svg:rect')
       .attr('class', 'drag-rect');

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

  link_groups.each(function(d)
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

    d.p1 = [sourceX, sourceY];
    d.p2 = [targetX, targetY];
    d.center = [ 0.5 * (sourceX + targetX),
                 0.5 * (sourceY + targetY) ];

    var self = d3.select(this);

    self.selectAll('path')
      .attr( 'd', 'M' + sourceX + ',' + sourceY
                + 'L' + targetX + ',' + targetY );

    self.select('rect')
      .attr('x', d.center[0] - 0.5 * d.label_width - 3)
      .attr('y', d.center[1] - 10);

    self.select('text')
      .attr('x', d.center[0] - 0.5 * d.label_width)
      .attr('y', d.center[1] + 4);
  });

  if( force.alpha() < 0.031 || !force.nodes().length )
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
  if( update_layout )
  {
    [filtered_nodes, filtered_links] = search.filter( concept_graph.concepts,
                                                      concept_graph.relations,
                                                      filter );

    force.nodes(filtered_nodes)
         .links(filtered_links);
  }

  // ----------------------
  // Create links

  link_groups = links_group.selectAll('g.link')
                           .data(filtered_links);

  var links_enter =
    link_groups.enter().append('svg:g')
                       .attr('class', 'link');
  link_groups.exit().remove();

  link_groups
    .classed('selected', function(d) { return concept_graph.selection.has(d.id); });

  links_enter
    .call(applyMouseSelectionHandler);

  links_enter.append('svg:path')
    .attr('class', 'click-proxy');

  links_enter.append('svg:path');
  links_enter.append('svg:rect')
             .attr('height', 20);
  links_enter.append('svg:text');

  link_groups.select('text')
    .text(function(d) { return d['label'] || ''; });
  link_groups.each(function(d){
    var g = d3.select(this);
    d.label_width = g.select('text').node().getComputedTextLength();
    if( d.label_width > 0 )
      d.label_width += 6;

    g.select('rect').attr('width', d.label_width);
  });

  // ----------------------
  // Create nodes

  node_groups = nodes_group.selectAll('g.node')
                           .data(filtered_nodes, function(d) { return d.id; });

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
        for(var id of concept_graph.selection)
        {
          var node = concept_graph.getConceptById(id);
          if( node )
          {
            node.px = (node.x += d3.event.dx);
            node.py = (node.y += d3.event.dy);
            node.fixed = true;
          }
        }
        force.resume();
      })
    );

  nodes_enter.append('svg:ellipse')
    .call(applyMouseSelectionHandler)
    .call(externalFileDrop, {
      enter: function(d) { d3.select(this).attr('transform', 'scale(1.1)'); },
      leave: function(d) { d3.select(this).attr('transform', null); },
      drop: function(d, file)
      {
        updateConcept(d, 'img', file.img);
      }
    });

  nodes_enter
    .append('text')
    .attr('x', 0)
    .attr('y', 4)
    .attr('class', 'id');

  node_groups
    .classed('selected', function(d) { return concept_graph.selection.has(d.id); });
  node_groups.select('ellipse')
    .attr('rx', function(d) { return nodeSize(d)[0]; })
    .attr('ry', function(d) { return nodeSize(d)[1]; })
    .style('fill', function(d) { return d.getColor(); });
  node_groups.selectAll('text')
    .text(function(d) { return d.name; })
    .style('fill', function(d) { return contrastColor(d.getColor()); });

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

  var open_url_options =
    'toolbar=1,location=1,menubar=1,scrollbars=1,resizable=1';

  ref_enter
    .append('image')
    .attr('width', 16)
    .attr('height', 16)
    .on('click', function(d)
     {
       if( active_urls.has(d.url) )
         send({
           task: 'WM',
           cmd: 'activate-window',
           url: d.url
         });
       else
         window.open(
           d.url,
           '_blank',
           open_url_options + ',width=1000,height=800'
         );
     });
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

/**
 * Keydown handler
 */
function keydown()
{
  var e = d3.event,
      tag = e.target.tagName;
  if( tag == 'TEXTAREA' || tag == 'INPUT' )
    return;

  var drawer = d3.select('.mdl-layout__drawer');
  if( dlgActive || drawer.classed('is-visible') )
  {
    if( e.code == 'Escape' )
    {
      if( dlgActive )
        dlgActive.hide();
      else
        drawer.classed('is-visible', false);
    }

    return;
  }

  var cur_combo = '';
  if( e.shiftKey && e.key != 'Shift' )
    cur_combo += 'Shift+';
  if( e.ctrlKey && e.key != 'Control' )
    cur_combo += 'Control+';
  if( e.altKey && e.key != 'Alt' )
    cur_combo += 'Alt+';

  var key = e.key in [' '] ? key.code : e.key;
  if( key.length == 1 )
    key = key.toUpperCase();
  cur_combo += key;

  for(var a of user_actions)
  {
    if(   a.shortcuts.indexOf(cur_combo) < 0
       || (typeof(a.isEnabled) == 'function' && !a.isEnabled()) )
      continue;

    a.action();
    e.preventDefault();
  }
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

function applyMouseSelectionHandler(sel)
{
  sel
    .on('mousedown', function(d)
    {
      if( concept_graph.selection.has(d.id) )
      {
        d3.select(this).classed('new-selection', false);

        // Do not change selection on mousedown, as it could be the start of a
        // drag/move gesture.
        return;
      }

      concept_graph.updateSelection(d3.event.ctrlKey ? 'toggle' : 'set', d.id);
      d3.select(this).classed('new-selection', true);
    })
    .on('click', function(d)
    {
      if( d3.event.defaultPrevented )
        return;

      if( d3.select(this).classed('new-selection') )
        return;

      concept_graph.updateSelection(d3.event.ctrlKey ? 'toggle' : 'set', d.id);
    });
}

function updateDetailDialogs()
{
  SidePanel.updateActions(user_actions);

  var show_concept = concept_graph.getSelectedConcept(),
      show_relation = concept_graph.getSelectedRelation();

  SidePanel.showSelectionCard(!show_concept && !show_relation);
  SidePanel.showConceptDetailsCard(show_concept);
  SidePanel.showRelationDetailsCard(show_relation);
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

    d3.select('#dlg-concept-name-input')
      .node()
      .blur();
  }
};

var NotificationMessage = {
  show: function(msg, do_log = true) {
    d3.select('.notification-message')
      .style('visibility', 'visible')
      .html(msg);

    if( do_log )
      console.log('Notification:', msg);
  },
  hide: function() {
    d3.select('.notification-message')
      .style('visibility', 'hidden');
  }
}

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

function addConceptWithDialog()
{
  dlgConceptName.show(function(name){
    send({
      'task': 'CONCEPT-UPDATE',
      'cmd': 'new',
      'id': name
    });
  });
}
d3.select('#button-add-concept').on('click', addConceptWithDialog);

// app starts here
svg
  .on('mousedown', function(d)
  {
    if( d3.event.target.tagName != 'svg' )
      return;

    if( !d3.event.ctrlKey )
      concept_graph.updateSelection('set', null);
    drag_start_selection = new Set(concept_graph.selection);

    drag_start_pos = d3.mouse(this);
    drag_rect.attr('x', drag_start_pos[0])
             .attr('y', drag_start_pos[1])
             .attr('width', 0)
             .attr('height', 0);
  });

d3.select(window)
  .on('keydown', keydown)
  .on('resize', resize)

  // ----------------------
  // Global mouse move/drag
  .on('mousemove', function()
  {
    if( !drag_start_pos )
      return;

    var cur_pos = d3.mouse(svg.node()),
        x1 = Math.min(drag_start_pos[0], cur_pos[0]),
        y1 = Math.min(drag_start_pos[1], cur_pos[1]),
        x2 = Math.max(drag_start_pos[0], cur_pos[0]),
        y2 = Math.max(drag_start_pos[1], cur_pos[1]);

    drag_rect.attr('x', x1)
             .attr('y', y1)
             .attr('width', x2 - x1)
             .attr('height', y2 - y1);

    var selection = new Set(drag_start_selection);

    for(var node of filtered_nodes)
    {
      if(    node.x < x1 || node.x > x2
          || node.y < y1 || node.y > y2 )
        continue;

      // Toggle selection of element
      if( !selection.delete(node.id) )
        selection.add(node.id);
    }

    for(var link of filtered_links)
    {
      if(    link.center[0] < x1 || link.center[0] > x2
          || link.center[1] < y1 || link.center[1] > y2 )
        continue;

      // Toggle..
      if( !selection.delete(link.id) )
        selection.add(link.id);
    }

    concept_graph.updateSelection('set', selection);
  })

  // ----------------------
  // Drag end detection
  .on('mouseup', function()
  {
    if( !drag_start_pos )
      return;

    drag_start_pos = null;
    drag_rect.attr('width', 0);
  })

  // -------------------
  // Page load hook
  .on('load', function()
  {
    d3.select('#drawer-settings')
      .selectAll('div')
      .data(settings_menu_entries)
      .enter()
      .append('div')
      .each(function(d)
      {
        var css_id = d.id.replace('.', '_');
        var self = d3.select(this);
        self.attr('id', 'setting-' + css_id);

        var val = localStorage.getItem(d.id) || d.def;

        if( d.type == 'string' )
        {
          self.append('label')
            .attr('for', 'setting-string-' + css_id)
            .text(d.label);
          self.append('input')
            .attr('type', 'text')
            .attr('id', 'setting-string-' + css_id)
            .attr('value', val)
            .on('blur', function(d)
            {
              localStorage.setItem(d.id, this.value);
              d.change(this.value);
            });
        }
        else
        {
          val = val.toString() == 'true';
          var label =
            self.append('label')
              .attr('class', 'mdl-checkbox mdl-js-checkbox mdl-js-ripple-effect')
              .attr('for', 'setting-check-' + css_id);

          label.append('input')
            .attr('type', 'checkbox')
            .attr('id', 'setting-check-' + css_id)
            .attr('class', 'mdl-checkbox__input')
            .property('checked', val ? true : null)
            .on('click', function(d)
            {
              localStorage.setItem(d.id, this.checked.toString());
              d.change(this.checked);
            });
          label.append('span')
            .attr('class', 'mdl-checkbox__label')
            .text(d.label);
        }

        localStorage.setItem(d.id, val.toString());
      });
    componentHandler.upgradeDom(); // TODO just update single elements?

    // Update initial state of all settings (do it in a separate loop to ensure
    // all UI elements are already available and can eg. be hidden or colored)
    d3.select('#drawer-settings')
      .selectAll('div')
      .each(function(d)
      {
        var val = localStorage.getItem(d.id);
        if( d.type != 'string' )
          val = val == 'true';

        d.change(val);
      });

    d3.select('#input-filter').on('input', function()
    {
      filter = this.value.toLowerCase();
      restart();
      updateDetailDialogs();
    });

    restart();
    start();
  })
  .on('visibilitychange', function() {
    if( document.hidden )
      abortAllLinks();
    else if( localBool('auto-link') )
      for(var concept of concept_graph.getSelectedConcepts())
        sendInitiateForNode(concept);
  })
  .on('beforeunload', function(){
    abortAllLinks();
  });

//------------------------------
// Settings (in the side drawer)

var settings_menu_entries = [
  { id: 'link-circle',
   label: 'Link with Circle',
   change: function()
   {
     updateLinks();
   }
  },
  { id: 'auto-link',
   label: 'Auto-link selected Concepts',
   change: function() {}
  },
  { id: 'debug-mode',
    label: 'Enable Debug Tools',
    change: function(val)
    {
      d3.select('html').classed('debug-mode', val);
    }
  },
  { id: 'expert-mode',
    label: 'Enable Expert Modus',
    change: function(val)
    {
      updateDetailDialogs();
    }
  },
  { id: 'server.enabled',
    label: 'Use Server',
    def: true,
    change: function(val)
    {
      d3.selectAll( '#setting-server_ping-address,'
                  + '#setting-server_websocket-address' )
        .style('display', val ? 'inline' : 'none');

      try_connect = val;
      if( val )
        start();
      else
        stop();
    }
  },
  { id: 'server.ping-address',
    label: 'Ping Address',
    type: 'string',
    def: 'http://localhost:4486/',
    change: function() {}
  },
  { id: 'server.websocket-address',
    label: 'Websocket Address',
    type: 'string',
    def: 'ws://localhost:4487',
    change: function() {}
  }
];

//------------------------------------
// Action menu entries (and shortcuts)

var user_actions = [
  { label: 'Add Concept',
    icon: 'add',
    shortcuts: ['Shift+A'],
    action: addConceptWithDialog
  },
  { label: 'Select All Concepts and Relations',
    icon: 'select_all',
    shortcuts: ['Control+A'],
    isEnabled: function() { return filtered_nodes.length > 0; },
    action: function()
    {
      concept_graph.updateSelection('set', concept_graph.getIds());
    }
  },
  { label: 'Delete Selected Concept(s)/Relation(s)',
    icon: 'delete',
    shortcuts: ['Delete', 'Backspace'],
    //isMenuVisible: function() { return this.isEnabled(); },
    isEnabled: function() { return concept_graph.selection.size > 0; },
    action: function()
    {
      // TODO check if visible (not filtered)
      for(var id of concept_graph.selection)
        concept_graph.remove(id);

      restart();
    }
  },
  { label: 'Relate Selected Concepts',
    shortcuts: ['E'],
    //isMenuVisible: function() { return this.isEnabled(); },
    isEnabled: function()
    {
      // TODO check if visible (not filtered)
      var concepts = concept_graph.getSelectedConceptIds();
      return  concepts.size == 2
           && !concept_graph.getRelationForConcepts(...concepts);
    },
    action: function()
    {
      concept_graph.addRelation({
        nodes: [...concept_graph.getSelectedConceptIds()]
      });
    }
  },
  { label: 'Remove \'fixed\' attribute',
    shortcuts: ['F'],
    isEnabled: function()
    {
      // TODO check if visible (not filtered) and at least one concept
      return concept_graph.selection.size > 0
          && localStorage.getItem('expert-mode') == 'true';
    },
    action: function()
    {
      for(var id of concept_graph.selection)
      {
        var node = concept_graph.getConceptById(id);
        if( node )
          node.fixed = false;
      }
      force.resume();
    }
  }
];
