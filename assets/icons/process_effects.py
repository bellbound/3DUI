#!/usr/bin/env python3
"""
Apply visual effects to icon images after scaling.

Effects:
  --remove-partial-transparency : Make semi-transparent pixels fully opaque
  --opacity-threshold=128       : Only convert pixels with alpha >= threshold (0-255)
                                  Lower values preserve more anti-aliasing (default: 0 = all)
  --outline="14px #bdbabd"      : Add colored outline around opaque regions (circular/smooth)
  --brightness="-100"           : Adjust brightness (-100 to 100, 0 = no change)
  --padding=14                  : Add transparent padding around the image

Usage:
  python process_effects.py input.png output.png [options]
"""

import sys
import argparse
import re
import math
from pathlib import Path
from PIL import Image, ImageFilter, ImageEnhance
import numpy as np


def remove_partial_transparency(img: Image.Image, threshold: int = 0) -> Image.Image:
    """
    Convert alpha values to grayscale RGB, then make fully opaque.

    For each pixel with alpha >= threshold:
    - R, G, B are all set to the original alpha value (grayscale from opacity)
    - Alpha is set to 255 (fully opaque)

    Pixels with alpha < threshold are left unchanged (preserves anti-aliased edges).

    Args:
        img: Input RGBA image
        threshold: Only convert pixels with alpha >= this value (0-255)
                   Higher values preserve more anti-aliasing.
                   Example: threshold=128 keeps semi-transparent edge pixels intact.

    Example: RGBA(100, 150, 200, 200) with threshold=128 -> RGBA(200, 200, 200, 255)
    Example: RGBA(100, 150, 200, 64) with threshold=128 -> RGBA(100, 150, 200, 64) (unchanged)
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    # Get pixel data
    pixels = img.load()
    width, height = img.size

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a >= threshold and a > 0:
                # Set RGB to the alpha value, then make opaque
                pixels[x, y] = (a, a, a, 255)
            # else: leave pixel unchanged (preserves anti-aliased edges)

    return img


def parse_outline_arg(outline_str: str) -> tuple:
    """
    Parse outline argument like "14px #bdbabd" or "14 #bdbabd".
    Returns (width_px, color_tuple).
    """
    # Match patterns like "14px #bdbabd", "14 #bdbabd", "14px rgb(255,255,255)"
    match = re.match(r'(\d+)(?:px)?\s+(.+)', outline_str.strip())
    if not match:
        raise ValueError(f"Invalid outline format: '{outline_str}'. Expected: '14px #bdbabd'")

    width = int(match.group(1))
    color_str = match.group(2).strip()

    # Parse color
    if color_str.startswith('#'):
        # Hex color
        hex_color = color_str.lstrip('#')
        if len(hex_color) == 3:
            hex_color = ''.join(c * 2 for c in hex_color)
        if len(hex_color) != 6:
            raise ValueError(f"Invalid hex color: {color_str}")
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        color = (r, g, b, 255)
    elif color_str.startswith('rgb'):
        # RGB format: rgb(r,g,b)
        match_rgb = re.match(r'rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*(\d+))?\)', color_str)
        if not match_rgb:
            raise ValueError(f"Invalid rgb color: {color_str}")
        r, g, b = int(match_rgb.group(1)), int(match_rgb.group(2)), int(match_rgb.group(3))
        a = int(match_rgb.group(4)) if match_rgb.group(4) else 255
        color = (r, g, b, a)
    else:
        raise ValueError(f"Unknown color format: {color_str}")

    return width, color


def create_circular_kernel(radius: int) -> np.ndarray:
    """
    Create a circular structuring element (kernel) for morphological operations.
    This produces smooth, rounded outlines instead of square/blocky ones.
    """
    size = radius * 2 + 1
    kernel = np.zeros((size, size), dtype=np.uint8)
    center = radius

    for y in range(size):
        for x in range(size):
            # Check if point is within circle
            dist = math.sqrt((x - center) ** 2 + (y - center) ** 2)
            if dist <= radius:
                kernel[y, x] = 1

    return kernel


def dilate_with_circular_kernel(mask: Image.Image, radius: int) -> Image.Image:
    """
    Dilate a binary mask using a circular kernel.
    This creates smooth, rounded expansions instead of square ones.
    """
    if radius <= 0:
        return mask

    # Convert to numpy array
    mask_array = np.array(mask, dtype=np.uint8)

    # Create circular kernel
    kernel = create_circular_kernel(radius)
    kernel_size = kernel.shape[0]
    pad = radius

    # Pad the mask array
    padded = np.pad(mask_array, pad, mode='constant', constant_values=0)

    # Output array
    output = np.zeros_like(mask_array)

    height, width = mask_array.shape

    # Apply dilation: for each pixel, if any kernel pixel overlaps with a white pixel, output is white
    for y in range(height):
        for x in range(width):
            # Extract the region under the kernel
            region = padded[y:y + kernel_size, x:x + kernel_size]
            # Check if any kernel position overlaps with a white pixel
            if np.any((region > 0) & (kernel > 0)):
                output[y, x] = 255

    return Image.fromarray(output, mode='L')


def add_outline(img: Image.Image, width_px: int, color: tuple) -> Image.Image:
    """
    Add a colored outline around the opaque parts of the image.
    Uses circular dilation for smooth, rounded outlines.

    Algorithm:
    1. Create a mask from the alpha channel (opaque = white)
    2. Dilate the mask by width_px pixels using circular kernel
    3. Subtract original mask to get outline-only mask
    4. Create colored outline layer
    5. Composite: background <- outline <- original
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    # Extract alpha channel as mask (255 where opaque, 0 where transparent)
    alpha = img.split()[3]

    # Create binary mask (any alpha > 0 is considered opaque for outline purposes)
    binary_mask = alpha.point(lambda x: 255 if x > 0 else 0)

    # Dilate the mask using circular kernel for smooth outlines
    dilated = dilate_with_circular_kernel(binary_mask, width_px)

    # Subtract original to get outline-only region
    dilated_array = np.array(dilated, dtype=np.uint8)
    binary_array = np.array(binary_mask, dtype=np.uint8)

    # Outline = dilated AND NOT original
    outline_array = np.where((dilated_array > 0) & (binary_array == 0), 255, 0).astype(np.uint8)
    outline_mask = Image.fromarray(outline_array, mode='L')

    # Create outline layer (solid color where outline mask is white)
    outline_layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    outline_color_img = Image.new('RGBA', img.size, color)
    outline_layer.paste(outline_color_img, mask=outline_mask)

    # Composite: outline first, then original on top
    result = Image.new('RGBA', img.size, (0, 0, 0, 0))
    result = Image.alpha_composite(result, outline_layer)
    result = Image.alpha_composite(result, img)

    return result


