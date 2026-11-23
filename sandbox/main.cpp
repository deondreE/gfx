#include "gfx.hpp"
using namespace gfx;
#include <iostream>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

class SandboxApp : public Application {
public:
    SandboxApp() : Application(L"Test", 1280, 720) {}
protected:
    void on_init() override {
        std::cout << "Engine Systems Initialized." << std::endl;
        std::cout << "Native HWND: " << get_window().GetNatvieWindowHandle() << std::endl;

        _gfx_context = &GetGraphicsContext();
        _renderer = std::make_unique<Renderer>(*_gfx_context);

        _shader_file_path = L"sandbox_shaders.hlsl";
        std::ofstream out_shader_file("sandbox_shaders.hlsl");
        out_shader_file << R"HLSL(
            struct VSInput {
                float3 position : POSITION;
                float2 uv : TEXCOORD;
            };

            struct PSInput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD;
            };

            // Root constants: 16 floats + 1 uint = 17 DWORDs (must match the C++ DrawConstants).
            cbuffer DrawConstants : register(b0) {
                row_major float4x4 g_transform;
                uint g_texture_index;          // 0xFFFFFFFF means "no texture"
            };

            Texture2D g_textures[1024] : register(t0);
            SamplerState g_sampler : register(s0);

            PSInput VSMain(VSInput input) {
                PSInput output;
                output.position = mul(float4(input.position, 1.0f), g_transform);
                output.uv = input.uv;
                return output;
            }

            float4 PSMain(PSInput input) : SV_TARGET {
                if (g_texture_index == 0xFFFFFFFFu) {
                    return float4(input.uv.x, input.uv.y, 0.75f, 1.0f); // untextured fallback
                }
                return g_textures[NonUniformResourceIndex(g_texture_index)].SampleLevel(g_sampler, input.uv, 0);
            }
        )HLSL";
        out_shader_file.close();

        std::vector<Vertex> vertices = {
            {{ -0.5f,  0.5f,  0.0f}, {0.0f, 0.0f}},
            {{  0.5f,  0.5f,  0.0f}, {1.0f, 0.0f}},
            {{  0.5f, -0.5f,  0.0f}, {1.0f, 1.0f}},
            {{ -0.5f, -0.5f,  0.0f}, {0.0f, 1.0f}},
        };

        std::vector<u32> indices = {
            0, 1, 2,
            2, 3, 0,
        };

        _quad_mesh = _renderer->create_mesh(vertices, indices);
        _quad_material = _renderer->create_material(_shader_file_path);

        _font = _renderer->load_font(L"../gfx/assets/roboto_metrics.json", L"../gfx/assets/roboto_atlas.png");
    }

    void on_update(float deltaTime) override {
        _running_time += deltaTime;

        const float c = std::cos(_running_time);
        const float s = std::sin(_running_time);

        _transform = {};
        _transform.m[0][0] = c;    _transform.m[0][1] = s;
        _transform.m[1][0] = -s;   _transform.m[1][1] = c;
        _transform.m[2][2] = 1.0f;
        _transform.m[3][3] = 1.0f;

        _text_transform = {};
        _text_transform.m[0][0] = 0.05f; // Scale X
        _text_transform.m[1][1] = 0.05f; // Scale Y
        _text_transform.m[2][2] = 1.00f;
        _text_transform.m[3][3] = 1.00f;

        _text_transform.m[3][0] = -0.9f; // Left side of screen
        _text_transform.m[3][1] = -0.2f; // Vertical center-bottom
    }

    void on_render() override {
        _renderer->begin_frame();
        _renderer->draw_mesh(_quad_mesh, _quad_material, _transform, _has_texture ? &_texture : nullptr);
        _renderer->draw_text("Hello, Dx12 MSDF Text Renderer!", _font, _text_transform, 24.0f);
        _renderer->end_frame();
    }

    void on_shutdown() override {
        if (_has_texture) {
            _renderer->destroy_texture(_texture);
            _has_texture = false;
        }

        std::wcout << L"Cleaning up local file: " << _shader_file_path << std::endl;
        _wremove(_shader_file_path.c_str());

        std::cout << "Engine Systems Shutdown gracefully." << std::endl;
    }
private:
    GraphicsContext* _gfx_context = nullptr;
    std::unique_ptr<Renderer> _renderer;

    std::wstring _shader_file_path;
    Mesh _quad_mesh;
    Material _quad_material;

    GpuTexture _texture;
    bool _has_texture = false;

    Font _font;

    Mat4 _text_transform = {};
    Mat4 _transform = {};
    float _running_time = 0.0f;
};

#if defined(_WIN32)
#include <Windows.h>

#if defined(_DEBUG) || defined(DEBUG)
#define SANDBOX_DEBUG_CONSOLE 1
#else
#define SANDBOX_DEBUG_CONSOLE 0
#endif

// wWinMain uses the GUI subsystem, which has no console, so cout/cerr go nowhere by default.
// This opens one and rebinds the standard streams to it.
static void attach_debug_console() {
#if SANDBOX_DEBUG_CONSOLE
    if (!AllocConsole()) {
        return;
    }
    FILE* file = nullptr;
    freopen_s(&file, "CONOUT$", "w", stdout);
    freopen_s(&file, "CONOUT$", "w", stderr);
    freopen_s(&file, "CONIN$", "r", stdin);

    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::wcout.clear();
    std::cerr.clear();
    std::cin.clear();

    SetConsoleTitleW(L"Sandbox Debug Console");
#endif
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR pCmdLine,
    _In_ int nCmdShow)
{
    attach_debug_console();

    try {
        SandboxApp app;
        app.run();
    }
    catch (const std::exception& e) {
        // Exceptions from init (shader compile, device creation, ...) end up here.
        std::cerr << "Fatal error: " << e.what() << std::endl;
        MessageBoxA(nullptr, e.what(), "Fatal error", MB_OK | MB_ICONERROR);
#if SANDBOX_DEBUG_CONSOLE
        std::cerr << "Press Enter to close..." << std::endl;
        std::cin.get();
#endif
        return 1;
    }
    return 0;
}
#else
int main() {
    SandboxApp app;
    app.run();
    return 0;
}
#endif
