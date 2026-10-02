#!/usr/bin/env python3
"""
equirect_to_cubestrip.py

Convert an equirectangular (lat-long) HDRI into a 1x6 horizontal cubemap strip PNG:

    +X | -X | +Y | -Y | +Z | -Z

Face layout/orientation follows the OpenGL / Vulkan cube map convention, so
the strip can be split left-to-right straight into array layers 0..5 of a
VK_IMAGE_VIEW_TYPE_CUBE (or GL_TEXTURE_CUBE_MAP_POSITIVE_X + i).

HDR input is exposure-adjusted, tonemapped and sRGB-encoded for the PNG.

World convention used for the equirect lookup: +Y up, -Z is the center of
the panorama, +X is to the right of center. Use --yaw to spin it.

Usage:
    python equirect_to_cubestrip.py sky.hdr                      # -> sky_cubemap.png
    python equirect_to_cubestrip.py sky.exr strip.png --size 1024 --ss 2
    python equirect_to_cubestrip.py sky.hdr --exposure -1 --tonemap reinhard --16bit

Requires: numpy, opencv-python
"""
import os
os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")  # must be set before importing cv2

import argparse
import numpy as np
import cv2

FACE_NAMES = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]


def face_directions(face, n, r0, r1):
    """World-space directions for rows [r0, r1) of an n x n cube face.

    Inverse of the cube map major-axis table in the GL/Vulkan specs.
    a = horizontal face coord (s), b = vertical face coord (t), both in
    [-1, 1], with row 0 at the top of the image.
    """
    c = (np.arange(n, dtype=np.float32) + 0.5) / n * 2.0 - 1.0
    a, b = np.meshgrid(c, c[r0:r1])
    one = np.ones_like(a)

    if face == 0:   x, y, z =  one, -b,  -a   # +X
    elif face == 1: x, y, z = -one, -b,   a   # -X
    elif face == 2: x, y, z =    a, one,  b   # +Y
    elif face == 3: x, y, z =    a, -one, -b  # -Y
    elif face == 4: x, y, z =    a, -b,  one  # +Z
    else:           x, y, z =   -a, -b, -one  # -Z

    d = np.stack([x, y, z], axis=-1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    return d


def bilinear(img, u, v):
    """Bilinear sample with horizontal wrap and vertical clamp."""
    h, w = img.shape[:2]
    x0 = np.floor(u).astype(np.int64)
    y0 = np.floor(v).astype(np.int64)
    fx = (u - x0)[..., None].astype(np.float32)
    fy = (v - y0)[..., None].astype(np.float32)

    x1 = np.mod(x0 + 1, w)
    x0 = np.mod(x0, w)
    y1 = np.clip(y0 + 1, 0, h - 1)
    y0 = np.clip(y0, 0, h - 1)

    top = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    bot = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return top * (1 - fy) + bot * fy


def sample_equirect(img, d, yaw):
    h, w = img.shape[:2]
    x, y, z = d[..., 0], d[..., 1], d[..., 2]
    lon = np.arctan2(x, -z) + yaw                 # 0 at -Z, +pi/2 at +X
    lat = np.arcsin(np.clip(y, -1.0, 1.0))        # +pi/2 straight up
    u = (lon / (2 * np.pi) + 0.5) * w - 0.5
    v = (0.5 - lat / np.pi) * h - 0.5
    return bilinear(img, u, v)


def render_face(img, face, size, ss, yaw, chunk_rows=128):
    """Render one face, in row chunks to keep memory bounded."""
    n = size * ss
    out = np.empty((size, size, img.shape[2]), np.float32)
    step = chunk_rows * ss
    for r0 in range(0, n, step):
        r1 = min(r0 + step, n)
        s = sample_equirect(img, face_directions(face, n, r0, r1), yaw)
        if ss > 1:  # box-filter the supersamples down
            s = s.reshape((r1 - r0) // ss, ss, size, ss, -1).mean(axis=(1, 3))
        out[r0 // ss:r1 // ss] = s
    return out


def load_image(path):
    img = cv2.imread(path, cv2.IMREAD_ANYCOLOR | cv2.IMREAD_ANYDEPTH)
    if img is None:
        raise SystemExit(f"Could not read '{path}'. (EXR needs an OpenCV build with OpenEXR.)")
    if img.ndim == 2:
        img = img[..., None]
    if img.shape[2] == 4:
        img = img[..., :3]  # drop alpha

    if img.dtype == np.uint8:
        return img.astype(np.float32) / 255.0, False
    if img.dtype == np.uint16:
        return img.astype(np.float32) / 65535.0, False
    return img.astype(np.float32), True  # float = linear HDR


def linear_to_srgb(x):
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)


def save_png(path, strip, src_is_hdr, exposure, tonemap, bits16):
    if src_is_hdr:
        x = np.maximum(strip, 0.0) * (2.0 ** exposure)
        if tonemap == "reinhard":
            x = x / (1.0 + x)
        elif tonemap == "aces":  # Narkowicz ACES filmic fit
            x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
        x = linear_to_srgb(np.clip(x, 0.0, 1.0))
    else:
        x = np.clip(strip, 0.0, 1.0)  # LDR input is already display-encoded

    if bits16:
        out = (x * 65535.0 + 0.5).astype(np.uint16)
    else:
        out = (x * 255.0 + 0.5).astype(np.uint8)
    if not cv2.imwrite(path, out):
        raise SystemExit(f"Failed to write '{path}'.")


def main():
    p = argparse.ArgumentParser(description="Equirectangular HDRI -> 1x6 cubemap strip (+X -X +Y -Y +Z -Z)")
    p.add_argument("input", help="equirectangular image (.hdr, .exr, .png, .jpg, ...)")
    p.add_argument("output", nargs="?", help="output PNG (default: <input>_cubemap.png)")
    p.add_argument("--size", type=int, default=0, help="face size in pixels (default: input width / 4)")
    p.add_argument("--ss", type=int, default=1, help="supersampling factor per axis, e.g. 2 or 4 (default 1)")
    p.add_argument("--yaw", type=float, default=0.0, help="rotate the panorama around +Y, in degrees")
    p.add_argument("--exposure", type=float, default=0.0, help="exposure in stops applied before tonemapping (HDR input)")
    p.add_argument("--tonemap", choices=["clip", "reinhard", "aces"], default="aces",
                   help="how HDR values above 1.0 are compressed (default: aces)")
    p.add_argument("--16bit", dest="bits16", action="store_true", help="write a 16-bit PNG instead of 8-bit")
    args = p.parse_args()

    if not args.output:
        args.output = os.path.splitext(args.input)[0] + "_cubemap.png"
    elif os.path.splitext(args.output)[1].lower() != ".png":
        args.output = os.path.splitext(args.output)[0] + ".png"

    img, is_hdr = load_image(args.input)
    h, w = img.shape[:2]
    if abs(w - 2 * h) > 1:
        print(f"warning: input is {w}x{h}, equirect images are normally 2:1")

    size = args.size or max(1, w // 4)
    ss = max(1, args.ss)
    yaw = np.radians(args.yaw)

    faces = []
    for i, name in enumerate(FACE_NAMES):
        print(f"  rendering {name} ({size}x{size})")
        faces.append(render_face(img, i, size, ss, yaw))

    strip = np.concatenate(faces, axis=1)
    if strip.shape[2] == 1:
        strip = strip[..., 0]
    save_png(args.output, strip, is_hdr, args.exposure, args.tonemap, args.bits16)
    print(f"wrote {args.output}  ({strip.shape[1]}x{strip.shape[0]})")


if __name__ == "__main__":
    main()
