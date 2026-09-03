#include "engine/renderer.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <iostream>

// UI shaders (2D)
static const char* uiVertexSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
uniform mat4 proj;
uniform mat3 model;
uniform vec4 uUVRect;
void main() {
    vec2 uv = mix(uUVRect.xy, uUVRect.zw, aUV);
    vUV = uv;
    vec3 pos = model * vec3(aPos, 1.0);
    gl_Position = proj * vec4(pos.xy, 0.0, 1.0);
}
)";

static const char* uiFragmentSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform vec4 uColor;
uniform sampler2D uTex;
uniform int uUseTexture;
void main() {
    if (uUseTexture == 1) {
        vec4 tex = texture(uTex, vUV);
        float a = tex.a;
        FragColor = vec4(uColor.rgb, a);
    } else {
        FragColor = uColor;
    }
}
)";

static const char* vertexShaderSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vertexColor;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
void main() {
    vertexColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

static const char* fragmentShaderSrc = R"(
#version 330 core
in vec3 vertexColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vertexColor, 1.0);
}
)";

// Skybox shaders - renders a gradient sky based on direction
static const char* skyboxVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 vDir;
uniform mat4 projection;
uniform mat4 view;
void main() {
    vDir = aPos;
    vec4 pos = projection * mat4(mat3(view)) * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
    gl_Position.z = gl_Position.w - 0.0001;
}
)";

static const char* skyboxFragmentSrc = R"(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform float uTime;
void main() {
    vec3 dir = normalize(vDir);
    float t = dir.y * 0.5 + 0.5;

    // Horizon
    vec3 horizon = vec3(0.78, 0.85, 0.92);
    // Zenith (deep blue)
    vec3 zenith = vec3(0.12, 0.35, 0.72);
    // Below horizon (earthy/dark)
    vec3 below = vec3(0.55, 0.62, 0.68);

    vec3 color;
    if (dir.y >= 0.0) {
        // Blend horizon -> zenith
        float h = clamp(dir.y * 2.5, 0.0, 1.0);
        color = mix(horizon, zenith, h);

        // Subtle sun glow
        vec3 sunDir = normalize(vec3(0.3, 0.6, -0.5));
        float sun = max(dot(dir, sunDir), 0.0);
        color += vec3(1.0, 0.95, 0.85) * pow(sun, 220.0) * 0.9;
        color += vec3(1.0, 0.8, 0.6) * pow(sun, 6.0) * 0.25;
    } else {
        // Below horizon, fade to dark ground haze
        float h = clamp(-dir.y, 0.0, 1.0);
        color = mix(horizon, below * 0.5, h);
    }

    FragColor = vec4(color, 1.0);
}
)";

// Terrain shaders
static const char* terrainVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aHeight;
out vec3 vNormal;
out vec3 vWorldPos;
out float vHeight;
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
void main() {
    vWorldPos = (model * vec4(aPos, 1.0)).xyz;
    vNormal = mat3(model) * aNormal;
    vHeight = aHeight;
    gl_Position = projection * view * vec4(vWorldPos, 1.0);
}
)";

static const char* terrainFragmentSrc = R"(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeight;
out vec4 FragColor;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
void main() {
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);

    // Height-based coloring
    vec3 sand    = vec3(0.76, 0.70, 0.50);
    vec3 grass   = vec3(0.30, 0.62, 0.30);
    vec3 rock    = vec3(0.45, 0.45, 0.47);
    vec3 snow    = vec3(0.95, 0.96, 0.98);

    vec3 base;
    if (vHeight < 0.5) {
        base = sand;
    } else if (vHeight < 4.0) {
        float blend = clamp((vHeight - 0.5) / 3.5, 0.0, 1.0);
        base = mix(sand, grass, blend);
    } else if (vHeight < 9.0) {
        float blend = clamp((vHeight - 4.0) / 5.0, 0.0, 1.0);
        base = mix(grass, rock, blend);
    } else {
        float blend = clamp((vHeight - 9.0) / 3.0, 0.0, 1.0);
        base = mix(rock, snow, blend);
    }

    // Lighting
    float diff = max(dot(n, lightDir), 0.0);
    vec3 ambient = base * 0.35;
    vec3 diffuse = base * diff * 0.7;

    // Simple fog based on distance
    float dist = distance(vWorldPos, uCamPos);
    float fog = clamp((dist - 40.0) / 90.0, 0.0, 1.0);
    vec3 fogColor = vec3(0.78, 0.85, 0.92);

    vec3 color = ambient + diffuse;
    color = mix(color, fogColor, fog);

    FragColor = vec4(color, 1.0);
}
)";

