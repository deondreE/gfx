#pragma once 
#include "graphics_context.hpp"
#include "font.hpp"
#include "vendor/stb_image.h"
#include <string>
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

		Font load_font(std::wstring const& json_path, std::wstring const& texture_path);
		void draw_text(std::string const& text, Font const& font, Mat4 const& transform, float font_size);

		const float root_font_px = 16.0f;
		void draw_text_rem(std::string const& text, Font const& font,
							Mat4 const& transform, float rem)  {
			draw_text(text, font, transform, rem * root_font_px * _font_scale);
		}
		void set_font_scale(float scale) { _font_scale = scale; }

		// Simple Frame Rendering commands
		void begin_frame();
		void draw_mesh(const Mesh& mesh, const Material& material, Mat4 const& transform, GpuTexture const* texture);
		void end_frame();
	private:
		GraphicsContext& _context;
		GpuMemoryArena   _geometry_arena; 
		float  _font_scale = 25.0f;

		struct Impl; 
		std::unique_ptr<Impl> _impl;
	};
}
