#include "graphics_resources.hpp"
#include <basetsd.h>
#include <combaseapi.h>
#include <cstring>
#include <d3dcommon.h>
#include <stdlib.h>
#include <synchapi.h>
#include <sys/types.h>
#include <winbase.h>
#if defined(_WIN32)
#include "graphics_context.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include "util/paths.hpp"
#include "util/util.hpp"

#include <stdexcept>
#include <vector>
#include <deque>
#include <functional>
#include <span>

using Microsoft::WRL::ComPtr;


namespace gfx {

	class DX12IncludeHandler : public ID3DInclude {
	public:
		STDMETHOD(Open)(
			D3D_INCLUDE_TYPE IncludeType,
			LPCSTR pFileName,
			LPCVOID pParentData,
			LPCVOID* ppData,
			UINT* pBytes
			) override {
			std::ifstream file(pFileName, std::ios::binary | std::ios::ate);
			if (!file.is_open()) {
				return E_FAIL;
			}

			std::streamsize size = file.tellg();
			file.seekg(0, std::ios::beg);

			char* buffer = new char[size];
			if (file.read(buffer, size)) {
				*ppData = buffer;
				*pBytes = static_cast<UINT>(size);
				return S_OK;
			}

			delete[] buffer;
			return E_FAIL; 
		}

		STDMETHOD(Close)(LPCVOID pData) override {
			delete[] reinterpret_cast<const char*>(pData);
			return S_OK;
		}
	};

	struct DX12DeviceContext {
		ComPtr<IDXGIFactory4> factory;
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12CommandQueue> command_queue;
	};

	struct DX12Swapchain {
		static constexpr UINT FrameCount = 2;

		ComPtr<IDXGISwapChain3> handle;
		ComPtr<ID3D12Resource> render_targets[FrameCount];
		ComPtr<ID3D12DescriptorHeap> rtv_heap;
		UINT rtvDescriptorSize = 0;
		UINT frame_index = 0;
		UINT width = 0;
		UINT height = 0;
	};

	struct DX12CommandContext {
		ComPtr<ID3D12CommandAllocator> allocators[DX12Swapchain::FrameCount];
		ComPtr<ID3D12GraphicsCommandList> list;
	};

	struct DX12SyncContext {
		ComPtr<ID3D12Fence> fence;
		UINT64 last_signaled = 0;
		HANDLE fence_event = nullptr;
	};

	static void throw_if_failed(HRESULT hr, const char* what) {
		if (FAILED(hr)) {
			char buf[160];
			std::snprintf(buf, sizeof(buf), "%s failed: 0x%08X", what, static_cast<unsigned>(hr));
			throw std::runtime_error(buf);
		}
	}

	static D3D12_HEAP_TYPE get_dx12_heap_type(MemoryUsage usage) {
		switch (usage) {
		case MemoryUsage::GpuOnly: return D3D12_HEAP_TYPE_DEFAULT;
		case MemoryUsage::Upload: return D3D12_HEAP_TYPE_UPLOAD;
		case MemoryUsage::Readback: return D3D12_HEAP_TYPE_READBACK;
		}
		return D3D12_HEAP_TYPE_DEFAULT;
	}

	static D3D12_RESOURCE_BARRIER make_transition(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = res;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter = after;
		return b;
	}

