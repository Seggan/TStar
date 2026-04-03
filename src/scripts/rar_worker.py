"""
RAR Worker
==========
Standalone ONNX inference worker for single-image restoration tasks
(e.g. denoising, artefact removal). The model is applied in an overlapping
tiled fashion using a Hann window for smooth tile blending, which avoids
visible seams on large images.

Usage:
    python rar_worker.py --input <image> --output <result> --model <model.onnx>
                         [--patch 512] [--overlap 64] [--provider CPU]

Providers: CPU | CUDA | DirectML | CoreML
"""

import sys
import os
import time
import argparse

import numpy as np
import tifffile

try:
    import onnxruntime as ort
except ImportError:
    print("Error: onnxruntime not installed. Please run: pip install onnxruntime")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Windowing and tiling utilities
# ---------------------------------------------------------------------------

def _hann2d(n: int) -> np.ndarray:
    """
    Compute a 2-D Hann (raised-cosine) window of size n x n.

    The window is used as a per-tile blending weight during stitching,
    tapering contributions smoothly toward tile edges to suppress seam
    artefacts.

    Parameters
    ----------
    n : Window side length in pixels.

    Returns
    -------
    np.ndarray of shape (n, n) and dtype float32.
    """
    w = np.hanning(n).astype(np.float32)
    return w[:, None] * w[None, :]


def _tile_indices(n: int, patch: int, overlap: int) -> list:
    """
    Compute the set of top-left pixel offsets for a 1-D tiling scheme.

    Tiles are placed at regular strides of (patch - overlap) pixels. The
    last tile is always anchored to the far edge so the full extent is
    covered without exceeding it.

    Parameters
    ----------
    n       : Total length of the dimension to tile.
    patch   : Tile size in pixels.
    overlap : Overlap between adjacent tiles in pixels.

    Returns
    -------
    Sorted list of unique integer offsets.
    """
    stride = patch - overlap

    if patch >= n:
        return [0]

    indices = []
    pos = 0
    while True:
        if pos + patch >= n:
            indices.append(n - patch)
            break
        indices.append(pos)
        pos += stride

    return sorted(set(indices))


def _pad_chw(arr: np.ndarray, patch: int) -> tuple:
    """
    Edge-pad a CHW array so that both spatial dimensions are at least
    `patch` pixels, enabling full tile coverage without out-of-bounds access.

    Parameters
    ----------
    arr   : Input array with layout (C, H, W).
    patch : Minimum required spatial size.

    Returns
    -------
    Tuple of (padded_array, original_H, original_W).
    """
    C, H, W = arr.shape
    pad_h = max(0, patch - H)
    pad_w = max(0, patch - W)

    if pad_h or pad_w:
        arr = np.pad(arr, ((0, 0), (0, pad_h), (0, pad_w)), mode="edge")

    return arr, H, W


def _get_model_fixed_patch(session) -> int | None:
    """
    Inspect the ONNX session's input shape to determine whether the model
    enforces a fixed spatial patch size.

    Returns the patch size as an integer if the model has static, equal
    height and width dimensions, otherwise returns None.

    Parameters
    ----------
    session : An onnxruntime.InferenceSession instance.

    Returns
    -------
    int or None.
    """
    try:
        shape = session.get_inputs()[0].shape
        if len(shape) >= 4:
            h, w = shape[-2], shape[-1]
            if isinstance(h, int) and isinstance(w, int) and h == w and h > 0:
                return int(h)
    except Exception:
        pass

    return None


