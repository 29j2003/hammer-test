#!/usr/bin/env python3
"""Simulate the SSAO maths of shaders/src/fidelity_ssao_ps2x.fxc on test scenes.

The scenes are ray-cast from planes and encoded exactly like the engine's
destination-alpha depth (linear, 8 bit, 512 unit range). The simulation
mirrors the shader step by step, so wrong signs or a broken range check show
up as numbers. Expected results:

- a flat floor seen at a grazing angle stays at 1.0 (no false occlusion)
- edges where floor and walls meet get darker, open wall areas stay at 1.0
- the blur (fidelity_ssao_blur_ps2x.fxc) removes the 4 x 4 sampling pattern
  on smooth surfaces completely (no grain) and keeps depth edges

    python tools/ssao_sim.py
"""

import math

import numpy as np

WIDTH, HEIGHT = 320, 240
FOV = 90.0
Z_NEAR, Z_FAR = 4.0, 4096.0
DEPTH_RANGE = 512.0
DEPTH_SCALE = DEPTH_RANGE * (Z_FAR - Z_NEAR) / Z_FAR
TAN_HALF_FOV = math.tan(math.radians(FOV / 2))
PROJ_SCALE = 0.5 * WIDTH / TAN_HALF_FOV

# Scenes as planes (normal, d) with normal . p = d in view space (x right, y up, z forward)
FLOOR = [((0, 1, 0), -64)]
ROOM = [((0, 1, 0), -64), ((0, 0, 1), 200), ((1, 0, 0), 120)]


def view_rays():
    """Per-pixel view direction (x, y) for z = 1."""
    xs = (np.arange(WIDTH) + 0.5) / WIDTH * 2 - 1
    ys = 1 - (np.arange(HEIGHT) + 0.5) / HEIGHT * 2
    return np.meshgrid(xs * TAN_HALF_FOV, ys * TAN_HALF_FOV * HEIGHT / WIDTH)


def render_depth(planes):
    """View-space depth of the nearest plane for every pixel."""
    ray_x, ray_y = view_rays()
    depth = np.full(ray_x.shape, 1e9)
    for normal, distance in planes:
        denominator = normal[0] * ray_x + normal[1] * ray_y + normal[2]
        with np.errstate(divide="ignore", invalid="ignore"):
            t = distance / denominator
        t[(t <= 0) | ~np.isfinite(t)] = 1e9
        depth = np.minimum(depth, t)
    return depth


def encode_dest_alpha(depth):
    """Encode depth like the engine's DepthToDestAlpha, quantized to 8 bit."""
    alpha = np.clip((depth - Z_NEAR) / DEPTH_SCALE, 0, 1)
    return np.round(alpha * 255) / 255


def tile_noise(x, y):
    """4 x 4 pattern, as FidelityTileNoise in fidelity_common.h (x, y at pixel centres)."""
    index = np.floor(np.mod(x * 0.25, 1) * 4) + 4 * np.floor(np.mod(y * 0.25, 1) * 4)
    return np.mod(index * 5 / 16 + 1 / 32, 1), np.mod(index * 11 / 16 + 1 / 32, 1)


def bilinear(image, px, py):
    """Bilinear fetch with clamped addressing; pixel centres at integer coordinates."""
    height, width = image.shape
    px = np.clip(px, 0, width - 1)
    py = np.clip(py, 0, height - 1)
    x0 = np.floor(px).astype(int)
    y0 = np.floor(py).astype(int)
    x1 = np.minimum(x0 + 1, width - 1)
    y1 = np.minimum(y0 + 1, height - 1)
    fx = px - x0
    fy = py - y0
    return (
        image[y0, x0] * (1 - fx) * (1 - fy)
        + image[y0, x1] * fx * (1 - fy)
        + image[y1, x0] * (1 - fx) * fy
        + image[y1, x1] * fx * fy
    )