	// static API to make things simpler to follow.
	static void create_device(DX12DeviceContext& out_context) {
		// Validation for DX12
		#if defined(_DEBUG) || defined(DEBUG)
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
				debugController->EnableDebugLayer();
			}
		#endif
		UINT dxgiFactoryFlags = 0;
		#if defined(_DEBUG) || defined(DEBUG)
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		#endif
		CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&out_context.factory));
		if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&out_context.device)))) {
			throw std::runtime_error("Failed to create DX12 Device.");
		}
    
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		out_context.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&out_context.command_queue));
	}
	
	static void create_rtvs(DX12DeviceContext& device_ctx, DX12Swapchain& swapchain) {
		D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(swapchain.rtv_heap->GetCPUDescriptorHandleForHeapStart());
		for (UINT i = 0; i < DX12Swapchain::FrameCount; i++) {
			swapchain.handle->GetBuffer(i, IID_PPV_ARGS(&swapchain.render_targets[i]));
			device_ctx.device->CreateRenderTargetView(swapchain.render_targets[i].Get(), nullptr, rtv_handle);
			rtv_handle.ptr += swapchain.rtvDescriptorSize;
		}
	}

	static void create_swapchain(DX12DeviceContext& device_ctx, HWND hwnd, UINT width, UINT height, DX12Swapchain& out_swapchain) {
		out_swapchain.width = width;
		out_swapchain.height = height;

		DXGI_SWAP_CHAIN_DESC1 sc_desc = {
			.Width = width,
			.Height = height,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc = {.Count = 1 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = DX12Swapchain::FrameCount,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		};

		ComPtr<IDXGISwapChain1> swapchain1;
		device_ctx.factory->CreateSwapChainForHwnd(
			device_ctx.command_queue.Get(),
			hwnd,
			&sc_desc,
			nullptr, nullptr,
			&swapchain1
		);
		swapchain1.As(&out_swapchain.handle);
		out_swapchain.frame_index = out_swapchain.handle->GetCurrentBackBufferIndex();

		D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {
			.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
			.NumDescriptors = DX12Swapchain::FrameCount,
			.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE
		};
		device_ctx.device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&out_swapchain.rtv_heap));
		out_swapchain.rtvDescriptorSize = device_ctx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		create_rtvs(device_ctx, out_swapchain);
	}

	// static void wait_for_gpu(DX12DeviceContext& device_ctx, DX12Swapchain& swapchain, DX12SyncContext& sync) {
	// 	const UINT64 fence_value_to_wait = sync.fence_value;
	// 	device_ctx.command_queue->Signal(sync.fence.Get(), fence_value_to_wait);
	// 	sync.fence_value++;

	// 	if (sync.fence->GetCompletedValue() < fence_value_to_wait) {
	// 		sync.fence->SetEventOnCompletion(fence_value_to_wait, sync.fence_event);
	// 		WaitForSingleObject(sync.fence_event, INFINITE);
	// 	}
	// 	swapchain.frame_index = swapchain.handle->GetCurrentBackBufferIndex();
	// }

	class DX12Context : public GraphicsContext {
	public:
		DX12Context() = default;
		~DX12Context() override { shutdown(); }

		void init(Window& window) override {
			_hwnd = reinterpret_cast<HWND>(window.GetNatvieWindowHandle());
			
			create_device(_device_ctx);

			RECT rect;
			GetClientRect(_hwnd, &rect);
			UINT width = rect.right - rect.left;
			UINT height = rect.bottom - rect.top;

			create_swapchain(_device_ctx, _hwnd, width, height, _swapchain);

			for (UINT i = 0; i < DX12Swapchain::FrameCount; i++) {
				throw_if_failed(_device_ctx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmd_ctx.allocators[i])), "CreateCommandAllocator");
			}
			throw_if_failed(_device_ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmd_ctx.allocators[0].Get(), nullptr, IID_PPV_ARGS(&_cmd_ctx.list)), "CreateCommandList");
			_cmd_ctx.list->Close();

			throw_if_failed(_device_ctx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_sync.fence)), "CreateFence");
			_sync.fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (!_sync.fence_event) {
				throw std::runtime_error("CreateEvent failed.");
			}

			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
				.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				.NumDescriptors = MaxGlobalDescriptors,
				.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
				.NodeMask = 0,
			};
			throw_if_failed(_device_ctx.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&_srv_cbv_uav_heap)), "CreateDescriptorHeap(SRV)");
			_descriptor_size = _device_ctx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		void begin_frame() override {
			if (_frame_begun) return;
		}

		void clear(float r, float g, float b, float a) override {
			const float clearColor[] = { r, g, b, a };
			submit_frame(clearColor, {});
		}

		void present() override {
			_swapchain.handle->Present(1, 0);
			_frame_fence[_swapchain.frame_index] = signal();
			_swapchain.frame_index = _swapchain.handle->GetCurrentBackBufferIndex();
			_frame_begun = false;
		}

		void resize(int width, int height) override {
			if (!_swapchain.handle || (width == _swapchain.width && height == _swapchain.height)) return;

			flush();

			for (UINT i = 0; i < DX12Swapchain::FrameCount; i++) {
				_swapchain.render_targets[i].Reset();
			}
			_swapchain.handle->ResizeBuffers(DX12Swapchain::FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
			_swapchain.width = width;
			_swapchain.height = height;
			_swapchain.frame_index = _swapchain.handle->GetCurrentBackBufferIndex();
			create_rtvs(_device_ctx, _swapchain);
		}

		GpuMemoryArena create_arena(MemoryUsage usage, u64 capacity) override {
			u64 aligned_capacity = util::align_up(capacity, (u64)D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			
			D3D12_HEAP_DESC heap_desc = {
				.SizeInBytes = aligned_capacity,
				.Properties = {
					.Type = get_dx12_heap_type(usage),
					.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
					.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
					.CreationNodeMask = 1,
					.VisibleNodeMask = 1,
				},
				.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
				.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, // Simple buffer allocation pools
			}; 

			ID3D12Heap* native_heap = nullptr;
			if (FAILED(_device_ctx.device->CreateHeap(&heap_desc, IID_PPV_ARGS(&native_heap)))) {
				throw std::runtime_error("Failed to allocate GPU Memory Arena Heap.");
			}

			GpuMemoryArena arena = {
				.native_heap = reinterpret_cast<void*>(native_heap),
				.capacity = aligned_capacity,
				.allocated_bytes = 0,
				.cpu_base_address = nullptr,
				.usage = usage
			};
			return arena;
		}

		void destroy_arena(GpuMemoryArena& arena) override {
			if (!arena.native_heap) return;
			auto* heap = reinterpret_cast<ID3D12Heap*>(arena.native_heap);
			arena.native_heap = nullptr;
			defer([heap] { heap->Release(); });
		}

		GpuBuffer create_buffer(
		    GpuMemoryArena& arena,
		    u32 size,
		    u32 stride
		) override {
		    constexpr u64 HEAP_ALIGN = 65536; // 64KB heap placement alignment
		    constexpr u64 CBV_ALIGN  = 256;   // 256B hardware alignment

		    u64 start_offset = util::align_up(arena.allocated_bytes, HEAP_ALIGN);
		    u64 aligned_size = util::align_up(static_cast<u64>(size), CBV_ALIGN);

		    GFX_ASSERT(
		        (start_offset + aligned_size) <= arena.capacity,
		        "GPU Memory Arena Out of Space!"
		    );

		    D3D12_RESOURCE_DESC desc = {
		        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		        .Alignment = 0, // Must be 0 for buffers inside ALLOW_ONLY_BUFFERS heaps
		        .Width = aligned_size,
		        .Height = 1,
		        .DepthOrArraySize = 1,
		        .MipLevels = 1,
		        .Format = DXGI_FORMAT_UNKNOWN,
		        .SampleDesc = {.Count = 1},
		        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		        .Flags = D3D12_RESOURCE_FLAG_NONE,
		    };

		    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
		    if (arena.usage == MemoryUsage::Upload) {
		        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
		    } else if (arena.usage == MemoryUsage::Readback) {
		        initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
		    }

		    auto* native_heap = reinterpret_cast<ID3D12Heap*>(arena.native_heap);
		    ID3D12Resource* native_resource = nullptr;

		    HRESULT hr = _device_ctx.device->CreatePlacedResource(
		        native_heap,
		        start_offset,
		        &desc,
		        initial_state,
		        nullptr,
		        IID_PPV_ARGS(&native_resource)
		    );

		    if (FAILED(hr)) {
		        throw std::runtime_error("Failed to map placed resource buffer in Arena.");
		    }

		    GpuAllocation alloc = {
		        .native_heap_ptr = arena.native_heap,
		        .offset = start_offset,
		        .size = aligned_size,
		        .cpu_mapped_pointer = nullptr
		    };

		    if (arena.usage == MemoryUsage::Upload) {
		        D3D12_RANGE read_range = {0, 0}; // Host will not read this on the CPU
		        native_resource->Map(0, &read_range, &alloc.cpu_mapped_pointer);
		    }

		    arena.allocated_bytes = start_offset + aligned_size;

		    return GpuBuffer{
		        .allocation = alloc,
		        .native_resources = reinterpret_cast<void*>(native_resource),
		        .size_in_bytes = static_cast<u32>(aligned_size),
		        .stride = stride
		    };
		}

		virtual void  upload_buffer(GpuBuffer const& dst, const void* data, u32 size, BufferKind kind) override {
			if (size == 0) return;

			GpuMemoryArena staging_arena = create_arena(MemoryUsage::Upload, size);
			GpuBuffer staging = create_buffer(staging_arena, size, 0);
			std::memcpy(staging.allocation.cpu_mapped_pointer, data, size);

			auto* dst_res = reinterpret_cast<ID3D12Resource*>(dst.native_resources);
			auto* src_res = reinterpret_cast<ID3D12Resource*>(staging.native_resources);

			ComPtr<ID3D12CommandAllocator> alloc;
			ComPtr<ID3D12GraphicsCommandList> list;
			throw_if_failed(_device_ctx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator(upload)");
			throw_if_failed(_device_ctx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr , IID_PPV_ARGS(&list)), "CreateCommandList(upload)");

			const D3D12_RESOURCE_STATES final_state = (kind == BufferKind::Index)
				? D3D12_RESOURCE_STATE_INDEX_BUFFER
				: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

			D3D12_RESOURCE_BARRIER pre = make_transition(dst_res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			list->ResourceBarrier(1, &pre);
			list->CopyBufferRegion(dst_res, 0, src_res, 0, size);
			D3D12_RESOURCE_BARRIER post = make_transition(dst_res, D3D12_RESOURCE_STATE_COPY_DEST, final_state);
			list->ResourceBarrier(1, &post);
			throw_if_failed(list->Close(), "Close upload list");

			ID3D12CommandList* lists[] = { list.Get() };
			_device_ctx.command_queue->ExecuteCommandLists(1, lists);
			flush();

			destroy_buffer(staging);
			destroy_arena(staging_arena);
 		}

		void destroy_buffer(GpuBuffer& buffer) override {
			if (!buffer.native_resources) return;
			auto* resource = reinterpret_cast<ID3D12Resource*>(buffer.native_resources);
			defer([resource, mapped = buffer.allocation.cpu_mapped_pointer] {
				if (mapped) resource->Unmap(0, nullptr);
				resource->Release();
			});
		}

		ShaderBlob compile_shader(std::wstring const& filepath, std::string const& entry, ShaderStage stage) override {
			std::string target;
			switch (stage) {
			case ShaderStage::Vertex: target = "vs_5_1"; break;
			case ShaderStage::Pixel:  target = "ps_5_1"; break;
			case ShaderStage::Compute:target = "cs_5_1"; break;
			}

			UINT compile_flags = D3DCOMPILE_ENABLE_STRICTNESS;
		#if defined(_DEBUG) || defined(DEBUG)
			compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
		#endif

			ComPtr<ID3DBlob> shader_code_blob;
			ComPtr<ID3DBlob> error_blob;

			DX12IncludeHandler include_handler;

			HRESULT hr = D3DCompileFromFile(
				util::paths::resolve_w(filepath).c_str(),
				nullptr,
				&include_handler,
				entry.c_str(),
				target.c_str(),
				compile_flags,
				0,
				&shader_code_blob,
				&error_blob
			);

			if (FAILED(hr)) {
				char code[32];
				std::snprintf(code, sizeof(code), " (0x%08X)", static_cast<unsigned>(hr));
				std::string message = std::string("Shader compilation failed") + code;
				if (error_blob) {
					message += ": ";
					message += static_cast<const char*>(error_blob->GetBufferPointer());
				}
				throw std::runtime_error(message);
			}

			std::vector<u8> bytecode(shader_code_blob->GetBufferSize());
			std::memcpy(bytecode.data(), shader_code_blob->GetBufferPointer(), shader_code_blob->GetBufferSize());

			return ShaderBlob{
				.bytecode = std::move(bytecode),
				.entry_point = std::wstring(entry.begin(), entry.end()),
				.stage = stage
			};
		}

		RootSignature create_root_signature(const RootSignatureDescription& desc) override {
			std::vector<D3D12_ROOT_PARAMETER> root_parameters;
			std::vector<D3D12_DESCRIPTOR_RANGE> dx12_ranges;

			D3D12_DESCRIPTOR_RANGE bindless_range = {};
			if (desc.is_bindless) {
				D3D12_ROOT_PARAMETER root_consts = {
					.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
					.Constants = {
						.ShaderRegister = 0,
						.RegisterSpace = 0,
						.Num32BitValues = sizeof(DrawConstants) / 4,
					},
					.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
				};
				root_parameters.push_back(root_consts);

				bindless_range = {
					.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
					.NumDescriptors = MaxGlobalDescriptors,
					.BaseShaderRegister = 0,
					.RegisterSpace = 0,
					.OffsetInDescriptorsFromTableStart = 0
				};

				D3D12_ROOT_PARAMETER bindless_table = {
					.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
					.DescriptorTable = {
						.NumDescriptorRanges = 1,
						.pDescriptorRanges = &bindless_range,
					},
					.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL
				};
				root_parameters.push_back(bindless_table);
			} else {
				dx12_ranges.reserve(desc.ranges.size());

				for (auto const& range : desc.ranges) {
					D3D12_DESCRIPTOR_RANGE_TYPE dx12_type;
					switch (range.type) {
					case DescriptorType::ConstantBufferView:  dx12_type = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; break;
					case DescriptorType::ShaderResourceView:  dx12_type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
					case DescriptorType::UnorderedAccessView: dx12_type = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; break;
					case DescriptorType::Sampler:             dx12_type = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; break;
					}

					dx12_ranges.push_back({
						.RangeType = dx12_type,
						.NumDescriptors = range.count,
						.BaseShaderRegister = range.register_index,
						.RegisterSpace = 0,
						.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
					});
				}

				D3D12_ROOT_PARAMETER root_param = {
					.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
					.DescriptorTable = {
						.NumDescriptorRanges = (UINT)dx12_ranges.size(),
						.pDescriptorRanges = dx12_ranges.data(),
					},
					.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
				};
				root_parameters.push_back(root_param);
			}

			// Standard dynamic sampler definition for texture sampling inside pixel shaders
			D3D12_STATIC_SAMPLER_DESC static_sampler = {
				.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.MipLODBias = 0.0f,
				.MaxAnisotropy = 1,
				.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
				.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
				.MinLOD = 0.0f,
				.MaxLOD = D3D12_FLOAT32_MAX,
				.ShaderRegister = 0,
				.RegisterSpace = 0,
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
			};

			D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {
				.NumParameters = static_cast<UINT>(root_parameters.size()),
				.pParameters = root_parameters.data(),
				.NumStaticSamplers = 1,
				.pStaticSamplers = &static_sampler,
				.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
			};
			ComPtr<ID3DBlob> signature_blob;
			ComPtr<ID3DBlob> error_blob;

			if (FAILED(D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob))) {
				if (error_blob) {
					OutputDebugStringA(static_cast<const char*>(error_blob->GetBufferPointer()));
				}
				throw std::runtime_error("Failed to serialize Root Signature.");
			}

			ID3D12RootSignature* native_sig = nullptr;
			if (FAILED(_device_ctx.device->CreateRootSignature(
				0,
				signature_blob->GetBufferPointer(),
				signature_blob->GetBufferSize(),
				IID_PPV_ARGS(&native_sig)
			))) {
				throw std::runtime_error("Failed to create Root Signature.");
			}

			return RootSignature{ .native_handle = reinterpret_cast<void*>(native_sig) };
		}

		void destroy_root_signature(RootSignature& sig) override {
			if (!sig.native_handle) return;
			auto* native = reinterpret_cast<ID3D12RootSignature*>(sig.native_handle);
			native->Release();
			sig.native_handle = nullptr;
			defer([native] { native->Release(); });
		}

		u64 get_descriptor_heap_gpu_start() override {
			return _srv_cbv_uav_heap->GetGPUDescriptorHandleForHeapStart().ptr;
		}

		PipelineState create_pipeline_state(const PipelineDescription& desc) override {
			D3D12_INPUT_ELEMENT_DESC input_elements[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			D3D12_RENDER_TARGET_BLEND_DESC rt_blend = {};
			if (!desc.depth_test_enabled) {
				rt_blend.BlendEnable = TRUE;
				rt_blend.LogicOpEnable = FALSE;
				rt_blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
				rt_blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				rt_blend.BlendOp = D3D12_BLEND_OP_ADD;
				rt_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
				rt_blend.DestBlendAlpha = D3D12_BLEND_ZERO;
				rt_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
				rt_blend.LogicOp = D3D12_LOGIC_OP_NOOP;
				rt_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			} else {
				rt_blend.BlendEnable = FALSE;
				rt_blend.LogicOpEnable = FALSE;
				rt_blend.SrcBlend = D3D12_BLEND_ONE;
				rt_blend.DestBlend = D3D12_BLEND_ZERO;
				rt_blend.BlendOp = D3D12_BLEND_OP_ADD;
				rt_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
				rt_blend.DestBlendAlpha = D3D12_BLEND_ZERO;
				rt_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
				rt_blend.LogicOp = D3D12_LOGIC_OP_NOOP;
				rt_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}

			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
				.pRootSignature = reinterpret_cast<ID3D12RootSignature*>(desc.root_signature.native_handle),
				.VS = { desc.vertex_shader.bytecode.data(), desc.vertex_shader.bytecode.size() },
				.PS = { desc.pixel_shader.bytecode.data(), desc.pixel_shader.bytecode.size() },
				.BlendState = {
					.AlphaToCoverageEnable = FALSE,
					.IndependentBlendEnable = FALSE,
					.RenderTarget = { rt_blend },
				},
				.SampleMask = UINT_MAX,
				.RasterizerState = {
					.FillMode = desc.wireframe_mode ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID,
					.CullMode = D3D12_CULL_MODE_NONE,
					.FrontCounterClockwise = FALSE,
					.DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
					.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
					.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
					.DepthClipEnable = TRUE,
					.MultisampleEnable = FALSE,
					.AntialiasedLineEnable = FALSE,
					.ForcedSampleCount = 0,
					.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
				},
				.DepthStencilState = {
					.DepthEnable = desc.depth_test_enabled,
					.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
					.DepthFunc = D3D12_COMPARISON_FUNC_LESS,
					.StencilEnable = FALSE
				},
				.InputLayout = { input_elements, _countof(input_elements) },
				.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
				.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
				.NumRenderTargets = 1,
				.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
				.DSVFormat = DXGI_FORMAT_UNKNOWN,
				.SampleDesc = {.Count = 1, .Quality = 0 }
			};
			
			ID3D12PipelineState* native_pso = nullptr;
			if (FAILED(_device_ctx.device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&native_pso)))) {
				throw std::runtime_error("Failed to compile Pipeline State Object.");
			}

			return PipelineState{ .native_handle = reinterpret_cast<void*>(native_pso) };
		}

		void destroy_pipeline_state(PipelineState& pipeline) override {
			if (!pipeline.native_handle) return;
			auto* native = reinterpret_cast<ID3D12PipelineState*>(pipeline.native_handle);
			pipeline.native_handle = nullptr;
			defer([native] {native->Release(); });
		}

		DescriptorHandle allocate_descriptor(const GpuBuffer& buffer, DescriptorType type) override {
			GFX_ASSERT(_allocated_descriptors_count < MaxGlobalDescriptors, "Out of global shader descriptors!");

			D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = _srv_cbv_uav_heap->GetCPUDescriptorHandleForHeapStart();
			D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = _srv_cbv_uav_heap->GetGPUDescriptorHandleForHeapStart();

			const UINT offset = _allocated_descriptors_count * _descriptor_size;
			cpu_handle.ptr += offset;
			gpu_handle.ptr += offset;

			auto* resource = reinterpret_cast<ID3D12Resource*>(buffer.native_resources);

			if (type == DescriptorType::ConstantBufferView) {
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {
					.BufferLocation = resource->GetGPUVirtualAddress(),
					.SizeInBytes = buffer.size_in_bytes
				};
				_device_ctx.device->CreateConstantBufferView(&cbv_desc, cpu_handle);
			}
			// Expand here later for SRVs (Textures) or UAVs

			_allocated_descriptors_count++;

			return DescriptorHandle{
				.cpu_ptr = cpu_handle.ptr,
				.gpu_ptr = gpu_handle.ptr
			};
		}

		void bind_descriptor_heaps(void* native_command_list) override {
			auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(native_command_list);
			ID3D12DescriptorHeap* heaps[] = {_srv_cbv_uav_heap.Get() };
			list->SetDescriptorHeaps(1, heaps);
		}

		void submit_frame(const float clear_color[4], std::span<const DrawCommand> draws) override {
			begin_frame();

			auto* list = _cmd_ctx.list.Get();
			throw_if_failed(_cmd_ctx.list->Reset(_cmd_ctx.allocators[_swapchain.frame_index].Get(), nullptr), "CommandList Reset");

			ID3D12Resource* backbuffer = _swapchain.render_targets[_swapchain .frame_index].Get();
			D3D12_RESOURCE_BARRIER to_rt = make_transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			list->ResourceBarrier(1, &to_rt);

			D3D12_CPU_DESCRIPTOR_HANDLE rtv = _swapchain.rtv_heap->GetCPUDescriptorHandleForHeapStart();
			rtv.ptr += static_cast<SIZE_T>(_swapchain.frame_index) * _swapchain.rtvDescriptorSize;
			list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
			list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);

			D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)_swapchain.width,(float)_swapchain.height, 0.0f, 1.0f };
			D3D12_RECT scissor = { 0, 0, (LONG)_swapchain.width, (LONG)_swapchain.height };
			list->RSSetViewports(1, &vp);
			list->RSSetScissorRects(1, &scissor);

			bind_descriptor_heaps(list);
			const D3D12_GPU_DESCRIPTOR_HANDLE heap_start = _srv_cbv_uav_heap->GetGPUDescriptorHandleForHeapStart();

			void* bound_root_sig = nullptr;
			void* bound_pso = nullptr;
			for (const DrawCommand& cmd : draws) {
				// Only rebind whne the state actually changes.
				if (cmd.material.root_signature.native_handle != bound_root_sig) {
					bound_root_sig = cmd.material.root_signature.native_handle;
					list->SetGraphicsRootSignature(reinterpret_cast<ID3D12RootSignature*>(bound_root_sig));
					list->SetGraphicsRootDescriptorTable(1, heap_start); // root sig changes clears binding.
				}
				if (cmd.material.pipeline_state.native_handle != bound_pso) {
					bound_pso = cmd.material.pipeline_state.native_handle;
					list->SetPipelineState(reinterpret_cast<ID3D12PipelineState*>(bound_pso));
				}
				list->SetGraphicsRoot32BitConstants(0, sizeof(DrawConstants) / 4, &cmd.constants, 0);

				D3D12_VERTEX_BUFFER_VIEW vbv = {
					.BufferLocation = reinterpret_cast<ID3D12Resource*>(cmd.mesh.vertex_buffer.native_resources)->GetGPUVirtualAddress(),
					.SizeInBytes = cmd.mesh.vertex_buffer.size_in_bytes,
					.StrideInBytes = cmd.mesh.vertex_buffer.stride,
				};
				D3D12_INDEX_BUFFER_VIEW ibv = {
					.BufferLocation = reinterpret_cast<ID3D12Resource*>(cmd.mesh.index_buffer.native_resources)->GetGPUVirtualAddress(),
					.SizeInBytes = cmd.mesh.index_buffer.size_in_bytes,
					.Format = DXGI_FORMAT_R32_UINT,
				};
				list->IASetVertexBuffers(0, 1, &vbv);
				list->IASetIndexBuffer(&ibv);
				list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				list->DrawIndexedInstanced(cmd.mesh.index_count, 1, 0, 0, 0);
			}

			D3D12_RESOURCE_BARRIER to_present = make_transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			list->ResourceBarrier(1, &to_present);
			throw_if_failed(list->Close(), "CommandList Close");

			ID3D12CommandList* lists[] = { list };
			_device_ctx.command_queue->ExecuteCommandLists(1, lists);
		}

		GpuTexture create_texture(TextureDescription const& desc, const void* initial_data) override {
		    GFX_ASSERT(_allocated_descriptors_count < MaxGlobalDescriptors,
		               "Descriptor Heap Full!");

		    DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
		    switch (desc.format) {
		    case TextureFormat::RGBA8_UNorm: f = DXGI_FORMAT_R8G8B8A8_UNORM; break;
		    }
		    GFX_ASSERT(f != DXGI_FORMAT_UNKNOWN, "Unsupported texture format!");

		    D3D12_RESOURCE_DESC tex_desc = {
		        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		        .Alignment = 0,
		        .Width = desc.width,
		        .Height = desc.height,
		        .DepthOrArraySize = 1,
		        .MipLevels = 1,
		        .Format = f,
		        .SampleDesc = {.Count = 1},
		        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		        .Flags = D3D12_RESOURCE_FLAG_NONE,
		    };

		    D3D12_HEAP_PROPERTIES gpu_heap_props = {.Type = D3D12_HEAP_TYPE_DEFAULT};
		    ComPtr<ID3D12Resource> native_texture;
		    throw_if_failed(
		        _device_ctx.device->CreateCommittedResource(
		            &gpu_heap_props, D3D12_HEAP_FLAG_NONE, &tex_desc,
		            D3D12_RESOURCE_STATE_COMMON, nullptr,
		            IID_PPV_ARGS(&native_texture)),
		        "CreateCommittedResource(texture)");

		    u64 required_size = 0;
		    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
		    _device_ctx.device->GetCopyableFootprints(
		        &tex_desc, 0, 1, 0, &layout, nullptr, nullptr, &required_size);

		    D3D12_HEAP_PROPERTIES upload_heap_props = {.Type = D3D12_HEAP_TYPE_UPLOAD};
		    D3D12_RESOURCE_DESC upload_desc = {
		        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
		        .Width = required_size,
		        .Height = 1,
		        .DepthOrArraySize = 1,
		        .MipLevels = 1,
		        .Format = DXGI_FORMAT_UNKNOWN,
		        .SampleDesc = {.Count = 1},
		        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
		    };

		    ComPtr<ID3D12Resource> upload_buffer;
		    throw_if_failed(
		        _device_ctx.device->CreateCommittedResource(
		            &upload_heap_props, D3D12_HEAP_FLAG_NONE, &upload_desc,
		            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		            IID_PPV_ARGS(&upload_buffer)),
		        "CreateCommittedResource(texture upload)");

		    void* mapped_ptr = nullptr;
		    D3D12_RANGE read_range = {0, 0};
		    throw_if_failed(upload_buffer->Map(0, &read_range, &mapped_ptr),
		                    "Map(texture upload)");

		    const u8* src_pixels = static_cast<const u8*>(initial_data);
		    u8* dest_pixels = static_cast<u8*>(mapped_ptr);
		    const u32 src_pitch = desc.width * 4;
		    for (u32 y = 0; y < desc.height; ++y) {
		        std::memcpy(dest_pixels + y * layout.Footprint.RowPitch,
		                    src_pixels + y * src_pitch, src_pitch);
		    }
		    upload_buffer->Unmap(0, nullptr);

		    ComPtr<ID3D12CommandAllocator> temp_alloc;
		    ComPtr<ID3D12GraphicsCommandList> temp_list;
		    throw_if_failed(_device_ctx.device->CreateCommandAllocator(
		                        D3D12_COMMAND_LIST_TYPE_DIRECT,
		                        IID_PPV_ARGS(&temp_alloc)),
		                    "CreateCommandAllocator(texture)");
		    throw_if_failed(_device_ctx.device->CreateCommandList(
		                        0, D3D12_COMMAND_LIST_TYPE_DIRECT, temp_alloc.Get(),
		                        nullptr, IID_PPV_ARGS(&temp_list)),
		                    "CreateCommandList(texture)");

		    // COMMON -> COPY_DEST is implicit-promotable, but the explicit barrier
		    // is fine.
		    D3D12_RESOURCE_BARRIER barrier = make_transition(
		        native_texture.Get(), D3D12_RESOURCE_STATE_COMMON,
		        D3D12_RESOURCE_STATE_COPY_DEST);
		    temp_list->ResourceBarrier(1, &barrier);

		    D3D12_TEXTURE_COPY_LOCATION dst_loc = {
		        .pResource = native_texture.Get(),
		        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		        .SubresourceIndex = 0,
		    };
		    D3D12_TEXTURE_COPY_LOCATION src_loc = {
		        .pResource = upload_buffer.Get(),
		        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
		        .PlacedFootprint = layout,
		    };
		    temp_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

		    barrier = make_transition(
		        native_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
		        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		    temp_list->ResourceBarrier(1, &barrier);

		    throw_if_failed(temp_list->Close(), "Close texture upload list");

		    ID3D12CommandList* submission_list[] = {temp_list.Get()};
		    _device_ctx.command_queue->ExecuteCommandLists(1, submission_list);

		    flush(); // upload_buffer, temp_list and temp_alloc are safe after this

		    // Descriptor in the global heap
		    const u32 srv_index = _allocated_descriptors_count;
		    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle =
		        _srv_cbv_uav_heap->GetCPUDescriptorHandleForHeapStart();
		    cpu_handle.ptr += static_cast<SIZE_T>(srv_index) * _descriptor_size;

		    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
		        .Format = f,
		        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
		        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
		        .Texture2D = {.MipLevels = 1},
		    };
		    _device_ctx.device->CreateShaderResourceView(
		        native_texture.Get(), &srv_desc, cpu_handle);
		    _allocated_descriptors_count++;

		    return GpuTexture{
		        .native_resource = native_texture.Detach(),
		        .width = desc.width,
		        .height = desc.height,
		        .srv_index = srv_index,
		    };
		}

		void destroy_texture(GpuTexture& texture) override {
			if (!texture.native_resource) return;
			auto* resource = reinterpret_cast<ID3D12Resource*>(texture.native_resource);
			texture.native_resource = nullptr;
			defer([resource] { resource->Release(); });
		}

	private:
		void shutdown() {
			if (_device_ctx.command_queue && _sync.fence) {
				flush();
				collect_garbage(UINT64_MAX); // drain everything still queued.
			}

			if (_sync.fence_event) {
				CloseHandle(_sync.fence_event);
				_sync.fence_event = nullptr;
			}
		}

		UINT64 signal() {
			throw_if_failed(_device_ctx.command_queue->Signal(_sync.fence.Get(), ++_sync.last_signaled), "Signal");
			return _sync.last_signaled;
		}

		void wait_for_value(UINT64 value) {
			if (_sync.fence->GetCompletedValue() < value) {
				throw_if_failed(_sync.fence->SetEventOnCompletion(value, _sync.fence_event), "SetOnEventCompletion");
				WaitForSingleObject(_sync.fence_event, INFINITE);
			}
		}

		void flush() { wait_for_value(signal()); }

		void defer(std::function<void()> release) {
			_pending_releases.push_back({ _sync.last_signaled + 1, std::move(release) });
		}

		void collect_garbage(UINT64 completed) {
			while (!_pending_releases.empty() && _pending_releases.front().fence_value <= completed) {
				_pending_releases.front().release();
				_pending_releases.pop_front();
			}
		}

		struct PendingRelease {
			UINT64 fence_value;
			std::function<void()> release;
		};

		HWND _hwnd = nullptr;
		DX12DeviceContext _device_ctx;
		DX12Swapchain _swapchain;
		DX12CommandContext _cmd_ctx;
		DX12SyncContext _sync;
		ComPtr<ID3D12DescriptorHeap> _srv_cbv_uav_heap;
		UINT _descriptor_size = 0;
		UINT _allocated_descriptors_count = 0;

		std::deque<PendingRelease> _pending_releases;
		UINT64 _frame_fence[DX12Swapchain::FrameCount] = {};
		bool _frame_begun = false;

		static constexpr UINT MaxGlobalDescriptors = 1024; // Big enough for simple rendering.
	};

	std::unique_ptr<GraphicsContext> GraphicsContext::create() {
		return std::make_unique<DX12Context>();
	}
}
#endif
