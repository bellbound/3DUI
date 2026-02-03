#!/usr/bin/env python3
"""
Font Texture Atlas Generator
Generates a texture atlas of characters from a specified font
"""

from PIL import Image, ImageDraw, ImageFont
import math
import csv

# Configuration
CHAR_SIZE = 128  # Size of each character cell
FONT_SIZE = 120   # Font size (adjust to fit well in 128x128)
ATLAS_COLS = 16  # Number of columns in the atlas
TEXT_COLOR = (255, 255, 255, 255)  # White with full opacity

# Character set to include (ASCII + extended Latin + special characters from image)
CHARACTERS = (
    # Basic ASCII
    ' !"#$%&\'()*+,-./'
    '0123456789:;<=>?'
    '@ABCDEFGHIJKLMNO'
    'PQRSTUVWXYZ[\\]^_'
    '`abcdefghijklmno'
    'pqrstuvwxyz{|}~'
    # Extended ASCII and Latin-1 Supplement
    '\xa0¡¢£¤¥¦§¨©ª«¬\xad®¯'
    '°±²³´µ¶·¸¹º»¼½¾¿'
    'ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ'
    'ÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß'
    'àáâãäåæçèéêëìíîï'
    'ðñòóôõö÷øùúûüýþÿ'
    # Additional special characters
    'ŒœŠšŸŽž'
    '–—''‚""„†‡•…‰‹›€™'
)

def create_font_atlas(font_path=None):
    """
    Create a texture atlas from a font
    
    Args:
        font_path: Path to the font file. If None, uses system default.
    """
    # Calculate atlas dimensions
    num_chars = len(CHARACTERS)
    atlas_rows = math.ceil(num_chars / ATLAS_COLS)
    atlas_width = CHAR_SIZE * ATLAS_COLS
    atlas_height = CHAR_SIZE * atlas_rows
    
    # Create the atlas image with transparency
    atlas = Image.new('RGBA', (atlas_width, atlas_height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(atlas)
    
    # Try to load the font
    try:
        if font_path:
            font = ImageFont.truetype(font_path, FONT_SIZE)
            print(f"Using font: {font_path}")
        else:
            # Try to find Futura font in common locations
            font_names = [
                '/usr/share/fonts/truetype/futura/Futura.ttf',
                '/System/Library/Fonts/Futura.ttc',
                'C:\\Windows\\Fonts\\Futura.ttf',
                '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',  # Fallback
            ]
            
            font = None
            for font_name in font_names:
                try:
                    font = ImageFont.truetype(font_name, FONT_SIZE)
                    print(f"Using font: {font_name}")
                    break
                except:
                    continue
            
            if font is None:
                # Use default font as last resort
                font = ImageFont.load_default()
                print("Warning: Using default font. For better results, specify a Futura font path.")
    except Exception as e:
        print(f"Error loading font: {e}")
        font = ImageFont.load_default()
        print("Using default font.")
    
    # Draw each character in its cell
    for idx, char in enumerate(CHARACTERS):
        row = idx // ATLAS_COLS
        col = idx % ATLAS_COLS
        
        # Calculate cell position
        x = col * CHAR_SIZE
        y = row * CHAR_SIZE
        
        # Get character bounding box to center it
        bbox = draw.textbbox((0, 0), char, font=font)
        char_width = bbox[2] - bbox[0]
        char_height = bbox[3] - bbox[1]
        
        # Center the character in its cell
        char_x = x + (CHAR_SIZE - char_width) // 2 - bbox[0]
        char_y = y + (CHAR_SIZE - char_height) // 2 - bbox[1]
        
        # Draw the character
        draw.text((char_x, char_y), char, font=font, fill=TEXT_COLOR)
        
        # Optional: Draw grid lines for debugging (comment out for final version)
        # draw.rectangle([x, y, x + CHAR_SIZE - 1, y + CHAR_SIZE - 1], outline=(100, 100, 100, 128), width=1)
    
    return atlas

def save_character_mapping_csv(output_path):
    """
    Save character mapping to CSV file
    
    Args:
        output_path: Path where to save the CSV file
    """
    with open(output_path, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.writer(csvfile)
        
        # Write header
        writer.writerow(['Character', 'Unicode', 'Index', 'Row', 'Col', 'X', 'Y'])
        
        # Write character data
        for idx, char in enumerate(CHARACTERS):
            row = idx // ATLAS_COLS
            col = idx % ATLAS_COLS
            x = col * CHAR_SIZE
            y = row * CHAR_SIZE
            
            # Format character for display (handle special characters)
            if char == '\n':
                char_display = '\\n'
            elif char == '\r':
                char_display = '\\r'
            elif char == '\t':
                char_display = '\\t'
            elif ord(char) < 32 or ord(char) == 127:
                char_display = f'\\x{ord(char):02x}'
            else:
                char_display = char
            
            unicode_hex = f"U+{ord(char):04X}"
            
            writer.writerow([char_display, unicode_hex, idx, row, col, x, y])

def main():
    print("Generating font texture atlas...")
    print(f"Character size: {CHAR_SIZE}x{CHAR_SIZE}")
    print(f"Atlas layout: {ATLAS_COLS} columns")
    print(f"Total characters: {len(CHARACTERS)}")
    
    # Use the uploaded Futura Condensed Medium font
    custom_font = '/mnt/user-data/uploads/Futura_Condensed_Medium.otf'
    
    # Create the atlas
    atlas = create_font_atlas(custom_font)
    
    # Save the atlas image
    output_image_path = '/mnt/user-data/outputs/font_texture_atlas.png'
    atlas.save(output_image_path, 'PNG')
    print(f"\nTexture atlas saved to: {output_image_path}")
    print(f"Atlas dimensions: {atlas.width}x{atlas.height}")
    
    # Save the character mapping CSV
    output_csv_path = '/mnt/user-data/outputs/font_character_mapping.csv'
    save_character_mapping_csv(output_csv_path)
    print(f"Character mapping saved to: {output_csv_path}")
    
    # Print sample character mapping for reference
    print("\nSample character mapping (first 10 characters):")
    print("Character | Unicode | Index | Row | Col | X   | Y")
    print("-" * 55)
    for idx in range(min(10, len(CHARACTERS))):
        char = CHARACTERS[idx]
        row = idx // ATLAS_COLS
        col = idx % ATLAS_COLS
        x = col * CHAR_SIZE
        y = row * CHAR_SIZE
        
        # Handle non-printable or whitespace characters
        char_display = repr(char) if char.strip() == '' or ord(char) < 32 else char
        print(f"{char_display:^9} | U+{ord(char):04X}  | {idx:5} | {row:3} | {col:3} | {x:3} | {y:3}")

if __name__ == "__main__":
    main()
