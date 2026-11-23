# Gfx

A small, cross-platform-minded game engine toolkit: the pieces I keep rewriting for every project, in one place. Currently a D3D12 renderer with a
render graph and MSDF text.

## Status

| Platform | Status |
| -------- | ------ |
| Windows (D3D12) | Working |
| macos / Linux   | Planned |
| Consoles        | Planned |

## Features
- D3D12 backend with bindless textures
- Render graph with automatic resource sync between command lists
- MSDF text rendering
- Not yet: entities, physics, audio (see Roadmap)

## Contributing

Thank you for the interest in contributing. Above lays out how the project basically works, any issue that has not been claimed, anyone is welcome to work on.

Please read the section on [AI Use](#AI_USE) before submitting a pull request.

## Quick Start
Requirements: Windows 10+, Visual Studio 2022, Premake 5, a D3D12 GPU.

```bash
premake5 vs2022
build.bat
```

## Example
```cpp
class SandboxApp : public Application {
public:
	void on_render() override {
		_renderer->begin_frame();
		_renderer->draw_text("Hello", _font, _transform, 24.0f);
		_renderer->end_frame();
	}
};
```

## Architecture
- `dx_context.hpp`: device, memory, pipeline, and descriptor management.
- `render_graph.hpp`: pass ordering and sync.
- `renderer.hpp`: high-level rendering API.

## Roadmap

Features I want in place before tagging a first useful release (v0.1).

### Done
- [x] MSDF text rendering

### Now
- [ ] Text Polish: layout, kerning, wrapping, rem-style sizing.
- [ ] Asset Pipeline: packaging system for shaders, font, and textures.

### Next
- [ ] Audio: native platform backends: NO FMOD.
- [ ] Animation: skeletal and property tweening.
- [ ] Physics: broadphase, rigid bodies.

### Later
- [ ] Terrain: heightmap import, chunked mesh, LOD.
- [ ] Cinematic Tools: camera paths, timeline sequencer.

## Contributing

See CONTRIBUTING.md. Short version: claim and issue first, write cody by hand.

## LICENSE

SEE [LICENSE](LICENSE.txt)
