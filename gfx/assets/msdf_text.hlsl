struct VSInput {
	float3 position : POSITION;
	float2 uv : TEXCOORD;
};

struct VSOutput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

cbuffer DrawConstants : register(b0) {
	row_major float4x4 transform;
	uint texture_index;
};

Texture2D<float4> bindlessTextures[1024] : register(t0);
SamplerState LinearSampler : register(s0);

// Must match "distanceRange" in your atlas JSON (the "atlas" section).
static const float PX_RANGE = 4.0f;

VSOutput VSMain(VSInput input) {
	VSOutput output;
	output.position = mul(float4(input.position, 1.0f), transform);
	output.uv = input.uv;
	return output;
}

float median(float r, float g, float b) {
	return max(min(r, g), min(max(r, g), b));
}

float4 PSMain(VSOutput input) : SV_Target {
	if (texture_index == 0xFFFFFFFFu) {
		return float4(1.0f, 0.0f, 1.0f, 1.0f); // debug: magenta = no texture bound
	}

	Texture2D<float4> atlas = bindlessTextures[NonUniformResourceIndex(texture_index)];

	uint w, h;
	atlas.GetDimensions(w, h);

	float3 msd = atlas.Sample(LinearSampler, input.uv).rgb;
	float sd = median(msd.r, msd.g, msd.b);

	// Standard msdfgen screen-pixel-range computation
	float2 unit_range = PX_RANGE / float2(w, h);
	float2 screen_tex_size = 1.0f / fwidth(input.uv);
	float screen_px_range = max(0.5f * dot(unit_range, screen_tex_size), 1.0f);

	float opacity = saturate(screen_px_range * (sd - 0.5f) + 0.5f);

	return float4(1.0f, 1.0f, 1.0f, opacity);
}
