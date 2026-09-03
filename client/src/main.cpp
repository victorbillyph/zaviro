#include "engine/engine.h"
#include <iostream>

int main() {
    Engine& engine = Engine::instance();

    if (!engine.initialize("Zaviro", 1280, 720, "127.0.0.1", 8765)) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return 1;
    }

    engine.run();
    engine.shutdown();
    return 0;
}
