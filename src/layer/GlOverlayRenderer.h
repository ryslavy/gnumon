#pragma once

#include "HudVertexGenerator.h"
#include <dlfcn.h>
#include <mutex>
#include <vector>

namespace gnumon::layer {

// Minimal OpenGL types and constants without requiring external GL headers
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long GLulong;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_TRIANGLES                      0x0004
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_CULL_FACE                      0x0B44
#define GL_TEXTURE_2D                     0x0DE1
#define GL_VIEWPORT                       0x0BA2
#define GL_SCISSOR_BOX                    0x0C10
#define GL_BLEND_DST_ALPHA                0x80CA
#define GL_BLEND_DST_RGB                  0x80C8
#define GL_BLEND_SRC_ALPHA                0x80CB
#define GL_BLEND_SRC_RGB                  0x80C9
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_ONE                            1
#define GL_RGBA                           0x1908
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_FLOAT                          0x1406
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_TEXTURE_BINDING_2D             0x8069
#define GL_ARRAY_BUFFER                   0x8892
#define GL_STREAM_DRAW                    0x88E0
#define GL_STATIC_DRAW                    0x88E4
#define GL_CURRENT_PROGRAM                0x8B8D
#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_ARRAY_BUFFER_BINDING           0x8894
#define GL_VERTEX_ARRAY_BINDING           0x85B5
#define GL_ACTIVE_TEXTURE                 0x84E0
#define GL_TEXTURE0                       0x84C0
#define GL_COLOR_WRITEMASK                0x0C13
#define GL_DEPTH_WRITEMASK                0x0982
#define GL_POLYGON_MODE                   0x0B40
#define GL_FRONT_AND_BACK                 0x0408
#define GL_FILL                           0x1B02
#define GL_BLEND_EQUATION_RGB             0x8009
#define GL_BLEND_EQUATION_ALPHA           0x883D
#define GL_ALL_ATTRIB_BITS                0x000FFFFF
#define GL_CLIENT_ALL_ATTRIB_BITS         0xFFFFFFFF

class GlOverlayRenderer : public HudVertexGenerator {
public:
    GlOverlayRenderer() = default;
    ~GlOverlayRenderer() override {
        Cleanup();
    }

    bool Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) return true;

        LoadProcs();

        // Prepare RGBA font texture from bitmap
        std::vector<uint8_t> rgba(FONT_TEX_W * FONT_TEX_H * 4);
        for (size_t i = 0; i < FONT_TEX_W * FONT_TEX_H; ++i) {
            uint8_t a = font_atlas_bitmap[i];
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = a;
        }

        if (p_glGenTextures && p_glBindTexture && p_glTexImage2D && p_glTexParameteri) {
            p_glGenTextures(1, &fontTexture_);
            GLint lastTex = 0;
            GLint lastPixelUnpackBuf = 0;
            GLint lastUnpackAlign = 4;
            GLint lastUnpackRowLen = 0;
            if (p_glGetIntegerv) {
                p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);
                p_glGetIntegerv(0x88EC /* GL_PIXEL_UNPACK_BUFFER_BINDING */, &lastPixelUnpackBuf);
                p_glGetIntegerv(0x0CF5 /* GL_UNPACK_ALIGNMENT */, &lastUnpackAlign);
                p_glGetIntegerv(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, &lastUnpackRowLen);
            }
            if (p_glBindBuffer && lastPixelUnpackBuf != 0) {
                p_glBindBuffer(0x88EC /* GL_PIXEL_UNPACK_BUFFER */, 0);
            }
            if (p_glPixelStorei) {
                p_glPixelStorei(0x0CF5 /* GL_UNPACK_ALIGNMENT */, 1);
                p_glPixelStorei(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, 0);
            }

            p_glBindTexture(GL_TEXTURE_2D, fontTexture_);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FONT_TEX_W, FONT_TEX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

