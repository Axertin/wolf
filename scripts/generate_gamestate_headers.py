#!/usr/bin/env python3
"""
Generate C++ headers with unordered_map lookups from YAML game state definitions.

This script reads the YAML files from src/devtools/game-data/ and generates
corresponding C++ headers in include/okami/gamestate/ with const unordered_map
data structures for fast flag description lookups.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional

try:
    import yaml
except ImportError:
    print("Error: PyYAML is required. Install with: pip install PyYAML")
    sys.exit(1)


def normalize_map_name(map_name: str) -> str:
    """Normalize map name to match the registry's expectations."""
    return ''.join(c for c in map_name if c.isalnum())


def escape_string_literal(s: str) -> str:
    """Escape a string for use in C++ string literal."""
    # Replace backslashes first, then quotes
    s = s.replace('\\', '\\\\')
    s = s.replace('"', '\\"')
    return s


def generate_map_entries(data: Dict[int, str], compact: bool = False) -> str:
    """Generate C++ unordered_map entries from YAML data."""
    if not data:
        return "{}"
    
    sorted_items = sorted(data.items())
    
    # Simple format - let clang-format handle the styling
    entries = []
    for index, description in sorted_items:
        escaped_desc = escape_string_literal(description)
        entries.append(f'{{ {index}, "{escaped_desc}" }}')
    
    return "{ " + ", ".join(entries) + " }"


def format_file_with_clang_format(file_path: Path, project_root: Path) -> bool:
    """Format a file using clang-format with the project's configuration."""
    try:
        # Run clang-format in-place with the project's .clang-format file
        result = subprocess.run([
            "clang-format", 
            "-i",  # in-place
            "--style=file",  # use .clang-format file
            str(file_path)
        ], 
        cwd=project_root,  # Run from project root to find .clang-format
        capture_output=True, 
        text=True,
        timeout=30
        )
        
        if result.returncode != 0:
            print(f"Warning: clang-format failed for {file_path}: {result.stderr}")
            return False
        return True
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        print(f"Warning: Could not run clang-format on {file_path}: {e}")
        return False


def generate_global_header(global_config: Dict[str, Any], output_path: Path, project_root: Path) -> None:
    """Generate the global.hpp header file."""
    
    # Global categories from gamestateregistry.cpp parseGlobalYamlFile
    global_categories = [
        "brushUpgrades", "areasRestored", "commonStates", "gameProgress",
        "keyItemsFound", "goldDustsFound", "animalsFound", 
        "animalsFedFirstTime", "globalGameState"
    ]
    
    header_lines = [
        "#pragma once",
        "#include <unordered_map>",
        "",
        "// Auto-generated from src/devtools/game-data/global.yml",
        "// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py",
        "",
        "namespace okami::game_state::global",
        "{",
        ""
    ]
    
    # Generate map definitions
    for category in global_categories:
        if category in global_config and global_config[category]:
            entries = generate_map_entries(global_config[category])
            header_lines.extend([
                f"const std::unordered_map<unsigned, const char *> {category} ={entries};",
                ""
            ])
        else:
            header_lines.extend([
                f"const std::unordered_map<unsigned, const char *> {category} = {{}};",
                ""
            ])
    
    header_lines.extend([
        "} // namespace okami::game_state::global",
        ""
    ])
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(header_lines))
    
    # Format the file with clang-format
    format_file_with_clang_format(output_path, project_root)


