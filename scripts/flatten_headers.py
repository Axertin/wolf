#!/usr/bin/env python3
"""
Wolf API Header Flattening Script

This script processes Wolf API headers and combines them into a single distributable header file.
It properly handles file comments, includes, and dependency ordering.
"""

import os
import sys
import re
import argparse
from pathlib import Path
from typing import List, Dict, Set, Optional


class HeaderProcessor:
    """Processes individual header files, removing unwanted content."""
    
    def __init__(self):
        self.file_comment_pattern = re.compile(
            r'/\*\*\s*\n\s*\*\s*@file[^*]*(\*(?!/)[^*]*)*\*/', 
            re.MULTILINE | re.DOTALL
        )
        
    def process_header(self, file_path: Path, version_content: str = None) -> str: # pyright: ignore[reportArgumentType]
        """Process a single header file, removing includes and file comments."""
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Remove file header comment blocks (/** @file ... */)
        content = self.file_comment_pattern.sub('', content)
        
        # If this is wolf_core.hpp and we have version content, replace the framework version section
        if file_path.name == 'wolf_core.hpp' and version_content:
            content = self.replace_framework_version_section(content, version_content)
        
        lines = content.split('\n')
        processed_lines = []
        skip_empty_at_start = True
        
        for line in lines:
            # Skip #pragma once
            if re.match(r'^\s*#pragma\s+once', line):
                continue
                
            # Skip #include statements for wolf_ headers only, keep system includes
            if re.match(r'^\s*#include\s+["\<]wolf_', line):
                continue
            
            # Skip leading empty lines, but preserve them once content starts
            stripped = line.strip()
            if skip_empty_at_start and not stripped:
                continue
            elif stripped:
                skip_empty_at_start = False
                
            processed_lines.append(line)
        
        return '\n'.join(processed_lines)
    
    def replace_framework_version_section(self, content: str, version_content: str) -> str:
        """Replace content between // FRAMEWORK VERSION and // END FRAMEWORK VERSION tags."""
        pattern = r'(// FRAMEWORK VERSION\n).*?\n(// END FRAMEWORK VERSION)'
        replacement = f'{version_content.strip()}\n'
        return re.sub(pattern, replacement, content, flags=re.DOTALL)


class DependencyResolver:
    """Resolves header dependencies to determine proper inclusion order."""
    
    def __init__(self, api_dir: Path):
        self.api_dir = api_dir
        self.wolf_include_pattern = re.compile(r'#include\s+["\<](wolf_\w+\.hpp)["\>]')
    
    def extract_dependencies(self, file_path: Path) -> List[str]:
        """Extract wolf_ header dependencies from a file."""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except UnicodeDecodeError:
            with open(file_path, 'r', encoding='latin-1') as f:
                content = f.read()
                
        dependencies = []
        for match in self.wolf_include_pattern.finditer(content):
            dependencies.append(match.group(1))
        
        return dependencies
    
    def build_dependency_order(self, headers: List[str], priority_order: List[str] = None) -> List[str]: # pyright: ignore[reportArgumentType]
        """Build dependency-aware ordering of headers."""
        if priority_order is None:
            priority_order = ['wolf_core.hpp']
            
        # Build dependency graph
        deps = {}
        for header in headers:
            header_path = self.api_dir / header
            if header_path.exists():
                deps[header] = self.extract_dependencies(header_path)
        
        ordered = []
        remaining = set(headers)
        
        # Process headers in dependency order
        while remaining:
            added_this_round = []
            
            for header in list(remaining):
                # Check if all dependencies are already added
                header_deps = deps.get(header, [])
                deps_satisfied = all(dep in ordered or dep not in remaining for dep in header_deps)
                
                if deps_satisfied:
                    added_this_round.append(header)
            
            if not added_this_round:
                # Circular dependency - add remaining in priority/alphabetical order
                print(f"Warning: Possible circular dependency. Adding remaining headers in fallback order.", 
                      file=sys.stderr)
                
                # Add priority headers first
                for priority_header in priority_order:
                    if priority_header in remaining:
                        added_this_round.append(priority_header)
                
                # Add remaining alphabetically
                remaining_sorted = sorted(remaining - set(added_this_round))
                added_this_round.extend(remaining_sorted)
            
            # Sort this round by priority, then alphabetical
            priority_this_round = [h for h in priority_order if h in added_this_round]
            others_this_round = sorted([h for h in added_this_round if h not in priority_order])
            
            ordered.extend(priority_this_round)
            ordered.extend(others_this_round)
            
            for added in added_this_round:
                remaining.discard(added)
        
        return ordered


