#!/usr/bin/env python3
"""
Font Atlas Generator for Skyrim VR 3D Text Rendering

Generates a texture atlas with characters positioned on a consistent baseline.
Output is a PNG file that is converted to DDS using texconv.
"""

import sys
import csv
import shutil
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# =============================================================================
# Atlas Configuration - Must match TextAssets.h in C++ code
# =============================================================================
ATLAS_COLS = 16
ATLAS_ROWS = 14
CELL_SIZE = 128  # pixels per cell
ATLAS_WIDTH = ATLAS_COLS * CELL_SIZE   # 2048
ATLAS_HEIGHT = ATLAS_ROWS * CELL_SIZE  # 1792

# Font settings
FONT_PATH = "C:/Users/mika/Downloads/futura-condensed-medium_17du0/Futura Condensed Medium/Futura Condensed Medium.otf"
FONT_SIZE = 110  # Fills cell well

# Appearance
FILL_COLOR = (255, 255, 255, 255)  # White
OUTLINE_COLOR = (0, 0, 0, 255)      # Black
OUTLINE_WIDTH = 5
BACKGROUND_COLOR = (0, 0, 0, 0)     # Transparent

# CRITICAL: Character positioning within cell
# Characters must be positioned at BASELINE, NOT centered vertically!
# This ensures consistent text alignment across all characters
BASELINE_RATIO = 0.75  # Baseline at 75% from top of cell

# Output (local)
OUTPUT_PNG = "text_atlas_main.png"
OUTPUT_DDS = "text_atlas_main.dds"
OUTPUT_CSV = "text_atlas_main_mapping.csv"
TEXCONV_PATH = "texconv"  # Assumes texconv is in PATH
DDS_FORMAT = "BC3_UNORM"

# Deployment directories (Skyrim mod folder)
DEPLOY_CSV_DIR = Path(r"C:\games\skyrim\VRDEV\mods\DressVR\SKSE\Plugins\3DUI")
DEPLOY_TEXTURE_DIR = Path(r"C:\games\skyrim\VRDEV\mods\DressVR\textures\3DUI")

# Input
CHARACTERS_FILE = "characters.txt"

# =============================================================================
# Script
# =============================================================================

def print_config(char_count):
    """Print configuration on startup"""
    print("=== Font Atlas Generator ===")
    print(f"Atlas: {ATLAS_WIDTH}x{ATLAS_HEIGHT} ({ATLAS_COLS} cols x {ATLAS_ROWS} rows)")
    print(f"Cell size: {CELL_SIZE}x{CELL_SIZE} pixels")
    print(f"Font: {FONT_PATH} @ {FONT_SIZE}pt")
    print(f"Fill: White ({FILL_COLOR[0]},{FILL_COLOR[1]},{FILL_COLOR[2]})")
    print(f"Outline: Black ({OUTLINE_COLOR[0]},{OUTLINE_COLOR[1]},{OUTLINE_COLOR[2]}) @ {OUTLINE_WIDTH}px")
    print(f"Baseline: {int(BASELINE_RATIO * 100)}% from top")
    print(f"Characters: {char_count}")
    print(f"Output: {OUTPUT_PNG} -> {OUTPUT_DDS}")
    print()


def load_characters():
    """Load characters from characters.txt"""
    char_file = Path(__file__).parent / CHARACTERS_FILE

    if not char_file.exists():
        print(f"ERROR: {CHARACTERS_FILE} not found!")
        print(f"Please run extract_characters.py first to generate {CHARACTERS_FILE}")
        sys.exit(1)

    with open(char_file, 'r', encoding='utf-8') as f:
        return f.read()


def draw_text_with_outline(draw, text, position, font, fill_color, outline_color, outline_width):
    """Draw text with outline by drawing outline in 8 directions then fill"""
    x, y = position

    # Draw outline in 8 directions
    for dx in range(-outline_width, outline_width + 1):
        for dy in range(-outline_width, outline_width + 1):
            if dx != 0 or dy != 0:
                draw.text((x + dx, y + dy), text, font=font, fill=outline_color)

    # Draw fill on top
    draw.text((x, y), text, font=font, fill=fill_color)


