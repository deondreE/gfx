#pragma once
#include <memory>
#include <string>
#include <functional>

namespace gfx {
	struct ResizeEvent {
		int width;
		int height;
	};
	struct KeyEvent {
		unsigned int keycode;
		bool isDown;
	};

	class Window {
	public:
		Window(std::wstring const& name, int width, int height);
		virtual ~Window();

		Window(Window const&) = delete;
		Window& operator=(Window const&) = delete;

		void show();
		bool process_messages();

		int get_width() const;
		int get_height() const;
		void set_title(std::wstring const& title);
		bool should_close() const { return _should_close; }
		bool is_visible() const;

		void* GetNatvieWindowHandle() const;
		void* GetNativeDisplayHandle() const;

		void set_resize_callback(std::function<void(ResizeEvent)> cb) { _resizeCallback = std::move(cb); }
		void set_key_callback(std::function<void(KeyEvent)> cb) { _keyCallback = std::move(cb); }


	protected:
		virtual void OnKeyDown(KeyEvent const& e);
		virtual void OnResize(ResizeEvent const& e);

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
		bool _should_close = false;

		std::function<void(ResizeEvent)> _resizeCallback;
		std::function<void(KeyEvent)> _keyCallback;
	};
}
