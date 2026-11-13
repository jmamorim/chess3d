#include "net.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <vector>

// Strings cross the boundary as UTF-8 in the wasm heap. JS-to-C++ results go through a caller
// buffer: the JS side returns the full length, and the caller retries with a bigger buffer.
EM_JS(int, jsCall, (const char* fn, const char* a, int b, char* out, int cap), {
  const text = (p) => { let e = p; while (HEAPU8[e]) ++e; return new TextDecoder().decode(HEAPU8.subarray(p, e)); };
  const net = Module.chessNet;
  const name = text(fn), arg = a ? text(a) : "";
  let r;
  switch (name) {
    case "newCode": r = net.newCode(); break;
    case "host": net.host(arg, !!b); break;
    case "join": net.join(arg); break;
    case "leave": net.leave(); break;
    case "send": net.send(arg); break;
    case "poll": r = net.peek(); if (r === undefined) return -1; break;
    case "urlCode": r = net.urlCode(); break;
    case "setUrlCode": net.setUrlCode(arg); break;
    case "copyLink": net.copyLink(); break;
    case "load": r = net.load(arg); break;
    case "save": { const i = arg.indexOf("\n"); net.save(arg.slice(0, i), arg.slice(i + 1)); break; }
  }
  if (r === undefined) return 0;
  const bytes = new TextEncoder().encode(r);
  if (bytes.length < cap) {
    HEAPU8.set(bytes, out);
    HEAPU8[out + bytes.length] = 0;
    if (name === "poll") net.take();  // only consume a message once it has been copied out
  }
  return bytes.length;
});

namespace {

int call(const char* fn, const std::string& arg = {}, int flag = 0) { return jsCall(fn, arg.c_str(), flag, nullptr, 0); }

// Returns the JS result's length (-1 = nothing), filling `out`.
int callString(const char* fn, std::string& out, const std::string& arg = {}) {
    std::vector<char> buf(256);
    int n = jsCall(fn, arg.c_str(), 0, buf.data(), static_cast<int>(buf.size()));
    if (n >= static_cast<int>(buf.size())) {
        buf.resize(n + 1);
        n = jsCall(fn, arg.c_str(), 0, buf.data(), static_cast<int>(buf.size()));
    }
    out.assign(buf.data(), n > 0 ? n : 0);
    return n;
}

std::string callString(const char* fn, const std::string& arg = {}) {
    std::string s;
    callString(fn, s, arg);
    return s;
}

}  // namespace

namespace net {

bool available() { return true; }
std::string newCode() { return callString("newCode"); }
void host(const std::string& code, bool resume) { call("host", code, resume ? 1 : 0); }
void join(const std::string& code) { call("join", code); }
void leave() { call("leave"); }
void send(const std::string& msg) { call("send", msg); }
bool poll(std::string& out) { return callString("poll", out) >= 0; }
std::string urlCode() { return callString("urlCode"); }
void setUrlCode(const std::string& code) { call("setUrlCode", code); }
void copyLink() { call("copyLink"); }
std::string load(const std::string& key) { return callString("load", key); }
void save(const std::string& key, const std::string& value) { call("save", key + "\n" + value); }

}  // namespace net
#else
namespace net {

bool available() { return false; }
std::string newCode() { return {}; }
void host(const std::string&, bool) {}
void join(const std::string&) {}
void leave() {}
void send(const std::string&) {}
bool poll(std::string&) { return false; }
std::string urlCode() { return {}; }
void setUrlCode(const std::string&) {}
void copyLink() {}
std::string load(const std::string&) { return {}; }
void save(const std::string&, const std::string&) {}

}  // namespace net
#endif
