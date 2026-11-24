#pragma once


// Public API
#include "../application.hpp"
#include "../renderer.hpp"
#include "../graphics_context.hpp"
#include "../graphics_resources.hpp"
#include "../gfx_math.hpp"
#include "../camera.hpp"
#include "../util/paths.hpp"

inline constexpr unsigned int megabytes(unsigned int value) {
	return value * 1024 * 1024;
}

inline constexpr unsigned int kilobytes(unsigned int value) {
	return value * 1024;
}


template<typename T>
inline constexpr T align_up(T value, T alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}
