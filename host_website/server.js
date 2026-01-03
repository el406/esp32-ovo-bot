var WebSocketServer = require('websocket').server;
var http = require('http');
var fs = require('fs');



var index = fs.readFileSync('index.html');

var server = http.createServer(function(request, response) {
    console.log((new Date()) + ' Received request for ' + request.url);
    response.writeHead(200, {'Content-Type': 'text/html'});
    response.end(index);
});

server.listen(8080, function() {
    console.log((new Date()) + ' Initializing control panel on port 8080');
});


 // websocket stuff

wsServer = new WebSocketServer({
    httpServer: server,
    autoAcceptConnections: false
});



function originIsAllowed(origin) {
  return true;
}

// for sending the information over
let websiteClient = null;
let espClient = null;




wsServer.on('request', function(request) {
    if (!originIsAllowed(request.origin)) {
      // Make sure we only accept requests from an allowed origin
      request.reject();
      console.log((new Date()) + ' Connection from origin ' + request.origin + ' rejected.');
      return;
    }
    var connection = request.accept(null, request.origin);
    console.log((new Date()) + " accpeted request from " + request.origin);
    console.log((new Date()) + ' Connection accepted.');
    connection.on('message', function(message) {
        if (message.type === 'utf8') {
            console.log('Received Message: ' + message.utf8Data);
            if(message.utf8Data == "ID_CONTROL")
            {
                websiteClient = connection;
                console.log("welcome website!");
            }
            if(message.utf8Data == "ID_ESP"){
                espClient = connection;
                console.log("welcome esp!");
            }
            else{
                // event polling
                if (connection == websiteClient && espClient){
                    espClient.send(message.utf8Data);
                }
                else if (connection == espClient && websiteClient){
                    websiteClient.send(message.utf8Data);
                }
                else{
                    console.log("message not sent");
                }
            }
            //connection.sendUTF(message.utf8Data);
        }
        else if (message.type === 'binary') {
            console.log('Received Binary Message of ' + message.binaryData.length + ' bytes');
            if(connection == espClient && websiteClient){
               websiteClient.send(message.binaryData);
            }
            if (connection == websiteClient && espClient){
                    espClient.send(message.binaryData);
            }

            //connection.sendBytes(message.binaryData);
        }
    });
    connection.on('close', function(reasonCode, description) {
        console.log((new Date()) + ' Peer ' + connection.remoteAddress + ' disconnected.');
        if(connection == websiteClient){websiteClient = null};
        if(connection == espClient){espClient = null};
    });
});