def generate_map_header(map_name: str, map_config: Dict[str, Any], output_path: Path, project_root: Path) -> None:
    """Generate a map-specific header file."""
    
    # Map categories from gamestateregistry.cpp parseMapYamlFile
    map_categories = [
        "worldStateBits", "userIndices", "collectedObjects", "areasRestored",
        "treesBloomed", "cursedTreesBloomed", "fightsCleared", "npcs",
        "mapsExplored", "field_DC", "field_E0"
    ]
    
    normalized_name = normalize_map_name(map_name)
    
    header_lines = [
        "#pragma once",
        "#include <unordered_map>",
        "",
        f"// Auto-generated from src/devtools/game-data/maps/{map_name}.yml",
        "// Do not edit manually - regenerate with scripts/generate_gamestate_headers.py",
        "",
        f"namespace okami::game_state::maps::{normalized_name}",
        "{",
        ""
    ]
    
    # Generate map definitions
    for category in map_categories:
        if category in map_config and map_config[category]:
            entries = generate_map_entries(map_config[category])
            header_lines.extend([
                f"const std::unordered_map<unsigned, const char *> {category} ={entries};",
                ""
            ])
        else:
            header_lines.extend([
                f"const std::unordered_map<unsigned, const char *> {category} = {{}};",
                ""
            ])
    
    header_lines.extend([
        f"}} // namespace okami::game_state::maps::{normalized_name}",
        ""
    ])
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(header_lines))
    
    # Format the file with clang-format
    format_file_with_clang_format(output_path, project_root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, 
                       help='Source directory containing game-data (default: auto-detect from script location)')
    parser.add_argument('--output-dir', type=Path,
                       help='Output directory for generated headers (default: auto-detect from script location)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose output')
    
    args = parser.parse_args()
    
    # Auto-detect paths relative to script location
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    if args.source_dir:
        source_dir = args.source_dir
    else:
        source_dir = project_root / "src" / "devtools" / "game-data"
    
    if args.output_dir:
        output_dir = args.output_dir
    else:
        output_dir = project_root / "include" / "okami" / "gamestate"
    
    if not source_dir.exists():
        print(f"Error: Source directory not found: {source_dir}")
        sys.exit(1)
    
    global_yml = source_dir / "global.yml"
    maps_dir = source_dir / "maps"
    
    if not global_yml.exists():
        print(f"Error: global.yml not found at: {global_yml}")
        sys.exit(1)
    
    if not maps_dir.exists():
        print(f"Error: maps directory not found at: {maps_dir}")
        sys.exit(1)
    
    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)
    maps_output_dir = output_dir / "maps"
    maps_output_dir.mkdir(parents=True, exist_ok=True)
    
    if args.verbose:
        print(f"Source directory: {source_dir}")
        print(f"Output directory: {output_dir}")
    
    # Process global.yml
    if args.verbose:
        print(f"Processing {global_yml}")
    
    try:
        with open(global_yml, 'r', encoding='utf-8') as f:
            global_config = yaml.safe_load(f) or {}
        
        global_header_path = output_dir / "global.hpp"
        generate_global_header(global_config, global_header_path, project_root)
        
        if args.verbose:
            print(f"Generated {global_header_path}")
    
    except Exception as e:
        print(f"Error processing global.yml: {e}")
        sys.exit(1)
    
    # Process map files
    map_files = list(maps_dir.glob("*.yml"))
    if not map_files:
        print(f"Warning: No .yml files found in {maps_dir}")
    
    for map_file in sorted(map_files):
        if args.verbose:
            print(f"Processing {map_file}")
        
        try:
            with open(map_file, 'r', encoding='utf-8') as f:
                map_config = yaml.safe_load(f) or {}
            
            map_name = map_file.stem  # filename without extension
            output_path = maps_output_dir / f"{map_name}.hpp"
            
            generate_map_header(map_name, map_config, output_path, project_root)
            
            if args.verbose:
                print(f"Generated {output_path}")
        
        except Exception as e:
            print(f"Error processing {map_file}: {e}")
            continue
    
    # Generate main gamestate.hpp header that includes everything
    gamestate_header = output_dir / "gamestate.hpp"
    
    header_lines = [
        "#pragma once",
        "",
        "// Auto-generated by scripts/generate_gamestate_headers.py",
        "// Do not edit manually",
        "",
        "// Game state flag descriptions for Okami HD",
        "",
        '#include "global.hpp"',
        ""
    ]
    
    # Include all map headers
    for map_file in sorted(map_files):
        map_name = map_file.stem
        header_lines.append(f'#include "maps/{map_name}.hpp"')
    
    with open(gamestate_header, 'w', encoding='utf-8') as f:
        f.write('\n'.join(header_lines) + '\n')
    
    # Format the main header with clang-format
    format_file_with_clang_format(gamestate_header, project_root)
    
    if args.verbose:
        print(f"Generated main header: {gamestate_header}")
    
    print(f"Successfully generated {len(map_files) + 1} headers in {output_dir}")


if __name__ == "__main__":
    main()