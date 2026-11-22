#pragma once 
#include "graphics_context.hpp"
#include "vendor/stb_image.h"
#include <vector>

namespace gfx {
	class Renderer {
	public:
		Renderer(GraphicsContext& context);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		// Object Creation Helpers (Allocates underlying buffers automatically)
		Mesh     create_mesh(const std::vector<Vertex>& vertices, const std::vector<u32>& indices);
		Material create_material(const std::wstring& shader_path);

		GpuTexture create_texture_from_file(std::wstring const& path);
		void destroy_texture(GpuTexture& texture);

		// Simple Frame Rendering commands
		void begin_frame();
		void draw_mesh(const Mesh& mesh, const Material& material, Mat4 const& transform, GpuTexture const* texture);
		void end_frame();
	private:
		GraphicsContext& _context;
		GpuMemoryArena   _geometry_arena; 

		struct Impl; 
		std::unique_ptr<Impl> _impl;
	};
}
