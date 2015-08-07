var links_socket = null;
var nodes = [],
    links = [];

function $(id)
{
  return document.getElementById(id);
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
    else if( msg.task == 'CONCEPT-LINK-NEW' )
    {
      delete msg.task;
      addLink(msg);
      restart();
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

        restart();
      }
      else
        console.log("Got unknown data: " + event.data);
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
    //'pid': getPid(),
    'cmds': ['create-concept'],
    "client-id": 'testing',
    'geom': [
      window.screenX, window.screenY,
      window.outerWidth, window.outerHeight
    ]
  });
  send({
    task: 'GET',
    id: '/concepts/all'
  });
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

  if( node.x == undefined ) node.x = 100;
  if( node.y == undefined ) node.y = 100;
  if( node.reflexive == undefined ) node.reflexive = false;

  nodes.push(node);

  /*var img_node = {
    id: msg.id + '-icon',
    icon: msg['src-icon'],
    reflexive: false,
    x: 80,
    y: 100
  };
  nodes.push(img_node);

  links.push({
    source: img_node,
    target: node,
    left: false,
    right: true
  });*/
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
  for(var i = 0; i < nodes.length; ++i)
    if( nodes[i].id == id )
      return nodes[i];

  return null;
}

// init D3 force layout
var force = d3.layout.force()
    .nodes(nodes)
    .links(links)
    .linkDistance(function(link){
      return 50;/*Math.max(
        150,
        50 + (nodeSize(link.source)[0] + nodeSize(link.target)[0]) / 2
      );*/
    })
    .charge(-500)
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

// line displayed when dragging new nodes
var drag_line = svg.append('svg:path')
  .attr('class', 'link dragline hidden')
  .attr('d', 'M0,0L0,0');

// handles to link and node element groups
var path = svg.append('svg:g').selectAll('path'),
    circle = svg.append('svg:g').selectAll('g');

// mouse event vars
var selected_node = null,
    selected_link = null,
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
  // draw directed edges with proper padding from node centers
  path.attr('d', function(d)
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

  circle.attr('transform', function(d)
  {
    return 'translate(' + d.x + ',' + d.y + ')';
  });
}

function resize() {
  var width  = parseInt(svg.style('width'), 10),
      height = parseInt(svg.style('height'), 10);
  force.size([width, height]).resume();
}

