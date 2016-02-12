var EXPORTED_SYMBOLS = [
  "browser_server"
];

Components.utils.import("resource://gre/modules/devtools/Console.jsm");
Components.utils.import("resource://gre/modules/Timer.jsm");
Components.utils.import("chrome://hidden-content/content/ConceptGraph.js");

function BrowserServer()
{
  console.log('new BrowserServer');
  this.concept_graph = new ConceptGraph();
  this.active = false;

//  var storage_url = "http://concept-graph-storage.caleydo.com:123";
//  var ios = Cc["@mozilla.org/network/io-service;1"]
//            .getService(Ci.nsIIOService);
//  var ssm = Cc["@mozilla.org/scriptsecuritymanager;1"]
//            .getService(Ci.nsIScriptSecurityManager);
//  var dsm = Cc["@mozilla.org/dom/storagemanager;1"]
//            .getService(Ci.nsIDOMStorageManager);
//
//  var principal = ssm.getCodebasePrincipal(ios.newURI(storage_url, "", null));
//  this.storage = dsm.getLocalStorageForPrincipal(principal, "");
}

BrowserServer.prototype.onStorageChange = function(event)
{
  if( event.key !== "concept-graph.server-message" )
    return console.log('unknown storage event', event.key, event.newValue);

  var msg = JSON.parse(event.newValue);
  if( msg.from_server )
  {
    console.log('ignore message from server');
    return;
  }
  if( ["ABORT", "RESIZE", "SYNC"].indexOf(msg.task) >= 0 )
    return;

  this.active = true;
  if( msg.task == 'GET' && msg.id == '/concepts/all' )
  {
    this.distributeMessage(this.getConceptGraphState());
  }
  else if( !this.concept_graph.handleMessage(msg) )
    console.log("Failed to handle message (storage)", msg);
  else
    this.distributeMessage(JSON.parse(event.newValue));
}

BrowserServer.prototype.distributeMessage = function(msg)
{
  msg.from_server = true;
  var msg_json = JSON.stringify(msg);
  var self = this;

  setTimeout(function() {
    self._distributeMessageImpl.call(self, msg_json);
  }, 0);
}

BrowserServer.prototype._distributeMessageImpl = function(msg_json)
{
  console.log('distribute', msg_json);

  var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                     .getService(Components.interfaces.nsIWindowMediator);
  var browserEnumerator = wm.getEnumerator("navigator:browser");

  while( browserEnumerator.hasMoreElements() )
  {
    var browserWin = browserEnumerator.getNext();
    var tabbrowser = browserWin.gBrowser;

    for( var index = 0; index < tabbrowser.browsers.length; index++ )
    {
      try
      {
        var browser = tabbrowser.getBrowserAtIndex(index);
        console.log('send to', browser.currentURI.spec);

        var dsm = Components.classes["@mozilla.org/dom/storagemanager;1"]
                  .getService(Components.interfaces.nsIDOMStorageManager);

        dsm.getLocalStorageForPrincipal(browser.contentPrincipal, "")
           .setItem("concept-graph.server-message", msg_json);
      }
      catch(e)
      {
        console.log('failed to set local storage', e);
      }
    }
  }
}

BrowserServer.prototype.getConceptGraphState = function()
{
  var simplify = function(obj)
  {
    return JSON.parse(JSON.stringify(obj));
  };

  return {
    'task': 'GET-FOUND',
    'id': '/concepts/all',
    'concepts': simplify(mapToObj(this.concept_graph.concepts)),
    'relations': simplify(mapToObj(this.concept_graph.relations)),
    'selection': [...this.concept_graph.selection],
    //'layout': _concept_layout
  };
}

var browser_server = new BrowserServer();

function mapToObj(map)
{
  var obj = {};
  for(var [k,v] of map)
    obj[k] = v;
  return obj;
}