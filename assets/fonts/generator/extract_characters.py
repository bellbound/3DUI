#!/usr/bin/env python3
"""
Extract characters from CSV mapping file in Index order.
Outputs a single continuous string to characters.txt (UTF-8 encoded).
"""

import csv
import sys
from pathlib import Path

# Path to the CSV mapping file
CSV_PATH = Path(__file__).parent.parent / "fake_futura_condensed_medium_mapping.csv"
OUTPUT_PATH = Path(__file__).parent / "characters.txt"

def main():
    print(f"=== Character Extractor ===")
    print(f"Reading from: {CSV_PATH}")

    if not CSV_PATH.exists():
        print(f"ERROR: CSV file not found at {CSV_PATH}")
        sys.exit(1)

    # Read CSV and sort by Index
    characters = []

    with open(CSV_PATH, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            index = int(row['Index'])
            char = row['Character']
            characters.append((index, char))

    # Sort by index
    characters.sort(key=lambda x: x[0])

    # Extract just the characters in order
    char_string = ''.join(char for _, char in characters)

    # Write to output file
    with open(OUTPUT_PATH, 'w', encoding='utf-8') as f:
        f.write(char_string)

    print(f"Extracted {len(characters)} characters")
    print(f"Saved to: {OUTPUT_PATH}")
    print(f"Character string: {char_string[:50]}..." if len(char_string) > 50 else f"Character string: {char_string}")
    print("Done!")

if __name__ == "__main__":
    main()