static unsigned int compileShader(unsigned int type, const char* source) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);

    int success;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        std::cerr << "Shader compilation failed: " << log << std::endl;
    }
    return id;
}

static unsigned int linkProgram(unsigned int vs, unsigned int fs) {
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "Shader linking failed: " << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static void mat4Perspective(float* out, float fov, float aspect, float near, float far) {
    float tanHalf = tanf(fov / 2.0f);
    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0] = 1.0f / (aspect * tanHalf);
    out[5] = 1.0f / tanHalf;
    out[10] = -(far + near) / (far - near);
    out[11] = -1.0f;
    out[14] = -(2.0f * far * near) / (far - near);
}

static void mat4LookAt(float* out, Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = (target - eye).normalized();
    Vec3 s = Vec3{
        f.y * up.z - f.z * up.y,
        f.z * up.x - f.x * up.z,
        f.x * up.y - f.y * up.x
    }.normalized();
    Vec3 u = Vec3{
        s.y * f.z - s.z * f.y,
        s.z * f.x - s.x * f.z,
        s.x * f.y - s.y * f.x
    };

    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0] = s.x; out[4] = s.y; out[8] = s.z;  out[12] = -(s.x*eye.x + s.y*eye.y + s.z*eye.z);
    out[1] = u.x; out[5] = u.y; out[9] = u.z;  out[13] = -(u.x*eye.x + u.y*eye.y + u.z*eye.z);
    out[2] = -f.x; out[6] = -f.y; out[10] = -f.z; out[14] = (f.x*eye.x + f.y*eye.y + f.z*eye.z);
    out[15] = 1.0f;
}

static void mat4Translate(float* out, Vec3 t) {
    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    out[12] = t.x; out[13] = t.y; out[14] = t.z;
}

static void mat4Scale(float* out, Vec3 s) {
    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0] = s.x; out[5] = s.y; out[10] = s.z; out[15] = 1.0f;
}

static void mat4Mul(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            tmp[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] + a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
    for (int i = 0; i < 16; i++) out[i] = tmp[i];
}

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

bool Renderer::initialize(int width, int height) {
    initShaders();
    initMeshes();
    initSkybox();
    initFont();
    initUI();
    buildTerrain(m_terrainSize, m_terrainSegments, m_terrainSeed);
    setViewport(width, height);
    m_uiWidth = width;
    m_uiHeight = height;
    return true;
}

void Renderer::shutdown() {
    if (m_shaderProgram) glDeleteProgram(m_shaderProgram);
    if (m_skyboxShader) glDeleteProgram(m_skyboxShader);
    if (m_terrainShader) glDeleteProgram(m_terrainShader);
    if (m_uiShader) glDeleteProgram(m_uiShader);

    if (m_cubeVAO) { glDeleteVertexArrays(1, &m_cubeVAO); glDeleteBuffers(1, &m_cubeVBO); }
    if (m_skyboxVAO) { glDeleteVertexArrays(1, &m_skyboxVAO); glDeleteBuffers(1, &m_skyboxVBO); }
    if (m_terrainVAO) {
        glDeleteVertexArrays(1, &m_terrainVAO);
        glDeleteBuffers(1, &m_terrainVBO);
        glDeleteBuffers(1, &m_terrainEBO);
    }
    if (m_uiVAO) { glDeleteVertexArrays(1, &m_uiVAO); glDeleteBuffers(1, &m_uiVBO); }
    if (m_fontTexture) glDeleteTextures(1, &m_fontTexture);

    delete[] m_terrainHeights;
    m_terrainHeights = nullptr;
}

