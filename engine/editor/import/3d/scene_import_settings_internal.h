/**************************************************************************/
/*  scene_import_settings.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


#pragma once

// Constants shared by the files of SceneImportSettingsScreen (Model View):
// scene_import_settings.cpp and its _hitboxes/_lod/_environment/_materials
// parts. Internal to the editor, not a public API.

#include "core/math/math_defs.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"

// Preview camera and the reference camera the LOD transition estimate uses.
static constexpr real_t PREVIEW_FOV_DEGREES = 75.0;
static constexpr real_t REFERENCE_VIEWPORT_WIDTH = 1920.0;
static constexpr real_t REFERENCE_ASPECT = 16.0 / 9.0;

// Model View's own preview environment (scene_import_settings_environment.cpp).
Ref<Environment> create_model_view_environment(const Ref<ShaderMaterial> &p_sky_material);
