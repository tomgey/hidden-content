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
    } else if (class_list.contains("change-page")) {
        $("#main-page").classList.toggle("hidden");
        $("#routing-page").classList.toggle("hidden");
        return
    } else if (class_list.contains("abort")) {
        browser.runtime.sendMessage({
          'task': 'ABORT',
          'id': '',
          'stamp': -1,
          'scope': 'all'
        });
    }

    close();
});

document.addEventListener("change", e => {
    const element = e.target.closest(".input-change");
    if (!element)
    {
        return;
    }

    browser.runtime.sendMessage({
      'task': 'SET',
      'id': '/config',
      'id': element.id,
      'val': element.value,
      'type': element.dataset.type
    });
})
})()