void Renderer::initShaders() {
    m_shaderProgram = linkProgram(
        compileShader(GL_VERTEX_SHADER, vertexShaderSrc),
        compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc)
    );

    m_skyboxShader = linkProgram(
        compileShader(GL_VERTEX_SHADER, skyboxVertexSrc),
        compileShader(GL_FRAGMENT_SHADER, skyboxFragmentSrc)
    );

    m_terrainShader = linkProgram(
        compileShader(GL_VERTEX_SHADER, terrainVertexSrc),
        compileShader(GL_FRAGMENT_SHADER, terrainFragmentSrc)
    );

    m_uiShader = linkProgram(
        compileShader(GL_VERTEX_SHADER, uiVertexSrc),
        compileShader(GL_FRAGMENT_SHADER, uiFragmentSrc)
    );
}

void Renderer::initMeshes() {
    float cubeVertices[] = {
        // positions       // colors
        // Front face
        -0.5f, -0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.2f, 0.6f, 1.0f,
        // Back face
        -0.5f, -0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
         0.5f, -0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
         0.5f,  0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
         0.5f,  0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
        -0.5f,  0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
        -0.5f, -0.5f, -0.5f,  0.5f, 0.2f, 0.8f,
        // Top face
        -0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 0.4f,
         0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 0.4f,
         0.5f,  0.5f,  0.5f,  0.2f, 1.0f, 0.4f,
         0.5f,  0.5f,  0.5f,  0.2f, 1.0f, 0.4f,
        -0.5f,  0.5f,  0.5f,  0.2f, 1.0f, 0.4f,
        -0.5f,  0.5f, -0.5f,  0.2f, 1.0f, 0.4f,
        // Bottom face
        -0.5f, -0.5f, -0.5f,  1.0f, 0.6f, 0.2f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.6f, 0.2f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.6f, 0.2f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.6f, 0.2f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.6f, 0.2f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.6f, 0.2f,
        // Right face
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.2f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.2f,
         0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 0.2f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
        // Left face
        -0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 0.4f,
        -0.5f,  0.5f, -0.5f,  1.0f, 0.2f, 0.4f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.2f, 0.4f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.2f, 0.4f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 0.4f,
        -0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 0.4f,
    };

    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);

    glBindVertexArray(m_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Renderer::initSkybox() {
    float skyboxVertices[] = {
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,

        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
    };

    glGenVertexArrays(1, &m_skyboxVAO);
    glGenBuffers(1, &m_skyboxVBO);
    glBindVertexArray(m_skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Renderer::initUI() {
    // Quad (triangle strip): pos2 + uv2
    float quadVertices[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_uiVAO);
    glGenBuffers(1, &m_uiVBO);
    glBindVertexArray(m_uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::initFont() {
    // Procedural 8x8 bitmap font atlas (16 cols x 16 rows = 256 glyphs)
    const unsigned int atlasW = m_fontGlyphW * m_fontCols;
    const unsigned int atlasH = m_fontGlyphH * m_fontCols;
    std::vector<unsigned char> atlas(atlasW * atlasH * 4, 0);

    // Simple 8x8 pixel font for ASCII 32-127.
    // 0 = pixel off, 1 = pixel on.
    static const unsigned char font8x8[96][8] = {
        {0,0,0,0,0,0,0,0}, // space
        {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18},
        {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
        {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
        {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
        {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00},
        {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
        {0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
        {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
        {0x00,0x18,0x7E,0x3C,0x7E,0x18,0x00,0x00}, // *
        {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // +
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
        {0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00}, // -
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
        {0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80}, // /
        {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
        {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 1
        {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 2
        {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
        {0x0C,0x1C,0x3C,0x6C,0xFE,0x0C,0x0C,0x00}, // 4
        {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
        {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
        {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
        {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
        {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 9
        {0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x00}, // :
        {0x00,0x00,0x18,0x00,0x00,0x18,0x18,0x30}, // ;
        {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // <
        {0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00}, // =
        {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // >
        {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // ?
        {0x3C,0x66,0x6E,0x6E,0x6C,0x60,0x3C,0x00}, // @
        {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // A
        {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
        {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
        {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
        {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // E
        {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F
        {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, // G
        {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
        {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // I
        {0x0E,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, // J
        {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K
        {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L
        {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // M
        {0x66,0x76,0x7E,0x6E,0x66,0x66,0x66,0x00}, // N
        {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
        {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
        {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // Q
        {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // R
        {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S
        {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
        {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
        {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
        {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // W
        {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X
        {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
        {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z
        {0x78,0x60,0x60,0x60,0x60,0x60,0x78,0x00}, // [
        {0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02}, // backslash
        {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // ]
        {0x18,0x3C,0x66,0x42,0x00,0x00,0x00,0x00}, // ^
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
        {0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, // `
        {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // a
        {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // b
        {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00}, // c
        {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // d
        {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // e
        {0x1C,0x30,0x78,0x30,0x30,0x30,0x30,0x00}, // f
        {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // g
        {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // h
        {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // i
        {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38}, // j
        {0x60,0x60,0x6C,0x78,0x70,0x78,0x6C,0x00}, // k
        {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // l
        {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00}, // m
        {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // n
        {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // o
        {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
        {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
        {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // r
        {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // s
        {0x30,0x30,0x78,0x30,0x30,0x30,0x1C,0x00}, // t
        {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // u
        {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // v
        {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, // w
        {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // x
        {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // y
        {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // z
        {0x0C,0x18,0x18,0x70,0x18,0x18,0x0C,0x00}, // {
        {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // |
        {0x30,0x18,0x18,0x0E,0x18,0x18,0x30,0x00}, // }
        {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
    };

    for (int glyph = 0; glyph < 96; glyph++) {
        int row = glyph / m_fontCols;
        int col = glyph % m_fontCols;
        int ox = col * m_fontGlyphW;
        int oy = (m_fontCols - 1 - row) * m_fontGlyphH;

        for (int gy = 0; gy < 8; gy++) {
            unsigned char bits = font8x8[glyph][gy];
            for (int gx = 0; gx < 8; gx++) {
                bool on = (bits >> (7 - gx)) & 1;
                int px = ox + gx;
                int py = oy + gy;
                if (on) {
                    int idx = (py * atlasW + px) * 4;
                    atlas[idx+0] = 255;
                    atlas[idx+1] = 255;
                    atlas[idx+2] = 255;
                    atlas[idx+3] = 255;
                }
            }
        }
    }

    glGenTextures(1, &m_fontTexture);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasW, atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::uiBegin() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_uiShader);

    setUiResolution(m_uiWidth, m_uiHeight);

    int uvRectLoc = glGetUniformLocation(m_uiShader, "uUVRect");
    glUniform4f(uvRectLoc, 0.0f, 0.0f, 1.0f, 1.0f);

    float identity[9] = {1,0,0, 0,1,0, 0,0,1};
    int modelLoc = glGetUniformLocation(m_uiShader, "model");
    glUniformMatrix3fv(modelLoc, 1, GL_FALSE, identity);

    glBindVertexArray(m_uiVAO);
}

void Renderer::setUiResolution(int width, int height) {
    m_uiWidth = width;
    m_uiHeight = height;

    float proj[16] = {};
    proj[0] = 2.0f / (float)m_uiWidth;
    proj[5] = -2.0f / (float)m_uiHeight;
    proj[10] = -1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;

    int projLoc = glGetUniformLocation(m_uiShader, "proj");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, proj);
}

void Renderer::uiEnd() {
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

void Renderer::drawRect(float x, float y, float w, float h, const Color& color) {
    int colorLoc = glGetUniformLocation(m_uiShader, "uColor");
    glUniform4f(colorLoc, color.r, color.g, color.b, color.a);
    int texLoc = glGetUniformLocation(m_uiShader, "uTex");
    glUniform1i(texLoc, 0);
    int useTexLoc = glGetUniformLocation(m_uiShader, "uUseTexture");
    glUniform1i(useTexLoc, 0);

    float scaleX = w, scaleY = h;
    float model[9] = {
        scaleX, 0.0f, 0.0f,
        0.0f, scaleY, 0.0f,
        x, y, 1.0f,
    };

    // Apply transform in shader via gl_Vertex scaled in CPU (simplest):
    // We send quad unit coords and set scale through uniforms. For simplicity,
    // draw using a small dynamic matrix: use 2x3 affine.
    int modelLoc = glGetUniformLocation(m_uiShader, "model");
    glUniformMatrix3fv(modelLoc, 1, GL_FALSE, model);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::drawText(const std::string& text, float x, float y, float scale, const Color& color) {
    int colorLoc = glGetUniformLocation(m_uiShader, "uColor");
    glUniform4f(colorLoc, color.r, color.g, color.b, color.a);
    int texLoc = glGetUniformLocation(m_uiShader, "uTex");
    glUniform1i(texLoc, 0);
    int useTexLoc = glGetUniformLocation(m_uiShader, "uUseTexture");
    glUniform1i(useTexLoc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);

    float gw = (float)m_fontGlyphW * scale;
    float gh = (float)m_fontGlyphH * scale;
    float uStep = 1.0f / (float)m_fontCols;
    float vStep = 1.0f / (float)m_fontCols;

    float cursorX = x;

    for (char c : text) {
        int ascii = (unsigned char)c;
        if (ascii < 32) {
            cursorX += gw * 0.5f;
            continue;
        }
        int glyph = ascii - 32;
        if (glyph < 0 || glyph > 95) glyph = 0;

        int row = glyph / m_fontCols;
        int col = glyph % m_fontCols;

        float u0 = col * uStep;
        float v0 = row * vStep;
        float u1 = u0 + uStep;
        float v1 = v0 + vStep;

        float model[9] = {
            gw, 0.0f, 0.0f,
            0.0f, gh, 0.0f,
            cursorX, y, 1.0f,
        };
        int modelLoc = glGetUniformLocation(m_uiShader, "model");
        glUniformMatrix3fv(modelLoc, 1, GL_FALSE, model);

        // Override UV via uniform
        int u0Loc = glGetUniformLocation(m_uiShader, "uUVRect");
        glUniform4f(u0Loc, u0, v0, u1, v1);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        cursorX += gw;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}


// Simplex-ish value noise hash
static float hash11(float n) {
    return std::fmod(std::sin(n * 127.1f + 311.7f) * 43758.5453f, 1.0f);
}

static float hash12(float x, float y) {
    return std::fmod(std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f, 1.0f);
}

static float smoothstepf(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static float valueNoise2D(float x, float y, unsigned int seed) {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float fx = x - ix;
    float fy = y - iy;

    float s = hash12((float)ix, (float)iy);
    float t = hash12((float)(ix + 1), (float)iy);
    float u = hash12((float)ix, (float)(iy + 1));
    float v = hash12((float)(ix + 1), (float)(iy + 1));

    float sx = smoothstepf(fx);
    float sy = smoothstepf(fy);

    float a = s + (t - s) * sx;
    float b = u + (v - u) * sx;
    return a + (b - a) * sy;
}

static float fbm2D(float x, float y, unsigned int seed, int octaves) {
    float total = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += valueNoise2D(x * frequency + (float)seed * 0.01f, y * frequency + (float)seed * 0.013f, seed + i) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return total / maxValue;
}

float Renderer::noise2D(float x, float z, unsigned int seed) const {
    return fbm2D(x, z, seed, 4);
}

float Renderer::terrainHeightAt(float x, float z) const {
    float nx = (x + m_terrainSize * 0.5f) / m_terrainSize;
    float nz = (z + m_terrainSize * 0.5f) / m_terrainSize;

    float h = noise2D(nx * 4.0f, nz * 4.0f, m_terrainSeed) * 12.0f;
    h += noise2D(nx * 8.0f, nz * 8.0f, m_terrainSeed + 1) * 4.0f;
    h -= 4.0f;

    return std::max(h, 0.0f);
}

void Renderer::buildTerrain(float size, int segments, unsigned int seed) {
    m_terrainSize = size;
    m_terrainSegments = segments;
    m_terrainSeed = seed;

    const int vertsPerRow = segments + 1;
    const int vertexCount = vertsPerRow * vertsPerRow;
    const int indexCount = segments * segments * 6;

    float half = size * 0.5f;
    float step = size / segments;

    if (m_terrainHeights) delete[] m_terrainHeights;
    m_terrainHeights = new float[vertexCount];

    // 8 floats per vertex: pos(3) + normal(3) + height(1)... use pos3 + height1 = 4 floats
    // We'll compute normals separately.
    std::vector<float> vertices;
    vertices.reserve(vertexCount * 7); // pos3 + normal3 + height1

    // Generate heights
    for (int z = 0; z < vertsPerRow; z++) {
        for (int x = 0; x < vertsPerRow; x++) {
            int i = z * vertsPerRow + x;
            float wx = -half + x * step;
            float wz = -half + z * step;
            m_terrainHeights[i] = terrainHeightAt(wx, wz);
        }
    }

    // Build vertices
    for (int z = 0; z < vertsPerRow; z++) {
        for (int x = 0; x < vertsPerRow; x++) {
            int i = z * vertsPerRow + x;
            float wx = -half + x * step;
            float wz = -half + z * step;
            float h = m_terrainHeights[i];

            vertices.push_back(wx);
            vertices.push_back(h);
            vertices.push_back(wz);
        }
    }

    // Compute normals using central differences
    std::vector<float> normals(vertexCount * 3);
    for (int z = 0; z < vertsPerRow; z++) {
        for (int x = 0; x < vertsPerRow; x++) {
            int i = z * vertsPerRow + x;
            int xm = std::max(x - 1, 0);
            int xp = std::min(x + 1, segments);
            int zm = std::max(z - 1, 0);
            int zp = std::min(z + 1, segments);

            float hL = m_terrainHeights[z * vertsPerRow + xm];
            float hR = m_terrainHeights[z * vertsPerRow + xp];
            float hD = m_terrainHeights[zm * vertsPerRow + x];
            float hU = m_terrainHeights[zp * vertsPerRow + x];

            float dx = (hR - hL) / (2.0f * step);
            float dz = (hU - hD) / (2.0f * step);

            Vec3 n = Vec3(-dx, 1.0f, -dz).normalized();
            normals[i*3+0] = n.x;
            normals[i*3+1] = n.y;
            normals[i*3+2] = n.z;
        }
    }

    // Interleave: pos3 + normal3 + height1
    std::vector<float> interleaved;
    interleaved.reserve(vertexCount * 7);
    for (int i = 0; i < vertexCount; i++) {
        int ox = i * 3;
        interleaved.push_back(vertices[ox+0]);
        interleaved.push_back(vertices[ox+1]);
        interleaved.push_back(vertices[ox+2]);
        interleaved.push_back(normals[ox+0]);
        interleaved.push_back(normals[ox+1]);
        interleaved.push_back(normals[ox+2]);
        interleaved.push_back(m_terrainHeights[i]);
    }

    // Build indices
    std::vector<unsigned int> indices;
    indices.reserve(indexCount);
    for (int z = 0; z < segments; z++) {
        for (int x = 0; x < segments; x++) {
            int tl = z * vertsPerRow + x;
            int tr = tl + 1;
            int bl = (z + 1) * vertsPerRow + x;
            int br = bl + 1;

            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    m_terrainVertexCount = vertexCount;
    m_terrainIndexCount = (unsigned int)indices.size();

    glGenVertexArrays(1, &m_terrainVAO);
    glGenBuffers(1, &m_terrainVBO);
    glGenBuffers(1, &m_terrainEBO);

    glBindVertexArray(m_terrainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // height
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void Renderer::drawSkybox(const Vec3& cameraPosition) {
    glDepthFunc(GL_LEQUAL);
    glUseProgram(m_skyboxShader);

    float skyView[16];
    for (int i = 0; i < 16; i++) skyView[i] = m_view[i];
    skyView[12] = skyView[13] = skyView[14] = 0.0f;

    int projLoc = glGetUniformLocation(m_skyboxShader, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, m_projection);
    int viewLoc = glGetUniformLocation(m_skyboxShader, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, skyView);

    float t = (float)glfwGetTime();
    int timeLoc = glGetUniformLocation(m_skyboxShader, "uTime");
    glUniform1f(timeLoc, t);

    glBindVertexArray(m_skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void Renderer::drawTerrain(const Vec3& cameraPosition) {
    if (!m_terrainVAO) return;

    glUseProgram(m_terrainShader);

    int projLoc = glGetUniformLocation(m_terrainShader, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, m_projection);
    int viewLoc = glGetUniformLocation(m_terrainShader, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, m_view);

    float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    int modelLoc = glGetUniformLocation(m_terrainShader, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, identity);

    Vec3 lightDir(-0.5f, 0.8f, -0.3f);
    lightDir = lightDir.normalized();
    int lightLoc = glGetUniformLocation(m_terrainShader, "uLightDir");
    glUniform3f(lightLoc, lightDir.x, lightDir.y, lightDir.z);

    int camLoc = glGetUniformLocation(m_terrainShader, "uCamPos");
    glUniform3f(camLoc, cameraPosition.x, cameraPosition.y, cameraPosition.z);

    glBindVertexArray(m_terrainVAO);
    glDrawElements(GL_TRIANGLES, m_terrainIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Renderer::beginFrame() {
    float currentTime = (float)glfwGetTime();
    m_deltaTime = currentTime - m_lastFrame;
    m_lastFrame = currentTime;
    m_frameCount++;

    glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(m_shaderProgram);
}

void Renderer::endFrame() {
    glBindVertexArray(0);
}

void Renderer::setCamera(const Vec3& position, const Vec3& target) {
    mat4LookAt(m_view, position, target, {0.0f, 1.0f, 0.0f});
    m_viewPos[0] = position.x;
    m_viewPos[1] = position.y;
    m_viewPos[2] = position.z;

    int viewLoc = glGetUniformLocation(m_shaderProgram, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, m_view);
}

void Renderer::drawCube(const Vec3& position, const Vec3& scale, const Color& color) {
    float model[16], tmp[16];
    mat4Translate(model, position);
    mat4Scale(tmp, scale);
    mat4Mul(model, model, tmp);

    int modelLoc = glGetUniformLocation(m_shaderProgram, "model");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model);

    glBindVertexArray(m_cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void Renderer::drawSphere(const Vec3& position, float radius, const Color& color) {
    drawCube(position, {radius, radius, radius}, color);
}

void Renderer::setCubeProgram() {
    glUseProgram(m_shaderProgram);

    int projLoc = glGetUniformLocation(m_shaderProgram, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, m_projection);
    int viewLoc = glGetUniformLocation(m_shaderProgram, "view");
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, m_view);
}

void Renderer::setViewport(int width, int height) {
    glViewport(0, 0, width, height);
    float aspect = (float)width / (float)height;
    mat4Perspective(m_projection, 45.0f * 3.14159f / 180.0f, aspect, 0.1f, 1000.0f);

    glUseProgram(m_shaderProgram);
    int projLoc = glGetUniformLocation(m_shaderProgram, "projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, m_projection);
}