def generate_atlas(characters):
    """Generate the font atlas texture"""
    # Create RGBA image with transparent background
    atlas = Image.new('RGBA', (ATLAS_WIDTH, ATLAS_HEIGHT), BACKGROUND_COLOR)
    draw = ImageDraw.Draw(atlas)

    # Load font
    try:
        font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    except Exception as e:
        print(f"ERROR: Failed to load font from {FONT_PATH}")
        print(f"Error: {e}")
        sys.exit(1)

    # Get font metrics for baseline calculation
    # Note: getmetrics() returns (ascent, descent)
    ascent, descent = font.getmetrics()

    # Calculate baseline Y position in cell
    baseline_y = int(CELL_SIZE * BASELINE_RATIO)

    print(f"Font metrics: ascent={ascent}, descent={descent}")
    print(f"Baseline position: {baseline_y} pixels from top of cell")
    print()

    # Track mapping data for CSV
    mapping_data = []

    # Render each character
    total_chars = len(characters)
    for index, char in enumerate(characters):
        # Calculate grid position
        row = index // ATLAS_COLS
        col = index % ATLAS_COLS

        # Calculate cell top-left position
        cell_x = col * CELL_SIZE
        cell_y = row * CELL_SIZE

        # Get character bounding box for horizontal centering
        bbox = draw.textbbox((0, 0), char, font=font)
        char_width = bbox[2] - bbox[0] + 2 * OUTLINE_WIDTH

        # Center horizontally
        char_x = cell_x + (CELL_SIZE - char_width) // 2

        # Position at baseline (NOT centered vertically)
        # The character's baseline should sit at baseline_y
        # PIL draws text with the top of the character at the given Y position
        # So we need to offset upward by the ascent
        char_y = cell_y + baseline_y - ascent

        # Draw character with outline
        draw_text_with_outline(
            draw,
            char,
            (char_x, char_y),
            font,
            FILL_COLOR,
            OUTLINE_COLOR,
            OUTLINE_WIDTH
        )

        # Store mapping data with width metrics for kerning
        unicode_val = f"U+{ord(char):04X}"
        # Width ratio: character width relative to cell size (0.0 to 1.0+)
        width_ratio = char_width / CELL_SIZE
        mapping_data.append({
            'Character': char,
            'Unicode': unicode_val,
            'Index': index,
            'Row': row,
            'Col': col,
            'X': cell_x,
            'Y': cell_y,
            'Width': char_width,
            'WidthRatio': round(width_ratio, 4)
        })

        # Progress indicator
        if (index + 1) % 32 == 0 or index == total_chars - 1:
            print(f"Rendered {index + 1}/{total_chars} characters...")

    print()
    return atlas, mapping_data


def save_csv_mapping(mapping_data):
    """Save character mapping to CSV with width metrics for kerning"""
    csv_path = Path(__file__).parent / OUTPUT_CSV

    with open(csv_path, 'w', newline='', encoding='utf-8') as f:
        fieldnames = ['Character', 'Unicode', 'Index', 'Row', 'Col', 'X', 'Y', 'Width', 'WidthRatio']
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(mapping_data)

    print(f"Saved mapping to: {csv_path}")


def convert_to_dds(png_path):
    """Convert PNG to DDS using texconv"""
    output_dir = Path(__file__).parent

    print(f"\nConverting to DDS format...")
    print(f"Running: {TEXCONV_PATH} -f {DDS_FORMAT} -y -o {output_dir} {png_path}")

    try:
        result = subprocess.run(
            [TEXCONV_PATH, '-f', DDS_FORMAT, '-y', '-o', str(output_dir), str(png_path)],
            capture_output=True,
            text=True,
            check=True
        )
        print(result.stdout)
        print(f"Successfully created: {output_dir / OUTPUT_DDS}")
    except FileNotFoundError:
        print(f"ERROR: texconv not found in PATH!")
        print(f"Please install DirectXTex tools and add texconv to your PATH")
        print(f"PNG file saved, but DDS conversion failed.")
    except subprocess.CalledProcessError as e:
        print(f"ERROR: texconv failed!")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        print(f"PNG file saved, but DDS conversion failed.")


def deploy_files():
    """Copy generated files to Skyrim mod folder"""
    script_dir = Path(__file__).parent

    print("\n" + "=" * 60)
    print("Deploying to Skyrim mod folder...")
    print("=" * 60)

    # Ensure deployment directories exist
    DEPLOY_CSV_DIR.mkdir(parents=True, exist_ok=True)
    DEPLOY_TEXTURE_DIR.mkdir(parents=True, exist_ok=True)

    # Copy CSV
    src_csv = script_dir / OUTPUT_CSV
    dst_csv = DEPLOY_CSV_DIR / OUTPUT_CSV
    if src_csv.exists():
        shutil.copy2(src_csv, dst_csv)
        print(f"Copied CSV to: {dst_csv}")
    else:
        print(f"WARNING: CSV not found: {src_csv}")

    # Copy DDS texture
    src_dds = script_dir / OUTPUT_DDS
    dst_dds = DEPLOY_TEXTURE_DIR / OUTPUT_DDS
    if src_dds.exists():
        shutil.copy2(src_dds, dst_dds)
        print(f"Copied DDS to: {dst_dds}")
    else:
        print(f"WARNING: DDS not found: {src_dds}")

    print("\nDeployment complete!")


def main():
    # Load characters
    characters = load_characters()

    # Print configuration
    print_config(len(characters))

    # Validate character count
    max_chars = ATLAS_COLS * ATLAS_ROWS
    if len(characters) > max_chars:
        print(f"ERROR: Too many characters ({len(characters)}) for atlas size ({max_chars})")
        sys.exit(1)

    # Generate atlas
    print("Generating atlas...")
    atlas, mapping_data = generate_atlas(characters)

    # Save PNG
    output_path = Path(__file__).parent / OUTPUT_PNG
    atlas.save(output_path)
    print(f"Saved PNG to: {output_path}")

    # Save CSV mapping
    save_csv_mapping(mapping_data)

    # Convert to DDS
    convert_to_dds(output_path)

    # Deploy to Skyrim mod folder
    deploy_files()

    print("\nDone!")


if __name__ == "__main__":
    main()