def add_padding(img: Image.Image, padding: int) -> Image.Image:
    """
    Add transparent padding around the image.
    The image is scaled down to fit within the new padded bounds.
    """
    if padding <= 0:
        return img

    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    orig_width, orig_height = img.size

    # Calculate new size for the content (shrunk to make room for padding)
    new_content_width = orig_width - (padding * 2)
    new_content_height = orig_height - (padding * 2)

    if new_content_width <= 0 or new_content_height <= 0:
        raise ValueError(f"Padding {padding}px is too large for image size {orig_width}x{orig_height}")

    # Resize the image to fit within the padded area
    resized = img.resize((new_content_width, new_content_height), Image.LANCZOS)

    # Create new canvas at original size with transparency
    canvas = Image.new('RGBA', (orig_width, orig_height), (0, 0, 0, 0))

    # Paste resized image centered (which means padding on all sides)
    canvas.paste(resized, (padding, padding))

    return canvas


def adjust_brightness(img: Image.Image, brightness: int) -> Image.Image:
    """
    Adjust brightness of the image.
    brightness: -100 (black) to 0 (unchanged) to 100 (white)

    Only affects RGB channels, preserves alpha.
    """
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    # Clamp brightness to valid range
    brightness = max(-100, min(100, brightness))

    if brightness == 0:
        return img

    # Split into RGB and Alpha
    r, g, b, a = img.split()
    rgb_img = Image.merge('RGB', (r, g, b))

    # Convert brightness to enhancement factor
    # brightness = -100 -> factor = 0 (black)
    # brightness = 0    -> factor = 1 (unchanged)
    # brightness = 100  -> factor = 2 (double brightness, clamped to white)
    if brightness > 0:
        # Positive: enhance brightness
        factor = 1.0 + (brightness / 100.0)
    else:
        # Negative: reduce brightness
        factor = 1.0 + (brightness / 100.0)  # This gives 0 to 1 range

    enhancer = ImageEnhance.Brightness(rgb_img)
    brightened = enhancer.enhance(factor)

    # Recombine with alpha
    r2, g2, b2 = brightened.split()
    result = Image.merge('RGBA', (r2, g2, b2, a))

    return result


