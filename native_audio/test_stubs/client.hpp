#pragma once
#include <string>
#include <vector>
#include "format.h"

namespace native_audio {
inline std::vector<std::string> calls;
inline bool initOK = true, loadOK = true, sendOK = true;
inline bool statusOK=true;
inline NsStatus status{};
class Client {
public:
    bool init() { calls.push_back("init"); return initOK; }
    bool stop() { calls.push_back("stop"); return sendOK; }
    bool load(const char *path) { calls.push_back(path); return loadOK; }
    bool play() { calls.push_back("play"); return sendOK; }
    bool fadeOut() { calls.push_back("fade"); return sendOK; }
    bool loadSfx(const char *path) { calls.push_back(path); return loadOK; }
    bool playSfx(unsigned id) { calls.push_back("sfx:" + std::to_string(id)); return sendOK; }
    bool stopSfx() { calls.push_back("sfx:stop"); return sendOK; }
    bool send(unsigned op) { calls.push_back(std::to_string(op)); return sendOK; }
    bool readStatus(NsStatus &out) const { if(!statusOK) return false;out=status;return true; }
};
}