def _preserve_border(dst: np.ndarray, src: np.ndarray, px: int = 10) -> np.ndarray:
    """
    Overwrite a `px`-wide ring of pixels around the border of `dst` with the
    corresponding region from `src`.

    This corrects edge artefacts that can be introduced by tiled inference,
    where border tiles receive fewer blending contributions than interior ones.
    Both arrays must share the same shape in CHW layout.

    Parameters
    ----------
    dst : Destination array (CHW) to be corrected in-place.
    src : Source array (CHW) supplying the original border pixels.
    px  : Border width in pixels.

    Returns
    -------
    The modified `dst` array.
    """
    if px <= 0 or dst.shape != src.shape:
        return dst

    H, W = dst.shape[1], dst.shape[2]
    px = int(max(0, min(px, H // 2, W // 2)))

    if px == 0:
        return dst

    dst[:, :px, :]   = src[:, :px, :]    # top
    dst[:, -px:, :]  = src[:, -px:, :]   # bottom
    dst[:, :, :px]   = src[:, :, :px]    # left
    dst[:, :, -px:]  = src[:, :, -px:]   # right

    return dst


# ---------------------------------------------------------------------------
# Tiled ONNX inference
# ---------------------------------------------------------------------------

def run_onnx_tiled(
    session,
    img: np.ndarray,
    patch_size: int = 512,
    overlap: int = 64,
) -> np.ndarray:
    """
    Run an ONNX model over a full image using overlapping tiles with
    Hann-window blending.

    The function accepts images in HW or HWC layout (as produced by tifffile),
    internally converts to CHW for inference, and returns the result in the
    original spatial layout (HW for single-channel, HWC for multi-channel).

    Each tile is processed independently as a (1, 1, P, P) batch; the model
    is assumed to accept single-channel input per call. Multi-channel images
    are processed channel-by-channel.

    Parameters
    ----------
    session    : Loaded onnxruntime.InferenceSession.
    img        : Input image as float32 in HW or HWC layout, values in [0, 1].
    patch_size : Tile side length in pixels.
    overlap    : Pixel overlap between adjacent tiles.

    Returns
    -------
    np.ndarray of the same spatial shape as `img`, values clipped to [0, 1].
    """
    # Normalise input layout to CHW.
    if img.ndim == 2:
        arr = img[np.newaxis, ...]                 # (H, W) -> (1, H, W)
    elif img.ndim == 3:
        arr = img.transpose(2, 0, 1)               # (H, W, C) -> (C, H, W)

    arr = arr.astype(np.float32)

    # Pad to ensure full tile coverage.
    arr_padded, H0, W0 = _pad_chw(arr, patch_size)
    C, H, W = arr_padded.shape

    win      = _hann2d(patch_size)
    out_buf  = np.zeros_like(arr_padded, dtype=np.float32)
    wgt_buf  = np.zeros_like(arr_padded, dtype=np.float32)

    hs = _tile_indices(H, patch_size, overlap)
    ws = _tile_indices(W, patch_size, overlap)

    inp_name    = session.get_inputs()[0].name
    total_tiles = len(hs) * len(ws) * C
    processed   = 0
    last_pct    = -1

    print(f"INFO: Processing {total_tiles} tiles...")

    for c in range(C):
        for i in hs:
            for j in ws:
                # Extract single-channel patch and add batch dimension: (1, 1, P, P).
                patch = arr_padded[c:c+1, i:i+patch_size, j:j+patch_size]
                inp   = np.ascontiguousarray(patch[np.newaxis, ...])

                res = session.run(None, {inp_name: inp})[0]  # (1, 1, P, P)
                res = np.squeeze(res, axis=0)                 # (1, P, P)

                # Accumulate weighted result for later normalisation.
                out_buf[c:c+1, i:i+patch_size, j:j+patch_size] += res * win
                wgt_buf[c:c+1, i:i+patch_size, j:j+patch_size] += win

                processed += 1
                pct = int(processed / total_tiles * 100)
                if pct > last_pct:
                    print(f"Progress: {pct}%")
                    sys.stdout.flush()
                    last_pct = pct

    # Normalise accumulated weights to produce the blended output.
    wgt_buf[wgt_buf == 0] = 1.0
    result = out_buf / wgt_buf

    # Crop padding and clamp output range.
    result = result[:, :H0, :W0]
    result = np.clip(result, 0.0, 1.0)

    # Restore original border pixels to suppress edge artefacts.
    result = _preserve_border(result, arr, px=10)

    # Convert back to the original spatial layout.
    if result.shape[0] == 1:
        return result[0]          # (1, H, W) -> (H, W)
    else:
        return result.transpose(1, 2, 0)  # (C, H, W) -> (H, W, C)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Tiled ONNX image restoration worker")
    parser.add_argument("--input",    required=True,           help="Input image path (TIFF or FITS)")
    parser.add_argument("--output",   required=True,           help="Output image path (.tiff or .raw)")
    parser.add_argument("--model",    required=True,           help="ONNX model path")
    parser.add_argument("--patch",    type=int, default=512,   help="Tile size in pixels (default: 512)")
    parser.add_argument("--overlap",  type=int, default=64,    help="Tile overlap in pixels (default: 64)")
    parser.add_argument("--provider", default="CPU",           help="Execution provider: CPU | CUDA | DirectML | CoreML")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: Input file not found: {args.input}")
        sys.exit(1)

    # Load input image.
    try:
        data = tifffile.imread(args.input).astype(np.float32)
        print(f"DEBUG: Input shape={data.shape}  min={data.min():.4f}  max={data.max():.4f}  mean={data.mean():.4f}")
    except Exception as e:
        try:
            from astropy.io import fits
            with fits.open(args.input) as hdul:
                data = hdul[0].data.astype(np.float32)
        except Exception:
            print(f"Error: Failed to load input image: {e}")
            sys.exit(1)

    # Build the provider list, honouring the requested accelerator with CPU fallback.
    available = ort.get_available_providers()
    providers = []

    provider_map = {
        "DirectML": "DmlExecutionProvider",
        "CUDA":     "CUDAExecutionProvider",
        "CoreML":   "CoreMLExecutionProvider",
    }

    requested = provider_map.get(args.provider)
    if requested and requested in available:
        providers.append(requested)

    if "CPUExecutionProvider" in available:
        providers.append("CPUExecutionProvider")

    print(f"INFO: Requesting providers: {providers}")

    # Load model.
    try:
        session = ort.InferenceSession(args.model, providers=providers)
    except Exception as e:
        print(f"Error: Failed to load model: {e}")
        sys.exit(1)

    # Override patch size if the model enforces fixed spatial dimensions.
    patch_size = args.patch
    fixed_patch = _get_model_fixed_patch(session)
    if fixed_patch is not None:
        print(f"INFO: Model enforces fixed patch size: {fixed_patch}")
        patch_size = fixed_patch

    # Run tiled inference.
    start = time.time()
    try:
        result = run_onnx_tiled(session, data, patch_size, args.overlap)
    except Exception as e:
        print(f"Error: Inference failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    elapsed = time.time() - start
    print(f"INFO: Inference completed in {elapsed:.2f}s")
    print(f"DEBUG: Output shape={result.shape}  min={result.min():.4f}  max={result.max():.4f}  mean={result.mean():.4f}")

    # Write output.
    if args.output.endswith(".raw"):
        result.tofile(args.output)
        print("RESULT: Saved RAW")
    else:
        tifffile.imwrite(args.output, result)
        print("RESULT: Saved TIFF")


if __name__ == "__main__":
    main()