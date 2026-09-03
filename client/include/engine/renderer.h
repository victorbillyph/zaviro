#pragma once

#include "math/vec3.h"
#include <cstdint>
#include <string>
#include <vector>

class World;

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize(int width, int height);
    void shutdown();

    void beginFrame();
    void endFrame();

    void setCamera(const Vec3& position, const Vec3& target);
    void drawCube(const Vec3& position, const Vec3& scale, const Color& color);
    void drawSphere(const Vec3& position, float radius, const Color& color);
    void setCubeProgram();

    void setViewport(int width, int height);

    // UI system (2D orthographic overlay)
    void uiBegin();
    void uiEnd();
    void drawRect(float x, float y, float w, float h, const Color& color);
    void drawText(const std::string& text, float x, float y, float scale, const Color& color);
    void setUiResolution(int width, int height);
    int getUiWidth() const { return m_uiWidth; }
    int getUiHeight() const { return m_uiHeight; }

    // Skybox
    void drawSkybox(const Vec3& cameraPosition);

    // Terrain
    void buildTerrain(float size, int segments, unsigned int seed);
    void drawTerrain(const Vec3& cameraPosition);
    float terrainHeightAt(float x, float z) const;
    float getTerrainSize() const { return m_terrainSize; }

    uint64_t getFrameCount() const { return m_frameCount; }
    float getDeltaTime() const { return m_deltaTime; }

private:
    void initShaders();
    void initMeshes();
    void initSkybox();
    void initUI();
    void initFont();
    float noise2D(float x, float z, unsigned int seed) const;

    uint64_t m_frameCount = 0;
    float m_deltaTime = 0.0f;
    float m_lastFrame = 0.0f;

    // Cube rendering
    unsigned int m_shaderProgram = 0;
    unsigned int m_cubeVAO = 0, m_cubeVBO = 0;

    // Skybox (procedural sky gradient)
    unsigned int m_skyboxShader = 0;
    unsigned int m_skyboxVAO = 0, m_skyboxVBO = 0;

    // UI (2D overlay)
    unsigned int m_uiShader = 0;
    unsigned int m_uiVAO = 0, m_uiVBO = 0;
    int m_uiWidth = 1280, m_uiHeight = 720;

    // Font texture atlas
    unsigned int m_fontTexture = 0;
    unsigned int m_fontGlyphW = 8, m_fontGlyphH = 8;
    unsigned int m_fontCols = 16;

    // Terrain
    unsigned int m_terrainShader = 0;
    unsigned int m_terrainVAO = 0, m_terrainVBO = 0, m_terrainEBO = 0;
    unsigned int m_terrainVertexCount = 0;
    unsigned int m_terrainIndexCount = 0;
    float m_terrainSize = 100.0f;
    int m_terrainSegments = 128;
    unsigned int m_terrainSeed = 1337;
    float* m_terrainHeights = nullptr;

    // Current view position (for skybox centering)
    float m_viewPos[3] = {0, 0, 0};

    float m_projection[16] = {};
    float m_view[16] = {};
};