// update graph (called when needed)
function restart() {
  // path (link) group
  path = path.data(links);

  // update existing links
  path.classed('selected', function(d) { return d === selected_link; })
    .style('marker-start', function(d) { return d.left ? 'url(#start-arrow)' : ''; })
    .style('marker-end', function(d) { return d.right ? 'url(#end-arrow)' : ''; });


  // add new links
  path.enter().append('svg:path')
    .attr('class', 'link')
    .classed('selected', function(d) { return d === selected_link; })
    .style('marker-start', function(d) { return d.left ? 'url(#start-arrow)' : ''; })
    .style('marker-end', function(d) { return d.right ? 'url(#end-arrow)' : ''; })
    .on('mousedown', function(d) {
      if(d3.event.ctrlKey) return;

      // select link
      mousedown_link = d;
      if(mousedown_link === selected_link) selected_link = null;
      else selected_link = mousedown_link;
      selected_node = null;

      d3.select('.card-concept-details')
        .style('display', 'none');

      restart();
    });

  // remove old links
  path.exit().remove();


  // circle (node) group
  // NB: the function arg is crucial here! nodes are known by id, not by index!
  circle = circle.data(nodes, function(d) { return d.id; });

  // update existing nodes (reflexive & selected visual states)
  circle.selectAll('ellipse')
        .classed('selected', function(d) { return d === selected_node; })
        .classed('reflexive', function(d) { return d.reflexive; });

  // add new nodes
  var g = circle.enter().append('svg:g');

  g.filter(function(d){ return !d.icon; })
   .append('svg:ellipse')
    .attr('class', 'node')
    .attr('rx', function(d) { return nodeSize(d)[0]; })
    .attr('ry', function(d) { return nodeSize(d)[1]; })
    .classed('reflexive', function(d) { return d.reflexive; })
    .on('mouseover', function(d) {
      if(!mousedown_node || d === mousedown_node) return;
      // enlarge target node
      d3.select(this).attr('transform', 'scale(1.1)');
    })
    .on('mouseout', function(d) {
      if(!mousedown_node || d === mousedown_node) return;
      // unenlarge target node
      d3.select(this).attr('transform', '');
    })
    .on('mousedown', function(d) {
      if(d3.event.ctrlKey) return;

      var card = d3.select('.card-concept-details');

      // select node
      mousedown_node = d;
      if(mousedown_node === selected_node)
      {
        selected_node = null;
        card.style('display', 'none');
      }
      else
      {
        selected_node = mousedown_node;

        card.style('display', '');
        card.select('.mdl-card__title-text').text(selected_node.name);
        card.select('.mdl-card__supporting-text').text(JSON.stringify(selected_node, null, 1));

        var li = card.select('.mdl-card__supporting-text')
                     .append('ul')
                     .selectAll('li')
                     .data(selected_node.refs || []);
            li_enter = li.enter();

        li_enter.append('img')
                .attr('src', function(d){ return d.icon; });
        li_enter.append('a')
                .attr('href', function(d){ return d.url; })
                .text(function(d){ return d.url; });

        li.exit().remove();
      }
      selected_link = null;

      // reposition drag line
      drag_line
        .style('marker-end', 'url(#end-arrow)')
        .classed('hidden', false)
        .attr('d', 'M' + mousedown_node.x + ',' + mousedown_node.y + 'L' + mousedown_node.x + ',' + mousedown_node.y);

      restart();
    })
    .on('mouseup', function(d) {
      if(!mousedown_node) return;

      // needed by FF
      drag_line
        .classed('hidden', true)
        .style('marker-end', '');

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
          'task': 'CONCEPT-UPDATE-LINK',
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

  var icons = g.filter(function(d){ return d.icon; });
  icons.append('svg:circle')
    .attr('r', 11)
    .style('fill', '#eee')
    .style('stroke', '#333');
  icons.append('svg:image')
    .attr('x', -8)
    .attr('y', -8)
    .attr('width', 16)
    .attr('height', 16)
    .attr('xlink:href', function(d){ return d.icon; });

  // show node IDs
  g.filter(function(d){ return !d.icon; })
   .append('svg:text')
      .attr('x', 0)
      .attr('y', 4)
      .attr('class', 'id')
      .text(function(d) { return d.name; });

  // remove old nodes
  circle.exit().remove();

  // set the graph in motion
  resize();
  force.start();
}

function mousemove() {
  if(!mousedown_node) return;

  // update drag line
  drag_line.attr('d', 'M' + mousedown_node.x + ',' + mousedown_node.y
                    + 'L' + d3.mouse(this)[0] + ',' + d3.mouse(this)[1]);

  restart();
}

function mouseup() {
  if(mousedown_node) {
    // hide drag line
    drag_line
      .classed('hidden', true)
      .style('marker-end', '');
  }

  // because :active only works in WebKit?
  svg.classed('active', false);

  // clear mouse event vars
  resetMouseVars();
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

  if( d3.event.keyCode == 27 ) // Escape
  {
    if( dlgActive )
      dlgActive.hide();
    else
      d3.select('.mdl-layout__drawer')
        .classed('is-visible', false);
  }
  return;

  d3.event.preventDefault();

  if(lastKeyDown !== -1) return;
  lastKeyDown = d3.event.keyCode;

  // ctrl
  if(d3.event.keyCode === 17) {
    circle.call(force.drag);
    svg.classed('ctrl', true);
  }

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
    case 66: // B
      if(selected_link) {
        // set link direction to both left and right
        selected_link.left = true;
        selected_link.right = true;
      }
      restart();
      break;
    case 76: // L
      if(selected_link) {
        // set link direction to left only
        selected_link.left = true;
        selected_link.right = false;
      }
      restart();
      break;
    case 82: // R
      if(selected_node) {
        // toggle node reflexivity
        selected_node.reflexive = !selected_node.reflexive;
      } else if(selected_link) {
        // set link direction to right only
        selected_link.left = false;
        selected_link.right = true;
      }
      restart();
      break;
  }
}

function keyup() {
  lastKeyDown = -1;

  // ctrl
  if(d3.event.keyCode === 17) {
    circle
      .on('mousedown.drag', null)
      .on('touchstart.drag', null);
    svg.classed('ctrl', false);
  }
}

function linkSelectedNode() {
  var n = selected_node;
  var refs = {};
  if( n.refs )
    for(var i = 0; i < n.refs.length; i++)
      refs[ n.refs[i].url ] = n.refs[i].selections;

  send({
    'task': 'INITIATE',
    'id': selected_node.name,
    'refs': refs
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

var dlgNewConcept = {
  show: function() {
    overlay.show();

    d3.select('#create-concept-input-name')
      .property('value', '')
      .node().focus();

    dlgActive = dlgNewConcept;
  },
  hide: function() {
    overlay.hide();

    d3.select('#create-concept-name')
      .classed('is-dirty is-invalid is-focused', false);

    dlgActive = null;
  }
};

d3.select('#dlg-create-concept').on('submit', function() {
  d3.event.preventDefault();
  var name = d3.select('#create-concept-input-name')
               .property('value')
               .trim();

  if( !name.length )
  {
    d3.select('#create-concept-name')
      .classed('is-invalid', true);
    d3.select('#create-concept-input-name')
      .node().focus();
  }
  else
  {
    dlgNewConcept.hide();
    send({
      'task': 'CONCEPT-UPDATE',
      'cmd': 'new',
      'id': name
    });
  }
});

// app starts here
svg
  .on('mousemove', mousemove)
  .on('mouseup', mouseup);
d3.select(window)
  .on('keydown', keydown)
  .on('keyup', keyup)
  .on('resize', resize);
restart();
start();