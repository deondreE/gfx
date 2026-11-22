#pragma once 

#include "window.hpp"
#include "graphics_context.hpp"
#include <memory>

namespace gfx {

	class Application {
	public:
		Application(std::wstring const& name, int width, int height);
		virtual ~Application();

		Application(Application const&) = delete;
		Application& operator=(Application const&) = delete;

		void run();
		void close();
	protected:
		virtual void on_init() {}
		virtual void on_update(float deltaTime) {}
		virtual void on_render() {}
		virtual void on_shutdown() {}

		Window& get_window() { return *_window; }
		GraphicsContext& GetGraphicsContext() { return *_graphics; }
	private:
		std::unique_ptr<Window> _window;
		std::unique_ptr<GraphicsContext> _graphics;
		bool _running = false;
	};

}