            if (p_glPixelStorei) {
                p_glPixelStorei(0x0CF5, lastUnpackAlign);
                p_glPixelStorei(0x0CF2, lastUnpackRowLen);
            }
            if (p_glBindBuffer && lastPixelUnpackBuf != 0) {
                p_glBindBuffer(0x88EC, lastPixelUnpackBuf);
            }
            p_glBindTexture(GL_TEXTURE_2D, lastTex);
        }

        // Compile modern shader program if modern functions available
        if (p_glCreateShader && p_glShaderSource && p_glCompileShader && p_glCreateProgram &&
            p_glAttachShader && p_glLinkProgram && p_glGenBuffers) {
            const char* vsSource =
                "#version 120\n"
                "attribute vec2 in_pos;\n"
                "attribute vec2 in_uv;\n"
                "attribute vec4 in_col;\n"
                "uniform vec2 u_screen;\n"
                "varying vec2 v_uv;\n"
                "varying vec4 v_col;\n"
                "void main() {\n"
                "    v_uv = in_uv;\n"
                "    v_col = in_col;\n"
                "    vec2 norm = (in_pos / u_screen) * 2.0 - 1.0;\n"
                "    gl_Position = vec4(norm.x, -norm.y, 0.0, 1.0);\n"
                "}\n";

            const char* fsSource =
                "#version 120\n"
                "varying vec2 v_uv;\n"
                "varying vec4 v_col;\n"
                "uniform sampler2D u_tex;\n"
                "void main() {\n"
                "    if (v_uv.x < 0.0) {\n"
                "        gl_FragColor = v_col;\n"
                "    } else {\n"
                "        vec4 t = texture2D(u_tex, v_uv);\n"
                "        gl_FragColor = vec4(v_col.rgb, v_col.a * t.a);\n"
                "    }\n"
                "}\n";

            GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
            p_glShaderSource(vs, 1, &vsSource, nullptr);
            p_glCompileShader(vs);

            GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
            p_glShaderSource(fs, 1, &fsSource, nullptr);
            p_glCompileShader(fs);

            shaderProgram_ = p_glCreateProgram();
            p_glAttachShader(shaderProgram_, vs);
            p_glAttachShader(shaderProgram_, fs);
            p_glLinkProgram(shaderProgram_);

            locPos_ = p_glGetAttribLocation(shaderProgram_, "in_pos");
            locUv_ = p_glGetAttribLocation(shaderProgram_, "in_uv");
            locCol_ = p_glGetAttribLocation(shaderProgram_, "in_col");
            locScreen_ = p_glGetUniformLocation(shaderProgram_, "u_screen");
            locTex_ = p_glGetUniformLocation(shaderProgram_, "u_tex");

            p_glDeleteShader(vs);
            p_glDeleteShader(fs);

            p_glGenBuffers(1, &vbo_);
            if (p_glGenVertexArrays) {
                p_glGenVertexArrays(1, &vao_);
            }
        }

        initialized_ = true;
        return true;
    }

    void Cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) return;
        if (fontTexture_ && p_glDeleteTextures) {
            p_glDeleteTextures(1, &fontTexture_);
            fontTexture_ = 0;
        }
        if (vbo_ && p_glDeleteBuffers) {
            p_glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (vao_ && p_glDeleteVertexArrays) {
            p_glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (shaderProgram_ && p_glDeleteProgram) {
            p_glDeleteProgram(shaderProgram_);
            shaderProgram_ = 0;
        }
        initialized_ = false;
    }

    void Render(int screenW, int screenH, int corner,
                double presentFps, double displayedFps, double fps1PercentLow,
                double frameTimeMs, double latencyMs, double animErrorMs,
                bool isRecording,
                const ipc::TelemetrySnapshot* telem = nullptr,
                bool hudVisible = true)
    {
        if (screenW <= 0 || screenH <= 0) return;
        if (!Initialize()) return;

        std::vector<OverlayVertex> verts;
        GenerateHudVertices(verts, static_cast<uint32_t>(screenW), static_cast<uint32_t>(screenH),
                            corner, presentFps, displayedFps, fps1PercentLow,
                            frameTimeMs, latencyMs, animErrorMs, isRecording, telem, hudVisible);

        if (verts.empty()) return;

        // Save full OpenGL state
        GLint lastProgram = 0, lastTexture = 0, lastVbo = 0, lastVao = 0;
        GLint lastActiveTexture = GL_TEXTURE0;
        GLint lastViewport[4]{0, 0, 0, 0};
        GLint lastScissorBox[4]{0, 0, 0, 0};
        GLint lastPolygonMode[2]{GL_FILL, GL_FILL};
        GLint lastBlendSrcRgb = GL_SRC_ALPHA, lastBlendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
        GLint lastBlendSrcAlpha = GL_ONE, lastBlendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
        GLint lastBlendEqRgb = 0x8006 /* GL_FUNC_ADD */, lastBlendEqAlpha = 0x8006;
        GLboolean lastBlend = GL_FALSE, lastCull = GL_FALSE, lastDepth = GL_FALSE, lastScissor = GL_FALSE;
        GLboolean lastColorMask[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean lastDepthMask = GL_TRUE;

        if (p_glPushAttrib) p_glPushAttrib(GL_ALL_ATTRIB_BITS);
        if (p_glPushClientAttrib) p_glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

        if (p_glGetIntegerv) {
            p_glGetIntegerv(GL_VIEWPORT, lastViewport);
            p_glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);
            p_glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
            p_glGetIntegerv(GL_ACTIVE_TEXTURE, &lastActiveTexture);
            p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
            p_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastVbo);
            p_glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrcRgb);
            p_glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDstRgb);
            p_glGetIntegerv(GL_BLEND_SRC_ALPHA, &lastBlendSrcAlpha);
            p_glGetIntegerv(GL_BLEND_DST_ALPHA, &lastBlendDstAlpha);
            p_glGetIntegerv(GL_BLEND_EQUATION_RGB, &lastBlendEqRgb);
            p_glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &lastBlendEqAlpha);
            p_glGetIntegerv(GL_POLYGON_MODE, lastPolygonMode);
            if (p_glGenVertexArrays) p_glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVao);
        }
        if (p_glGetBooleanv) {
            p_glGetBooleanv(GL_COLOR_WRITEMASK, lastColorMask);
            p_glGetBooleanv(GL_DEPTH_WRITEMASK, &lastDepthMask);
        }
        if (p_glIsEnabled) {
            lastBlend = p_glIsEnabled(GL_BLEND);
            lastCull = p_glIsEnabled(GL_CULL_FACE);
            lastDepth = p_glIsEnabled(GL_DEPTH_TEST);
            lastScissor = p_glIsEnabled(GL_SCISSOR_TEST);
        }

        // Set Render state for HUD
        if (p_glActiveTexture) p_glActiveTexture(GL_TEXTURE0);
        if (p_glEnable) p_glEnable(GL_BLEND);
        if (p_glBlendFuncSeparate) {
            p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else if (p_glBlendFunc) {
            p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        if (p_glColorMask) p_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (p_glDepthMask) p_glDepthMask(GL_FALSE);
        if (p_glPolygonMode) p_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (p_glDisable) {
            p_glDisable(GL_CULL_FACE);
            p_glDisable(GL_DEPTH_TEST);
            p_glDisable(GL_SCISSOR_TEST);
        }
        if (p_glViewport) p_glViewport(0, 0, screenW, screenH);

        // Bind Texture
        if (p_glBindTexture) p_glBindTexture(GL_TEXTURE_2D, fontTexture_);

        // Render via Shader Pipeline
        if (shaderProgram_ && vbo_) {
            if (vao_ && p_glBindVertexArray) p_glBindVertexArray(vao_);
            p_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            p_glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(OverlayVertex), verts.data(), GL_STREAM_DRAW);

            p_glUseProgram(shaderProgram_);
            if (locScreen_ >= 0 && p_glUniform2f) {
                p_glUniform2f(locScreen_, static_cast<float>(screenW), static_cast<float>(screenH));
            }
            if (locTex_ >= 0 && p_glUniform1i) {
                p_glUniform1i(locTex_, 0);
            }

            if (locPos_ >= 0 && p_glEnableVertexAttribArray && p_glVertexAttribPointer) {
                p_glEnableVertexAttribArray(locPos_);
                p_glVertexAttribPointer(locPos_, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, x)));
            }
            if (locUv_ >= 0 && p_glEnableVertexAttribArray && p_glVertexAttribPointer) {
                p_glEnableVertexAttribArray(locUv_);
                p_glVertexAttribPointer(locUv_, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, u)));
            }
            if (locCol_ >= 0 && p_glEnableVertexAttribArray && p_glVertexAttribPointer) {
                p_glEnableVertexAttribArray(locCol_);
                p_glVertexAttribPointer(locCol_, 4, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, r)));
            }

            if (p_glDrawArrays) p_glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

            if (locPos_ >= 0 && p_glDisableVertexAttribArray) p_glDisableVertexAttribArray(locPos_);
            if (locUv_ >= 0 && p_glDisableVertexAttribArray) p_glDisableVertexAttribArray(locUv_);
            if (locCol_ >= 0 && p_glDisableVertexAttribArray) p_glDisableVertexAttribArray(locCol_);
        }

        // Restore State
        if (p_glBindTexture) p_glBindTexture(GL_TEXTURE_2D, lastTexture);
        if (p_glActiveTexture) p_glActiveTexture(lastActiveTexture);
        if (p_glBindBuffer) p_glBindBuffer(GL_ARRAY_BUFFER, lastVbo);
        if (vao_ && p_glBindVertexArray) p_glBindVertexArray(lastVao);
        if (p_glUseProgram) p_glUseProgram(lastProgram);
        if (p_glViewport) p_glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        if (p_glScissor) p_glScissor(lastScissorBox[0], lastScissorBox[1], lastScissorBox[2], lastScissorBox[3]);
        if (p_glPolygonMode) p_glPolygonMode(GL_FRONT_AND_BACK, lastPolygonMode[0]);
        if (p_glColorMask) p_glColorMask(lastColorMask[0], lastColorMask[1], lastColorMask[2], lastColorMask[3]);
        if (p_glDepthMask) p_glDepthMask(lastDepthMask);

        if (p_glBlendFuncSeparate) {
            p_glBlendFuncSeparate(lastBlendSrcRgb, lastBlendDstRgb, lastBlendSrcAlpha, lastBlendDstAlpha);
        } else if (p_glBlendFunc) {
            p_glBlendFunc(lastBlendSrcRgb, lastBlendDstRgb);
        }
        if (p_glBlendEquationSeparate) {
            p_glBlendEquationSeparate(lastBlendEqRgb, lastBlendEqAlpha);
        }

        if (p_glEnable && p_glDisable) {
            if (lastBlend) p_glEnable(GL_BLEND); else p_glDisable(GL_BLEND);
            if (lastCull) p_glEnable(GL_CULL_FACE); else p_glDisable(GL_CULL_FACE);
            if (lastDepth) p_glEnable(GL_DEPTH_TEST); else p_glDisable(GL_DEPTH_TEST);
            if (lastScissor) p_glEnable(GL_SCISSOR_TEST); else p_glDisable(GL_SCISSOR_TEST);
        }

        if (p_glPopClientAttrib) p_glPopClientAttrib();
        if (p_glPopAttrib) p_glPopAttrib();
    }

