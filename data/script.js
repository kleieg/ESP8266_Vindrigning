var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    websocket.send(JSON.stringify({ "action": "states" }));
}

function reboot() {
    console.log('Rebooting');
    websocket.send(JSON.stringify({ "action": "reboot" }));
}

function setSetting(setting, value) {
    console.log(`Updating setting ${setting}`);
    var data = {};
    data[setting] = value;
    websocket.send(JSON.stringify({ "action": "settings", "data": data }));
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    var myObj = JSON.parse(event.data);
    console.log(myObj);
    for (i in myObj.cards) {
        var c_text = myObj.cards[i].c_text;
        console.log(c_text);
        var el = document.getElementById(i + "h");
        if(el.tagName === 'INPUT') {
            if(document.activeElement != el) {
                el.value = c_text;
            }
        } else {
            el.innerHTML = c_text;
        }
    }
    for (i in myObj.gpios) {
        var output = myObj.gpios[i].output;
        var state = myObj.gpios[i].state;
        console.log(output);
        console.log(state);
        if (state == "1") {
            document.getElementById(output).checked = true;
            document.getElementById(output + "s").innerHTML = "ON";
        }
        else {
            document.getElementById(output).checked = false;
            document.getElementById(output + "s").innerHTML = "OFF";
        }
    }
    console.log(event.data);
}