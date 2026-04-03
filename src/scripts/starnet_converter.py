"""
StarNet Converter
=================
Utility script for converting a TIFF image to a raw float32 binary file.
Used as a preprocessing step before passing data to the StarNet inference pipeline.

Usage:
    python starnet_converter.py <input_tiff> <output_raw>
"""

import sys
import numpy as np
import tifffile


def convert(input_path: str, output_path: str) -> None:
    """
    Load a TIFF image, cast it to float32, and write the raw binary data to disk.

    The output file contains the flat array of float32 values in row-major order,
    preserving the original spatial layout (HWC or HW). The caller is responsible
    for knowing the image dimensions from the printed shape string.

    Parameters
    ----------
    input_path  : Path to the source TIFF file.
    output_path : Destination path for the raw float32 binary output.
    """
    data = tifffile.imread(input_path).astype(np.float32)
    data.tofile(output_path)
    print(f"Done:{data.shape}")


if __name__ == "__main__":
    try:
        convert(sys.argv[1], sys.argv[2])
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)