## blah
A small 2D C++ Game Framework, using few dependencies and simple code to maintain easy building and portability.

**☆ This will likely see breaking changes! Use at your own risk! ☆**

#### a sample application

```cpp
#include <blah.h>
using namespace Blah;

Batch batch;

int main()
{
    Config config;
    config.name = "blah app";
    config.on_render = []()
    {
        auto target = App::backbuffer();
        target->clear(Color::black);

        auto center = Vec2f(target->width(), target->height()) / 2;
        auto rotation = Time::seconds * Calc::TAU;
        auto transform = Mat3x2f::create_transform(center, Vec2f::zero, Vec2f::one, rotation);

        batch.push_matrix(transform);
        batch.rect(Rectf(-32, -32, 64, 64), Color::red);
        batch.pop_matrix();

        batch.render(target);
        batch.clear();
    };

    return App::run(&config);
}

```

#### building
 - Requires C++17 and CMake 4.0+
 - At least one **Renderer** implementation must be enabled in CMake:
	- [OpenGL](https://github.com/NoelFB/blah/blob/master/src/internal/blah_renderer_opengl.cpp) (Default on Linux/macOS) `BLAH_RENDERER_OPENGL`
	- [D3D11](https://github.com/NoelFB/blah/blob/master/src/internal/blah_renderer_d3d11.cpp) (Default on Windows) `BLAH_RENDERER_D3D11`
	- Additional renderers can be added by implementing the [Renderer Backend](https://github.com/NoelFB/blah/blob/master/src/internal/blah_renderer.h)
 
#### notes
 - There's no Shader abstraction, so you need to swap between GLSL/HLSL depending on the Renderer.
 - Only floatN/mat3x2/mat4x4 uniforms are supported.
 - No threaded rendering, so it will explode if you try that.


#### Emscripten

Use an activated current Emscripten SDK with the SDL3 port (`-sUSE_SDL=3`).
Configure your application with `emcmake cmake`, then build with `cmake --build`.
Linking `blah` propagates the SDL3 compile/link flags, WebGL 2 requirement, GL
function lookup support, and memory growth setting. The web build defines
`BLAH_NO_THREADING` and requires `BLAH_RENDERER_OPENGL=ON` and D3D11 disabled.

The application owns the HTML shell and asset preloading; set its executable
suffix to `.html`, provide a canvas through `Module.canvas`, and use
`--preload-file` for assets. Serve all generated files over HTTP. The parent
DEFEND.EXE project includes a working configuration and a page with canvas and
log output; see its README for commands.

The browser schedules frames through `requestAnimationFrame`. Fixed timestep
updates accumulate elapsed time without sleeping on the browser thread, and
`App::exit()` cancels the loop and runs the shutdown callback. Browser filesystem
writes are temporary unless the application configures persistence.