def process_image(input_path: str, output_path: str,
                  remove_transparency: bool = False,
                  opacity_threshold: int = 0,
                  outline: str = None,
                  brightness: int = None,
                  padding: int = None) -> str:
    """
    Apply requested effects to the image.
    Effects are applied in order:
    1. Add padding (if requested) - done first so other effects work on padded image
    2. Remove partial transparency (if requested, with threshold)
    3. Adjust brightness (if requested)
    4. Add outline (if requested) - done last so outline is at full brightness
    """
    input_path = Path(input_path)
    output_path = Path(output_path)

    with Image.open(input_path) as img:
        if img.mode != 'RGBA':
            img = img.convert('RGBA')

        effects_applied = []

        # Effect 1: Add padding (first, so other effects work on padded image)
        if padding is not None and padding > 0:
            img = add_padding(img, padding)
            effects_applied.append(f"padding={padding}px")

        # Effect 2: Remove partial transparency
        if remove_transparency:
            img = remove_partial_transparency(img, opacity_threshold)
            if opacity_threshold > 0:
                effects_applied.append(f"remove-partial-transparency(threshold={opacity_threshold})")
            else:
                effects_applied.append("remove-partial-transparency")

        # Effect 3: Adjust brightness
        if brightness is not None and brightness != 0:
            img = adjust_brightness(img, brightness)
            effects_applied.append(f"brightness={brightness}")

        # Effect 4: Add outline (last, so outline has consistent color)
        if outline:
            width_px, color = parse_outline_arg(outline)
            img = add_outline(img, width_px, color)
            effects_applied.append(f"outline={width_px}px")

        # Save result
        img.save(output_path, 'PNG')

        if effects_applied:
            print(f"Applied effects: {', '.join(effects_applied)}")
        print(f"Saved to: {output_path}")

    return str(output_path)


def main():
    parser = argparse.ArgumentParser(
        description='Apply visual effects to icon images.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python process_effects.py input.png output.png --remove-partial-transparency
  python process_effects.py input.png output.png --outline="14px #bdbabd"
  python process_effects.py input.png output.png --brightness=-50
  python process_effects.py input.png output.png --padding=14
  python process_effects.py input.png output.png --remove-partial-transparency --outline="10px #ffffff" --brightness=-20 --padding=10
        """
    )

    parser.add_argument('input', help='Input image path')
    parser.add_argument('output', help='Output image path')
    parser.add_argument('--remove-partial-transparency', action='store_true',
                        help='Make semi-transparent pixels fully opaque')
    parser.add_argument('--opacity-threshold', type=int, default=0, metavar='VALUE',
                        help='Only convert pixels with alpha >= threshold (0-255). '
                             'Higher values preserve more anti-aliased edges. Default: 0 (all)')
    parser.add_argument('--outline', type=str, metavar='"WIDTHpx #COLOR"',
                        help='Add circular outline, e.g., "14px #bdbabd"')
    parser.add_argument('--brightness', type=int, metavar='VALUE',
                        help='Adjust brightness (-100 to 100, 0 = no change)')
    parser.add_argument('--padding', type=int, metavar='PIXELS',
                        help='Add transparent padding around the image')

    args = parser.parse_args()

    # Check if any effect was requested
    if not args.remove_partial_transparency and not args.outline and args.brightness is None and args.padding is None:
        print("No effects requested. Use --help for options.")
        sys.exit(0)

    try:
        process_image(
            args.input,
            args.output,
            remove_transparency=args.remove_partial_transparency,
            opacity_threshold=args.opacity_threshold,
            outline=args.outline,
            brightness=args.brightness,
            padding=args.padding
        )
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
