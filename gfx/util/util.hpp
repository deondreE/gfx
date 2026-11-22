#pragma once

#include "platform.hpp"
#include <iostream>
//#define static static
//#define local_persistent static
//#define global static

// Assertion
#ifndef _DEBUG
#define GFX_ASSERT(expr, msg) \
	do { \
		if (!(expr)) { \
			GFX_DEBUG_BREAK(); \
		} \
	} while (0)
#else
#define GFX_ASSERT(expr, msg)
#endif

namespace gfx {
	namespace util {
		// com_ptr safe release
		template<typename T>
		GFX_FORCE_INLINE void safe_release(T * &resource)
		{
			if (resource) {
				resource->Release();
				resource = nullptr;
			}
		}

		template<typename T>
		GFX_FORCE_INLINE void unused(T&&...) {}

		// Alignment logic for memory arenas and constant/uniform buffers
		template<typename T>
		GFX_FORCE_INLINE constexpr T align_up(T value, T alignment) {
			return (value + alignment - 1) & ~(alignment - 1);
		}

		GFX_FORCE_INLINE constexpr unsigned int megabytes(unsigned int value) {
			return value * 1024 * 1024;
		}

		GFX_FORCE_INLINE constexpr unsigned int kilobytes(unsigned int value) {
			return value * 1024;
		}
	} // gfx::util
}
