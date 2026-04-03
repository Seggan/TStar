"""
Cosmic Clarity ONNX Bridge
===========================
Python bridge between the TStar C++ host and the Cosmic Clarity ONNX models.
Handles all image I/O, pre/post-processing, and tiled model inference for the
following processing modes:

  - sharpen  : Stellar and/or non-stellar sharpening via deep-learning models.
  - denoise  : Luminance and optional chrominance denoising.
  - superres : Super-resolution upscaling by an integer scale factor.
  - both     : Sharpening followed by denoising in a single pass.

Communication with the C++ host uses raw float32 binary files and a JSON
parameter file. Dimensional metadata is printed to stdout on the line:

    RESULT: <w> <h> <c>

Usage:
    python cosmic_bridge.py process <params.json> <input.raw> <output.raw>
    python cosmic_bridge.py save    <tiff_out> <w> <h> <c> <raw_in>
    python cosmic_bridge.py load    <tiff_in> <raw_out>
"""

import sys
import os
import json
import math

import numpy as np

try:
    import tifffile
except ImportError:
    print("Error: tifffile not found. pip install tifffile")
    sys.exit(1)

try:
    import onnxruntime as ort
except ImportError:
    ort = None  # Only required for the 'process' command.


# ===========================================================================
# Color-space conversion  (BT.601 YCbCr)
# ===========================================================================

# Forward transform matrix: RGB -> YCbCr
_M_RGB2YCBCR = np.array([
    [ 0.299,      0.587,      0.114    ],
    [-0.168736,  -0.331264,   0.5      ],
    [ 0.5,       -0.418688,  -0.081312 ],
], dtype=np.float32)

# Inverse transform matrix: YCbCr -> RGB
_M_YCBCR2RGB = np.array([
    [1.0,  0.0,       1.402    ],
    [1.0, -0.344136, -0.714136 ],
    [1.0,  1.772,     0.0      ],
], dtype=np.float32)


def extract_luminance(rgb: np.ndarray) -> tuple:
    """
    Convert an RGB image to the YCbCr color space and return the three
    components separately.

    The Cb and Cr chrominance channels are offset by +0.5 so that the neutral
    (grey) value maps to 0.5 rather than 0.0.  The caller must pass this offset
    back to merge_luminance when reconstructing the RGB image.

    Parameters
    ----------
    rgb : Float32 array of shape (H, W, 3) with values in [0, 1].

    Returns
    -------
    Tuple of (Y, Cb, Cr), each a float32 array of shape (H, W).
    Y  is luminance in [0, 1].
    Cb and Cr are chrominance in approximately [0, 1] after the +0.5 offset.
    """
    rgb   = np.asarray(rgb, dtype=np.float32)
    ycbcr = rgb @ _M_RGB2YCBCR.T
    return ycbcr[..., 0], ycbcr[..., 1] + 0.5, ycbcr[..., 2] + 0.5


def merge_luminance(y: np.ndarray, cb: np.ndarray, cr: np.ndarray) -> np.ndarray:
    """
    Reconstruct an RGB image from separate Y, Cb, Cr components.

    Each component is clipped to [0, 1] independently before applying the
    inverse YCbCr transform.  This prevents out-of-gamut colours that can
    arise when model outputs exceed the nominal range.  The Cb / Cr inputs
    are assumed to carry the +0.5 offset applied by extract_luminance.

    Parameters
    ----------
    y  : Luminance array of shape (H, W).
    cb : Chrominance-blue array of shape (H, W), offset by +0.5.
    cr : Chrominance-red  array of shape (H, W), offset by +0.5.

    Returns
    -------
    Float32 RGB array of shape (H, W, 3), values clipped to [0, 1].
    """
    y  = np.clip(np.asarray(y,  dtype=np.float32), 0.0, 1.0)
    cb = np.clip(np.asarray(cb, dtype=np.float32), 0.0, 1.0)
    cr = np.clip(np.asarray(cr, dtype=np.float32), 0.0, 1.0)

    ycbcr = np.stack([y, cb - 0.5, cr - 0.5], axis=-1)
    rgb   = ycbcr @ _M_YCBCR2RGB.T
    return np.clip(rgb, 0.0, 1.0)


# ===========================================================================
# Tiling constants
# ===========================================================================

CHUNK    = 256   # Default tile side length in pixels.
OVERLAP  = 64    # Default overlap between adjacent tiles in pixels.
BORDER   = 16    # Border width discarded from each tile during stitching.
PAD_MULT = 16    # Spatial dimensions are padded to a multiple of this value.


# ===========================================================================
# Tiling utilities
# ===========================================================================

def pad_to_mult(arr2d: np.ndarray, mult: int = PAD_MULT) -> tuple:
    """
    Reflect-pad the spatial dimensions of an array to the nearest multiple
    of `mult`.

    Both 2-D (H, W) and 3-D (H, W, C) layouts are supported. If no padding
    is required the original array is returned unchanged.

    Parameters
    ----------
    arr2d : Input array.
    mult  : Padding target multiple.

    Returns
    -------
    Tuple of (padded_array, original_H, original_W).
    """
    h, w  = arr2d.shape[:2]
    pad_h = (mult - h % mult) % mult
    pad_w = (mult - w % mult) % mult

    if pad_h == 0 and pad_w == 0:
        return arr2d, h, w

    if arr2d.ndim == 2:
        padded = np.pad(arr2d, ((0, pad_h), (0, pad_w)), mode="reflect")
    else:
        padded = np.pad(arr2d, ((0, pad_h), (0, pad_w), (0, 0)), mode="reflect")

    return padded, h, w