private:
    void LoadProcs() {
        p_glGetIntegerv = reinterpret_cast<PFN_glGetIntegerv>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
        p_glGetBooleanv = reinterpret_cast<PFN_glGetBooleanv>(dlsym(RTLD_DEFAULT, "glGetBooleanv"));
        p_glViewport = reinterpret_cast<PFN_glViewport>(dlsym(RTLD_DEFAULT, "glViewport"));
        p_glScissor = reinterpret_cast<PFN_glScissor>(dlsym(RTLD_DEFAULT, "glScissor"));
        p_glEnable = reinterpret_cast<PFN_glEnable>(dlsym(RTLD_DEFAULT, "glEnable"));
        p_glDisable = reinterpret_cast<PFN_glDisable>(dlsym(RTLD_DEFAULT, "glDisable"));
        p_glIsEnabled = reinterpret_cast<PFN_glIsEnabled>(dlsym(RTLD_DEFAULT, "glIsEnabled"));
        p_glBlendFunc = reinterpret_cast<PFN_glBlendFunc>(dlsym(RTLD_DEFAULT, "glBlendFunc"));
        p_glBlendFuncSeparate = reinterpret_cast<PFN_glBlendFuncSeparate>(dlsym(RTLD_DEFAULT, "glBlendFuncSeparate"));
        p_glBlendEquationSeparate = reinterpret_cast<PFN_glBlendEquationSeparate>(dlsym(RTLD_DEFAULT, "glBlendEquationSeparate"));
        p_glBindTexture = reinterpret_cast<PFN_glBindTexture>(dlsym(RTLD_DEFAULT, "glBindTexture"));
        p_glActiveTexture = reinterpret_cast<PFN_glActiveTexture>(dlsym(RTLD_DEFAULT, "glActiveTexture"));
        p_glColorMask = reinterpret_cast<PFN_glColorMask>(dlsym(RTLD_DEFAULT, "glColorMask"));
        p_glDepthMask = reinterpret_cast<PFN_glDepthMask>(dlsym(RTLD_DEFAULT, "glDepthMask"));
        p_glPolygonMode = reinterpret_cast<PFN_glPolygonMode>(dlsym(RTLD_DEFAULT, "glPolygonMode"));
        p_glPushAttrib = reinterpret_cast<PFN_glPushAttrib>(dlsym(RTLD_DEFAULT, "glPushAttrib"));
        p_glPopAttrib = reinterpret_cast<PFN_glPopAttrib>(dlsym(RTLD_DEFAULT, "glPopAttrib"));
        p_glPushClientAttrib = reinterpret_cast<PFN_glPushClientAttrib>(dlsym(RTLD_DEFAULT, "glPushClientAttrib"));
        p_glPopClientAttrib = reinterpret_cast<PFN_glPopClientAttrib>(dlsym(RTLD_DEFAULT, "glPopClientAttrib"));
        p_glGenTextures = reinterpret_cast<PFN_glGenTextures>(dlsym(RTLD_DEFAULT, "glGenTextures"));
        p_glDeleteTextures = reinterpret_cast<PFN_glDeleteTextures>(dlsym(RTLD_DEFAULT, "glDeleteTextures"));
        p_glTexParameteri = reinterpret_cast<PFN_glTexParameteri>(dlsym(RTLD_DEFAULT, "glTexParameteri"));
        p_glTexImage2D = reinterpret_cast<PFN_glTexImage2D>(dlsym(RTLD_DEFAULT, "glTexImage2D"));
        p_glPixelStorei = reinterpret_cast<PFN_glPixelStorei>(dlsym(RTLD_DEFAULT, "glPixelStorei"));
        p_glDrawArrays = reinterpret_cast<PFN_glDrawArrays>(dlsym(RTLD_DEFAULT, "glDrawArrays"));

        // Shaders & VBO
        p_glCreateShader = reinterpret_cast<PFN_glCreateShader>(dlsym(RTLD_DEFAULT, "glCreateShader"));
        p_glShaderSource = reinterpret_cast<PFN_glShaderSource>(dlsym(RTLD_DEFAULT, "glShaderSource"));
        p_glCompileShader = reinterpret_cast<PFN_glCompileShader>(dlsym(RTLD_DEFAULT, "glCompileShader"));
        p_glDeleteShader = reinterpret_cast<PFN_glDeleteShader>(dlsym(RTLD_DEFAULT, "glDeleteShader"));
        p_glCreateProgram = reinterpret_cast<PFN_glCreateProgram>(dlsym(RTLD_DEFAULT, "glCreateProgram"));
        p_glAttachShader = reinterpret_cast<PFN_glAttachShader>(dlsym(RTLD_DEFAULT, "glAttachShader"));
        p_glLinkProgram = reinterpret_cast<PFN_glLinkProgram>(dlsym(RTLD_DEFAULT, "glLinkProgram"));
        p_glUseProgram = reinterpret_cast<PFN_glUseProgram>(dlsym(RTLD_DEFAULT, "glUseProgram"));
        p_glDeleteProgram = reinterpret_cast<PFN_glDeleteProgram>(dlsym(RTLD_DEFAULT, "glDeleteProgram"));
        p_glGetAttribLocation = reinterpret_cast<PFN_glGetAttribLocation>(dlsym(RTLD_DEFAULT, "glGetAttribLocation"));
        p_glGetUniformLocation = reinterpret_cast<PFN_glGetUniformLocation>(dlsym(RTLD_DEFAULT, "glGetUniformLocation"));
        p_glUniform2f = reinterpret_cast<PFN_glUniform2f>(dlsym(RTLD_DEFAULT, "glUniform2f"));
        p_glUniform1i = reinterpret_cast<PFN_glUniform1i>(dlsym(RTLD_DEFAULT, "glUniform1i"));
        p_glGenBuffers = reinterpret_cast<PFN_glGenBuffers>(dlsym(RTLD_DEFAULT, "glGenBuffers"));
        p_glBindBuffer = reinterpret_cast<PFN_glBindBuffer>(dlsym(RTLD_DEFAULT, "glBindBuffer"));
        p_glBufferData = reinterpret_cast<PFN_glBufferData>(dlsym(RTLD_DEFAULT, "glBufferData"));
        p_glDeleteBuffers = reinterpret_cast<PFN_glDeleteBuffers>(dlsym(RTLD_DEFAULT, "glDeleteBuffers"));
        p_glEnableVertexAttribArray = reinterpret_cast<PFN_glEnableVertexAttribArray>(dlsym(RTLD_DEFAULT, "glEnableVertexAttribArray"));
        p_glDisableVertexAttribArray = reinterpret_cast<PFN_glDisableVertexAttribArray>(dlsym(RTLD_DEFAULT, "glDisableVertexAttribArray"));
        p_glVertexAttribPointer = reinterpret_cast<PFN_glVertexAttribPointer>(dlsym(RTLD_DEFAULT, "glVertexAttribPointer"));
        p_glGenVertexArrays = reinterpret_cast<PFN_glGenVertexArrays>(dlsym(RTLD_DEFAULT, "glGenVertexArrays"));
        p_glBindVertexArray = reinterpret_cast<PFN_glBindVertexArray>(dlsym(RTLD_DEFAULT, "glBindVertexArray"));
        p_glDeleteVertexArrays = reinterpret_cast<PFN_glDeleteVertexArrays>(dlsym(RTLD_DEFAULT, "glDeleteVertexArrays"));
    }

    typedef void (*PFN_glGetIntegerv)(GLenum pname, GLint *data);
    typedef void (*PFN_glGetBooleanv)(GLenum pname, GLboolean *data);
    typedef void (*PFN_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
    typedef void (*PFN_glScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
    typedef void (*PFN_glEnable)(GLenum cap);
    typedef void (*PFN_glDisable)(GLenum cap);
    typedef GLboolean (*PFN_glIsEnabled)(GLenum cap);
    typedef void (*PFN_glBlendFunc)(GLenum sfactor, GLenum dfactor);
    typedef void (*PFN_glBlendFuncSeparate)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
    typedef void (*PFN_glBlendEquationSeparate)(GLenum modeRGB, GLenum modeAlpha);
    typedef void (*PFN_glBindTexture)(GLenum target, GLuint texture);
    typedef void (*PFN_glActiveTexture)(GLenum texture);
    typedef void (*PFN_glColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
    typedef void (*PFN_glDepthMask)(GLboolean flag);
    typedef void (*PFN_glPolygonMode)(GLenum face, GLenum mode);
    typedef void (*PFN_glPushAttrib)(GLbitfield mask);
    typedef void (*PFN_glPopAttrib)();
    typedef void (*PFN_glPushClientAttrib)(GLbitfield mask);
    typedef void (*PFN_glPopClientAttrib)();
    typedef void (*PFN_glGenTextures)(GLsizei n, GLuint *textures);
    typedef void (*PFN_glDeleteTextures)(GLsizei n, const GLuint *textures);
    typedef void (*PFN_glTexParameteri)(GLenum target, GLenum pname, GLint param);
    typedef void (*PFN_glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
    typedef void (*PFN_glDrawArrays)(GLenum mode, GLint first, GLsizei count);

    typedef GLuint (*PFN_glCreateShader)(GLenum type);
    typedef void (*PFN_glShaderSource)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
    typedef void (*PFN_glCompileShader)(GLuint shader);
    typedef void (*PFN_glDeleteShader)(GLuint shader);
    typedef GLuint (*PFN_glCreateProgram)(void);
    typedef void (*PFN_glAttachShader)(GLuint program, GLuint shader);
    typedef void (*PFN_glLinkProgram)(GLuint program);
    typedef void (*PFN_glUseProgram)(GLuint program);
    typedef void (*PFN_glDeleteProgram)(GLuint program);
    typedef GLint (*PFN_glGetAttribLocation)(GLuint program, const GLchar *name);
    typedef GLint (*PFN_glGetUniformLocation)(GLuint program, const GLchar *name);
    typedef void (*PFN_glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
    typedef void (*PFN_glUniform1i)(GLint location, GLint v0);
    typedef void (*PFN_glGenBuffers)(GLsizei n, GLuint *buffers);
    typedef void (*PFN_glBindBuffer)(GLenum target, GLuint buffer);
    typedef void (*PFN_glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    typedef void (*PFN_glDeleteBuffers)(GLsizei n, const GLuint *buffers);
    typedef void (*PFN_glEnableVertexAttribArray)(GLuint index);
    typedef void (*PFN_glDisableVertexAttribArray)(GLuint index);
    typedef void (*PFN_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
    typedef void (*PFN_glGenVertexArrays)(GLsizei n, GLuint *arrays);
    typedef void (*PFN_glBindVertexArray)(GLuint array);
    typedef void (*PFN_glDeleteVertexArrays)(GLsizei n, const GLuint *arrays);

    PFN_glGetIntegerv p_glGetIntegerv = nullptr;
    PFN_glGetBooleanv p_glGetBooleanv = nullptr;
    PFN_glViewport p_glViewport = nullptr;
    PFN_glScissor p_glScissor = nullptr;
    PFN_glEnable p_glEnable = nullptr;
    PFN_glDisable p_glDisable = nullptr;
    PFN_glIsEnabled p_glIsEnabled = nullptr;
    PFN_glBlendFunc p_glBlendFunc = nullptr;
    PFN_glBlendFuncSeparate p_glBlendFuncSeparate = nullptr;
    PFN_glBlendEquationSeparate p_glBlendEquationSeparate = nullptr;
    PFN_glBindTexture p_glBindTexture = nullptr;
    PFN_glActiveTexture p_glActiveTexture = nullptr;
    PFN_glColorMask p_glColorMask = nullptr;
    PFN_glDepthMask p_glDepthMask = nullptr;
    PFN_glPolygonMode p_glPolygonMode = nullptr;
    PFN_glPushAttrib p_glPushAttrib = nullptr;
    PFN_glPopAttrib p_glPopAttrib = nullptr;
    PFN_glPushClientAttrib p_glPushClientAttrib = nullptr;
    PFN_glPopClientAttrib p_glPopClientAttrib = nullptr;
    PFN_glGenTextures p_glGenTextures = nullptr;
    PFN_glDeleteTextures p_glDeleteTextures = nullptr;
    PFN_glTexParameteri p_glTexParameteri = nullptr;
    PFN_glTexImage2D p_glTexImage2D = nullptr;
    typedef void (*PFN_glPixelStorei)(GLenum pname, GLint param);
    PFN_glPixelStorei p_glPixelStorei = nullptr;
    PFN_glDrawArrays p_glDrawArrays = nullptr;

    PFN_glCreateShader p_glCreateShader = nullptr;
    PFN_glShaderSource p_glShaderSource = nullptr;
    PFN_glCompileShader p_glCompileShader = nullptr;
    PFN_glDeleteShader p_glDeleteShader = nullptr;
    PFN_glCreateProgram p_glCreateProgram = nullptr;
    PFN_glAttachShader p_glAttachShader = nullptr;
    PFN_glLinkProgram p_glLinkProgram = nullptr;
    PFN_glUseProgram p_glUseProgram = nullptr;
    PFN_glDeleteProgram p_glDeleteProgram = nullptr;
    PFN_glGetAttribLocation p_glGetAttribLocation = nullptr;
    PFN_glGetUniformLocation p_glGetUniformLocation = nullptr;
    PFN_glUniform2f p_glUniform2f = nullptr;
    PFN_glUniform1i p_glUniform1i = nullptr;
    PFN_glGenBuffers p_glGenBuffers = nullptr;
    PFN_glBindBuffer p_glBindBuffer = nullptr;
    PFN_glBufferData p_glBufferData = nullptr;
    PFN_glDeleteBuffers p_glDeleteBuffers = nullptr;
    PFN_glEnableVertexAttribArray p_glEnableVertexAttribArray = nullptr;
    PFN_glDisableVertexAttribArray p_glDisableVertexAttribArray = nullptr;
    PFN_glVertexAttribPointer p_glVertexAttribPointer = nullptr;
    PFN_glGenVertexArrays p_glGenVertexArrays = nullptr;
    PFN_glBindVertexArray p_glBindVertexArray = nullptr;
    PFN_glDeleteVertexArrays p_glDeleteVertexArrays = nullptr;

    std::mutex mutex_;
    bool initialized_ = false;
    GLuint fontTexture_ = 0;
    GLuint shaderProgram_ = 0;
    GLuint vbo_ = 0;
    GLuint vao_ = 0;
    GLint locPos_ = -1;
    GLint locUv_ = -1;
    GLint locCol_ = -1;
    GLint locScreen_ = -1;
    GLint locTex_ = -1;
};

} // namespace gnumon::layer
