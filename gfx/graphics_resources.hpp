#pragma once
#include <string>
#include <vector> 

using u64 = uint64_t;
using u32 = uint32_t;
using u8 = uint8_t;

namespace gfx {

	enum class MemoryUsage {
		GpuOnly,  // Optimal GPU Read/Write (Textures, Static Vertex/Index Buffers)
		Upload,   // Host-visible, CPU-writable (Constant Buffers, Dynamic Vertices)
		Readback, // Host-visible, GPU-writable, CPU-readable (Capture, Compute Results)
	};

	enum class ShaderStage {
		Vertex, 
		Pixel, 
		Compute
	};

	struct GpuAllocation {
		void* native_heap_ptr = nullptr;
		u64 offset = 0;
		u64 size = 0;
		void* cpu_mapped_pointer = nullptr;
	};

	struct GpuBuffer {
		GpuAllocation allocation;
		void* native_resources = nullptr;
		u32 size_in_bytes = 0;
		u32 stride = 0;
	};

	struct GpuMemoryArena {
		void* native_heap = nullptr;
		u64 capacity = 0;
		u64 allocated_bytes = 0;
		void* cpu_base_address = nullptr;
		MemoryUsage usage;
	};

	struct ShaderBlob {
		std::vector<u8> bytecode;
		std::wstring entry_point;
		ShaderStage stage;
	};

	enum class DescriptorType {
		ConstantBufferView,   // CBV (b registers)
		ShaderResourceView,   // SRV (t registers - e.g. Textures)
		UnorderedAccessView,  // UAV (u registers - e.g. Compute read/write)
		Sampler               // Sampler (s registers)
	};

	struct DescriptorRange {
		DescriptorType type;
		u32 register_index;
		u32 count;
	};

	struct GpuTexture {
        void* native_resource = nullptr; // ID3D12Resource*
        u32 width = 0;
        u32 height = 0;
        u32 srv_index = 0; // Bindless global descriptor heap offset index
    };

	enum class TextureFormat {
		RGBA8_UNorm,
	};

    struct TextureDescription {
        u32 width;
        u32 height;
        TextureFormat format = TextureFormat::RGBA8_UNorm;
    };

	struct RootSignatureDescription {
		std::vector<DescriptorRange> ranges;
		bool is_bindless = false;
	};

	struct RootSignature {
		void* native_handle = nullptr; // ID3D12RootSignature*
	};

	struct PipelineDescription {
		RootSignature root_signature;
		ShaderBlob vertex_shader;
		ShaderBlob pixel_shader;
		bool depth_test_enabled = false;
		bool wireframe_mode = false;
	};

	struct PipelineState {
		void* native_handle = nullptr; // ID3D12PipelineState*
	};

	//// Resources

	struct Vertex {
		float position[3];
		float uv[2];
	};

	struct Mesh {
		GpuBuffer vertex_buffer;
		GpuBuffer index_buffer;
		u32 index_count;
	};

	struct Material {
		PipelineState pipeline_state;
		RootSignature root_signature;
	};

	struct Mat4 {
		float m[4][4];
	};
}
