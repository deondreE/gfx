#pragma once
#include "graphics_context.hpp"
#include "graphics_resources.hpp"
#include <unordered_map>
#include <string>

namespace gfx {
	const std::string TEXT_SHADER = R"(
		struct VSInput {
			float3 position : POSITION;
			float2 uv : TEXCOORD;
		};

		struct VSOutput {
			float4 position : SV_POSITION;
			float2 uv : TEXCOORD;
		};

		cbuffer DrawConstants : register(b0) {
			float4x4 transform;
			uint texture_index;
		};

		Texture2D<float4> bindlessTextures[1024] : register(t0);
		SamplerState LinearSampler : register(s0);

		VSOutput VSMain(VSInput input) {
			VSOutput output;
			output.position = mul(transform, float4(input.position, 1.0f));
			output.uv = input.uv;
			return output;
		}

		float median(float r, float g, float b) {
			return max(min(r, g), min(max(r, g), b));
		}

		float4 PSMain(VSOutput input) : SV_Target {
			float3 msd = bindlessTextures[texture_index].Sample(LinearSampler, input.uv).rgb;
			float sd = median(msd.r, msd.g, msd.b);

			float screen_px_range = 2.0f;
			float2 dxdy = fwidth(input.uv);
			float pixel_dist = (sd - 0.5f) * screen_px_range / length(dxdy);

			float opacity = clamp(pixel_dist + 0.5f, 0.0f, 1.0f);

			if (opacity < 0.01f) {
				discard;
			}

			return float4(1.0f, 1.0f, 1.0f, opacity);
		}
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
