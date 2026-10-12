#!/usr/bin/env python3
"""Emit a local C unit containing only selected canonical SM64 behavior scripts.

The generated file is a build artifact: behavior bytecode is derived from the
user's local decomp checkout and is not tracked or distributed by this tool.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


DECL = re.compile(r"const\s+BehaviorScript\s+(bhv\w+)\s*\[\]\s*=\s*\{")


def extract_arrays(source: str) -> dict[str, str]:
    arrays: dict[str, str] = {}
    for match in DECL.finditer(source):
        open_brace = source.find("{", match.start())
        depth = 0
        in_string = False
        escaped = False
        end = None
        for i in range(open_brace, len(source)):
            ch = source[i]
            if in_string:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    in_string = False
                continue
            if ch == '"':
                in_string = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    end = source.find(";", i) + 1
                    break
        if end is None or end <= 0:
            raise ValueError(f"unterminated behavior array {match.group(1)}")
        arrays[match.group(1)] = source[match.start():end]
    return arrays


def reachable_scripts(arrays: dict[str, str], roots: list[str]) -> list[str]:
    missing = sorted(set(roots) - arrays.keys())
    if missing:
        raise ValueError("canonical behavior definitions not found: " + ", ".join(missing))
    todo = list(roots)
    selected: list[str] = []
    seen: set[str] = set()
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        selected.append(name)
        # Any bhv symbol referenced by a script macro and defined as a script
        # is a script-to-script edge (CALL/GOTO/spawn); function callbacks are
        # not arrays and are left for the native linker to resolve.
        for ref in re.findall(r"\b(bhv\w+)\b", arrays[name]):
            if ref in arrays and ref not in seen:
                todo.append(ref)
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("deps/n64decomp-sm64"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("behavior", nargs="+", help="canonical names such as bhvStar")
    args = parser.parse_args()

    source_path = args.source / "data/behavior_data.c"
    source = source_path.read_text()
    arrays = extract_arrays(source)
    names = reachable_scripts(arrays, args.behavior)

    macro_start = source.index("#define BC_B(")
    macro_end = source.index("const BehaviorScript", macro_start)
    macros = source[macro_start:macro_end]
    header = """/* Generated locally from the CC0 SM64 decompilation. Do not commit. */
#define OBJECT_FIELDS_INDEX_DIRECTLY
#include <ultra64.h>
#include "sm64.h"
#include "object_constants.h"
#include "object_fields.h"
#include "behavior_data.h"
#include "game/object_list_processor.h"
#include "game/behavior_actions.h"

"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header + macros + "\n".join(arrays[n] for n in names) + "\n")
    print(f"generated {len(names)} canonical scripts ({', '.join(names)}) -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
