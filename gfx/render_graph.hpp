#pragma once
#include "graphics_context.hpp"
#include <vector>
#include <string>
#include <functional>

#include "util/util.hpp"

#if defined(WIN32)
#include "dx12_context.hpp"
#endif

namespace gfx 
{
	enum class RGResourceState {
		Present,
		RenderTarget,
		ShaderResource,
		UnorderedAccess,
	};

	struct RGResource {
		std::string name;
		void* native_resource = nullptr;
		RGResourceState current_state = RGResourceState::Present;
	};

	struct RGPass {
		std::string name;
		std::vector<RGResource*> writes;
		std::vector<RGResource*> reads;
		std::function<void(GraphicsContext& ctx)> execute_callback;
	};

	class RenderGraph {
	private:
		static D3D12_RESOURCE_STATES get_dx12_state(RGResourceState state) {
			switch (state) {
			case RGResourceState::Present:        return D3D12_RESOURCE_STATE_PRESENT;
			case RGResourceState::RenderTarget:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
			case RGResourceState::ShaderResource: return D3D12_RESOURCE_STATE_GENERIC_READ;
			case RGResourceState::UnorderedAccess:return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			}
			return D3D12_RESOURCE_STATE_COMMON;
		}

		static D3D12_RESOURCE_BARRIER create_barrier(void* resource, RGResourceState before, RGResourceState after) {
			D3D12_RESOURCE_BARRIER barrier = {
				.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
				.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			};
			barrier.Transition = {
				.pResource = reinterpret_cast<ID3D12Resource*>(resource),
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = get_dx12_state(before),
				.StateAfter = get_dx12_state(after),
			};
			return barrier;
		}

		std::vector<RGResource> _resources;
		std::vector<RGPass> _passes;

	public:
		RenderGraph() = default;

		void reset() {
			_resources.clear();
			_passes.clear();
		}

		RGResource* import_resource(std::string const& name, void* native_handle, RGResourceState initial_state) {
			_resources.push_back({ name, native_handle, initial_state });
			return &_resources.back();
		}

		template<typename F>
		void add_pass(std::string const& name, std::vector<RGResource*> const& reads, std::vector<RGResource*> const& writes, F&& execute) {
			_passes.push_back({
				.name = name,
				.writes = writes,
				.reads = reads,
				.execute_callback = std::forward<F>(execute)
			});
		}

		void execute(GraphicsContext& ctx, DX12CommandContext& cmd_ctx) {
			auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(cmd_ctx.list.Get());

			for (auto const& pass : _passes) {
				// Compile & Inject Transitions
				std::vector<D3D12_RESOURCE_BARRIER> barriers;
				for (auto* res : pass.writes) {
					if (res->current_state != RGResourceState::RenderTarget) {
						barriers.push_back(create_barrier(res->native_resource, res->current_state, RGResourceState::RenderTarget));
						res->current_state = RGResourceState::RenderTarget;
					}
				}

				if (!barriers.empty()) {
					list->ResourceBarrier((UINT)(barriers.size()), barriers.data());
				}
				// Execute step
				pass.execute_callback(ctx);

				// Prepare backbuffers back for present state if required
				std::vector<D3D12_RESOURCE_BARRIER> post_barriers;
				for (auto* res : pass.writes) {
					if (res->name == "Backbuffer") {
						post_barriers.push_back(create_barrier(res->native_resource, RGResourceState::RenderTarget, RGResourceState::Present));
						res->current_state = RGResourceState::Present;
					}
				}

				if (!post_barriers.empty()) {
					list->ResourceBarrier(static_cast<UINT>(post_barriers.size()), post_barriers.data());
				}
			}
		}
	};

} // namespace gfx
