// Local static preview, restricted to this prototype directory and loopback.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const types = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.mjs': 'text/javascript', '.js': 'text/javascript', '.txt': 'text/plain; charset=utf-8' };
http.createServer((request, response) => {
  let relative;
  try {
    relative = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
  } catch {
    response.writeHead(400).end();
    return;
  }
  const file = path.resolve(root, `.${relative === '/' ? '/index.html' : relative}`);
  if (!file.startsWith(root + path.sep)) {
    response.writeHead(403).end();
    return;
  }
  fs.readFile(file, (error, buffer) => {
    if (error) {
      response.writeHead(404).end('Not found');
      return;
    }
    response.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    response.end(buffer);
  });
}).listen(4178, '127.0.0.1', () => console.log('Deco preview: http://127.0.0.1:4178'));
