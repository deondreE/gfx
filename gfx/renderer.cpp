#include "gfxpch.hpp"
#include "renderer.hpp"
#include "render_graph.hpp"
namespace gfx {

#if defined(WIN32)
	using Microsoft::WRL::ComPtr;
	template <typename T>
	using Ptr = Microsoft::WRL::ComPtr<T>;

	struct DrawConstants {
		Mat4 transform;
		u32 texture_index;
	};
	static_assert(sizeof(DrawConstants) == 17 * sizeof(u32), "DrawConstants must be 17 DWORDs");

	static constexpr u32 NO_TEXTURE = 0xFFFFFFFFu;

	struct RenderCommand {
		Mesh mesh;
		Material material;
		DrawConstants constants;
		DescriptorHandle transform_cbv;
	};

	struct Renderer::Impl {
		RenderGraph render_graph;
		std::vector<RenderCommand> draw_queue;
	};

	// Expose underlying DX12Context structure safely without changing its header definition.
	struct DX12ContextLayout {
		void* virtual_table_ptr; // Compilers place the vtable pointer here first
		HWND hwnd;
		DX12DeviceContext device_ctx;
		DX12Swapchain swapchain;
		DX12CommandContext cmd_ctx;
	};

	static void throw_if_failed(HRESULT hr, const char* what) {
		if (FAILED(hr)) {
			char buf[160];
			std::snprintf(buf, sizeof(buf), "%s failed: 0x%08X", what, static_cast<unsigned>(hr));
			throw std::runtime_error(buf);
		}
	}

	static D3D12_RESOURCE_BARRIER make_transition(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		b.Transition.pResource = res;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter = after;
		return b;
	}

	Renderer::Renderer(GraphicsContext& context)
		: _context(context), _impl(std::make_unique<Impl>())
	{
		_geometry_arena = _context.create_arena(MemoryUsage::GpuOnly, util::megabytes(128));
	}

	Renderer::~Renderer() 
	{
		_context.destroy_arena(_geometry_arena);
	}
	
