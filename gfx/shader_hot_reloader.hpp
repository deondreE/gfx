#pragma once

#include "graphics_context.hpp"
#include "graphics_resources.hpp"
#include <chrono>
#include <exception>
#include <fileapi.h>
#include <handleapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <iostream>
#include <winbase.h>
#include <winnt.h>
#if defined(WIN32)
#include <windows.h>
#endif

namespace gfx {
	struct LiveMaterial {
		std::wstring shader_path;
		Material* material_reference;
		PipelineDescription pipeline_desc;
	};

	struct PendingSwapCommand {
		Material* material_to_update;
		void* old_pipeline_handle;
		void* new_pipeline_handle;
	};

	class ShaderHotReloader {
		ShaderHotReloader(GraphicsContext& context)
			: _context(context), _running(false)
		{

		}

		~ShaderHotReloader() {
			stop();
		}

		void start(std::wstring const directory_to_watch) {
			_directory = directory_to_watch;
			_running = true;
			_thread = std::thread(&ShaderHotReloader::monitor_directory, this);
		}

		void stop() {
			if (_running) {
				_running = false;
				if (_thread.joinable()) {
					_thread.join();
				}
			}
		}

		void register_material(std::wstring const& path, Material& material, PipelineDescription const& desc) {
			std::lock_guard<std::mutex> lock(_materials_mutex);
			_live_materials.push_back({path, &material, desc});
		}

		void process_pending_swaps() {
			std::lock_guard<std::mutex> lock(_queue_mutex);
			while (!_pending_swaps.empty()) {
				PendingSwapCommand cmd = _pending_swaps.front();
				_pending_swaps.pop();

				cmd.material_to_update->pipeline_state.native_handle = cmd.new_pipeline_handle;

				PipelineState old_pso = { .native_handle = cmd.old_pipeline_handle };
				_context.destroy_pipeline_state(old_pso);

				std::cout << "[Shader Hot-Reload]: Successfully swapped pipeline state on main thread!" << std::endl;
            }
 		}

	private:
		void monitor_directory() {
		#if defined(WIN32)
			HANDLE dir_handle = CreateFileW(
				_directory.c_str(),
				FILE_LIST_DIRECTORY,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS,
				nullptr
			);

			if (dir_handle == INVALID_HANDLE_VALUE) {
				return;
			}

			std::vector<u8> buffer(32768);
			DWORD bytes_returned = 0;

			while (_running) {
				if (ReadDirectoryChangesW(dir_handle,
					buffer.data(),
					static_cast<DWORD>(buffer.size()),
					FALSE,
					FILE_NOTIFY_CHANGE_LAST_WRITE,
					&bytes_returned,
					nullptr,
					nullptr))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));

					auto* notify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
					do {
						std::wstring filename(notify->FileName, notify->FileNameLength / sizeof(wchar_t));
						recompile_matching_material(filename);

						notify = notify->NextEntryOffset ?
							reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<u8*>(notify) + notify->NextEntryOffset) : nullptr;
					} while (notify);
				}
			}
			CloseHandle(dir_handle);
		#endif
		}

		void recompile_matching_material(std::wstring const& changed_filname) {
			std::lock_guard<std::mutex> lock(_materials_mutex);

			for (auto& live : _live_materials) {
				if (live.shader_path.find(changed_filname) != std::wstring::npos) {
					try {
						std::wcout << L"[Shader Background Worker]: Recompiling: " << changed_filname << std::endl;

                        // Compile and create the pipeline fully on this background thread!
                        ShaderBlob new_vs = _context.compile_shader(live.shader_path, "VSMain", ShaderStage::Vertex);
                        ShaderBlob new_ps = _context.compile_shader(live.shader_path, "PSMain", ShaderStage::Pixel);

                        PipelineDescription updated_desc = live.pipeline_desc;
                        updated_desc.vertex_shader = new_vs;
                        updated_desc.pixel_shader = new_ps;

                        PipelineState new_pso = _context.create_pipeline_state(updated_desc);

                        // Queue the finished pipeline state for the main thread to swap
                        {
                            std::lock_guard<std::mutex> queue_lock(_queue_mutex);
                            _pending_swaps.push({
                                .material_to_update = live.material_reference,
                                .old_pipeline_handle = live.material_reference->pipeline_state.native_handle,
                                .new_pipeline_handle = new_pso.native_handle
                            });
                        }

                        std::cout << "[Shader Background Worker]: Compilation completed. Enqueued for swap." << std::endl;
					}
					catch(const  std::exception& e) {
						std::cout << "[Shader Background Worker Error]: " << e.what() << std::endl;
					}
				}
			}
		}

		GraphicsContext& _context;
		std::wstring _directory;
		std::atomic<bool> _running;
		std::thread _thread;

		std::mutex _materials_mutex;
		std::vector<LiveMaterial> _live_materials;

		std::mutex _queue_mutex;
		std::queue<PendingSwapCommand> _pending_swaps;
	};
}
