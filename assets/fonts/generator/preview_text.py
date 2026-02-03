#!/usr/bin/env python3
"""
Text Rendering Preview Tool for Skyrim VR 3D Text

This script renders text using the exact same spacing logic as TextDriver.cpp
to verify that the font atlas and character spacing work correctly.

Usage:
    python preview_text.py "Hello World"
    python preview_text.py "Hello World" --title
    python preview_text.py "Hello World" --subtitle --letter-spacing 1.2
"""

import sys
import csv
import argparse
from pathlib import Path
from PIL import Image, ImageDraw

# =============================================================================
# Constants from TextAssets.h and TextDriver.cpp
# =============================================================================

# Cell configuration (must match generate_atlas.py)
CELL_SIZE = 128  # pixels per cell in the atlas

# Spacing constants from TextAssets.h
BASE_LETTER_DISTANCE = 175.0
TITLE_SCALE = 4.2
SUBTITLE_SCALE = 3.5
CHAR_GAP = 0.2

# Atlas files
ATLAS_PNG = "text_atlas_main.png"
MAPPING_CSV = "text_atlas_main_mapping.csv"
OUTPUT_PNG = "preview_output.png"

# =============================================================================
# Character Mapping
# =============================================================================

class CharacterMap:
    """Loads and provides access to character mapping data from CSV"""

    def __init__(self, csv_path):
        self.char_data = {}
        self._load_csv(csv_path)

    def _load_csv(self, csv_path):
        """Load character mapping from CSV file"""
        with open(csv_path, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                char = row['Character']
                self.char_data[char] = {
                    'unicode': row['Unicode'],
                    'index': int(row['Index']),
                    'row': int(row['Row']),
                    'col': int(row['Col']),
                    'x': int(row['X']),
                    'y': int(row['Y']),
                    'width': int(row['Width']),
                    'width_ratio': float(row['WidthRatio'])
                }

        print(f"Loaded {len(self.char_data)} characters from mapping")

    def get_char_info(self, char):
        """Get character info, returns None if character not in atlas"""
        return self.char_data.get(char)

    def get_width_ratio(self, char):
        """Get width ratio for character (default 0.5 if not found)"""
        info = self.get_char_info(char)
        return info['width_ratio'] if info else 0.5

# =============================================================================
# Text Layout Engine (matches TextDriver.cpp logic)
# =============================================================================

class TextLayoutEngine:
    """Computes character positions using the same logic as TextDriver.cpp"""

    def __init__(self, char_map, is_title=False, text_scale=1.0, letter_spacing=1.0):
        self.char_map = char_map
        self.is_title = is_title
        self.text_scale = text_scale
        self.letter_spacing = letter_spacing

        # Calculate letter distance (matches TextDriver.cpp ComputeCharacterOffsets)
        scale_multiplier = TITLE_SCALE if is_title else SUBTITLE_SCALE
        self.letter_distance = BASE_LETTER_DISTANCE * scale_multiplier * text_scale * letter_spacing

        print(f"\nLayout Configuration:")
        print(f"  Type: {'TITLE' if is_title else 'SUBTITLE'}")
        print(f"  Scale multiplier: {scale_multiplier}")
        print(f"  Text scale: {text_scale}")
        print(f"  Letter spacing: {letter_spacing}")
        print(f"  Letter distance: {self.letter_distance:.2f}")

    def compute_character_positions(self, text):
        """
        Compute character positions using the exact logic from TextDriver.cpp

        Returns list of (char, x_position) tuples for visible characters
        """
        positions = []
        current_x = 0.0
        first_pos_x = 0.0
        last_pos_x = 0.0
        found_first = False

        # Iterate through text, accumulating positions (matches TextDriver.cpp lines 255-279)
        for char in text:
            # Get width ratio for this character
            width_ratio = self.char_map.get_width_ratio(char)

            # Spaces add to position but don't render (matches line 262-265)
            if char in (' ', '\t', '\u00A0'):
                current_x += self.letter_distance * (width_ratio + CHAR_GAP)
                continue

            # Position at current X (center of character cell) - line 268
            pos_x = current_x
            positions.append((char, pos_x))

            if not found_first:
                first_pos_x = pos_x
                found_first = True
            last_pos_x = pos_x

            # Advance by character width + gap (matches line 278)
            current_x += self.letter_distance * (width_ratio + CHAR_GAP)

        # Center alignment: offset so text is centered around 0 (matches line 281)
        center_offset = (first_pos_x + last_pos_x) / -2.0

        # Apply center offset to all positions (matches lines 298-301)
        centered_positions = [(char, pos + center_offset) for char, pos in positions]

        print(f"\nText Layout:")
        print(f"  Characters: {len(positions)}")
        print(f"  First position: {first_pos_x:.2f}")
        print(f"  Last position: {last_pos_x:.2f}")
        print(f"  Total width: {last_pos_x - first_pos_x:.2f}")
        print(f"  Center offset: {center_offset:.2f}")

        return centered_positions

# =============================================================================
# Text Renderer
# =============================================================================

class TextRenderer:
    """Renders text by extracting character cells from atlas and compositing them"""

    def __init__(self, atlas_path, char_map):
        self.atlas = Image.open(atlas_path).convert('RGBA')
        self.char_map = char_map

        print(f"\nAtlas loaded: {self.atlas.size[0]}x{self.atlas.size[1]}")

    def extract_character_cell(self, char):
        """Extract a character's 128x128 cell from the atlas"""
        info = self.char_map.get_char_info(char)
        if not info:
            print(f"  Warning: Character '{char}' not found in atlas")
            return None

        # Extract cell based on X, Y position in atlas
        x = info['x']
        y = info['y']
        cell = self.atlas.crop((x, y, x + CELL_SIZE, y + CELL_SIZE))

        return cell

    def render_text(self, text, layout_engine, output_path):
        """
        Render text to an image using actual glyph widths for tight spacing.

        This shows what "proper" text rendering would look like with characters
        positioned based on their actual widths, not the C++ world-space formula.
        """
        # Filter to visible characters only
        visible_chars = [ch for ch in text if ch not in (' ', '\t', '\u00A0')]

        if not visible_chars:
            print("No visible characters to render")
            return

        # Calculate positions using widthRatio (like C++ code)
        # To achieve -10px equivalent gap: -10 / 128 = -0.078
        RATIO_GAP = -0.078  # ratio-based gap (negative = overlap)
        char_positions = []  # (char, x_position) - position is LEFT edge of glyph

        current_x = 0
        for i, char in enumerate(text):
            width_ratio = self.char_map.get_width_ratio(char)

            if char in (' ', '\t', '\u00A0'):
                # Space: advance using same formula as regular chars
                current_x += CELL_SIZE * (width_ratio + RATIO_GAP)
                continue

            info = self.char_map.get_char_info(char)
            if not info:
                continue

            # Position this character
            char_positions.append((char, current_x))

            # Advance by widthRatio + gap (matches C++ formula structure)
            current_x += CELL_SIZE * (width_ratio + RATIO_GAP)

        total_width = current_x  # total advance

        # Calculate canvas dimensions
        padding = 64
        canvas_width = int(total_width) + CELL_SIZE + padding * 2  # extra cell for overhang
        canvas_height = CELL_SIZE + padding

        # Create canvas
        canvas = Image.new('RGBA', (canvas_width, canvas_height), (40, 40, 40, 255))
        baseline_y = padding // 2

        print(f"\nRendering (widthRatio-based spacing):")
        print(f"  Canvas: {canvas_width}x{canvas_height}")
        print(f"  Total text width: {total_width:.1f}px")
        print(f"  Ratio gap: {RATIO_GAP} ({RATIO_GAP * CELL_SIZE:.1f}px equivalent)")

        # Render each character
        for char, char_x in char_positions:
            cell = self.extract_character_cell(char)
            if not cell:
                continue

            # char_x is the left edge position of where the glyph should be
            # The glyph is centered in the cell, so offset by half the empty space
            info = self.char_map.get_char_info(char)
            glyph_width = info['width']
            cell_offset = (CELL_SIZE - glyph_width) // 2

            paste_x = int(padding + char_x - cell_offset)
            paste_y = baseline_y

            canvas.paste(cell, (paste_x, paste_y), cell)

        # Save output
        canvas.save(output_path)
        print(f"\nSaved output to: {output_path}")

        # Print character details for debugging
        print(f"\nCharacter Details:")
        for char, char_x in char_positions:
            info = self.char_map.get_char_info(char)
            if info:
                advance = CELL_SIZE * (info['width_ratio'] + RATIO_GAP)
                print(f"  '{char}': x={char_x:6.1f}, ratio={info['width_ratio']:.4f}, "
                      f"advance={advance:.1f}px")

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Render text preview using font atlas with C++ spacing logic',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python preview_text.py "Hello World"
  python preview_text.py "Hello World" --title
  python preview_text.py "The Quick Brown Fox" --subtitle --letter-spacing 1.1
        """
    )

    parser.add_argument('text', nargs='?', default='Hello World',
                        help='Text to render (default: "Hello World")')
    parser.add_argument('--title', action='store_true',
                        help=f'Use title scale ({TITLE_SCALE}x)')
    parser.add_argument('--subtitle', action='store_true',
                        help=f'Use subtitle scale ({SUBTITLE_SCALE}x) [default]')
    parser.add_argument('--text-scale', type=float, default=1.0,
                        help='Additional text scale multiplier (default: 1.0)')
    parser.add_argument('--letter-spacing', type=float, default=1.0,
                        help='Letter spacing multiplier (default: 1.0)')
    parser.add_argument('--output', default=OUTPUT_PNG,
                        help=f'Output PNG file (default: {OUTPUT_PNG})')

    args = parser.parse_args()

    # Determine if title or subtitle
    is_title = args.title or not args.subtitle  # Default to title if neither specified
    if args.title and args.subtitle:
        print("Warning: Both --title and --subtitle specified, using title")
        is_title = True

    # Get script directory
    script_dir = Path(__file__).parent

    # Paths to atlas files
    atlas_path = script_dir / ATLAS_PNG
    mapping_path = script_dir / MAPPING_CSV
    output_path = script_dir / args.output

    # Check if files exist
    if not atlas_path.exists():
        print(f"ERROR: Atlas not found: {atlas_path}")
        print(f"Please run generate_atlas.py first to create {ATLAS_PNG}")
        sys.exit(1)

    if not mapping_path.exists():
        print(f"ERROR: Mapping not found: {mapping_path}")
        print(f"Please run generate_atlas.py first to create {MAPPING_CSV}")
        sys.exit(1)

    print("=" * 70)
    print("Font Atlas Text Preview Tool")
    print("=" * 70)
    print(f"\nText: \"{args.text}\"")

    # Load character mapping
    char_map = CharacterMap(mapping_path)

    # Create layout engine
    layout_engine = TextLayoutEngine(
        char_map,
        is_title=is_title,
        text_scale=args.text_scale,
        letter_spacing=args.letter_spacing
    )

    # Create renderer and render text
    renderer = TextRenderer(atlas_path, char_map)
    renderer.render_text(args.text, layout_engine, output_path)

    print("\n" + "=" * 70)
    print("Done!")
    print("=" * 70)

if __name__ == "__main__":
    main()
