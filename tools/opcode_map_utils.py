#!/usr/bin/env python3

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Dict, Set


RE_OPCODE_NAME = re.compile(r"^(?:CMSG|SMSG|MSG)_[A-Z0-9_]+$")


def load_opcode_map(path: Path, _seen: Set[Path] | None = None) -> Dict[str, str]:
    if _seen is None:
        _seen = set()

    path = path.resolve()
    if path in _seen:
        chain = " -> ".join(str(p) for p in list(_seen) + [path])
        raise ValueError(f"Opcode map inheritance cycle: {chain}")
    _seen.add(path)

    data = json.loads(path.read_text())
    merged: Dict[str, str] = {}

    extends = data.get("_extends")
    if isinstance(extends, str) and extends:
        merged.update(load_opcode_map(path.parent / extends, _seen))

    remove = data.get("_remove", [])
    if isinstance(remove, list):
        for name in remove:
            if isinstance(name, str):
                merged.pop(name, None)

    for key, value in data.items():
        if not isinstance(key, str) or not RE_OPCODE_NAME.match(key):
            continue
        if isinstance(value, str):
            merged[key] = value
        elif isinstance(value, int):
            merged[key] = str(value)

    _seen.remove(path)
    return merged