class APIFlattener:
    """Main class for flattening Wolf API headers."""
    
    def __init__(self, api_dir: Path, version_file: Optional[Path] = None):
        self.api_dir = api_dir
        self.version_file = version_file
        self.processor = HeaderProcessor()
        self.resolver = DependencyResolver(api_dir)
    
    def discover_headers(self) -> List[str]:
        """Discover all wolf_*.hpp files in the API directory."""
        pattern = "wolf_*.hpp"
        headers = []
        
        for file_path in self.api_dir.glob(pattern):
            if file_path.is_file():
                headers.append(file_path.name)
        
        return sorted(headers)
    
    def generate_file_header(self, build_type: str = "Unknown", compiler: str = "Unknown") -> str:
        """Generate the header comment for the flattened file."""
        return f'''/**
 * @file wolf_framework.hpp  
 * @brief WOLF Okami Loader Framework - Complete Single-Header Distribution
 * 
 * This file contains the complete Wolf API flattened into a single header 
 * for easy distribution and integration with mod projects.
 * 
 * Generated automatically - do not edit directly.
 * Edit the source templates in src/api/ instead.
 * 
 * Build: {build_type} ({compiler})
 * 
 * MIT License
 * 
 * Copyright (c) 2025 WOLF Contributors
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

'''
    
    def read_version_content(self) -> str:
        """Read and process the version header content (for framework version section replacement)."""
        if not self.version_file or not self.version_file.exists():
            return '''// Auto-generated version information - file not found
#define WOLF_VERSION_MAJOR 0
#define WOLF_VERSION_MINOR 1
#define WOLF_VERSION_PATCH 0
#define WOLF_VERSION_STRING "0.1.0-Missing"

// Semantic version as single integer for comparisons
#define WOLF_VERSION_INT ((0 << 16) | (1 << 8) | 0)

// Build information
#define WOLF_BUILD_TYPE "Missing"
#define WOLF_COMPILER "Missing"'''
        
        with open(self.version_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Remove #pragma once and comments from version header
        content = re.sub(r'#pragma\s+once[^\r\n]*[\r\n]+', '', content)
        content = re.sub(r'^//.*$', '', content, flags=re.MULTILINE)
        content = content.strip()
        
        return content
    
    def flatten(self, output_file: Path, build_type: str = "Unknown", compiler: str = "Unknown") -> bool:
        """Flatten all API headers into a single file."""
        print(f"Flattening Wolf API into single header: {output_file}")
        
        # Discover and order headers
        headers = self.discover_headers()
        if not headers:
            print(f"Error: No wolf_*.hpp files found in {self.api_dir}", file=sys.stderr)
            return False
        
        ordered_headers = self.resolver.build_dependency_order(headers)
        
        # Read version content for framework version section replacement
        version_content = self.read_version_content()
        
        # Start building content
        content = []
        content.append(self.generate_file_header(build_type, compiler))
        
        # Process each header
        for header in ordered_headers:
            header_path = self.api_dir / header
            print(f"  Processing {header}")
            
            processed_content = self.processor.process_header(header_path, version_content)
            content.append(f"\n{processed_content}\n")
        
        # Write output (only if changed)
        final_content = ''.join(content)
        
        # Check if file needs updating
        needs_update = True
        if output_file.exists():
            try:
                with open(output_file, 'r', encoding='utf-8') as f:
                    existing_content = f.read()
                needs_update = existing_content != final_content
            except (UnicodeDecodeError, IOError):
                needs_update = True
        
        if needs_update:
            # Ensure output directory exists
            output_file.parent.mkdir(parents=True, exist_ok=True)
            
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(final_content)
            print(f"  Updated: {output_file}")
        else:
            print(f"  No change: {output_file}")
        
        return True


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='Flatten Wolf API headers into a single distributable header')
    parser.add_argument('--api-dir', required=True, help='Directory containing wolf_*.hpp files')
    parser.add_argument('--output', required=True, help='Output file path')
    parser.add_argument('--version-header', help='Path to wolf_version.h file')
    parser.add_argument('--build-type', default='Unknown', help='Build type for header comment')
    parser.add_argument('--compiler', default='Unknown', help='Compiler info for header comment')
    
    args = parser.parse_args()
    
    api_dir = Path(args.api_dir)
    output_file = Path(args.output)
    version_file = Path(args.version_header) if args.version_header else None
    
    if not api_dir.exists() or not api_dir.is_dir():
        print(f"Error: API directory does not exist: {api_dir}", file=sys.stderr)
        return 1
    
    flattener = APIFlattener(api_dir, version_file)
    success = flattener.flatten(output_file, args.build_type, args.compiler)
    
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
