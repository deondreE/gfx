#pragma once

// Platform stuff & things
#if defined(__NX__) || defined(NN_SDK_BUILD) || defined(NN_NINTENDO_SDK)
// Nintendo Switch (NX)
	#define GFX_PLATFORM_SWITCH 1
	#define GFX_PLATFORM_CONSOLE 1
#elif defined(__PROSPERO__)
	#define GFX_PLATFORM_PS5 1
	#define GFX_PLATFORM_CONSOLE 1
#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
	#include <d3d12.h>
	#include <wrl.h>
	#include <shellscalingapi.h>
	#include <windef.h>
	#include <winuser.h>
	#pragma comment(lib, "Shcore.lib")
	#define GFX_PLATFORM_WINDOWS 1
	#define GFX_PLATFORM_DESKTOP 1
#elif defined(__linux__) || defined(__gnu_linux__)
	#define GFX_PLATFORM_LINUX 1
	#define GFX_PLATFORM_DESKTOP 1
#elif defined(__APPLE__) && defined(__MACH__)
#include <TargetConditionals.h>
	#if TARGET_OS_IPHONE
		#define GFX_PLATFORM_IPHONE 1
		#define GFX_PLATFORM_MOBILE 1
	#else
		#define GFX_PLATFORM_MACOS 1
		#define GFX_PLATFORM_DESKTOP 1
	#endif
#elif defined(__ANDROID__)
	#define GFX_PLATFORM_ANDROID 1 
	#define GFX_PLATFORM_MOBILE 1
#endif

// Compiler defs: MSVC and GCC
#if defined(_MSC_VER)
	#define GFX_COMPILER_MSVC 1
	#define GFX_FORCE_INLINE __forceinline
	#define GFX_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
	#define GFX_COMPILER_CLANG 1
	#define GFX_FORCE_INLINE __attribute__((always_inline)) inline
	#if defined(GFX_PLATFORM_SWITCH)
		#define GFX_DEBUG_BREAK() __asm__ volatile("brk #0")
	#elif defined(GFX_PLATFORM_PS5)
		#define GFX_DEBUG_BREAK() __asm__ volatile("int $3")
	#else 
		#define GFX_DEBUG_BREAK() __builtin_trap()
	#endif
#endif
