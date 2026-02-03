#!/usr/bin/env python3
"""
Extend available slots for 3DUI.esp by generating new Projectile and Ammo YAML files.

Current layout:
- Projectiles: 0x80A - 0x835 (44 records)
- Ammo:        0x87C - 0x8A7 (44 records, each references corresponding Projectile)

New slots will be added after 0x8A7, maintaining the same offset pattern.
"""

import argparse
import os
from pathlib import Path

# Current form ID ranges
CURRENT_PROJECTILE_START = 0x80A
CURRENT_PROJECTILE_END = 0x835
CURRENT_AMMO_START = 0x87C
CURRENT_AMMO_END = 0x8A7

CURRENT_COUNT = 44
PLUGIN_NAME = "3DUI.esp"

# Templates based on existing files
PROJECTILE_TEMPLATE = """FormKey: {form_key}:{plugin}
Model:
  File: 3DUI\\empty.nif
  Data: 0x020000000000000000000000
Flags:
- PassThroughSmallTransparent
- DisableCombatAimCorrection
- 0x800
Type: Arrow
Speed: 10
Range: 99999
RelaunchInterval: 0.25
CollisionLayer: 000804:{plugin}
MuzzleFlashModel: ''
SoundLevel: 2
"""

AMMO_TEMPLATE = """FormKey: {form_key}:{plugin}
Model:
  File: Magic\\orbunequip.nif
  Data: 0x020000000000000000000000
Description:
  TargetLanguage: English
  Value: ''
Keywords:
- 0917E7:Skyrim.esm
Projectile: {projectile_key}:{plugin}
Flags:
- NonBolt
Weight: 0
"""


def format_form_id(form_id: int) -> str:
    """Format form ID as 6-digit hex string."""
    return f"{form_id:06X}"


def generate_projectile(form_id: int, output_dir: Path) -> None:
    """Generate a Projectile YAML file."""
    form_key = format_form_id(form_id)
    filename = f"{form_key}_{PLUGIN_NAME}.yaml"
    content = PROJECTILE_TEMPLATE.format(form_key=form_key, plugin=PLUGIN_NAME)

    filepath = output_dir / filename
    filepath.write_text(content)
    print(f"  Created: {filepath.name}")


def generate_ammo(form_id: int, projectile_id: int, output_dir: Path) -> None:
    """Generate an Ammo YAML file that references a Projectile."""
    form_key = format_form_id(form_id)
    projectile_key = format_form_id(projectile_id)
    filename = f"{form_key}_{PLUGIN_NAME}.yaml"
    content = AMMO_TEMPLATE.format(
        form_key=form_key,
        projectile_key=projectile_key,
        plugin=PLUGIN_NAME
    )

    filepath = output_dir / filename
    filepath.write_text(content)
    print(f"  Created: {filepath.name} -> Projectile {projectile_key}")


def generate_formids_h_snippet(new_count: int, proj_start: int, ammo_start: int) -> str:
    """Generate C++ code snippet for FormIDs.h."""

    proj_ids = [f"0x{proj_start + i:03X}" for i in range(new_count)]
    ammo_ids = [f"0x{ammo_start + i:03X}" for i in range(new_count)]

    def format_id_list(ids: list[str], per_row: int = 8) -> str:
        lines = []
        for i in range(0, len(ids), per_row):
            chunk = ids[i:i+per_row]
            lines.append("        " + ", ".join(chunk) + ",")
        # Remove trailing comma from last line
        if lines:
            lines[-1] = lines[-1].rstrip(",")
        return "\n".join(lines)

    return f"""
    // === NEW EXTENDED SLOTS ===
    // Projectile form IDs: 0x{proj_start:03X} - 0x{proj_start + new_count - 1:03X} ({new_count} projectiles)
    inline const std::vector<uint32_t> ExtendedProjectileFormIDs = {{
{format_id_list(proj_ids)}
    }};

    // Ammo form IDs: 0x{ammo_start:03X} - 0x{ammo_start + new_count - 1:03X} ({new_count} ammo records)
    inline const std::vector<uint32_t> ExtendedAmmoFormIDs = {{
{format_id_list(ammo_ids)}
    }};
"""


def main():
    parser = argparse.ArgumentParser(
        description="Extend 3DUI.esp slots by generating new YAML files"
    )
    parser.add_argument(
        "count",
        type=int,
        help="Number of new slots to add"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be created without actually creating files"
    )
    parser.add_argument(
        "--start-after",
        type=lambda x: int(x, 0),
        default=CURRENT_AMMO_END,
        help=f"Start new form IDs after this value (default: 0x{CURRENT_AMMO_END:03X})"
    )

    args = parser.parse_args()

    # Calculate new form ID ranges
    # New projectiles start after current ammo end
    new_proj_start = args.start_after + 1
    new_proj_end = new_proj_start + args.count - 1

    # New ammo starts after new projectiles
    new_ammo_start = new_proj_end + 1
    new_ammo_end = new_ammo_start + args.count - 1

    print(f"Extending 3DUI.esp with {args.count} new slots")
    print(f"\nNew form ID ranges:")
    print(f"  Projectiles: 0x{new_proj_start:03X} - 0x{new_proj_end:03X}")
    print(f"  Ammo:        0x{new_ammo_start:03X} - 0x{new_ammo_end:03X}")
    print(f"\nTotal new form IDs: {args.count * 2}")
    print(f"Highest form ID: 0x{new_ammo_end:03X}")

    # Check ESL limit (0xFFF)
    if new_ammo_end > 0xFFF:
        print(f"\nERROR: Form ID 0x{new_ammo_end:03X} exceeds ESL limit (0xFFF)!")
        return 1

    if args.dry_run:
        print("\n[DRY RUN] No files will be created.")
        print("\nC++ FormIDs.h snippet:")
        print(generate_formids_h_snippet(args.count, new_proj_start, new_ammo_start))
        return 0

    # Get script directory as base
    script_dir = Path(__file__).parent
    proj_dir = script_dir / "Projectiles"
    ammo_dir = script_dir / "Ammunitions"

    # Verify directories exist
    for d in [proj_dir, ammo_dir]:
        if not d.exists():
            print(f"ERROR: Directory not found: {d}")
            return 1

    print("\nGenerating Projectiles...")
    for i in range(args.count):
        generate_projectile(new_proj_start + i, proj_dir)

    print("\nGenerating Ammunitions...")
    for i in range(args.count):
        generate_ammo(new_ammo_start + i, new_proj_start + i, ammo_dir)

    print("\n" + "=" * 60)
    print("C++ FormIDs.h snippet to add:")
    print("=" * 60)
    print(generate_formids_h_snippet(args.count, new_proj_start, new_ammo_start))

    print("\nDone! Remember to:")
    print("1. Add the above snippet to FormIDs.h")
    print("2. Rebuild the ESP with Spriggit")
    print("3. Update any code that uses slot counts")

    return 0


if __name__ == "__main__":
    exit(main())
