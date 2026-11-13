#pragma once
#include <string>

// Bridge to web/net.js (WebRTC through PeerJS). Online play exists only in the web build;
// on desktop available() is false and the rest does nothing.
namespace net {

bool available();
std::string newCode();
void host(const std::string& code, bool resume);  // resume: keep retrying if the id is still held
void join(const std::string& code);
void leave();
void send(const std::string& msg);
// Next incoming line, if any. Lines starting with '@' are connection events:
// @waiting, @connected, @disconnected, @error <type>.
bool poll(std::string& out);

std::string urlCode();  // room code in the page URL (#CODE), or ""
void setUrlCode(const std::string& code);
void copyLink();
std::string load(const std::string& key);
void save(const std::string& key, const std::string& value);

}  // namespace net
