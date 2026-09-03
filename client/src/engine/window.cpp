#include "engine/window.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

static void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

static void charCallback(GLFWwindow* window, unsigned int codepoint) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && codepoint >= 32 && codepoint != 127) {
        char utf8[5] = {0, 0, 0, 0, 0};
        if (codepoint <= 0x7F) {
            utf8[0] = (char)codepoint;
        } else if (codepoint <= 0x7FF) {
            utf8[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
            utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            utf8[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
            utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        } else {
            utf8[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
            utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
            utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        }
        self->appendTyped(utf8);
    }
}

Window::Window(int width, int height, const std::string& title)
    : m_width(width), m_height(height) {

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetCharCallback(m_window, charCallback);
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    std::cout << "OpenGL " << glGetString(GL_VERSION) << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    // Snapshot previous key states (so isKeyJustPressed compares last frame)
    m_prevKeys.clear();
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; k++) {
        if (glfwGetKey(m_window, k) == GLFW_PRESS) m_prevKeys.insert(k);
    }
    m_typedText.clear();

    glfwPollEvents();

    m_prevMouseBtn[0] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    m_prevMouseBtn[1] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    m_prevMouseBtn[2] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
}

void Window::beginFrameInput() {
    m_typedText.clear();
    m_prevKeys.clear();
}

void Window::appendTyped(const char* utf8) {
    m_typedText += utf8;
}

std::string Window::consumeTypedText() {
    std::string s = m_typedText;
    m_typedText.clear();
    return s;
}

bool Window::isKeyJustPressed(int key) const {
    bool current = glfwGetKey(m_window, key) == GLFW_PRESS;
    bool prev = m_prevKeys.count(key) > 0;
    return current && !prev;
}

void Window::swapBuffers() {
    glfwSwapBuffers(m_window);
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Window::isMousePressed(int button) const {
    return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

bool Window::isMouseJustPressed(int button) const {
    bool current = glfwGetMouseButton(m_window, button) == GLFW_PRESS;
    bool prev = m_prevMouseBtn[button];
    return current && !prev;
}

void Window::getMousePosition(double& x, double& y) const {
    glfwGetCursorPos(m_window, &x, &y);
}

void Window::setCursorDisabled(bool disabled) {
    m_cursorDisabled = disabled;
    glfwSetInputMode(m_window, GLFW_CURSOR, disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}
