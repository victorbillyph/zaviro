#pragma once

#include <string>
#include <memory>
#include <unordered_set>

struct GLFWwindow;

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void pollEvents();
    void swapBuffers();

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    GLFWwindow* getHandle() const { return m_window; }

    bool isKeyPressed(int key) const;
    bool isKeyJustPressed(int key) const;
    bool isMousePressed(int button) const;
    bool isMouseJustPressed(int button) const;
    void getMousePosition(double& x, double& y) const;
    bool isCursorDisabled() const { return m_cursorDisabled; }
    void setCursorDisabled(bool disabled);

    // Text input capture per frame (call pollEvents(), then consumeTypedText())
    void beginFrameInput();
    std::string consumeTypedText();
    void appendTyped(const char* utf8);

private:
    GLFWwindow* m_window = nullptr;
    int m_width, m_height;
    bool m_cursorDisabled = false;
    bool m_prevMouseBtn[3] = {false, false, false};
    std::string m_typedText;
    std::unordered_set<int> m_prevKeys;
};
