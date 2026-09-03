#include "engine/engine.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
    Engine& engine = Engine::instance();

    std::string host = "127.0.0.1";
    int port = 8765;
    std::string socks = ""; // e.g. "127.0.0.1:19050"

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--socks" && i + 1 < argc) {
            socks = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            host = arg; // positional: server host (ip or .onion)
        }
    }

    if (!socks.empty()) {
        size_t colon = socks.rfind(':');
        if (colon != std::string::npos) {
            std::string ph = socks.substr(0, colon);
            int pp = std::atoi(socks.substr(colon + 1).c_str());
            engine.setTorProxy(ph, pp);
        }
    }

    if (!engine.initialize("Zaviro", 1280, 720, host, port)) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return 1;
    }

    engine.run();
    engine.shutdown();
    return 0;
}
