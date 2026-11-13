// Online play over WebRTC (PeerJS). Bundled into index.js with --pre-js; src/net.cpp calls in.
// The host registers the peer id "chess3d-<CODE>" on the public PeerJS server and the guest
// connects to it; after that the two browsers talk directly. C++ reads everything through
// poll(): game messages as sent, and connection events as lines starting with "@".
Module.chessNet = (() => {
  const PREFIX = 'chess3d-';
  const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';  // no 0/O or 1/I, so codes read aloud cleanly
  const RETRY_MS = 3000;
  const inbox = [];
  let peer = null, conn = null, retry = null;

  const event = (text) => inbox.push('@' + text);
  const clearRetry = () => { if (retry) clearTimeout(retry); retry = null; };

  function leave() {
    clearRetry();
    if (conn) conn.close();
    if (peer) peer.destroy();
    peer = conn = null;
    inbox.length = 0;
  }

  // Wires up one data connection; a second guest while one is connected gets "full".
  function adopt(c) {
    if (conn && conn.open) {
      c.on('open', () => { c.send('full'); setTimeout(() => c.close(), 500); });
      return;
    }
    conn = c;
    c.on('open', () => event('connected'));
    c.on('data', (d) => { if (typeof d === 'string') inbox.push(d); });
    c.on('close', () => { if (conn === c) { conn = null; event('disconnected'); } });
  }

  function host(code, resume) {
    leave();
    if (typeof Peer === 'undefined') return event('error no-peerjs');
    peer = new Peer(PREFIX + code);
    peer.on('open', () => event('waiting'));
    peer.on('connection', adopt);
    peer.on('disconnected', () => { if (peer && !peer.destroyed) peer.reconnect(); });
    peer.on('error', (e) => {
      // After a reload the old registration can linger for a few seconds; keep trying for it.
      if (e.type === 'unavailable-id' && resume) retry = setTimeout(() => host(code, true), RETRY_MS);
      else event('error ' + e.type);
    });
  }

  function join(code) {
    leave();
    if (typeof Peer === 'undefined') return event('error no-peerjs');
    peer = new Peer();
    const attempt = () => { clearRetry(); if (peer && !peer.destroyed) adopt(peer.connect(PREFIX + code, { reliable: true })); };
    peer.on('open', attempt);
    peer.on('disconnected', () => { if (peer && !peer.destroyed) peer.reconnect(); });
    peer.on('error', (e) => {
      if (e.type === 'peer-unavailable') {  // host not there (yet, or reloading): keep knocking
        event('waiting');
        retry = setTimeout(attempt, RETRY_MS);
      } else {
        event('error ' + e.type);
      }
    });
    // A dropped host comes back under the same id, so reconnect to it.
    const watch = setInterval(() => {
      if (!peer || peer.destroyed) return clearInterval(watch);
      if (!conn && !retry && peer.open) attempt();
    }, RETRY_MS);
  }

  return {
    newCode: () => Array.from({ length: 4 }, () => ALPHABET[Math.floor(Math.random() * ALPHABET.length)]).join(''),
    host, join, leave,
    send: (text) => { if (conn && conn.open) conn.send(text); },
    peek: () => inbox[0],
    take: () => inbox.shift(),
    urlCode: () => decodeURIComponent(location.hash.slice(1)).toUpperCase(),
    setUrlCode: (code) => history.replaceState(null, '', code ? '#' + code : location.pathname + location.search),
    copyLink: () => { try { navigator.clipboard.writeText(location.href); } catch (e) {} },
    load: (key) => { try { return localStorage.getItem(key) || ''; } catch (e) { return ''; } },
    save: (key, value) => { try { localStorage.setItem(key, value); } catch (e) {} },
  };
})();
