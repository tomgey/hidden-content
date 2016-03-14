EXPORTED_SYMBOLS = [
  "utils"
];

var utils = {

  /**
   *
   */
  imgToBase64: function(url, fallback_text, cb_done)
  {
    if( url && url.startsWith('data:image/png;base64') )
    {
      if( cb_done )
        cb_done(url);
      return;
    }

    var img = new Image();
    img.src = url;

    var w = 18,
        h = 18;
    var canvas =
      document.createElementNS("http://www.w3.org/1999/xhtml", "html:canvas");
    canvas.width = w;
    canvas.height = h;
    var ctx = canvas.getContext("2d");
    var cb_wrapper = function()
    {
      var data = canvas.toDataURL("image/png");
      if( cb_done )
        cb_done( data /*.replace(/^data:image\/(png|jpg);base64,/, "")*/ );
    };

    img.onload = function ()
    {
      ctx.drawImage(this, 0, 0, w, h);
      cb_wrapper();
    };
    img.onerror = function(e)
    {
      ctx.fillStyle = "#fee";
      ctx.fillRect(0, 0, w, h);
      ctx.fontFamily = "Sans-serif";
      ctx.fontSize = fallback_text.length == 1 ? "14px" : "9px";
      ctx.fontWeight = "bold";
      ctx.fillStyle = "#333";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(fallback_text, w/2, h/2 + 1);

      cb_wrapper();
    }
  },

  /**
   *
   */
  getFavicon: function(cfg, cb)
  {
    var icon_text =
      cfg.url.startsWith('file://')
      ? cfg.url.substr(cfg.url.lastIndexOf('/') + 1)
               .split('.')[0]
               .substr(-3)
      : getBaseDomainFromHost(cfg.host || cfg.title)[0].toUpperCase();

    utils.imgToBase64(cfg.icon, icon_text, cb);
  },

  /**
   *
   */
  addReference: function(el, cfg, cb)
  {
    var url = cfg.url;
    if( typeof(url) !== 'string' || !url.length )
    {
      console.log('Missing/invalid url', cfg);
      return false;
    }

    el.refs = el.refs || {};
    var ref = el.refs[url] = el.refs[url] || {};

    if( cfg.title ) ref.title = cfg.title; else cfg.title = ref.title;
    if( cfg.icon  ) ref.icon  = cfg.icon;

    var ranges = cfg.ranges;
    if( !cfg.ranges || !cfg.ranges.length )
      ranges = [{type: 'document'}];

    ref.selections = ref.selections || [];
    ref.selections.push(ranges);

    utils.getFavicon(cfg, function(img_data)
    {
      ref.icon = img_data;
      if( cb )
        cb(ref, ranges);
    });

    return true;
  },

  /**
   * XPath for given DOM Element
   */
  getXPathForElement: function(el, root)
  {
    var xpath = '';
    while( el && el !== root )
    {
      var sibling = el;
      var pos = 0;

      do
      {
        if(    /*sibling.nodeType === Element.ELEMENT_NODE
            &&*/ sibling.nodeName === el.nodeName )
          pos += 1;

        sibling = sibling.previousSibling;
      } while( sibling );

      if( el.nodeType === Element.TEXT_NODE )
        var nodeName = 'text()';
      else
        var nodeName = el.nodeName;

      xpath = "/" + nodeName + "[" + pos + "]" + xpath;
      el = el.parentNode;
    }
    if( el )
      xpath = "." + xpath;
    return xpath;
  },

  /**
   * Get DOM Element for given XPath
   */
  getElementForXPath: function(xpath, root)
  {
    return root.ownerDocument
               .evaluate( xpath,
                          root,
                          null,
                          XPathResult.FIRST_ORDERED_NODE_TYPE,
                          null )
               .singleNodeValue;
  },

  /**
   * Get currently selected content ranges.
   */
  getContentSelection: function(sel)
  {
    var ranges = [];
    var sel = sel || content.getSelection();
    var body = content.document.body;

    for(var i = 0; i < sel.rangeCount; i++)
    {
      var range = sel.getRangeAt(i);
      if( !range.isCollapsed
          && (  range.startContainer != range.endContainer
             || range.startOffset != range.endOffset
             )
        )
      {
        ranges.push({
          'type': 'dom-range',
          'start-node': utils.getXPathForElement(range.startContainer, body),
          'start-offset': range.startOffset,
          'end-node': utils.getXPathForElement(range.endContainer, body),
          'end-offset': range.endOffset
        });
      }
    }

    return ranges;
  },

  /**
   * Add references to either the selected content parts or the whole document.
   */
  addReferenceToSelection: function(ids, scope, url)
  {
    if( scope == 'all' )
      var ranges = [{type: 'document'}];
    else
      var ranges = utils.getContentSelection();

    var ref = {
      title: content.document.title,
      url: url || getContentUrl(),
      host: content.location.hostname,
      icon: typeof(gBrowser) !== 'undefined' ? gBrowser.getIcon() : null,
      selections: [ranges]
    };

    utils.getFavicon(ref, function(img_data)
      {
        ref.icon = img_data;

        for(var i = 0; i < ids.length; i++)
        {
          var msg = {
            'cmd': 'add',
            'ref': ref
          };
          if(typeof ids[i] == "string")
          {
            msg['task'] = 'CONCEPT-UPDATE-REFS';
            msg['id'] = ids[i];
          }
          else
          {
            msg['task'] = 'CONCEPT-LINK-UPDATE-REFS';
            msg['nodes'] = ids[i];
          }

          send(msg);
        }
      });
  },

  /**
   * Add a new concept (ask for name and set creation reference).
   */
  addConcept: function(scope)
  {
    var sel = content.getSelection();
    if( /*scope == 'selection' &&*/ !sel.isCollapsed )
      var name = sel.toString();
    else
      var name = content.document.title;

    name = name.replace(/\s{2,}/g, ' ')
               .replace(/[^a-zA-Z0-9\s]/g, '')
               .trim();

    name = window.prompt("Enter name for new concept:", name);
    if(typeof name != "string")
      return;

    name = name.trim();
    if( name.length < 1 )
      return;

    send({
      'task': 'CONCEPT-NEW',
      'id': name
    });

    utils.addReferenceToSelection([name.toLowerCase()], scope || 'selection');
  },

  /**
   * Create a new relation (optionally with a creation reference)
   */
  addRelation: function(nodes, scope)
  {
    send({
      'task': 'CONCEPT-LINK-NEW',
      'nodes': [...nodes]
    });

    utils.addReferenceToSelection([nodes], scope);
  }
};