#!/bin/bash

const fs = require('fs');
const http = require('http');
const https = require('https');
const express = require('express');
const ws = require('ws');
const {handleMessage} = require('./build/Release/exokit.node');

function jsonParse(s) {
  try {
    return JSON.parse(s);
  } catch(err) {
    return null;
  }
}
function makePromise() {
  let accept, reject;
  const p = new Promise((a, r) => {
    accept = a;
    reject = r;
  });
  p.accept = accept;
  p.reject = reject;
  return p;
}

const app = express();
/* app.get('*', async (req, res, next) => {
  const contract = await loadPromise;
}); */
/* app.get('*', (req, res, next) => {
  res.end('Hello, MetaAssembly!');
}); */
app.use(express.static(__dirname));
const server = http.createServer(app);
const globalObjects = [];
const wss = new ws.Server({
  noServer: true,
});
wss.on('connection', async (s, req) => {
  // const o = url.parse(req.url, true);
  let localHandleMessage = null;
  s.on('message', async m => {
    if (localHandleMessage) {
      localHandleMessage(m);
    } else {
      if (typeof m === 'string') {
        const data = jsonParse(m);
        if (data) {
          const {method = '', args = []} = data;
          
          const messagePromises = [];
          for (let i = 0; i < args.length; i++) {
            if (args[i] === null) {
              const p = makePromise();
              p.then(v => {
                args[i] = [v.buffer, v.byteOffset, v.byteLength];
              });
              messagePromises.push(p);
            }
          }
          if (messagePromises.length > 0) {
            let messagePromiseIndex = 0;
            localHandleMessage = m => {
              console.log('got arg', messagePromiseIndex);
              const messagePromise = messagePromises[messagePromiseIndex++];
              if (messagePromiseIndex >= messagePromises.length) {
                localHandleMessage = null;
              }
              if (typeof m === 'string') {
                messagePromise.accept(jsonParse(m));
              } else {
                messagePromise.accept(m);
              }
            };
            await Promise.all(messagePromises);
          }

          console.log('calling', method, args);

          const o = handleMessage(method, args);
          const {error, result} = o;
          s.send(JSON.stringify({
            error,
            result,
          }));
        }
      } else {
        console.warn('cannot handle message', m);
      }
    }
  });
});
wss.on('error', err => {
  console.warn(err.stack);
});
const _ws = (req, socket, head) => {
  wss.handleUpgrade(req, socket, head, s => {
    wss.emit('connection', s, req);
  });
};
server.on('upgrade', _ws);
server.listen(3000);

console.log(`http://127.0.0.1:3000`);
