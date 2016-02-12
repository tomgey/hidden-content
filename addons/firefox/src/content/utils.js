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
      cb_done(url);
      return;
    }

    var img = new Image();
    img.src = url;

    var w = 16,
        h = 16;
    var canvas =
      document.createElementNS("http://www.w3.org/1999/xhtml", "html:canvas");
    canvas.width = w;
    canvas.height = h;
    var ctx = canvas.getContext("2d");
    var cb_wrapper = function()
    {
      var data = canvas.toDataURL("image/png");
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
      ctx.font = "14px Sans-serif";
      ctx.fontWeight = "bold";
      ctx.fillStyle = "#333";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(fallback_text, w/2, h/2);

      cb_wrapper();
    }
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
      if( !range.isCollapsed )
        ranges.push({
          'type': 'dom-range',
          'start-node': utils.getXPathForElement(range.startContainer, body),
          'start-offset': range.startOffset,
          'end-node': utils.getXPathForElement(range.endContainer, body),
          'end-offset': range.endOffset
        });
    }

    return ranges;
  },
  
  /**
   * Add references to either the selected content parts or the whole document.
   */
  addReference: function(ids, scope, url)
  {
    if( scope == 'all' )
      var ranges = [{type: 'document'}];
    else
      var ranges = utils.getContentSelection();

    var base_domain = getBaseDomainFromHost(content.location.hostname);
    var url = url || getContentUrl();
    var title = content.document.title;
    var icon = typeof(gBrowser) !== 'undefined' ? gBrowser.getIcon() : null;

    utils.imgToBase64(
      icon,
      base_domain[0].toUpperCase(),
      function(img_data)
      {
       var ref = {
          'title': title,
          'url': url,
          'icon': img_data,
          'selections': [ranges]
        };

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
      }
    );
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

    utils.addReference([name.toLowerCase()], scope || 'selection');
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

    utils.addReference([nodes], scope);
  }
};