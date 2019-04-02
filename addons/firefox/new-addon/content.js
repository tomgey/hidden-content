const KEY_CTRL = 17
const MAX_TIME_PRESSED = 300

let background_port = null

function isActive() {
    return document.visibilityState == "visible";
}

function send(msg) {
    return background_port.postMessage(msg);
}

function sendResize() {
    send({
        'task': "RESIZE",
        'viewport': getViewport(),
        'scroll-region': getScrollRegion(),
        'geom': getWindowGeometry()
    });
}

function sendRegions(task, id) {
    let msg = {
        'task': task,
        'title': document.title,
        'id': id,
        'stamp': 0,
        'own': true
//        'viewport': getViewport(),
//        'scroll-region': getScrollRegion(),
//        'geom': getWindowGeometry()
    };

    let bbs = searchDocument(id);
    if( bbs.length > 0 ) {
        msg['regions'] = bbs;
    }

    send(msg);
}

function getViewport(ref = "rel")
{
    var vp = [
        mozInnerScreenX - screenX,
        mozInnerScreenY - screenY,
        innerWidth,
        innerHeight
    ];

    if( ref == "abs" )
    {
        vp[0] += screenX;
        vp[1] += screenY;
    }

    return vp;
}

function getWindowGeometry()
{
    var title_and_tool_bar_height = mozInnerScreenY - screenY,
        tool_bar_height = outerHeight - innerHeight,
        title_bar_height = title_and_tool_bar_height - tool_bar_height + 38;

    return [
        screenX,
        screenY,
        outerWidth,
        outerHeight + title_bar_height
    ];
}

function getScrollRegion()
{
    var doc = content.document;
    return [
        -document.documentElement.scrollLeft, // coordinates relative to
        -document.documentElement.scrollTop,  // top left corner of viewport
        document.documentElement.scrollWidth,
        document.documentElement.scrollHeight
    ];
}

var last_ctrl_down = 0;
window.addEventListener("keydown", e => {
    last_ctrl_down = (e.keyCode == KEY_CTRL) ? e.timeStamp : 0;
}, false);

window.addEventListener("keyup", e => {
    if (  e.keyCode != KEY_CTRL
       || e.timeStamp - last_ctrl_down > MAX_TIME_PRESSED
       || !isActive()) {
        return
    }

    let id = getSelectionId();
    if (id != "") {
        sendRegions('INITIATE', id)
    }
}, false);

window.addEventListener("resize", debounce(e => {
    if (isActive()) {
        sendResize()
    }
}, 200), false);

window.addEventListener("scroll", throttle(e => {
    if (!isActive()) {
        return;
    }

    let scroll = getScrollRegion();
    let view = getViewport();

    let msg = {
        'task': 'SYNC',
        'type': 'SCROLL',
        'item': 'CONTENT',
        'pos': [scroll[0], scroll[1]],
        'pos-rel': [0, 0]
//        'tab-id': content.document._hcd_tab_id
    };

    var scroll_w = scroll[2] - view[2];
    if( scroll_w > 1 ) {
        msg['pos-rel'][0] = scroll[0] / scroll_w;
    }

    var scroll_h = scroll[3] - view[3];
    if( scroll_h > 1 ) {
        msg['pos-rel'][1] = scroll[1] / scroll_h;
    }

    send(msg);
}, 100), false);

setTimeout(() => {
    background_port = browser.runtime.connect({name: "content"})
    background_port.onMessage.addListener(msg => {
        if (msg.task == "NOTIFY-SIZE") {
            sendResize()
        } else if (msg.task == "REQUEST") {
            sendRegions('FOUND', msg.id)
        } else {
            console.log("msg-from-links", JSON.stringify(msg))
        }
    })
}, 100)

function getSelectionId()
{
  let selection = document.getSelection();
  selid = selection.toString().trim();
  selection.removeAllRanges();

  return selid.replace(/\s+/g, ' ').toLowerCase();
}
