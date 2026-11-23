#include "font.hpp"
#include "gfxpch.hpp"
#include "renderer.hpp"
#include "graphics_resources.hpp"
#include "render_graph.hpp"
#include <float.h>
#include <fstream>
#include <ratio>
#include <sstream>
#include <string>
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

	// Tracking transient allocation packets to clean up when the GPU finishes frame execution
	struct TransientAllocation {
		GpuMemoryArena arena;
		GpuBuffer vertex_buffer;
		GpuBuffer index_buffer;
	};

	struct Renderer::Impl {
		RenderGraph render_graph;
		std::vector<RenderCommand> draw_queue;
		std::vector<TransientAllocation> transient_allocs;
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

	static std::string extract_value(std::string const& token, std::string const& key) {
		size_t pos = token.find("\"" + key + "\"");
		if (pos == std::string::npos) return "";
		size_t colon = token.find(":", pos);
		if (colon == std::string::npos) return "";
		size_t start = colon + 1;
		while (start < token.size() && (std::isspace(token[start]) || token[start] == '"')) {
			start++;
		}
		size_t end = start;
		while (end < token.size() && !std::isspace(token[end]) && token[end] != ',' && token[end] != '}' && token[end] != ']' && token[end] != '"') {
			end++;
		}
		return token.substr(start, end - start);
	}

	static size_t find_matching(std::string const& s, size_t open, char o, char c) {
		int depth = 0;
		for (size_t i = open; i < s.size(); ++i)
		{
			if (s[i] == o) ++depth;
			else if (s[i] == c && --depth == 0) return i;
		}
		return std::string::npos;
	}

	Font Renderer::load_font(const std::wstring &json_path, const std::wstring &texture_path) {
		std::ifstream file(json_path);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open MSDF Font json file.");
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string content = buffer.str();

		Font font;
		font.texture = create_texture_from_file(texture_path);

		size_t glyphs_key = content.find("\"glyphs\"");
		if (glyphs_key == std::string::npos) {
			throw std::runtime_error("Font json has no glyphs array.");
		}

		std::string atlas_section = content.substr(0, glyphs_key);
		const bool y_origin_bottom = (extract_value(atlas_section, "yOrigin") != "top");
		font.geometry.em_size = std::stof(extract_value(atlas_section, "size"));

		size_t metrics_pos = content.find("\"metrics\"");
		if (metrics_pos != std::string::npos) {
			std::string metrics_section = content.substr(metrics_pos, content.find("}", metrics_pos) - metrics_pos);
			font.geometry.line_height = std::stof(extract_value(metrics_section, "lineHeight"));
			font.geometry.ascender    = std::stof(extract_value(metrics_section, "ascender"));
			font.geometry.descender   = std::stof(extract_value(metrics_section, "descender"));
		}

		size_t arr_open = content.find('[', glyphs_key);
		if (arr_open == std::string::npos) {
			throw std::runtime_error("Font json glyphs array is malformed.");
		}
		size_t arr_close = find_matching(content, arr_open, '[', ']');
		if (arr_close == std::string::npos) {
			throw std::runtime_error("Font json glyphs array is not terminated.");
		}

		size_t cursor = arr_open + 1;
		while (true) {
			size_t gs = content.find('{', cursor);
			if (gs == std::string::npos || gs > arr_close) break;

			size_t ge = find_matching(content, gs, '{', '}');
			if (ge == std::string::npos) break;

			std::string glyph_data = content.substr(gs, ge - gs + 1);
			cursor = ge + 1;

			std::string unicode_str = extract_value(glyph_data, "unicode");
			if (unicode_str.empty()) continue;

			MSDFGlyph glyph = {};
			glyph.codepoint = std::stoul(unicode_str);
			glyph.advance = std::stof(extract_value(glyph_data, "advance"));

			// planeBounds / atlasBounds are flat objects, so the first '}' after the key is correct.
			size_t plane_bounds = glyph_data.find("\"planeBounds\"");
			if (plane_bounds != std::string::npos) {
				std::string plane_data = glyph_data.substr(plane_bounds, glyph_data.find("}", plane_bounds) - plane_bounds);
				glyph.plane_bounds_left  = std::stof(extract_value(plane_data, "left"));
				glyph.plane_bounds_right = std::stof(extract_value(plane_data, "right"));

				float pb = std::stof(extract_value(plane_data, "bottom"));
				float pt = std::stof(extract_value(plane_data, "top"));
				if (!y_origin_bottom) { // y-down in the JSON -> convert to y-up for our quads
					pb = -pb;
					pt = -pt;
				}
				glyph.plane_bounds_bottom = pb;
				glyph.plane_bounds_top    = pt;
			}

			size_t atlas_bounds = glyph_data.find("\"atlasBounds\"");
			if (atlas_bounds != std::string::npos) {
				std::string atlas_data = glyph_data.substr(atlas_bounds, glyph_data.find("}", atlas_bounds) - atlas_bounds);

				const float W = static_cast<float>(font.texture.width);
				const float H = static_cast<float>(font.texture.height);

				float l = std::stof(extract_value(atlas_data, "left"))   / W;
				float r = std::stof(extract_value(atlas_data, "right"))  / W;
				float b = std::stof(extract_value(atlas_data, "bottom")) / H;
				float t = std::stof(extract_value(atlas_data, "top"))    / H;

				if (y_origin_bottom) { // image rows run top-down, JSON measured from the bottom
					b = 1.0f - b;
					t = 1.0f - t;
				}
				glyph.atlas_bounds_left   = l;
				glyph.atlas_bounds_right  = r;
				glyph.atlas_bounds_bottom = b;
				glyph.atlas_bounds_top    = t;
			}

			font.geometry.glyphs[glyph.codepoint] = glyph;
		}

		std::cout << "Loaded " << font.geometry.glyphs.size() << " glyphs, em_size=" << font.geometry.em_size << std::endl;
		return font;
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
		for (auto& dynamic_alloc : _impl->transient_allocs) {
			_context.destroy_buffer(dynamic_alloc.vertex_buffer);
			_context.destroy_buffer(dynamic_alloc.index_buffer);
			_context.destroy_arena(dynamic_alloc.arena);
		}
		_impl->transient_allocs.clear();
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

	void Renderer::draw_text(std::string const& text, Font const& font, Mat4 const& transform, float font_size) {
		if (text.empty()) return;

		std::vector<Vertex> vertices;
		std::vector<u32> indices;

		vertices.reserve(text.size() * 4);
		indices.reserve(text.size() * 6);

		float cursor_x = 0.0f;
		float cursor_y = 0.0f;
		const float scale = font_size / font.geometry.em_size;

		u32 quad_index = 0;
		for (char c : text) {
			if (c == '\n') {
				cursor_y -= font.geometry.line_height * scale;
				cursor_x = 0.0f;
				continue;
			}

			auto it = font.geometry.glyphs.find(static_cast<u32>(c));
			if (it == font.geometry.glyphs.end()) continue;

			const MSDFGlyph& glyph = it->second;

			// Skip layout calculations for whitespace, but advance cursor.
			if (c == ' ') {
				cursor_x += glyph.advance * scale;
				continue;
			}

			// Project plane;
			float x0 = cursor_x + glyph.plane_bounds_left * scale;
			float y0 = cursor_y + glyph.plane_bounds_bottom * scale;
			float x1 = cursor_x + glyph.plane_bounds_right * scale;
			float y1 = cursor_y + glyph.plane_bounds_top * scale;

			// Tex Coord mapping;
			float u0 = glyph.atlas_bounds_left;
			float u1 = glyph.atlas_bounds_right;
			float v0 = glyph.atlas_bounds_bottom;      // bottom vertices (y0)
			float v1 = glyph.atlas_bounds_top;
			// Vertices
			vertices.push_back({ { x0, y0, 0.0f }, { u0, v0 } });
			vertices.push_back({ { x1, y0, 0.0f }, { u1, v0 } });
			vertices.push_back({ { x1, y1, 0.0f }, { u1, v1 } });
			vertices.push_back({ { x0, y1, 0.0f }, { u0, v1 } });
			// Triangulation
			u32 base = quad_index * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			cursor_x += glyph.advance * scale;
			quad_index++;
		}

		if (vertices.empty()) return;

		const u32 vert_size = static_cast<u32>(vertices.size() * sizeof(Vertex));
		const u32 index_size = static_cast<u32>(indices.size() * sizeof(u32));

		constexpr u64 HEAP_ALIGN = 65536;
		auto align_up_64k = [](u64 v) { return (v + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1); };
		const u64 arena_size = align_up_64k(vert_size) + align_up_64k(index_size);

		GpuMemoryArena dynamic_arena = _context.create_arena(MemoryUsage::Upload, arena_size);
		GpuBuffer vertex_buffer = _context.create_buffer(dynamic_arena, vert_size, sizeof(Vertex));
		GpuBuffer index_buffer = _context.create_buffer(dynamic_arena, index_size, sizeof(u32));

	    std::memcpy(vertex_buffer.allocation.cpu_mapped_pointer, vertices.data(), vert_size);
    	std::memcpy(index_buffer.allocation.cpu_mapped_pointer, indices.data(), index_size);

  		Mesh dynamic_mesh = {
	        .vertex_buffer = vertex_buffer,
	        .index_buffer = index_buffer,
	        .index_count = static_cast<u32>(indices.size())
	    };

	    _impl->transient_allocs.push_back({
	    	.arena = dynamic_arena,
	    	.vertex_buffer = vertex_buffer,
	    	.index_buffer = index_buffer,
	    });

	    Material msdf_material = create_material(L"assets/msdf_text.hlsl");
	    draw_mesh(dynamic_mesh, msdf_material, transform, &font.texture);
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
