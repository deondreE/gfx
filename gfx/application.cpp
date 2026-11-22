#include "gfxpch.hpp"
#include "application.hpp"

namespace gfx {

	Application::Application(std::wstring const& name, int width, int height) {
		_window = std::make_unique<Window>(name, width, height);
		_graphics = GraphicsContext::create();
	};
	Application::~Application() {};


	void Application::run() {
		_window->show();
		_running = true;

		if (_graphics) {
			_graphics->init(*_window);
		}

		on_init();

		auto lastTime = std::chrono::high_resolution_clock::now();

		while (_running) {
			if (!_window->process_messages()) {
				_running = false;
				break;
			}

			auto currentTime = std::chrono::high_resolution_clock::now();
			float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
			lastTime = currentTime;

			on_update(deltaTime);

			if (_graphics) {
				on_render();
				_graphics->present();
			}
		}

		on_shutdown();
	};

	void Application::close() {
		_running = false;
	};
}
