# PostProcessVolume3D

`PostProcessVolume3D` applies selected `Environment` settings according to the active camera's position. Add it from the Create Node dialog, then resize its box in the Inspector or drag the box handles in the 3D viewport. The cyan outline is the active box; the larger outline shows the blend radius.

## Setup

1. Create a `PostProcessVolume3D` node in the level.
2. Set `Box Size` and move the node so the box covers the intended area.
3. `Environment` is created automatically for each volume. Expand that resource in the Inspector to edit its settings, then choose the sections to apply under `Environment Overrides`. Each checked section applies the corresponding settings from this resource.
4. Set `Blend Radius` to control how far the effect fades in and out. A value of `0` switches at the box boundary. The transition uses a smooth curve.
5. Use `Blend Weight` to reduce the effect strength, `Priority` to order overlapping volumes, or disable the volume with `Enabled`.

The volume reads `Environment` values from its embedded resource. Numeric and color values blend continuously; enum, boolean, and resource values switch at the halfway point. Higher priority volumes are applied after lower priority volumes. The active camera's explicit Environment is used as the base when present; otherwise the current World3D Environment is used.

## What the volume affects

The volume affects the camera's `Environment` settings, including background/sky, ambient and reflected light, tonemapping, screen-space effects, glow, fog, and color adjustments. `Environment Overrides` lets it leave unrelated sections unchanged. This can make the camera see a black background and disable sky-based ambient contribution while it is inside a room volume.

This is a camera effect, so it changes the whole rendered view while the camera is inside or near the volume. It does not create a physical barrier: use room geometry and shadow-casting lights to block direct light. Camera Attributes and Compositor resources are not blended by this initial implementation.
