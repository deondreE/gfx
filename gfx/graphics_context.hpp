#pragma once 

#include "gfxpch.hpp"

#include "window.hpp"
#include "graphics_resources.hpp"

namespace gfx {
	struct DescriptorHandle {
		u64 cpu_ptr = 0;
		u64 gpu_ptr = 0;
	};

	class GraphicsContext {
	public:
		virtual ~GraphicsContext() = default;

		virtual void init(Window& window) = 0;
		virtual void clear(float r, float g, float b, float a) = 0;
		virtual void present() = 0;
		virtual void resize(int width, int height) = 0;

		virtual GpuMemoryArena create_arena(MemoryUsage usage, u64 capacity) = 0;
		virtual void destroy_arena(GpuMemoryArena& arena) = 0;

		virtual GpuBuffer      create_buffer(GpuMemoryArena& arena, u32 size, u32 stride) = 0;
		virtual void           destroy_buffer(GpuBuffer& buffer) = 0;

		virtual GpuTexture create_texture(TextureDescription const& desc, const void* initial_data) = 0;
		virtual void destroy_texture(GpuTexture& texture) = 0;

		virtual RootSignature create_root_signature(const RootSignatureDescription& desc) = 0;
		virtual void          destroy_root_signature(RootSignature& sig) = 0;

		virtual PipelineState create_pipeline_state(const PipelineDescription& desc) = 0;
		virtual void          destroy_pipeline_state(PipelineState& pipeline) = 0;

		virtual ShaderBlob compile_shader(std::wstring const& filepath, std::string const& entry, ShaderStage stage) = 0;

		virtual DescriptorHandle allocate_descriptor(const GpuBuffer& buffer, DescriptorType type) = 0;
		virtual void bind_descriptor_heaps(void* native_command_list) = 0;
		virtual u64 get_descriptor_heap_gpu_start() = 0;

		static std::unique_ptr<GraphicsContext> create();
	};
}
