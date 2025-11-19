document.addEventListener('DOMContentLoaded', () => {
    let websocket;
    
    // Inițiază conexiunea WebSocket
    function initWebSocket() {
        console.log('Trying to open a WebSocket connection...');
        const gateway = `ws://${window.location.hostname}/ws`;
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
        websocket.onerror = onError;
    }

    function onOpen(event) {
        console.log('WebSocket connection opened successfully.');
    }

    function onClose(event) {
        console.log('WebSocket connection closed. Reconnecting...');
    }

    function onMessage(event) {

    }

    function onError(event) {
        console.error('WebSocket Error:', event);
    }

});