def simulate(alpha, pairs=8, radius=24.0, strength=1.2, bias=1.5, fade_start=300.0, fade_end=460.0):
    """Occlusion term (1 = unoccluded) for a dest-alpha image, mirroring the shader."""
    depth = alpha * DEPTH_SCALE + Z_NEAR

    def sample(px, py):
        """Bilinear depth fetch with clamped addressing."""
        return bilinear(alpha, px, py) * DEPTH_SCALE + Z_NEAR

    pixel_y, pixel_x = np.mgrid[0:HEIGHT, 0:WIDTH].astype(float)
    noise_a, noise_b = tile_noise(pixel_x + 0.5, pixel_y + 0.5)

    fade = 1 - np.clip((depth - fade_start) / (fade_end - fade_start), 0, 1)
    radius_px = np.clip(radius * PROJ_SCALE / depth, 1.5, 0.05 * HEIGHT)
    sin_r = np.sin(noise_a * math.pi / pairs)
    cos_r = np.cos(noise_a * math.pi / pairs)

    occlusion = np.zeros_like(depth)
    for k in range(pairs):
        angle = math.pi / pairs * k
        dir_x = math.cos(angle) * cos_r - math.sin(angle) * sin_r
        dir_y = math.cos(angle) * sin_r + math.sin(angle) * cos_r
        scale = (k + 0.5 + noise_b) / (pairs + 1)
        offset_x = dir_x * radius_px * scale
        offset_y = dir_y * radius_px * scale

        depth_a = sample(pixel_x + offset_x, pixel_y + offset_y)
        depth_b = sample(pixel_x - offset_x, pixel_y - offset_y)
        plane = 2 / (1 / depth_a + 1 / depth_b)
        crease = depth - plane - bias
        front = depth - np.minimum(depth_a, depth_b)
        in_range = np.clip(2 - front / radius, 0, 1)
        occlusion += np.clip(crease / radius, 0, 1) * in_range

    occlusion /= pairs
    return 1 - np.clip(occlusion * strength * fade, 0, 1)


def blur(ao, alpha, sharpness=12.0):
    """Depth-aware blur, horizontal then vertical, mirroring fidelity_ssao_blur_ps2x.fxc."""
    height, width = ao.shape
    depth = alpha * DEPTH_SCALE + Z_NEAR
    pixel_y, pixel_x = np.mgrid[0:height, 0:width].astype(float)
    relative = sharpness / depth

    def one_pass(image, step_x, step_y):
        total = image * 0.001
        weights = np.full(image.shape, 0.001)
        for offset in (0.5, 1.5, 2.5, 3.5):
            for sign in (1, -1):
                px = pixel_x + sign * offset * step_x
                py = pixel_y + sign * offset * step_y
                tap_depth = bilinear(alpha, px, py) * DEPTH_SCALE + Z_NEAR
                weight = np.clip(1 - np.abs(tap_depth - depth) * relative, 0, 1)
                total += bilinear(image, px, py) * weight
                weights += weight
        return total / weights

    return one_pass(one_pass(ao, 1, 0), 0, 1)


def room_probes(ao):
    """AO at three points of the ROOM scene: floor/wall edge, open wall, wall/wall edge."""
    ray_x, ray_y = view_rays()
    column = WIDTH // 2
    edge_row = int(np.argmin(np.abs(ray_y[:, column] * 200 + 64)))
    wall_row = HEIGHT // 3
    side_column = int(np.argmin(np.abs(ray_x[wall_row] * 200 - 120)))
    return {
        "floor_wall_edge": float(ao[edge_row - 1, column]),
        "open_wall": float(ao[wall_row, column]),
        "wall_wall_edge": float(ao[wall_row, side_column - 1]),
    }


def main():
    """Print the results for both test scenes."""
    floor = simulate(encode_dest_alpha(render_depth(FLOOR)))
    print("flat floor (grazing): min %.3f mean %.3f" % (floor.min(), floor.mean()))
    room = simulate(encode_dest_alpha(render_depth(ROOM)))
    print("room corner: min %.3f mean %.3f" % (room.min(), room.mean()))
    for name, value in room_probes(room).items():
        print("  %-16s %.2f" % (name, value))


if __name__ == "__main__":
    main()
