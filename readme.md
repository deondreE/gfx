# Project Overview

I was tired of writing engines, over and over again for each project that I make, this is just a combination, of those tools I always rewrite.

If you are interested in contributing to this project, look at the contributing section of this readme.

## The Project Currently

Currently the project is just a renderer, with the bones to be cross platform, support for

- MACOS, LINUX, NINTENDO, PLAYSTATION, XBOX

Entities do not exist, just resources that entities would control.

The renderer is driven by the `dx_context.hpp`, and `render_graph.hpp` allowing the renderer to understand how to control its own resources in real time. The graph ensures sycnronization between each command list, ensuring if one is dependent on another they get rendered in the right order.

The highlevel `renderer.hpp` is the current basis of the public api for the renderer, eventually this will be even more simple, the goal is that the renderer doesn't care what your graphics context is, but only responds to what it could be.

## Contributing

Thank you for the interest in contributing. Above lays out how the project basically works, any issue that has not been claimed, anyone is welcome to work on.

Please read the section on AI Use before submitting a pull request.

### Installation Requirements

#### Windows

1. Ensure you have [Visual Studio](https://visualstudio.microsoft.com/) installed.
2. Download [Premake](https://premake.github.io/)
3. Run `premake5 premake5.lua` from the root directory.
4. Run `build.bat` or open the visual studio solution, and use Visual Stuido.

### AI

To preserve the educational integrity of this engine, please refrain from using autonomous AI agents to drive development. Navigating the complexities of manual synchronization and interpreting low-level API error messages is fundamental to this project’s purpose. Contributions should be written manually to ensure full visibility into the hardware interaction. Standard IDE autocompletion and basic boilerplate generation are acceptable.

## Roadmap

I want to have these features to tag actually usefull:

- [ ] Physics
- [ ] Animation
- [ ] Cinematic tools
- [ ] Terrain tools
- [ ] Audio - Native Audio Handles, Not FMOD
- [x] Text Rendering through MSDF -- DONE
- [ ] Manifest, some kind of packaging system.

