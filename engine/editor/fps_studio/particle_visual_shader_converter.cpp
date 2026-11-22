/**************************************************************************/
/*  particle_visual_shader_converter.cpp                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_visual_shader_converter.h"

#include "modules/visual_shader/visual_shader.h"
#include "modules/visual_shader/vs_nodes/visual_shader_nodes.h"
#include "modules/visual_shader/vs_nodes/visual_shader_particle_nodes.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/particle_process_material.h"

namespace {

using PM = ParticleProcessMaterial;
using PT = VisualShaderNode::PortType;

struct ExprPort {
	PT type;
	String name;
	Variant default_value; // Used when the port is left unconnected.
};

// Adds nodes to one graph (Start, Process, ...) in readable columns:
// column 0 = inputs and constants, then particle nodes, expressions and
// finally the output node.
struct GraphBuilder {
	Ref<VisualShader> shader;
	VisualShader::Type type = VisualShader::TYPE_START;
	int next_id = 2;
	String *warnings = nullptr;

	static Vector2 position(int p_column, int p_row) {
		return Vector2(-1300 + p_column * 340, p_row * 180);
	}

	int add(const Ref<VisualShaderNode> &p_node, int p_column, int p_row) {
		const int id = next_id++;
		shader->add_node(type, p_node, position(p_column, p_row), id);
		return id;
	}

	void link(int p_from, int p_from_port, int p_to, int p_to_port) {
		if (p_from < 0 || p_to < 0) {
			return;
		}
		if (shader->connect_nodes(type, p_from, p_from_port, p_to, p_to_port) != OK) {
			*warnings += vformat("Internal: could not connect node %d:%d to %d:%d in graph %d.\n", p_from, p_from_port, p_to, p_to_port, int(type));
		}
	}

	void place_output(int p_column, int p_row) {
		shader->set_node_position(type, VisualShader::NODE_ID_OUTPUT, position(p_column, p_row));
	}

	int input(const String &p_name, int p_column, int p_row) {
		Ref<VisualShaderNodeInput> node;
		node.instantiate();
		node->set_input_name(p_name);
		return add(node, p_column, p_row);
	}

	int float_constant(float p_value, int p_column, int p_row) {
		Ref<VisualShaderNodeFloatConstant> node;
		node.instantiate();
		node->set_constant(p_value);
		return add(node, p_column, p_row);
	}

	int vec3_constant(const Vector3 &p_value, int p_column, int p_row) {
		Ref<VisualShaderNodeVec3Constant> node;
		node.instantiate();
		node->set_constant(p_value);
		return add(node, p_column, p_row);
	}

	int color_constant(const Color &p_value, int p_column, int p_row) {
		Ref<VisualShaderNodeColorConstant> node;
		node.instantiate();
		node->set_constant(p_value);
		return add(node, p_column, p_row);
	}

	// Per-particle random value in [min, max]. In Process it repeats every
	// frame for the same particle (the seed is derived from the particle).
	int random_scalar(float p_min, float p_max, int p_column, int p_row) {
		Ref<VisualShaderNodeParticleRandomness> node;
		node.instantiate();
		node->set_op_type(VisualShaderNodeParticleRandomness::OP_TYPE_SCALAR);
		node->set_input_port_default_value(1, p_min);
		node->set_input_port_default_value(2, p_max);
		return add(node, p_column, p_row);
	}

	int random_vec2(const Vector2 &p_min, const Vector2 &p_max, int p_column, int p_row) {
		Ref<VisualShaderNodeParticleRandomness> node;
		node.instantiate();
		node->set_op_type(VisualShaderNodeParticleRandomness::OP_TYPE_VECTOR_2D);
		node->set_input_port_default_value(1, p_min);
		node->set_input_port_default_value(2, p_max);
		return add(node, p_column, p_row);
	}

	int curve(const Ref<CurveTexture> &p_texture, int p_column, int p_row) {
		Ref<VisualShaderNodeCurveTexture> node;
		node.instantiate();
		node->set_texture(p_texture);
		return add(node, p_column, p_row);
	}

	// Samples a 1D texture (gradient) with textureLod(.., 0) so it also works
	// outside fragment shaders.
	int texture_lod(const Ref<Texture2D> &p_texture, int p_column, int p_row, int &r_lod_node) {
		Ref<VisualShaderNodeTexture> node;
		node.instantiate();
		node->set_source(VisualShaderNodeTexture::SOURCE_TEXTURE);
		node->set_texture(p_texture);
		const int id = add(node, p_column, p_row);
		r_lod_node = float_constant(0.0, p_column - 1, p_row);
		link(r_lod_node, 0, id, 1);
		return id;
	}

	int expression(const Vector<ExprPort> &p_inputs, const Vector<ExprPort> &p_outputs, const String &p_code, int p_column, int p_row) {
		Ref<VisualShaderNodeExpression> node;
		node.instantiate();
		for (int i = 0; i < p_inputs.size(); i++) {
			node->add_input_port(i, p_inputs[i].type, p_inputs[i].name);
			if (p_inputs[i].default_value.get_type() != Variant::NIL) {
				node->set_input_port_default_value(i, p_inputs[i].default_value);
			}
		}
		for (int i = 0; i < p_outputs.size(); i++) {
			node->add_output_port(i, p_outputs[i].type, p_outputs[i].name);
		}
		node->set_expression(p_code);
		const int lines = p_code.count("\n") + 1;
		node->set_size(Size2(320, MAX(140, 60 + lines * 20 + MAX(p_inputs.size(), p_outputs.size()) * 24)));
		return add(node, p_column, p_row);
	}
};

void warn(String &r_warnings, const String &p_line) {
	r_warnings += p_line + "\n";
}

bool param_used(const PM *p_material, PM::Parameter p_param) {
	return !Math::is_zero_approx(p_material->get_param_min(p_param)) || !Math::is_zero_approx(p_material->get_param_max(p_param));
}

void collect_warnings(PM *p_material, String &r_warnings) {
	const PM::EmissionShape shape = p_material->get_emission_shape();
	if (shape == PM::EMISSION_SHAPE_POINTS || shape == PM::EMISSION_SHAPE_DIRECTED_POINTS) {
		warn(r_warnings, "Emission points (texture) are not converted; particles spawn at the emitter center.");
	}
	if (shape == PM::EMISSION_SHAPE_RING && !Math::is_equal_approx(float(p_material->get_emission_ring_cone_angle()), 90.0f)) {
		warn(r_warnings, "Ring cone angle is not converted (the ring is a straight cylinder).");
	}
	if (p_material->get_flatness() > 0.0f) {
		warn(r_warnings, "Spread flatness is not converted.");
	}
	if (param_used(p_material, PM::PARAM_ORBIT_VELOCITY) || p_material->get_param_texture(PM::PARAM_ORBIT_VELOCITY).is_valid()) {
		warn(r_warnings, "Orbit velocity is not converted.");
	}
	if (param_used(p_material, PM::PARAM_RADIAL_VELOCITY) || param_used(p_material, PM::PARAM_DIRECTIONAL_VELOCITY)) {
		warn(r_warnings, "Radial and directional velocity are not converted.");
	}
	if (p_material->get_velocity_limit_curve().is_valid()) {
		warn(r_warnings, "Velocity limit curve is not converted.");
	}
	if (p_material->get_inherit_velocity_ratio() > 0.0) {
		warn(r_warnings, "Inherit velocity ratio is not converted.");
	}
	if (param_used(p_material, PM::PARAM_HUE_VARIATION)) {
		warn(r_warnings, "Hue variation is not converted.");
	}
	if (p_material->get_emission_curve().is_valid()) {
		warn(r_warnings, "Emission color curve is not converted.");
	}
	if (p_material->get_turbulence_enabled()) {
		warn(r_warnings, "Turbulence is not converted.");
	}
	if (p_material->get_sub_emitter_mode() != PM::SUB_EMITTER_DISABLED) {
		warn(r_warnings, "Sub-emitter triggers are not converted; use emit_subparticle() in an Expression node.");
	}
	if (param_used(p_material, PM::PARAM_SCALE_OVER_VELOCITY)) {
		warn(r_warnings, "Scale over velocity is not converted.");
	}
	static const PM::Parameter curve_params[] = { PM::PARAM_LINEAR_ACCEL, PM::PARAM_RADIAL_ACCEL, PM::PARAM_TANGENTIAL_ACCEL, PM::PARAM_ANGLE, PM::PARAM_ANGULAR_VELOCITY, PM::PARAM_ANIM_SPEED, PM::PARAM_ANIM_OFFSET };
	for (PM::Parameter param : curve_params) {
		if (p_material->get_param_texture(param).is_valid()) {
			warn(r_warnings, "Acceleration, angle and animation curves are not converted (the min/max values are).");
			break;
		}
	}
	if (p_material->get_param_texture(PM::PARAM_SCALE).is_valid() && Object::cast_to<CurveTexture>(p_material->get_param_texture(PM::PARAM_SCALE).ptr()) == nullptr) {
		warn(r_warnings, "Per-axis (XYZ) scale curve is not converted.");
	}
	if (p_material->get_param_texture(PM::PARAM_DAMPING).is_valid() && Object::cast_to<CurveTexture>(p_material->get_param_texture(PM::PARAM_DAMPING).ptr()) == nullptr) {
		warn(r_warnings, "Damping curve is not a CurveTexture and is not converted.");
	}
	if (p_material->get_alpha_curve().is_valid() && Object::cast_to<CurveTexture>(p_material->get_alpha_curve().ptr()) == nullptr) {
		warn(r_warnings, "Alpha curve is not a CurveTexture and is not converted.");
	}
	if (p_material->get_particle_flag(PM::PARTICLE_FLAG_DISABLE_Z)) {
		warn(r_warnings, "Disable Z is not converted.");
	}
	if (p_material->get_particle_flag(PM::PARTICLE_FLAG_DAMPING_AS_FRICTION)) {
		warn(r_warnings, "Damping as friction is approximated with regular damping.");
	}
	if (p_material->get_particle_flag(PM::PARTICLE_FLAG_INHERIT_EMITTER_SCALE)) {
		warn(r_warnings, "Inherit emitter scale is not converted.");
	}
	if (p_material->get_collision_mode() != PM::COLLISION_DISABLED) {
		warn(r_warnings, "Collision is handled in the \"Collision\" Expression node (COLLIDED built-in); the collision size stays on the emitter (Collision Base Size).");
	}
}

// --- Start: spawn position, initial velocity --------------------------------

void build_start(const Ref<VisualShader> &p_shader, PM *p_material, String &r_warnings) {
	GraphBuilder g;
	g.shader = p_shader;
	g.type = VisualShader::TYPE_START;
	g.warnings = &r_warnings;

	// Emission shape.
	int emitter = -1;
	Vector3 center;
	switch (p_material->get_emission_shape()) {
		case PM::EMISSION_SHAPE_SPHERE:
		case PM::EMISSION_SHAPE_SPHERE_SURFACE: {
			Ref<VisualShaderNodeParticleSphereEmitter> node;
			node.instantiate();
			const float radius = p_material->get_emission_sphere_radius();
			node->set_input_port_default_value(0, radius);
			node->set_input_port_default_value(1, p_material->get_emission_shape() == PM::EMISSION_SHAPE_SPHERE_SURFACE ? radius : 0.0f);
			emitter = g.add(node, 1, 0);
		} break;
		case PM::EMISSION_SHAPE_BOX: {
			Ref<VisualShaderNodeParticleBoxEmitter> node;
			node.instantiate();
			// The node takes the full size; the material stores half extents.
			node->set_input_port_default_value(0, p_material->get_emission_box_extents() * 2.0);
			emitter = g.add(node, 1, 0);
		} break;
		case PM::EMISSION_SHAPE_RING: {
			Ref<VisualShaderNodeParticleRingEmitter> node;
			node.instantiate();
			const float height = p_material->get_emission_ring_height();
			node->set_input_port_default_value(0, float(p_material->get_emission_ring_radius()));
			node->set_input_port_default_value(1, float(p_material->get_emission_ring_inner_radius()));
			node->set_input_port_default_value(2, height);
			emitter = g.add(node, 1, 0);
			// The node builds the ring around +Y (height 0..h); the material
			// uses its ring axis and a centered height.
			Vector3 axis = p_material->get_emission_ring_axis();
			axis = axis.is_zero_approx() ? Vector3(0, 0, 1) : axis.normalized();
			const Vector3 up(0, 1, 0);
			const Vector3 rotation_axis = up.cross(axis);
			if (rotation_axis.length() > 0.0001) {
				Ref<VisualShaderNodeParticleMultiplyByAxisAngle> rotate;
				rotate.instantiate();
				rotate->set_degrees_mode(false);
				rotate->set_input_port_default_value(1, rotation_axis.normalized());
				rotate->set_input_port_default_value(2, Math::acos(CLAMP(up.dot(axis), -1.0f, 1.0f)));
				const int rotate_id = g.add(rotate, 2, 0);
				g.link(emitter, 0, rotate_id, 0);
				emitter = rotate_id;
			} else if (up.dot(axis) < 0.0) {
				warn(r_warnings, "Downward ring axis is treated as +Y.");
			}
			center = -axis * height * 0.5;
		} break;
		default:
			break;
	}
	const int position = g.expression(
			{ { PT::PORT_TYPE_VECTOR_3D, "shape_point", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "shape_center", center },
					{ PT::PORT_TYPE_VECTOR_3D, "shape_scale", p_material->get_emission_shape_scale() },
					{ PT::PORT_TYPE_VECTOR_3D, "shape_offset", p_material->get_emission_shape_offset() } },
			{ { PT::PORT_TYPE_VECTOR_3D, "spawn_position", Variant() } },
			"spawn_position = (shape_point + shape_center) * shape_scale + shape_offset;", 3, 0);
	g.link(emitter, 0, position, 0);

	// Initial velocity: a random direction inside the spread cone.
	Ref<VisualShaderNodeParticleConeVelocity> cone;
	cone.instantiate();
	Vector3 direction = p_material->get_direction();
	direction = direction.is_zero_approx() ? Vector3(1, 0, 0) : direction.normalized();
	cone->set_input_port_default_value(0, direction);
	cone->set_input_port_default_value(1, p_material->get_spread());
	const int cone_id = g.add(cone, 1, 2);
	const int speed = g.random_scalar(p_material->get_param_min(PM::PARAM_INITIAL_LINEAR_VELOCITY), p_material->get_param_max(PM::PARAM_INITIAL_LINEAR_VELOCITY), 1, 3);
	const int velocity = g.expression(
			{ { PT::PORT_TYPE_VECTOR_3D, "launch_direction", direction },
					{ PT::PORT_TYPE_SCALAR, "launch_speed", 0.0f } },
			{ { PT::PORT_TYPE_VECTOR_3D, "launch_velocity", Variant() } },
			"launch_velocity = launch_direction * launch_speed;", 3, 2);
	g.link(cone_id, 0, velocity, 0);
	g.link(speed, 0, velocity, 1);

	// Base color (Process applies the color ramps and the alpha curve).
	const int color = g.color_constant(p_material->get_color(), 2, 4);
	const int split = g.expression(
			{ { PT::PORT_TYPE_VECTOR_4D, "base_color", Vector4(1, 1, 1, 1) } },
			{ { PT::PORT_TYPE_VECTOR_3D, "base_rgb", Variant() }, { PT::PORT_TYPE_SCALAR, "base_alpha", Variant() } },
			"base_rgb = base_color.rgb;\nbase_alpha = base_color.a;", 3, 4);
	g.link(color, 0, split, 0);

	// StartOutput: 1 velocity, 2 color, 3 alpha, 4 position.
	g.link(velocity, 0, VisualShader::NODE_ID_OUTPUT, 1);
	g.link(split, 0, VisualShader::NODE_ID_OUTPUT, 2);
	g.link(split, 1, VisualShader::NODE_ID_OUTPUT, 3);
	g.link(position, 0, VisualShader::NODE_ID_OUTPUT, 4);
	g.place_output(4, 1);
}

// --- Start Custom: CUSTOM = (angle, age, animation frame, life limit) ------

void build_start_custom(const Ref<VisualShader> &p_shader, PM *p_material, String &r_warnings) {
	GraphBuilder g;
	g.shader = p_shader;
	g.type = VisualShader::TYPE_START_CUSTOM;
	g.warnings = &r_warnings;

	const int angle = g.random_scalar(p_material->get_param_min(PM::PARAM_ANGLE), p_material->get_param_max(PM::PARAM_ANGLE), 1, 0);
	const int anim_offset = g.random_scalar(p_material->get_param_min(PM::PARAM_ANIM_OFFSET), p_material->get_param_max(PM::PARAM_ANIM_OFFSET), 1, 1);
	const int life_roll = g.random_scalar(0.0, 1.0, 1, 2);
	const int custom = g.expression(
			{ { PT::PORT_TYPE_SCALAR, "angle_degrees", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "anim_offset", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "life_roll", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "lifetime_randomness", float(p_material->get_lifetime_randomness()) } },
			{ { PT::PORT_TYPE_VECTOR_3D, "custom_start", Variant() }, { PT::PORT_TYPE_SCALAR, "life_limit", Variant() } },
			"// x = angle (radians, used by billboard materials), y = age (0..1 of LIFETIME),\n"
			"// z = animation frame, w = life limit (lifetime randomness).\n"
			"custom_start = vec3(radians(angle_degrees), 0.0, anim_offset);\n"
			"life_limit = 1.0 - lifetime_randomness * life_roll;",
			3, 1);
	g.link(angle, 0, custom, 0);
	g.link(anim_offset, 0, custom, 1);
	g.link(life_roll, 0, custom, 2);

	// CustomStartOutput: 0 custom (rgb), 1 custom alpha.
	g.link(custom, 0, VisualShader::NODE_ID_OUTPUT, 0);
	g.link(custom, 1, VisualShader::NODE_ID_OUTPUT, 1);
	g.place_output(4, 1);
}

// Age of the particle from CUSTOM (shared by Process and Process Custom).
int add_life(GraphBuilder &g, int p_column, int p_row) {
	const int custom = g.input("custom", p_column - 1, p_row);
	const int life = g.expression(
			{ { PT::PORT_TYPE_VECTOR_4D, "custom_data", Vector4() } },
			{ { PT::PORT_TYPE_SCALAR, "life", Variant() }, { PT::PORT_TYPE_SCALAR, "life_uv", Variant() }, { PT::PORT_TYPE_BOOLEAN, "alive", Variant() } },
			"life = clamp(custom_data.y / max(custom_data.w, 0.0001), 0.0, 1.0);\n"
			"// Keep texture lookups off the edges (textures may repeat).\n"
			"life_uv = clamp(life, 0.002, 0.998);\n"
			"alive = custom_data.y <= custom_data.w;",
			p_column, p_row);
	g.link(custom, 0, life, 0);
	return life;
}

int add_accelerator(GraphBuilder &g, PM *p_material, PM::Parameter p_param, VisualShaderNodeParticleAccelerator::Mode p_mode, int p_column, int p_row) {
	if (!param_used(p_material, p_param)) {
		return -1;
	}
	Ref<VisualShaderNodeParticleAccelerator> node;
	node.instantiate();
	node->set_mode(p_mode);
	const float min_value = p_material->get_param_min(p_param);
	const float max_value = p_material->get_param_max(p_param);
	// amount * mix(1, rand, randomness) spans [amount * (1 - randomness), amount],
	// so amount is the end of the range farther from zero (-5..0 is amount -5).
	// A range across zero (-2..2) has no such form: it becomes 0..amount.
	const float amount = Math::abs(max_value) >= Math::abs(min_value) ? max_value : min_value;
	const float other = Math::abs(max_value) >= Math::abs(min_value) ? min_value : max_value;
	const float randomness = Math::is_zero_approx(amount) ? 0.0f : CLAMP(1.0f - other / amount, 0.0f, 1.0f);
	if (!Math::is_zero_approx(amount) && other / amount < 0.0f && g.warnings) {
		*g.warnings += vformat(TTR("An acceleration range across zero (%s to %s) was converted to 0 to %s.") + "\n", String::num(min_value, 2), String::num(max_value, 2), String::num(amount, 2));
	}
	node->set_input_port_default_value(0, Vector3(amount, amount, amount));
	node->set_input_port_default_value(1, randomness);
	node->set_input_port_default_value(2, p_material->get_gravity().is_zero_approx() ? Vector3(0, -1, 0) : p_material->get_gravity());
	return g.add(node, p_column, p_row);
}

// --- Process: forces, damping, collision, color over life -------------------

void build_process(const Ref<VisualShader> &p_shader, PM *p_material, String &r_warnings) {
	GraphBuilder g;
	g.shader = p_shader;
	g.type = VisualShader::TYPE_PROCESS;
	g.warnings = &r_warnings;

	const int life = add_life(g, 1, 0);

	// Velocity: gravity + accelerations, then damping.
	const int velocity_in = g.input("velocity", 1, 2);
	const int delta = g.input("delta", 1, 3);
	const int gravity = g.vec3_constant(p_material->get_gravity(), 1, 4);
	const int linear = add_accelerator(g, p_material, PM::PARAM_LINEAR_ACCEL, VisualShaderNodeParticleAccelerator::MODE_LINEAR, 1, 5);
	const int radial = add_accelerator(g, p_material, PM::PARAM_RADIAL_ACCEL, VisualShaderNodeParticleAccelerator::MODE_RADIAL, 1, 6);
	const int tangential = add_accelerator(g, p_material, PM::PARAM_TANGENTIAL_ACCEL, VisualShaderNodeParticleAccelerator::MODE_TANGENTIAL, 1, 7);
	const int attractor = p_material->is_attractor_interaction_enabled() ? g.input("attractor_force", 1, 8) : -1;
	const int damping = g.random_scalar(p_material->get_param_min(PM::PARAM_DAMPING), p_material->get_param_max(PM::PARAM_DAMPING), 1, 9);
	int damping_curve = -1;
	if (Ref<CurveTexture> curve = p_material->get_param_texture(PM::PARAM_DAMPING); curve.is_valid()) {
		damping_curve = g.curve(curve, 2, 9);
		g.link(life, 1, damping_curve, 0);
	}
	const int velocity = g.expression(
			{ { PT::PORT_TYPE_VECTOR_3D, "velocity_in", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "gravity_acc", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "linear_acc", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "radial_acc", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "tangential_acc", Vector3() },
					{ PT::PORT_TYPE_VECTOR_3D, "attractor_acc", Vector3() },
					{ PT::PORT_TYPE_SCALAR, "damping_amount", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "damping_scale", 1.0f },
					{ PT::PORT_TYPE_SCALAR, "dt", 0.0f } },
			{ { PT::PORT_TYPE_VECTOR_3D, "velocity_out", Variant() } },
			"vec3 v = velocity_in + (gravity_acc + linear_acc + radial_acc + tangential_acc + attractor_acc) * dt;\n"
			"float speed = length(v);\n"
			"float drop = damping_amount * damping_scale * dt;\n"
			"velocity_out = speed > 0.0 ? v / speed * max(speed - drop, 0.0) : v;",
			3, 4);
	g.link(velocity_in, 0, velocity, 0);
	g.link(gravity, 0, velocity, 1);
	g.link(linear, 0, velocity, 2);
	g.link(radial, 0, velocity, 3);
	g.link(tangential, 0, velocity, 4);
	g.link(attractor, 0, velocity, 5);
	g.link(damping, 0, velocity, 6);
	g.link(damping_curve, 0, velocity, 7);
	g.link(delta, 0, velocity, 8);

	// Collision response (the emitter's collision size decides when).
	int velocity_final = velocity;
	int alive_final = life;
	int alive_port = 2;
	const PM::CollisionMode collision = p_material->get_collision_mode();
	if (collision != PM::COLLISION_DISABLED) {
		String code;
		code += "// COLLIDED, COLLISION_NORMAL and COLLISION_DEPTH are particle built-ins.\n";
		code += "velocity_out = velocity_in;\n";
		code += "alive_out = alive_in;\n";
		code += "if (COLLIDED) {\n";
		if (collision == PM::COLLISION_HIDE_ON_CONTACT) {
			code += "	alive_out = false;\n";
		} else {
			code += "	TRANSFORM[3].xyz += COLLISION_NORMAL * COLLISION_DEPTH;\n";
			code += "	float response = dot(COLLISION_NORMAL, velocity_in);\n";
			code += "	if (response < 0.0) {\n";
			code += "		// Slow contacts slide instead of bouncing (as ParticleProcessMaterial), so particles settle.\n";
			code += "		float bounce_trigger = step(2.0 / clamp(bounce + 1.0, 1.0, 2.0), abs(response));\n";
			code += "		vec3 v = velocity_in - COLLISION_NORMAL * response * (1.0 + bounce * bounce_trigger);\n";
			code += "		vec3 slide = v - COLLISION_NORMAL * dot(COLLISION_NORMAL, v);\n";
			code += "		velocity_out = v - slide * clamp(friction, 0.0, 1.0);\n";
			code += "	}\n";
		}
		code += "}";
		const int collide = g.expression(
				{ { PT::PORT_TYPE_VECTOR_3D, "velocity_in", Vector3() },
						{ PT::PORT_TYPE_BOOLEAN, "alive_in", true },
						{ PT::PORT_TYPE_SCALAR, "bounce", p_material->get_collision_bounce() },
						{ PT::PORT_TYPE_SCALAR, "friction", p_material->get_collision_friction() } },
				{ { PT::PORT_TYPE_VECTOR_3D, "velocity_out", Variant() }, { PT::PORT_TYPE_BOOLEAN, "alive_out", Variant() } },
				code, 4, 4);
		g.link(velocity, 0, collide, 0);
		g.link(life, 2, collide, 1);
		velocity_final = collide;
		alive_final = collide;
		alive_port = 1;
	}

	// Color: base color * initial ramp (random per particle) * ramp over
	// life, alpha curve over life.
	const int base_color = g.color_constant(p_material->get_color(), 1, 11);
	int initial_ramp = -1;
	if (Ref<Texture2D> ramp = p_material->get_color_initial_ramp(); ramp.is_valid()) {
		int lod = -1;
		initial_ramp = g.texture_lod(ramp, 2, 12, lod);
		const int roll = g.random_vec2(Vector2(0.002, 0.5), Vector2(0.998, 0.5), 1, 13);
		g.link(roll, 0, initial_ramp, 0);
	}
	int ramp_over_life = -1;
	if (Ref<Texture2D> ramp = p_material->get_color_ramp(); ramp.is_valid()) {
		int lod = -1;
		ramp_over_life = g.texture_lod(ramp, 2, 14, lod);
		g.link(life, 1, ramp_over_life, 0);
	}
	int alpha_curve = -1;
	if (Ref<CurveTexture> curve = p_material->get_alpha_curve(); curve.is_valid()) {
		alpha_curve = g.curve(curve, 2, 15);
		g.link(life, 1, alpha_curve, 0);
	}
	const int color = g.expression(
			{ { PT::PORT_TYPE_VECTOR_4D, "base_color", Vector4(1, 1, 1, 1) },
					{ PT::PORT_TYPE_VECTOR_4D, "initial_ramp", Vector4(1, 1, 1, 1) },
					{ PT::PORT_TYPE_VECTOR_4D, "life_ramp", Vector4(1, 1, 1, 1) },
					{ PT::PORT_TYPE_SCALAR, "alpha_scale", 1.0f } },
			{ { PT::PORT_TYPE_VECTOR_3D, "color_rgb", Variant() }, { PT::PORT_TYPE_SCALAR, "color_alpha", Variant() } },
			"vec4 c = base_color * initial_ramp * life_ramp;\n"
			"color_rgb = c.rgb;\n"
			"color_alpha = c.a * alpha_scale;",
			3, 12);
	g.link(base_color, 0, color, 0);
	g.link(initial_ramp, 0, color, 1);
	g.link(ramp_over_life, 0, color, 2);
	g.link(alpha_curve, 0, color, 3);

	// ProcessOutput: 0 active, 1 velocity, 2 color, 3 alpha.
	g.link(alive_final, alive_port, VisualShader::NODE_ID_OUTPUT, 0);
	g.link(velocity_final, 0, VisualShader::NODE_ID_OUTPUT, 1);
	g.link(color, 0, VisualShader::NODE_ID_OUTPUT, 2);
	g.link(color, 1, VisualShader::NODE_ID_OUTPUT, 3);
	g.place_output(6, 4);
}

// --- Process Custom: age, rotation, animation, scale over life -------------

void build_process_custom(const Ref<VisualShader> &p_shader, PM *p_material, String &r_warnings) {
	GraphBuilder g;
	g.shader = p_shader;
	g.type = VisualShader::TYPE_PROCESS_CUSTOM;
	g.warnings = &r_warnings;

	const int life = add_life(g, 1, 0);
	const int custom = g.input("custom", 1, 2);
	const int delta = g.input("delta", 1, 3);
	const int lifetime = g.input("lifetime", 1, 4);
	const int angular_velocity = g.random_scalar(p_material->get_param_min(PM::PARAM_ANGULAR_VELOCITY), p_material->get_param_max(PM::PARAM_ANGULAR_VELOCITY), 1, 5);
	const int anim_speed = g.random_scalar(p_material->get_param_min(PM::PARAM_ANIM_SPEED), p_material->get_param_max(PM::PARAM_ANIM_SPEED), 1, 6);
	const int age = g.expression(
			{ { PT::PORT_TYPE_VECTOR_4D, "custom_in", Vector4() },
					{ PT::PORT_TYPE_SCALAR, "dt", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "total_life", 1.0f },
					{ PT::PORT_TYPE_SCALAR, "spin_degrees", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "frames_per_life", 0.0f } },
			{ { PT::PORT_TYPE_VECTOR_3D, "custom_out", Variant() }, { PT::PORT_TYPE_SCALAR, "angle", Variant() } },
			"float life_step = dt / max(total_life, 0.0001);\n"
			"custom_out = vec3(custom_in.x + radians(spin_degrees) * dt, custom_in.y + life_step, custom_in.z + frames_per_life * life_step / max(custom_in.w, 0.0001));\n"
			"angle = custom_out.x;",
			3, 3);
	g.link(custom, 0, age, 0);
	g.link(delta, 0, age, 1);
	g.link(lifetime, 0, age, 2);
	g.link(angular_velocity, 0, age, 3);
	g.link(anim_speed, 0, age, 4);

	// Transform: keep the position, rebuild orientation and scale.
	const int transform_in = g.input("transform", 1, 8);
	const int velocity = g.input("velocity", 1, 9);
	const int scale = g.random_scalar(p_material->get_param_min(PM::PARAM_SCALE), p_material->get_param_max(PM::PARAM_SCALE), 1, 10);
	int scale_curve = -1;
	if (Ref<CurveTexture> curve = p_material->get_param_texture(PM::PARAM_SCALE); curve.is_valid()) {
		scale_curve = g.curve(curve, 2, 11);
		g.link(life, 1, scale_curve, 0);
	}
	String code;
	code += "float s = max(scale_amount * scale_curve, 0.0001);\n";
	if (p_material->get_particle_flag(PM::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY)) {
		code += "// Align Y to velocity.\n";
		code += "vec3 y_axis = length(move) > 0.0 ? normalize(move) : vec3(0.0, 1.0, 0.0);\n";
		code += "vec3 x_axis = abs(y_axis.y) < 0.999 ? normalize(cross(y_axis, vec3(0.0, 1.0, 0.0))) : vec3(1.0, 0.0, 0.0);\n";
		code += "vec3 z_axis = cross(x_axis, y_axis);\n";
		code += "mat4 m = mat4(vec4(x_axis, 0.0), vec4(y_axis, 0.0), vec4(z_axis, 0.0), vec4(0.0, 0.0, 0.0, 1.0));\n";
	} else if (p_material->get_particle_flag(PM::PARTICLE_FLAG_ROTATE_Y)) {
		code += "// Rotate around Y by the particle angle.\n";
		code += "mat4 m = mat4(vec4(cos(angle), 0.0, -sin(angle), 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(sin(angle), 0.0, cos(angle), 0.0), vec4(0.0, 0.0, 0.0, 1.0));\n";
	} else {
		code += "// Keep the spawn orientation (billboards rotate by CUSTOM.x).\n";
		code += "mat4 m = mat4(vec4(normalize(xform[0].xyz), 0.0), vec4(normalize(xform[1].xyz), 0.0), vec4(normalize(xform[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));\n";
	}
	code += "m[0] *= s;\nm[1] *= s;\nm[2] *= s;\n";
	code += "m[3] = xform[3];\n";
	code += "transform_out = m;";
	const int transform = g.expression(
			{ { PT::PORT_TYPE_TRANSFORM, "xform", Transform3D() },
					{ PT::PORT_TYPE_VECTOR_3D, "move", Vector3() },
					{ PT::PORT_TYPE_SCALAR, "angle", 0.0f },
					{ PT::PORT_TYPE_SCALAR, "scale_amount", 1.0f },
					{ PT::PORT_TYPE_SCALAR, "scale_curve", 1.0f } },
			{ { PT::PORT_TYPE_TRANSFORM, "transform_out", Variant() } },
			code, 3, 9);
	g.link(transform_in, 0, transform, 0);
	g.link(velocity, 0, transform, 1);
	g.link(age, 1, transform, 2);
	g.link(scale, 0, transform, 3);
	g.link(scale_curve, 0, transform, 4);

	// CustomProcessOutput: 0 custom (rgb), 5 transform.
	g.link(age, 0, VisualShader::NODE_ID_OUTPUT, 0);
	g.link(transform, 0, VisualShader::NODE_ID_OUTPUT, 5);
	g.place_output(5, 5);
}

} // namespace

Ref<VisualShader> FPSParticleVisualShaderConverter::convert(const Ref<ParticleProcessMaterial> &p_material, String &r_warnings) {
	r_warnings = String();
	ERR_FAIL_COND_V(p_material.is_null(), Ref<VisualShader>());
	PM *material = p_material.ptr();

	Ref<VisualShader> shader;
	shader.instantiate();
	shader->set_mode(Shader::MODE_PARTICLES);

	collect_warnings(material, r_warnings);
	build_start(shader, material, r_warnings);
	build_start_custom(shader, material, r_warnings);
	build_process(shader, material, r_warnings);
	build_process_custom(shader, material, r_warnings);
	return shader;
}