def split_chunks(plane2d: np.ndarray, chunk: int = CHUNK, overlap: int = OVERLAP):
    """
    Yield overlapping square tiles from a 2-D plane.

    Tiles are extracted with a stride of (chunk - overlap).  When the image
    edge is reached, the final tile is anchored to the boundary rather than
    being clipped, so all tiles share the same size.

    Parameters
    ----------
    plane2d : 2-D float32 array (H, W).
    chunk   : Tile side length in pixels.
    overlap : Overlap in pixels between adjacent tiles.

    Yields
    ------
    Tuple of (tile_array, row_offset, col_offset).
    """
    H, W  = plane2d.shape
    step  = chunk - overlap

    for i in range(0, H, step):
        for j in range(0, W, step):
            ei = min(i + chunk, H)
            ej = min(j + chunk, W)
            yield plane2d[i:ei, j:ej], i, j


def stitch_chunks(chunks: list, shape: tuple, border: int = BORDER) -> np.ndarray:
    """
    Stitch a list of 2-D tile results back into a full-size plane using a
    border-ignore (hard-crop) blending strategy.

    An inner region is taken from each tile by discarding `border` pixels on
    every side.  Overlapping inner regions are averaged using per-pixel
    accumulation weights.  Tiles whose inner dimensions collapse to zero are
    silently skipped.

    Parameters
    ----------
    chunks : Iterable of (tile_array, row_offset, col_offset).
    shape  : Target output shape (H, W) or (H, W, ...).
    border : Number of pixels to discard from each tile edge.

    Returns
    -------
    Float32 array of shape (H, W).
    """
    H, W     = shape[0], shape[1]
    stitched = np.zeros((H, W), dtype=np.float32)
    weights  = np.zeros((H, W), dtype=np.float32)

    for chunk, i, j in chunks:
        h, w = chunk.shape
        if h <= 0 or w <= 0:
            continue

        bh = min(border, h // 2)
        bw = min(border, w // 2)

        # Coordinates of the inner region in output space.
        y0, y1 = i + bh, i + h - bh
        x0, x1 = j + bw, j + w - bw

        if y1 <= y0 or x1 <= x0:
            continue

        inner = chunk[bh:h - bh, bw:w - bw]

        # Clip destination indices to valid output bounds.
        yy0, yy1 = max(0, y0), min(H, y1)
        xx0, xx1 = max(0, x0), min(W, x1)

        if yy1 <= yy0 or xx1 <= xx0:
            continue

        # Corresponding source slice offsets.
        sy0 = yy0 - y0
        sx0 = xx0 - x0
        sy1 = sy0 + (yy1 - yy0)
        sx1 = sx0 + (xx1 - xx0)

        stitched[yy0:yy1, xx0:xx1] += inner[sy0:sy1, sx0:sx1]
        weights [yy0:yy1, xx0:xx1] += 1.0

    stitched /= np.maximum(weights, 1.0)
    return stitched


def split_chunks_rgb(img3: np.ndarray, chunk: int = CHUNK, overlap: int = OVERLAP):
    """
    Yield overlapping square tiles from an RGB image in HWC layout.

    Parameters
    ----------
    img3    : Float32 array of shape (H, W, C).
    chunk   : Tile side length in pixels.
    overlap : Overlap in pixels between adjacent tiles.

    Yields
    ------
    Tuple of (tile_hwc_array, row_offset, col_offset).
    """
    H, W, _ = img3.shape
    step    = chunk - overlap

    for i in range(0, H, step):
        for j in range(0, W, step):
            ei = min(i + chunk, H)
            ej = min(j + chunk, W)
            yield img3[i:ei, j:ej], i, j


def stitch_chunks_rgb(chunks: list, shape: tuple, border: int = BORDER) -> np.ndarray:
    """
    Stitch a list of RGB tile results back into a full-size image using a
    border-ignore blending strategy.

    Functionally identical to stitch_chunks but operates on HWC arrays and
    maintains a single-channel weight map that is broadcast across all channels.

    Parameters
    ----------
    chunks : Iterable of (tile_hwc_array, row_offset, col_offset).
    shape  : Target output shape (H, W, C).
    border : Number of pixels to discard from each tile edge.

    Returns
    -------
    Float32 array of shape (H, W, C).
    """
    H, W, C  = shape
    stitched = np.zeros((H, W, C), dtype=np.float32)
    weights  = np.zeros((H, W, 1), dtype=np.float32)

    for chunk, i, j in chunks:
        h, w, _ = chunk.shape
        if h <= 0 or w <= 0:
            continue

        bh = min(border, h // 2)
        bw = min(border, w // 2)

        y0, y1 = i + bh, i + h - bh
        x0, x1 = j + bw, j + w - bw

        if y1 <= y0 or x1 <= x0:
            continue

        inner = chunk[bh:h - bh, bw:w - bw]

        yy0, yy1 = max(0, y0), min(H, y1)
        xx0, xx1 = max(0, x0), min(W, x1)

        if yy1 <= yy0 or xx1 <= xx0:
            continue

        sy0 = yy0 - y0
        sx0 = xx0 - x0
        sy1 = sy0 + (yy1 - yy0)
        sx1 = sx0 + (xx1 - xx0)

        stitched[yy0:yy1, xx0:xx1] += inner[sy0:sy1, sx0:sx1]
        weights [yy0:yy1, xx0:xx1] += 1.0

    stitched /= np.maximum(weights, 1.0)
    return stitched


# ===========================================================================
# ONNX inference helpers
# ===========================================================================

def _pick_io(session) -> tuple:
    """
    Identify the image input, optional PSF scalar input, and primary output
    tensors of an ONNX session by inspecting their shapes.

    The image input is identified as the rank-4 tensor; a scalar input of rank
    1 or 2 is treated as the PSF conditioning signal.  If the session has only
    one input, the PSF name is returned as None.

    Parameters
    ----------
    session : An onnxruntime.InferenceSession instance.

    Returns
    -------
    Tuple of (image_input_name, psf_input_name_or_None, output_name).
    """
    inputs   = session.get_inputs()
    out_name = session.get_outputs()[0].name
    img_name = inputs[0].name
    psf_name = None

    for inp in inputs:
        rank = len(inp.shape) if inp.shape else 0
        if rank == 4:
            img_name = inp.name
        elif rank in (1, 2):
            psf_name = inp.name

    if len(inputs) == 1:
        psf_name = None

    return img_name, psf_name, out_name


def _infer_2d(session, chunk2d: np.ndarray, psf01: float | None = None) -> np.ndarray:
    """
    Run an ONNX model on a single 2-D luminance tile.

    The tile is padded to a multiple of 16 via reflect-padding, tiled to a
    3-channel input tensor of shape (1, 3, H, W), and forwarded through the
    model.  Only the first output channel is returned after cropping the
    padding.

    Parameters
    ----------
    session : Loaded onnxruntime.InferenceSession.
    chunk2d : 2-D float32 tile of shape (H, W).
    psf01   : Optional PSF radius encoded to [0, 1].  Supplied only to
              conditionally-sharpened models.

    Returns
    -------
    Float32 array of shape (H, W), unclipped to preserve highlights.
    """
    chunk_p, h0, w0 = pad_to_mult(chunk2d.astype(np.float32), mult=16)

    # Expand to (1, 3, H, W) by tiling across the channel dimension.
    inp = chunk_p[np.newaxis, np.newaxis, :, :].astype(np.float32)
    inp = np.tile(inp, (1, 3, 1, 1))

    img_name, psf_name, out_name = _pick_io(session)
    feeds = {img_name: inp}

    if psf_name is not None:
        psf_value = float(psf01) if psf01 is not None else 0.5
        feeds[psf_name] = np.array([[psf_value]], dtype=np.float32)

    out = session.run([out_name], feeds)[0]

    # Extract the first spatial plane from whatever output rank the model uses.
    if out.ndim == 4:
        y = out[0, 0]
    elif out.ndim == 3:
        y = out[0]
        if y.shape[0] in (1, 3):
            y = y[0]
    else:
        y = out

    return y[:h0, :w0]


def _infer_rgb(session, chunk_hwc: np.ndarray) -> np.ndarray:
    """
    Run an ONNX model on a single RGB tile in HWC layout.

    The tile is padded to a multiple of 16, transposed to NCHW for inference,
    and the result is transposed back to HWC before returning.

    Parameters
    ----------
    session   : Loaded onnxruntime.InferenceSession.
    chunk_hwc : Float32 RGB tile of shape (H, W, 3).

    Returns
    -------
    Float32 array of shape (H, W, 3), unclipped.
    """
    chunk_p, h0, w0 = pad_to_mult(chunk_hwc.astype(np.float32), mult=16)

    # (H, W, 3) -> (1, 3, H, W)
    inp = np.transpose(chunk_p, (2, 0, 1))[None, ...].astype(np.float32)

    img_name, _, out_name = _pick_io(session)
    out = session.run([out_name], {img_name: inp})[0]

    # Remove batch dimension and convert back to HWC.
    if out.ndim == 4:
        y = out[0]
    else:
        y = out

    y = np.transpose(y, (1, 2, 0))
    return y[:h0, :w0]


# ===========================================================================
# Image processing utilities
# ===========================================================================

def blend(orig: np.ndarray, processed: np.ndarray, amount: float) -> np.ndarray:
    """
    Linear blend between an original and a processed image.

    Parameters
    ----------
    orig      : Original image array.
    processed : Model output array, same shape as `orig`.
    amount    : Blend factor in [0, 1]; 0.0 returns `orig`, 1.0 returns `processed`.

    Returns
    -------
    Float32 blended array.
    """
    a = float(np.clip(amount, 0.0, 1.0))
    return (1.0 - a) * orig + a * processed


def stretch_image_unlinked_rgb(
    image_rgb: np.ndarray,
    target_median: float = 0.25,
) -> tuple:
    """
    Apply a per-channel Midtone Transfer Function (MTF) stretch to bring
    under-exposed linear astronomical images into a suitable range for
    neural-network processing.

    The stretch is computed independently per channel so that colour balance
    is not altered.  The original minimum and per-channel median values are
    returned to allow exact inversion by unstretch_image_unlinked_rgb.

    Parameters
    ----------
    image_rgb     : Float32 array (H, W, 3) or 2-D mono array.
    target_median : Desired median after stretching (default 0.25).

    Returns
    -------
    Tuple of (stretched_image, orig_min, orig_meds), where orig_meds is a
    list of per-channel pre-stretch median values.
    """
    x        = image_rgb.astype(np.float32, copy=True)
    orig_min = float(np.min(x))
    x       -= orig_min

    t        = float(target_median)
    channels = 1 if x.ndim == 2 else x.shape[2]
    orig_meds = []

    for ch in range(channels):
        plane = x if x.ndim == 2 else x[..., ch]
        m0    = float(np.median(plane))
        orig_meds.append(m0)

        if m0 != 0.0:
            # MTF formula with a numerical guard against division by zero.
            denom = m0 * (t + plane - 1.0) - t * plane
            res   = np.where(np.abs(denom) > 1e-12,
                             ((m0 - 1.0) * t * plane) / denom,
                             plane)
            if x.ndim == 2:
                x = res
            else:
                x[..., ch] = res

    return np.clip(x, 0.0, 1.0), orig_min, orig_meds


def unstretch_image_unlinked_rgb(
    image_rgb: np.ndarray,
    orig_meds: list,
    orig_min: float,
    was_mono: bool,
) -> np.ndarray:
    """
    Invert the MTF stretch applied by stretch_image_unlinked_rgb.

    The inverse formula is guarded against numerical singularities in the
    same manner as the forward transform.  The original minimum offset is
    re-added and the result is clipped to [0, 1].

    Parameters
    ----------
    image_rgb : Stretched float32 image to invert.
    orig_meds : Per-channel median values returned by the forward stretch.
    orig_min  : Global minimum returned by the forward stretch.
    was_mono  : If True and the result is 3-channel, reduce to mono by averaging.

    Returns
    -------
    Float32 array in the original (pre-stretch) intensity scale.
    """
    y        = image_rgb.astype(np.float32, copy=True)
    t        = 0.25  # Must match the target_median used during stretching.
    channels = 1 if y.ndim == 2 else y.shape[2]

    for ch in range(channels):
        m0 = float(orig_meds[min(ch, len(orig_meds) - 1)])

        if m0 != 0.0:
            yc    = y if y.ndim == 2 else y[..., ch]
            denom = t * (m0 - 1.0 + yc) - yc * m0
            num   = yc * m0 * (t - 1.0)
            res   = np.where(np.abs(denom) > 1e-12, num / denom, yc)

            if y.ndim == 2:
                y = res
            else:
                y[..., ch] = res

    y += float(orig_min)
    y  = np.clip(y, 0.0, 1.0)

    if was_mono and y.ndim == 3 and y.shape[2] == 3:
        y = np.mean(y, axis=2, keepdims=True)

    return y


def encode_psf(psf_radius: float, lo: float = 1.0, hi: float = 8.0) -> float:
    """
    Encode a PSF blur radius in pixels to a normalised scalar in [0, 1]
    using a logarithmic (base-2) mapping.

    This encoding matches the conditioning scheme expected by the non-stellar
    sharpening model.

    Parameters
    ----------
    psf_radius : Physical PSF radius in pixels, expected in [lo, hi].
    lo         : Lower bound of the physical range (default 1.0 px).
    hi         : Upper bound of the physical range (default 8.0 px).

    Returns
    -------
    Float in [0, 1].
    """
    return float(np.clip(
        (math.log2(psf_radius) - math.log2(lo)) / (math.log2(hi) - math.log2(lo)),
        0.0, 1.0
    ))


def _is_triplicated_mono(rgb_image: np.ndarray, eps: float = 1e-7) -> bool:
    """
    Determine whether an ostensibly RGB image is in fact a mono image whose
    single channel has been copied across all three colour planes.

    Parameters
    ----------
    rgb_image : Float32 array of shape (H, W, 3).
    eps       : Maximum allowed per-pixel absolute difference between channels.

    Returns
    -------
    True if all three channels are numerically identical within `eps`.
    """
    if rgb_image.ndim != 3 or rgb_image.shape[2] != 3:
        return False

    r, g, b = rgb_image[..., 0], rgb_image[..., 1], rgb_image[..., 2]
    return (np.max(np.abs(r - g)) <= eps) and (np.max(np.abs(r - b)) <= eps)


def _every(*_, **__) -> bool:
    """
    Unconditional progress gate — always returns True so that every tile
    triggers a progress log line.

    The unused arguments preserve call-site compatibility with signatures
    that pass tile index and total count.
    """
    return True


# ===========================================================================
# Main processing pipeline
# ===========================================================================

def process(params_file: str, raw_in: str, raw_out: str) -> None:
    """
    Execute the full Cosmic Clarity processing pipeline as directed by the
    JSON parameter file produced by the TStar C++ host.

    The pipeline performs the following steps in order:
      1. Load image from raw float32 binary.
      2. Optionally apply an auto-stretch if the image is underexposed.
      3. Pad the border by 16 pixels using the per-channel median value.
      4. Expand mono images to 3 channels for model compatibility.
      5. Run the requested processing mode (sharpen / denoise / superres / both).
      6. Invert the stretch if one was applied.
      7. Remove the 16-pixel border padding.
      8. Restore the original channel count.
      9. Validate the output shape against the expected dimensions.
     10. Write the result to the raw float32 binary output file.

    The function terminates with a non-zero exit code on any unrecoverable
    error rather than raising exceptions, to ensure clean error reporting
    to the C++ host via the process exit status.

    Parameters
    ----------
    params_file : Path to the JSON file containing processing parameters.
    raw_in      : Path to the input raw float32 binary file.
    raw_out     : Path to the output raw float32 binary file.
    """
    if ort is None:
        print("Error: onnxruntime not found. pip install onnxruntime", flush=True)
        sys.exit(1)

    with open(params_file) as f:
        params = json.load(f)

    w         = int(params["width"])
    h         = int(params["height"])
    c         = int(params["channels"])
    mode      = params.get("mode", "sharpen")
    mdir      = params.get("models_dir", "")
    use_gpu   = bool(params.get("use_gpu", False))

    print(f"[Bridge] Image {w}x{h}x{c}  mode={mode}", flush=True)

    # -------------------------------------------------------------------
    # Load raw input
    # -------------------------------------------------------------------

    data = np.fromfile(raw_in, dtype=np.float32)
    if data.size != w * h * c:
        print(f"Error: expected {w * h * c} floats, got {data.size}", flush=True)
        sys.exit(2)

    img      = data.reshape((h, w, c) if c > 1 else (h, w, 1)).astype(np.float32)
    was_mono = (c == 1)

    print(
        f"[Bridge] Input:  min={float(np.min(img)):.4f}  "
        f"max={float(np.max(img)):.4f}  med={float(np.median(img)):.4f}",
        flush=True,
    )

    # -------------------------------------------------------------------
    # Auto-stretch detection
    # -------------------------------------------------------------------

    # The stretch threshold is slightly lower for denoising to avoid
    # misidentifying faint nebulae as unstretched data.
    stretch_threshold = 0.05 if mode == "denoise" else 0.08

    metric_val     = float(np.median(img - np.min(img)))
    stretch_needed = metric_val < stretch_threshold
    print(f"[Bridge] Stretch metric={metric_val:.6f}  needed={stretch_needed}", flush=True)

    # Guard against raw linear 16-bit data that somehow passed the stretch
    # threshold check — models require inputs normalised to [0, 1].
    img_max = float(np.max(img))
    if not stretch_needed and img_max > 2.0:
        print(
            f"[Bridge] WARNING: Input max={img_max:.1f} without stretch requested. "
            f"Normalising by 65535 (assuming 16-bit).",
            flush=True,
        )
        img /= 65535.0
        metric_val     = float(np.median(img - np.min(img)))
        stretch_needed = metric_val < stretch_threshold
        print(f"[Bridge] Re-evaluated metric={metric_val:.6f}  needed={stretch_needed}", flush=True)

    # -------------------------------------------------------------------
    # Apply stretch
    # -------------------------------------------------------------------

    if stretch_needed:
        print("[Bridge] Applying stretch...", flush=True)
        if was_mono:
            stretched_core, orig_min, orig_meds = stretch_image_unlinked_rgb(
                img[..., 0], target_median=0.25
            )
        else:
            stretched_core, orig_min, orig_meds = stretch_image_unlinked_rgb(
                img, target_median=0.25
            )
    else:
        print("[Bridge] No stretch required.", flush=True)
        stretched_core = img[..., 0] if was_mono else img
        orig_min  = None
        orig_meds = None

    # -------------------------------------------------------------------
    # Apply 16-pixel border padding using the per-channel median
    # -------------------------------------------------------------------

    if was_mono:
        med_val  = float(np.median(stretched_core))
        stretched = np.pad(
            stretched_core,
            ((16, 16), (16, 16)),
            mode="constant",
            constant_values=med_val,
        )
    else:
        channels = []
        for ch_idx in range(3):
            plane  = stretched_core[..., ch_idx]
            p_med  = float(np.median(plane))
            p_pad  = np.pad(plane, ((16, 16), (16, 16)), mode="constant", constant_values=p_med)
            channels.append(p_pad)
        stretched = np.stack(channels, axis=-1)

    print(
        f"[Bridge] Prepared:  min={float(np.min(stretched)):.4f}  "
        f"max={float(np.max(stretched)):.4f}",
        flush=True,
    )

    # -------------------------------------------------------------------
    # Promote mono to 3-channel (models expect 3-channel input)
    # -------------------------------------------------------------------

    img3 = np.stack([stretched, stretched, stretched], axis=-1) if was_mono else stretched

    # -------------------------------------------------------------------
    # ONNX execution provider selection
    # -------------------------------------------------------------------

    providers = ["CPUExecutionProvider"]
    if use_gpu:
        try:
            avail = ort.get_available_providers()
            print(f"[Bridge] Available ONNX providers: {avail}", flush=True)
            if "DmlExecutionProvider" in avail:
                providers.insert(0, "DmlExecutionProvider")
            elif "CUDAExecutionProvider" in avail:
                providers.insert(0, "CUDAExecutionProvider")
        except Exception as e:
            print(f"[Bridge] Error checking providers: {e}", flush=True)

    print(f"[Bridge] Selected providers: {providers}", flush=True)

    sess_opts = ort.SessionOptions()
    sess_opts.intra_op_num_threads = max(1, os.cpu_count() or 4)

    result = img3.copy()
    print(
        f"[Bridge] Before processing:  min={float(np.min(result)):.4f}  "
        f"max={float(np.max(result)):.4f}  med={float(np.median(result)):.4f}",
        flush=True,
    )

    # ===================================================================
    # SHARPEN
    # ===================================================================

    if mode in ("sharpen", "both"):
        sharpen_mode  = params.get("sharpen_mode", "Both")
        stellar_amt   = float(params.get("stellar_amount", 0.5))
        ns_amt        = float(params.get("nonstellar_amount", 0.5))
        ns_psf        = float(params.get("nonstellar_psf", 3.0))
        sep_channels  = bool(params.get("separate_channels_sharpen", False))

        # Decide whether to process the luminance plane or each RGB channel separately.
        if sep_channels and not was_mono:
            planes = [result[..., ch] for ch in range(3)]
            labels = ["R", "G", "B"]
        else:
            y_plane, cb, cr = extract_luminance(result)
            planes = [y_plane]
            labels = ["Y"]
            print(
                f"[Bridge] Extracted Y:  min={float(np.min(y_plane)):.4f}  "
                f"max={float(np.max(y_plane)):.4f}  med={float(np.median(y_plane)):.4f}",
                flush=True,
            )

        sharpened_planes = []

        for plane, label in zip(planes, labels):

            # -- Stellar sharpening --
            if sharpen_mode in ("Both", "Stellar Only") and stellar_amt > 0:
                model_path = os.path.join(mdir, "deep_sharp_stellar_AI4.onnx")
                if os.path.exists(model_path):
                    print(f"[Bridge] Stellar sharpen ({label})  amount={stellar_amt:.2f}", flush=True)
                    sess  = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
                    total = sum(1 for _ in split_chunks(plane))
                    out_chunks = []

                    for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                        y = _infer_2d(sess, ch)
                        out_chunks.append((blend(ch, y, stellar_amt), i, j))
                        if _every(idx, total):
                            print(f"[Bridge]   Stellar {label}: {idx}/{total}", flush=True)

                    plane = stitch_chunks(out_chunks, plane.shape)
                    print(
                        f"[Bridge] Stellar {label} stitched:  "
                        f"min={float(np.min(plane)):.4f}  max={float(np.max(plane)):.4f}",
                        flush=True,
                    )
                    del sess
                else:
                    print("[Bridge] WARNING: Stellar model not found.", flush=True)

            # -- Non-stellar sharpening --
            if sharpen_mode in ("Both", "Non-Stellar Only") and ns_amt > 0:
                model_path = os.path.join(mdir, "deep_nonstellar_sharp_conditional_psf_AI4.onnx")
                if os.path.exists(model_path):
                    r      = float(np.clip(ns_psf, 1.0, 8.0))
                    psf01  = encode_psf(r)
                    print(
                        f"[Bridge] Non-stellar sharpen ({label})  "
                        f"amount={ns_amt:.2f}  psf={r:.1f}  enc={psf01:.3f}",
                        flush=True,
                    )
                    sess  = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
                    total = sum(1 for _ in split_chunks(plane))
                    out_chunks = []

                    for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                        y = _infer_2d(sess, ch, psf01=psf01)
                        out_chunks.append((blend(ch, y, ns_amt), i, j))
                        if _every(idx, total):
                            print(f"[Bridge]   NonStellar {label}: {idx}/{total}", flush=True)

                    plane = stitch_chunks(out_chunks, plane.shape)
                    print(
                        f"[Bridge] NonStellar {label} stitched:  "
                        f"min={float(np.min(plane)):.4f}  max={float(np.max(plane)):.4f}",
                        flush=True,
                    )
                    del sess
                else:
                    print("[Bridge] WARNING: Non-stellar model not found.", flush=True)

            sharpened_planes.append(plane)

        # Reassemble the image from processed planes.
        if sep_channels and not was_mono:
            for ch_idx, sp in enumerate(sharpened_planes):
                result[..., ch_idx] = sp
        else:
            result = merge_luminance(sharpened_planes[0], cb, cr)
            print(
                f"[Bridge] After sharpen merge:  "
                f"min={float(np.min(result)):.4f}  "
                f"max={float(np.max(result)):.4f}  "
                f"med={float(np.median(result)):.4f}",
                flush=True,
            )

        # For a sharpen-only pass on a mono input, reduce back to mono now
        # to avoid a channel-count mismatch at the output stage.
        if was_mono and mode == "sharpen":
            result = np.mean(result, axis=2, keepdims=True).astype(np.float32)
            print(f"[Bridge] Reduced sharpen output to mono: shape={result.shape}", flush=True)

    # ===================================================================
    # DENOISE
    # ===================================================================

    if mode in ("denoise", "both"):
        dn_strength = float(params.get("denoise_luma", 0.5))
        dn_color    = float(params.get("denoise_color", 0.5))
        dn_mode     = params.get("denoise_mode", "full")

        # Re-detect effective mono status.  After a sharpening pass the image
        # may still be logically mono even though it has three identical channels.
        detected_mono = was_mono if mode == "denoise" else _is_triplicated_mono(result)

        if detected_mono:
            print("[Bridge] Denoise: detected mono input — forcing luminance-only mode.", flush=True)
            dn_mode = "luminance"

        # Resolve model paths with lite-model fallbacks.
        model_path_mono = os.path.join(mdir, "deep_denoise_mono_AI4.onnx")
        if not os.path.exists(model_path_mono):
            model_path_mono = os.path.join(mdir, "deep_denoise_mono_AI4_lite.onnx")

        model_path_color = os.path.join(mdir, "deep_denoise_color_AI4.onnx")
        if not os.path.exists(model_path_color):
            model_path_color = os.path.join(mdir, "deep_denoise_color_AI4_lite.onnx")

        if detected_mono:
            # Mono path: denoise the single luminance plane directly.
            if os.path.exists(model_path_mono):
                print(f"[Bridge] Denoise (mono)  strength={dn_strength:.2f}", flush=True)
                sess  = ort.InferenceSession(model_path_mono, sess_options=sess_opts, providers=providers)
                plane = result[..., 0]
                total = sum(1 for _ in split_chunks(plane))
                out_chunks = []

                for idx, (ch, i, j) in enumerate(split_chunks(plane), start=1):
                    y = _infer_2d(sess, ch)
                    out_chunks.append((blend(ch, y, dn_strength), i, j))
                    if _every(idx, total):
                        print(f"[Bridge]   Denoise mono: {idx}/{total}", flush=True)

                result[..., 0] = stitch_chunks(out_chunks, plane.shape)
                del sess
            else:
                print("[Bridge] WARNING: Denoise mono model not found.", flush=True)

        else:
            # RGB path: denoise luminance with the mono model, then optionally
            # denoise chrominance with the colour model.
            y_in, cb_in, cr_in = extract_luminance(result)
            y_denoised = y_in
            cb_out, cr_out = cb_in, cr_in  # Default: preserve original chrominance.

            # Step 1 — Luminance denoising.
            if os.path.exists(model_path_mono):
                print(f"[Bridge] Denoise (luma)  strength={dn_strength:.2f}", flush=True)
                sess  = ort.InferenceSession(model_path_mono, sess_options=sess_opts, providers=providers)
                total = sum(1 for _ in split_chunks(y_in))
                out_chunks = []

                for idx, (ch, i, j) in enumerate(split_chunks(y_in), start=1):
                    y = _infer_2d(sess, ch)
                    out_chunks.append((blend(ch, y, dn_strength), i, j))
                    if _every(idx, total):
                        print(f"[Bridge]   Denoise luma: {idx}/{total}", flush=True)

                y_denoised = stitch_chunks(out_chunks, y_in.shape)
                del sess
            else:
                print("[Bridge] WARNING: Denoise mono model not found — skipping luma denoising.", flush=True)

            # Step 2 — Chrominance denoising (full mode only).
            # Only the chrominance planes are taken from the colour model output;
            # the denoised luminance from step 1 is kept to avoid colour shifts.
            if dn_mode == "full":
                if os.path.exists(model_path_color):
                    print(f"[Bridge] Denoise (color)  blend strength={dn_color:.2f}", flush=True)
                    sess_color = ort.InferenceSession(
                        model_path_color, sess_options=sess_opts, providers=providers
                    )
                    total = sum(1 for _ in split_chunks_rgb(result))
                    out_chunks = []

                    for idx, (ch, i, j) in enumerate(split_chunks_rgb(result), start=1):
                        y = _infer_rgb(sess_color, ch)
                        # Accumulate pure model output; blending is applied after stitching.
                        out_chunks.append((y, i, j))
                        if _every(idx, total):
                            print(f"[Bridge]   Color RGB: {idx}/{total}", flush=True)

                    rgb_denoised = stitch_chunks_rgb(out_chunks, result.shape)
                    del sess_color

                    # Extract and blend only the chrominance from the colour model.
                    _, cb_den, cr_den = extract_luminance(rgb_denoised)
                    print(
                        f"[Bridge] Blending chroma channels  strength={dn_color:.2f}",
                        flush=True,
                    )
                    cb_out = blend(cb_in, cb_den, dn_color)
                    cr_out = blend(cr_in, cr_den, dn_color)
                else:
                    print(
                        "[Bridge] WARNING: Denoise colour model not found — keeping original chrominance.",
                        flush=True,
                    )

            # Merge the denoised luminance with the (optionally denoised) chrominance.
            result = merge_luminance(y_denoised, cb_out, cr_out)

    # ===================================================================
    # SUPER-RESOLUTION
    # ===================================================================

    if mode == "superres":
        scale      = int(params.get("scale", 2))
        model_path = os.path.join(mdir, f"superres_{scale}x.onnx")

        if os.path.exists(model_path):
            print(f"[Bridge] SuperRes {scale}x", flush=True)
            sess = ort.InferenceSession(model_path, sess_options=sess_opts, providers=providers)
            out_channels = []
            patch_sz     = 256

            for ch_idx in range(3):
                plane      = result[..., ch_idx]
                out_h      = h * scale
                out_w      = w * scale
                out_plane  = np.zeros((out_h, out_w), dtype=np.float32)
                total_patches = (
                    ((h + patch_sz - 1) // patch_sz) *
                    ((w + patch_sz - 1) // patch_sz)
                )
                done = 0

                for py in range(0, h, patch_sz):
                    for px in range(0, w, patch_sz):
                        pe_y  = min(py + patch_sz, h)
                        pe_x  = min(px + patch_sz, w)
                        patch = plane[py:pe_y, px:pe_x]

                        # Zero-pad to (patch_sz, patch_sz) and tile across 3 channels.
                        patch_in = np.zeros((patch_sz, patch_sz, 3), dtype=np.float32)
                        patch_in[:patch.shape[0], :patch.shape[1], 0] = patch
                        patch_in[:patch.shape[0], :patch.shape[1], 1] = patch
                        patch_in[:patch.shape[0], :patch.shape[1], 2] = patch

                        inp = np.expand_dims(patch_in.transpose(2, 0, 1), 0).astype(np.float32)
                        img_name, _, out_name = _pick_io(sess)
                        out = sess.run([out_name], {img_name: inp})[0]

                        if out.ndim == 4:
                            out = out[0]
                        if out.ndim == 3 and out.shape[0] == 3:
                            out = out[0]  # Take the first channel of a 3-channel output.

                        oh = (pe_y - py) * scale
                        ow = (pe_x - px) * scale
                        out_plane[
                            py * scale:py * scale + oh,
                            px * scale:px * scale + ow,
                        ] = out[:oh, :ow]

                        done += 1
                        if _every(done, total_patches):
                            print(f"[Bridge]   SuperRes ch{ch_idx}: {done}/{total_patches}", flush=True)

                out_channels.append(out_plane)

            result = np.stack(out_channels, axis=-1)
            w *= scale
            h *= scale
            del sess
        else:
            print(f"[Bridge] WARNING: SuperRes model not found: {model_path}", flush=True)

    # -------------------------------------------------------------------
    # Invert stretch
    # -------------------------------------------------------------------

    print(
        f"[Bridge] Before unstretch:  "
        f"min={float(np.min(result)):.4f}  "
        f"max={float(np.max(result)):.4f}  "
        f"med={float(np.median(result)):.4f}",
        flush=True,
    )

    if stretch_needed and orig_min is not None:
        print("[Bridge] Unstretching...", flush=True)
        result = unstretch_image_unlinked_rgb(result, orig_meds, orig_min, was_mono=False)

    # -------------------------------------------------------------------
    # Remove 16-pixel border padding
    # -------------------------------------------------------------------

    if was_mono:
        if result.ndim == 3:
            result = result[16:-16, 16:-16, :]
        else:
            result = result[16:-16, 16:-16]
    else:
        result = result[16:-16, 16:-16, :]

    # -------------------------------------------------------------------
    # Restore original channel count
    # -------------------------------------------------------------------

    if was_mono:
        if result.ndim == 3 and result.shape[2] == 3:
            # Average the three identical channels back to a single mono plane.
            result = np.mean(result, axis=2, keepdims=True).astype(np.float32)
            print("[Bridge] Final: converted HxWx3 back to mono HxWx1.", flush=True)
        elif result.ndim == 2:
            result = result[..., np.newaxis]
    else:
        result = result.astype(np.float32)

    final = np.clip(result, 0.0, 1.0).astype(np.float32)

    # -------------------------------------------------------------------
    # Output shape validation
    # -------------------------------------------------------------------

    expected_shape = (h, w, c) if c > 1 else (h, w, 1)
    if final.shape != expected_shape:
        print(
            f"[Bridge] ERROR: Output shape mismatch — "
            f"expected {expected_shape}, got {final.shape}. "
            f"This will cause memory corruption in the C++ host.",
            flush=True,
        )
        sys.exit(7)

    # For mono output, strip the trailing singleton channel dimension so the
    # flat binary matches what the C++ host expects (H * W floats, not H * W * 1).
    if c == 1:
        final = final[..., 0]

    final.tofile(raw_out)
    print(f"RESULT: {w} {h} {c} (output shape verified)", flush=True)
    print("[Bridge] Done.", flush=True)


# ===========================================================================
# Legacy TIFF conversion utilities  (backward compatibility)
# ===========================================================================

def save(tiff_out_path: str, w: int, h: int, c: int, raw_in_path: str) -> None:
    """
    Convert a raw float32 binary file to a 16-bit TIFF image.

    This function is retained for backward compatibility with older versions
    of the C++ host that exchange processed data as TIFF files rather than
    raw floats.

    Parameters
    ----------
    tiff_out_path : Destination TIFF file path.
    w             : Image width in pixels.
    h             : Image height in pixels.
    c             : Number of channels (1 for mono, 3 for RGB).
    raw_in_path   : Source raw float32 binary file path.
    """
    try:
        data          = np.fromfile(raw_in_path, dtype=np.float32)
        expected_size = w * h * c

        if data.size != expected_size:
            print(
                f"Error: Raw file size mismatch — "
                f"expected {expected_size} floats, got {data.size}.",
                flush=True,
            )
            sys.exit(2)

        img          = data.reshape((h, w) if c == 1 else (h, w, c))
        img          = np.clip(img, 0.0, 1.0)
        img          = (img * 65535.0).astype(np.uint16)
        photometric  = "minisblack" if c == 1 else "rgb"

        tifffile.imwrite(tiff_out_path, img, photometric=photometric)
        print(f"Saved TIFF: {tiff_out_path}", flush=True)

    except Exception as e:
        print(f"Error in save: {e}", flush=True)
        sys.exit(3)


def load(tiff_in_path: str, raw_out_path: str) -> None:
    """
    Load a TIFF image, normalise it to [0, 1] float32, and write the result
    as a raw float32 binary file.

    CHW layout (as produced by some TIFF exporters) is detected heuristically
    and transposed to HWC.  The resulting dimensions are printed to stdout in
    the form expected by the C++ host:

        RESULT: <w> <h> <c>

    Parameters
    ----------
    tiff_in_path  : Source TIFF file path.
    raw_out_path  : Destination raw float32 binary file path.
    """
    try:
        if not os.path.exists(tiff_in_path):
            print(f"Error: TIFF file not found: {tiff_in_path}", flush=True)
            sys.exit(4)

        img = tifffile.imread(tiff_in_path)

        # Normalise integer types to [0, 1].
        if img.dtype == np.uint16:
            img = img.astype(np.float32) / 65535.0
        elif img.dtype == np.uint8:
            img = img.astype(np.float32) / 255.0
        else:
            img = img.astype(np.float32)

        # Resolve layout and dimensions.
        if img.ndim == 2:
            h, w = img.shape
            c    = 1
        elif img.ndim == 3:
            d0, d1, d2 = img.shape
            # CHW layout is assumed when the first dimension is <= 4 and the
            # remaining dimensions are significantly larger.
            if d0 <= 4 and d1 > 4 and d2 > 4:
                img = np.transpose(img, (1, 2, 0))
            h, w, c = img.shape
        else:
            print(f"Error: Unexpected image dimensions: {img.shape}", flush=True)
            sys.exit(5)

        img.tofile(raw_out_path)
        print(f"RESULT: {w} {h} {c}", flush=True)

    except Exception as e:
        print(f"Error in load: {e}", flush=True)
        sys.exit(6)


# ===========================================================================
# CLI entry point
# ===========================================================================

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: cosmic_bridge.py <process|save|load> ...", flush=True)
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "process":
        if len(sys.argv) < 5:
            print("Usage: process <params.json> <input.raw> <output.raw>", flush=True)
            sys.exit(1)
        process(sys.argv[2], sys.argv[3], sys.argv[4])

    elif cmd == "save":
        if len(sys.argv) < 7:
            print("Usage: save <tiff_out> <w> <h> <c> <raw_in>", flush=True)
            sys.exit(1)
        save(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])

    elif cmd == "load":
        if len(sys.argv) < 4:
            print("Usage: load <tiff_in> <raw_out>", flush=True)
            sys.exit(1)
        load(sys.argv[2], sys.argv[3])

    else:
        print(f"Unknown command: {cmd}", flush=True)
        sys.exit(1)