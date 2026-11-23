/**************************************************************************/
/*  screen_flash_3d.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "screen_flash_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/animation/tween.h"
#include "scene/gui/color_rect.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/viewport.h"

real_t ScreenFlash3D::get_strength_at_camera() const {
	if (!is_inside_tree()) {
		return 0.0;
	}
	const Camera3D *camera = get_viewport()->get_camera_3d();
	if (!camera) {
		return strength; // No camera to measure from: full strength.
	}
	const real_t distance = camera->get_global_position().distance_to(get_global_position());
	if (distance <= inner_radius) {
		return strength;
	}
	if (distance >= radius) {
		return 0.0;
	}
	return strength * (1.0 - (distance - inner_radius) / MAX(real_t(0.001), radius - inner_radius));
}

void ScreenFlash3D::flash() {
	const real_t amount = get_strength_at_camera();
	if (amount <= 0.001) {
		return;
	}
	// Its own layer over the view it is in; frees itself when it has faded.
	CanvasLayer *layer = memnew(CanvasLayer);
	layer->set_layer(100);
	ColorRect *rect = memnew(ColorRect);
	rect->set_color(Color(color.r, color.g, color.b, color.a * amount));
	rect->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	rect->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	layer->add_child(rect);
	get_viewport()->add_child(layer);
	Ref<Tween> tween = layer->create_tween();
	tween->set_ease(Tween::EASE_OUT);
	tween->set_trans(Tween::TRANS_QUAD);
	tween->tween_property(rect, NodePath("color:a"), 0.0, MAX(0.02, double(duration)));
	tween->tween_callback(callable_mp(static_cast<Node *>(layer), &Node::queue_free));
}

void ScreenFlash3D::set_color(const Color &p_color) {
	color = p_color;
}

Color ScreenFlash3D::get_color() const {
	return color;
}

void ScreenFlash3D::set_strength(real_t p_strength) {
	strength = CLAMP(p_strength, real_t(0.0), real_t(1.0));
}

real_t ScreenFlash3D::get_strength() const {
	return strength;
}

void ScreenFlash3D::set_duration(real_t p_duration) {
	duration = MAX(real_t(0.02), p_duration);
}

real_t ScreenFlash3D::get_duration() const {
	return duration;
}

void ScreenFlash3D::set_inner_radius(real_t p_radius) {
	inner_radius = MAX(real_t(0.0), p_radius);
}

real_t ScreenFlash3D::get_inner_radius() const {
	return inner_radius;
}

void ScreenFlash3D::set_radius(real_t p_radius) {
	radius = MAX(real_t(0.0), p_radius);
}

real_t ScreenFlash3D::get_radius() const {
	return radius;
}

void ScreenFlash3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &ScreenFlash3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &ScreenFlash3D::get_color);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &ScreenFlash3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &ScreenFlash3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_duration", "duration"), &ScreenFlash3D::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &ScreenFlash3D::get_duration);
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &ScreenFlash3D::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &ScreenFlash3D::get_inner_radius);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &ScreenFlash3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &ScreenFlash3D::get_radius);
	ClassDB::bind_method(D_METHOD("get_strength_at_camera"), &ScreenFlash3D::get_strength_at_camera);
	ClassDB::bind_method(D_METHOD("flash"), &ScreenFlash3D::flash);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0.02,5,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:m"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater,suffix:m"), "set_radius", "get_radius");
}
