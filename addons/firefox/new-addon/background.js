let content_ports = new Map()
let client = new VislinkClient("Firefox")

function queryActiveTabs(fn_then) {
    return browser.tabs.query({active: true, url: "<all_urls>"})
                       .then(fn_then)
                       .catch(e => console.log(e))
}

function forEachActiveTab(fn_then) {
    queryActiveTabs(tabs => {
        for (let tab of tabs) {
            fn_then(tab, content_ports.get(tab.id))
        }
    })
}

/** Connection to links server established */
client.onopen = () => {
    client.send({task: 'GET', id: '/routing', type: 'String'})
    forEachActiveTab((tab, port) => port.postMessage({task: "NOTIFY-SIZE"}))
}

/** Update icon according to status */
client.onstatus = status => {
    let icon_path = "icons/icon"
    if (status != '') {
        icon_path += '-' + status
    }

    browser.browserAction.setIcon({path: icon_path + '.png'});
}

/** Messages from the links server */
client.onmessage = msg => {
    console.log("message", msg)
    forEachActiveTab((tab, port) => port.postMessage(msg))
}

/** Connections from content scripts */
browser.runtime.onConnect.addListener(port => {
    content_ports.set(port.sender.tab.id, port)

    port.onMessage.addListener(msg => {
        client.send(msg)

        if (msg.task == 'RESIZE') {
            for (let [id, data] of client.active_routes) {
                port.postMessage({task: 'REQUEST', id: id})
            }
        }
    })
    port.onDisconnect.addListener(port => {
        console.log("disconnect", port.sender.tab)
        content_ports.delete(port.sender.tab.id)
    })
})

/** Messages from the menu */
browser.runtime.onMessage.addListener((request, sender, sendResponse) => {
    if (request.task == "REGISTER") {
        client.connect({register: request});
    } else {
        console.log("send", request);
        client.send(request);
    }
});

//browser.tabs.onActivated.addListener(activeInfo => {
//    console.log(`Tab ${activeInfo.tabId} was activated`);
//});

//browser.tabs.onUpdated.addListener((tabId, changeInfo, tabInfo) => {
//    console.log(`Updated tab: ${tabId}`);
//    console.log("Changed attributes: ", changeInfo);
//    console.log("New tab Info: ", tabInfo);

//    queryActiveTabs(tabs => { console.log("tabs", tabs); });
//}, {properties: ["status"]});
