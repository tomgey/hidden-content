(() => {
let $ = selector => document.querySelector(selector);

document.addEventListener("click", e => {
    const element = e.target.closest(".click");
    if (!element)
    {
        return;
    }
    const class_list = element.classList;
    console.log("class list", JSON.stringify(class_list));

    if (class_list.contains("connect")) {
        browser.windows.getCurrent({populate: true}).then(window => {
            const TOOLBAR_HEIGHT = 37;
            browser.runtime.sendMessage({
                task: "REGISTER",
                geom: [
                  window.left,
                  window.top,
                  window.width,
                  window.height // + TOOLBAR_HEIGHT
                ],
                title: window.title,
                type: "Browser",
                "client-id": "very-random-id",
                url: "id-dont-care"
            });
        });
    } else if (class_list.contains("routing-algorithm")) {
        console.log("routing", element.dataset.algorithm);
        browser.runtime.sendMessage({
            'task': 'SET',
            'id': '/routing',
            'val': element.dataset.algorithm
        });
    } else if (class_list.contains("abort")) {
        browser.runtime.sendMessage({
          'task': 'ABORT',
          'id': '',
          'stamp': -1,
          'scope': 'all'
        });
    }

    close();
    //      $("#page-connect").classList.add("hidden");
    //    $("#page-menu").classList.remove("hidden");
});
})()
