#include "engine/window.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <iostream>

static void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
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
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
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
    glfwPollEvents();
    m_prevMouseBtn[0] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    m_prevMouseBtn[1] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    m_prevMouseBtn[2] = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
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
