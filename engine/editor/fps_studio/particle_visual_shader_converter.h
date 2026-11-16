/**************************************************************************/
/*  particle_visual_shader_converter.h                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class ParticleProcessMaterial;
class VisualShader;

// Builds a particles-mode VisualShader graph that reproduces the common
// ParticleProcessMaterial features (emission shape, initial velocity,
// gravity and accelerations, damping, scale/color/alpha over life,
// angle and angular velocity, lifetime randomness, rigid collision).
// Features the graph does not reproduce are listed in r_warnings, one
// short English line each (empty when everything converted).
namespace FPSParticleVisualShaderConverter {
Ref<VisualShader> convert(const Ref<ParticleProcessMaterial> &p_material, String &r_warnings);
}
