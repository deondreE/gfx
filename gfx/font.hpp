#pragma once
#include "graphics_context.hpp"
#include "graphics_resources.hpp"
#include <unordered_map>
#include <string>

namespace gfx {
	const std::string TEXT_SHADER = R"(

	)";

	struct MSDFGlyph {
		u32 codepoint;
		float plane_bounds_left;
		float plane_bounds_bottom;
		float plane_bounds_right;
		float plane_bounds_top;
		float atlas_bounds_left;
		float atlas_bounds_bottom;
		float atlas_bounds_top;
		float atlas_bounds_right;
		float advance;
	};

	struct FontGeometry {
		float em_size;
		float line_height;
		float ascender;
		float descender;
		std::unordered_map<u32, MSDFGlyph> glyphs;
	};

	struct Font {
		FontGeometry geometry;
		GpuTexture texture;
	};
}
