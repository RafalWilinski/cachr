const http = require('http');

const server = http.createServer((request, response) => {
    console.log('New request!');

    response.writeHead(200, {'Content-Type': 'text/html'});
    response.end('ok');
});

server.on('clientError', (err, socket) => {
    console.log(err);
    socket.end('HTTP/1.1 400 Bad Request\r\n\r\n');
});

server.on('connect', (request, socket, head) => {
    console.log('OnConnect');
});

server.listen(process.env.PORT || 8000);

console.log(`Server started at port ${process.env.PORT || 8000}`);
