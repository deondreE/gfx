#include "gfxpch.hpp"
#include "window.hpp"

#if defined(_WIN32)

namespace gfx {

	struct Window::Impl {
		HWND hwnd = nullptr;
		HINSTANCE hInst = nullptr;
		std::wstring className = L"w32_app";
		Window* publicWindow = nullptr;
		int width = 0;
		int height = 0;
		bool isVisible = false;
		bool should_close = false;

		static LRESULT CALLBACK RouteMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
			if (uMsg == WM_NCCREATE) {
				auto* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
			}
			
			auto* const pImpl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (pImpl) {
				return pImpl->HandleMessage(hwnd, uMsg, wParam, lParam);
			}
			return DefWindowProcW(hwnd, uMsg, wParam, lParam);
		}

		LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
			if (!publicWindow) {
				return DefWindowProcW(hWnd, uMsg, wParam, lParam);
			}

			switch (uMsg) {
			case WM_NCCREATE:
				return TRUE;
			case WM_SIZE: {
				width = LOWORD(lParam);
				height = HIWORD(lParam);

				ResizeEvent e{ width, height };
				publicWindow->OnResize(e);
				if (publicWindow->_resizeCallback) {
					publicWindow->_resizeCallback(e);
				}
				return 0;
			}
			case WM_SYSKEYDOWN:
			case WM_KEYDOWN: {
				KeyEvent e{ static_cast<unsigned int>(wParam), true };
				publicWindow->OnKeyDown(e);
				if (publicWindow->_keyCallback) {
					publicWindow->_keyCallback(e);
				}
				return 0;
			}
			case WM_SYSKEYUP:
			case WM_KEYUP: {
				KeyEvent e{ static_cast<unsigned int>(wParam), false };
				publicWindow->OnKeyDown(e);
				if (publicWindow->_keyCallback) {
					publicWindow->_keyCallback(e);
				}
				return 0;
			}
			case WM_SHOWWINDOW: {
				isVisible = static_cast<bool>(wParam);
				return 0;
			}
			case WM_CLOSE:
				should_close = true;
				return 0;
			case WM_DESTROY: {
				should_close = true;
				PostQuitMessage(0);
				return 0;
			}
			}
			return DefWindowProcW(hWnd, uMsg, wParam, lParam);
		}
	};

	Window::Window(std::wstring const& title, int width, int height)
		: impl(std::make_unique<Impl>()) 
	{
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

		impl->publicWindow = this;
		impl->hInst = GetModuleHandleW(nullptr);
		impl->width = width;
		impl->height = height;

		impl->className = L"gfx_window_class";

		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);	
		wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = Impl::RouteMessage;
		wc.hInstance = impl->hInst;
		wc.lpszClassName = impl->className.c_str();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		RegisterClassExW(&wc);

		RECT wr = { 0, 0, width, height };
		AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
		int windowWidth = wr.right - wr.left;
		int windowHeight = wr.bottom - wr.top;

		impl->hwnd = CreateWindowExW(
			0, 
			impl->className.c_str(), 
			title.c_str(),
			WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
			windowWidth, windowHeight, 
			nullptr, nullptr, impl->hInst, impl.get()
		);
	}

	Window::~Window() {
		if (impl->hwnd) {
			DestroyWindow(impl->hwnd);
		}
		UnregisterClassW(impl->className.c_str(), impl->hInst);
	}

	void Window::set_title(const std::wstring &title) {
		if (impl->hwnd) {
			SetWindowTextW(impl->hwnd, title.c_str());
		}
	}

	void Window::show() {
		if (impl->hwnd) {
			ShowWindow(impl->hwnd, SW_SHOW);
			UpdateWindow(impl->hwnd);
			impl->isVisible = true;
		}
	}

	bool Window::process_messages() {
		MSG msg = {};
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				impl->should_close = true;
				return false;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return !impl->should_close;
	}

	int Window::get_width() const { return impl->width; }
	int Window::get_height() const { return impl->height; }
	bool Window::is_visible() const { return impl->isVisible; }

	void* Window::GetNativeDisplayHandle() const { return reinterpret_cast<void*>(impl->hInst); }
	void* Window::GetNatvieWindowHandle() const { return reinterpret_cast<void*>(impl->hwnd); }
}
#elif defined(__linux__)

#endif

namespace gfx {
	// Virtual stub defaults
	void Window::OnKeyDown(KeyEvent const& e) {}
	void Window::OnResize(ResizeEvent const& e) {}
}