	Mesh Renderer::create_mesh(std::vector<Vertex> const& vertices, std::vector<u32> const& indices) {
		const u32 vert_size = (u32)(vertices.size() * sizeof(Vertex));
		const u32 index_size = (u32)(indices.size() * sizeof(u32));

		// create_buffer places every buffer on a 64KB boundary, so reserve one 64KB slot per buffer.
		constexpr u64 HEAP_ALIGN = 65536;
		GpuMemoryArena staging_arena = _context.create_arena(MemoryUsage::Upload, 2 * HEAP_ALIGN);
		GpuBuffer staging_vbuffer = _context.create_buffer(staging_arena, vert_size, sizeof(Vertex));
		GpuBuffer staging_ibuffer = _context.create_buffer(staging_arena, index_size, sizeof(u32));

		if (!staging_vbuffer.allocation.cpu_mapped_pointer || !staging_ibuffer.allocation.cpu_mapped_pointer) {
			throw std::runtime_error("Staging buffers are not CPU mapped.");
		}
		std::memcpy(staging_vbuffer.allocation.cpu_mapped_pointer, vertices.data(), vert_size);
		std::memcpy(staging_ibuffer.allocation.cpu_mapped_pointer, indices.data(), index_size);

		GpuBuffer vertex_buffer = _context.create_buffer(_geometry_arena, vert_size, sizeof(Vertex));
		GpuBuffer index_buffer = _context.create_buffer(_geometry_arena, index_size, sizeof(u32));

		auto* raw_layout = reinterpret_cast<DX12ContextLayout*>(&_context);
		auto* device = raw_layout->device_ctx.device.Get();
		auto* cmd_queue = raw_layout->device_ctx.command_queue.Get();

		auto* vb_res = reinterpret_cast<ID3D12Resource*>(vertex_buffer.native_resources);
		auto* ib_res = reinterpret_cast<ID3D12Resource*>(index_buffer.native_resources);
		auto* staging_vb_res = reinterpret_cast<ID3D12Resource*>(staging_vbuffer.native_resources);
		auto* staging_ib_res = reinterpret_cast<ID3D12Resource*>(staging_ibuffer.native_resources);

		Ptr<ID3D12CommandAllocator> temp_alloc;
		Ptr<ID3D12GraphicsCommandList> temp_list;
		throw_if_failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&temp_alloc)), "CreateCommandAllocator");
		throw_if_failed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, temp_alloc.Get(), nullptr, IID_PPV_ARGS(&temp_list)), "CreateCommandList");

		D3D12_RESOURCE_BARRIER pre_copy[2] = {
			make_transition(vb_res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
			make_transition(ib_res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
		};
		temp_list->ResourceBarrier(2, pre_copy);

		temp_list->CopyBufferRegion(vb_res, 0, staging_vb_res, 0, vert_size);
		temp_list->CopyBufferRegion(ib_res, 0, staging_ib_res, 0, index_size);

		D3D12_RESOURCE_BARRIER post_copy[2] = {
			make_transition(vb_res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
			make_transition(ib_res, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER),
		};
		temp_list->ResourceBarrier(2, post_copy);

		throw_if_failed(temp_list->Close(), "Close upload list");

		ID3D12CommandList* submission_list[] = { temp_list.Get() };
		cmd_queue->ExecuteCommandLists(1, submission_list);

		Ptr<ID3D12Fence> fence;
		throw_if_failed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
		HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!fence_event) {
			throw std::runtime_error("CreateEvent failed.");
		}
		throw_if_failed(cmd_queue->Signal(fence.Get(), 1), "Signal");

		if (fence->GetCompletedValue() < 1) {
			throw_if_failed(fence->SetEventOnCompletion(1, fence_event), "SetEventOnCompletion");
			WaitForSingleObject(fence_event, INFINITE);
		}
		CloseHandle(fence_event);

		_context.destroy_buffer(staging_vbuffer);
		_context.destroy_buffer(staging_ibuffer);
		_context.destroy_arena(staging_arena);

		return Mesh{
			.vertex_buffer = vertex_buffer,
			.index_buffer = index_buffer,
			.index_count = static_cast<u32>(indices.size())
		};
	}

	GpuTexture Renderer::create_texture_from_file(std::wstring const& path) {
		FILE* file = nullptr;
		if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
			throw std::runtime_error("Failed to open texture file.");
		}
		int width, height, channels;
		stbi_uc* pixels = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels) {
			throw std::runtime_error(std::string("Failed to decode texture: ") + stbi_failure_reason());
		}
		TextureDescription tex_desc = {
			.width = static_cast<u32>(width),
			.height = static_cast<u32>(height),
			.format = TextureFormat::RGBA8_UNorm,
		};

		GpuTexture texture = _context.create_texture(tex_desc, pixels);
		stbi_image_free(pixels);
		return texture;
	}

	Material Renderer::create_material(std::wstring const& shader_path) {
		ShaderBlob vs = _context.compile_shader(shader_path, "VSMain", ShaderStage::Vertex);
		ShaderBlob ps = _context.compile_shader(shader_path, "PSMain", ShaderStage::Pixel);

		RootSignatureDescription root_desc = {
			.ranges = {},
			.is_bindless = true
		};

		RootSignature root_sig = _context.create_root_signature(root_desc);
		PipelineDescription pipeline_desc = {
			.root_signature = root_sig,
			.vertex_shader = vs,
			.pixel_shader = ps,
			.depth_test_enabled = false,
			.wireframe_mode = false,
		};

		PipelineState pso = _context.create_pipeline_state(pipeline_desc);

		return Material {
			.pipeline_state = pso,
			.root_signature = root_sig
		};
	}

	void Renderer::begin_frame() {
		_impl->draw_queue.clear();
		_impl->render_graph.reset();
	}

	void Renderer::draw_mesh(const Mesh& mesh, const Material& material, const Mat4& transform, GpuTexture const* texture) {
		DrawConstants constants = {};
		constants.transform = transform;
		constants.texture_index = texture ? texture->srv_index : NO_TEXTURE;

		_impl->draw_queue.push_back({
			.mesh = mesh,
			.material = material,
			.constants = constants
		});
	}

	void Renderer::end_frame() {
		auto* raw_layout = reinterpret_cast<DX12ContextLayout*>(&_context);
		auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(raw_layout->cmd_ctx.list.Get());
		auto& sc = raw_layout->swapchain;

		// present() waits for the GPU every frame, so resetting the allocator here is safe.
		throw_if_failed(raw_layout->cmd_ctx.allocator->Reset(), "Allocator Reset");
		throw_if_failed(list->Reset(raw_layout->cmd_ctx.allocator.Get(), nullptr), "CommandList Reset");

		ID3D12Resource* backbuffer = sc.render_targets[sc.frame_index].Get();

		D3D12_RESOURCE_BARRIER to_rt = make_transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list->ResourceBarrier(1, &to_rt);

		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = sc.rtv_heap->GetCPUDescriptorHandleForHeapStart();
		rtv_handle.ptr += static_cast<SIZE_T>(sc.frame_index) * sc.rtvDescriptorSize;

		const float clearColor[] = { 0.1f, 0.11f, 0.13f, 1.0f };
		list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
		list->ClearRenderTargetView(rtv_handle, clearColor, 0, nullptr);

		D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(sc.width), static_cast<float>(sc.height), 0.0f, 1.0f };
		D3D12_RECT scissor = { 0, 0, static_cast<LONG>(sc.width), static_cast<LONG>(sc.height) };
		list->RSSetViewports(1, &viewport);
		list->RSSetScissorRects(1, &scissor);

		_context.bind_descriptor_heaps(list);

		D3D12_GPU_DESCRIPTOR_HANDLE heap_start = { .ptr = _context.get_descriptor_heap_gpu_start() };

		for (auto const& cmd : _impl->draw_queue) {
			list->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(cmd.material.root_signature.native_handle));
			list->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(cmd.material.pipeline_state.native_handle));

			// Changing the root signature clears root bindings, so set both per draw.
			list->SetGraphicsRoot32BitConstants(0, 17, &cmd.constants, 0);
			list->SetGraphicsRootDescriptorTable(1, heap_start);

			D3D12_VERTEX_BUFFER_VIEW vb_view = {
				.BufferLocation = reinterpret_cast<ID3D12Resource*>(cmd.mesh.vertex_buffer.native_resources)->GetGPUVirtualAddress(),
				.SizeInBytes = cmd.mesh.vertex_buffer.size_in_bytes,
				.StrideInBytes = cmd.mesh.vertex_buffer.stride
			};
			D3D12_INDEX_BUFFER_VIEW ib_view = {
				.BufferLocation = reinterpret_cast<ID3D12Resource*>(cmd.mesh.index_buffer.native_resources)->GetGPUVirtualAddress(),
				.SizeInBytes = cmd.mesh.index_buffer.size_in_bytes,
				.Format = DXGI_FORMAT_R32_UINT
			};

			list->IASetVertexBuffers(0, 1, &vb_view);
			list->IASetIndexBuffer(&ib_view);
			list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			list->DrawIndexedInstanced(cmd.mesh.index_count, 1, 0, 0, 0);
		}

		D3D12_RESOURCE_BARRIER to_present = make_transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		list->ResourceBarrier(1, &to_present);

		throw_if_failed(list->Close(), "CommandList Close");

		ID3D12CommandList* submission_list[] = { list };
		raw_layout->device_ctx.command_queue->ExecuteCommandLists(1, submission_list);
		// present() is called once per frame by Application::run after on_render().
	}

	void Renderer::destroy_texture(GpuTexture& texture) {
		_context.destroy_texture(texture);
	}
#endif

}
