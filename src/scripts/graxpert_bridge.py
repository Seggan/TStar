"""
GraXpert Bridge
===============
Provides TIFF/FITS I/O utilities for exchanging image data between the host
application and the GraXpert background-extraction tool.

The bridge communicates via raw float32 binary files: the host writes or reads
flat arrays, and this script handles all format detection and axis normalisation.

Usage:
    python graxpert_bridge.py save <tiff_out> <w> <h> <c> <raw_in>
    python graxpert_bridge.py load <image_path> <raw_out>
"""

import sys
import numpy as np
import tifffile as tiff
import os


# ---------------------------------------------------------------------------
# I/O helpers
# ---------------------------------------------------------------------------

def save(tiff_path: str, w: int, h: int, c: int, raw_path: str) -> None:
    """
    Reconstruct an image from a raw float32 binary file and write it as a TIFF.

    The raw file must contain exactly w * h * c float32 values in row-major
    (HWC or HW) order. For single-channel images the 'minisblack' photometric
    is used, which is the format preferred by GraXpert.

    Parameters
    ----------
    tiff_path : Destination TIFF file path.
    w         : Image width in pixels.
    h         : Image height in pixels.
    c         : Number of channels (1 for mono, 3 for RGB).
    raw_path  : Source raw float32 binary file path.
    """
    try:
        data = np.fromfile(raw_path, dtype=np.float32)

        if c == 1:
            data = data.reshape((h, w))
        else:
            data = data.reshape((h, w, c))

        photometric = "minisblack" if c == 1 else "rgb"
        tiff.imwrite(tiff_path, data, photometric=photometric)
        print("Saved TIFF")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


def load(path: str, raw_out_path: str) -> None:
    """
    Load a TIFF or FITS image, normalise its axis layout to HWC / HW, and
    write the data to a raw float32 binary file.

    For FITS files the data may arrive in CHW layout (channels-first); this
    is detected heuristically and transposed to HWC before writing. The
    resulting dimensions are printed to stdout in the form expected by the
    host application:

        RESULT: <w> <h> <c>

    Parameters
    ----------
    path         : Source image file path (TIFF or FITS).
    raw_out_path : Destination path for the raw float32 binary output.
    """
    try:
        # Attempt TIFF first; fall back to FITS via astropy.
        data = None
        try:
            data = tiff.imread(path)
        except Exception:
            from astropy.io import fits
            with fits.open(path) as hdul:
                data = hdul[0].data

        data = data.astype(np.float32)

        # Resolve image dimensions and normalise layout to HWC / HW.
        if data.ndim == 2:
            h, w = data.shape
            c = 1
        elif data.ndim == 3:
            # FITS files are typically channels-first (CHW) when the leading
            # dimension is small (< 5). TIFF files are typically HWC.
            if data.shape[0] < 5:
                c, h, w = data.shape
                data = np.transpose(data, (1, 2, 0))
            else:
                h, w, c = data.shape
        else:
            raise ValueError(f"Unexpected array dimensions: {data.ndim}")

        data.tofile(raw_out_path)
        print(f"RESULT: {w} {h} {c}")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: graxpert_bridge.py <save|load> ...")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "save":
        save(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])
    elif cmd == "load":
        load(sys.argv[2], sys.argv[3])
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)