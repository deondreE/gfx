#pragma once

#include "gfx_math.hpp"

namespace gfx {

enum class ProjectionType {
	Perspective,
	Orthographic
};

struct Viewport {
	float x = 0.0f;
	float y = 0.0f;
	float width = 1280.0f;
	float height = 720.0f;
	float znear = 0.1f;
	float zfar = 1000.0f;

	inline float get_aspect_ratio() const {
		return (height > 0.0f) ? (width / height) : 1.0f;
	}
};

struct CameraProjection {
	ProjectionType type = ProjectionType::Perspective;
	float fov_y = 0.785398f;  // 45degrees.
	float ortho_size = 10.0f; // vertical size
};

class Camera {
public:
	Camera() :
		_position{0.0f, 0.0f, 0.0f},
		_target{0.0f, 0.0f, -1.0f},
		_up{0.0f, 1.0f, 0.0f} {}
	virtual ~Camera() = default;

	Vec3 get_position() const { return _position; }
	void set_position(Vec3 const& pos) { _position = pos; }

	Vec3 get_target() const { return _target; }
	void set_target(Vec3 const& target) { _target = target; }

	Vec3 get_up() const { return _up; }
	void set_up(Vec3 const& up) { _up = up; }

	CameraProjection get_projection_config() const { return _projection; }
	void set_projection_config(CameraProjection const& proj) { _projection = proj; }

	Mat4 get_view() const {
		return Mat4::look_at(_position, _target, _up);
	}

	Mat4 get_projection(Viewport const& viewport) const {
		if (_projection.type == ProjectionType::Orthographic) {
			float half_h = _projection.ortho_size * 0.5f;
			float half_w = half_h * viewport.get_aspect_ratio();
			return Mat4::ortho(-half_w, half_w,-half_h, half_h, viewport.znear, viewport.zfar);
		}
		return Mat4::perspective(_projection.fov_y, viewport.get_aspect_ratio(), viewport.znear, viewport.zfar);
	}

	Mat4 get_view_projection(Viewport const& viewport) const {
		return Mat4::mul(get_view(), get_projection(viewport));
	}

	void configure_isometric(Vec3 const& focus_point, float distance = 15.0f) {
		_target = focus_point;

		constexpr float degrees_to_radians = 3.14159265f / 180.0f;
		const float pitch_radians = 30.0f * degrees_to_radians;
		const float yaw_radians = 45.0f * degrees_to_radians;
		const float horizontal_distance = distance * std::cos(pitch_radians);

		const float offset_x = horizontal_distance * std::sin(yaw_radians);
		const float offset_y = distance * std::sin(pitch_radians);
		const float offset_z = horizontal_distance * std::cos(yaw_radians);

		_position = {
			focus_point.x + offset_x,
			focus_point.y + offset_y,
			focus_point.z + offset_z,
		};
		_up = { 0.0f, 1.0f, 0.0f };

		_projection.type = ProjectionType::Perspective;
		_projection.fov_y = 45.0f * degrees_to_radians;
	}

	void configure_orthographic_ui(Viewport const& viewport) {
	  _position = { viewport.width * 0.5f, viewport.height * 0.5f, 1.0f };
      _target   = { viewport.width * 0.5f, viewport.height * 0.5f, 0.0f };
      _up       = { 0.0f, 1.0f, 0.0f };
      _projection.type = ProjectionType::Orthographic;
      _projection.ortho_size = viewport.height;
	}

private:
	Vec3 _position, _target, _up;
	CameraProjection _projection;
};

}
