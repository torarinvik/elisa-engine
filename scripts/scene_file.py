"""Validate and persist canonical, native-handle-free prefab scene files.

The Elisa prefab modules own runtime identity and world mutation. This file
owns the durable interchange shape: stable authoring IDs, bounded definitions
and links, plain transforms, and override records. Durable replacement is
delegated to the same fsynced journal used by world saves.
"""

from __future__ import annotations

import argparse
import copy
import math
from pathlib import Path
import tempfile

from save_journal import SaveError, load, save

CURRENT_VERSION = 1
MAX_DEFINITIONS = 8
MAX_NODES = 64
MAX_LINKS = 8
MAX_OVERRIDES = 64
TRANSFORM_VALUES = 10


class SceneError(ValueError):
    """A scene file cannot be safely validated or migrated."""


def _fail(message: str) -> None:
    raise SceneError(message)


def _positive(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        _fail(f"{label} must be a positive integer")
    return value


def _transform(value: object, label: str) -> list[float | int]:
    if not isinstance(value, list) or len(value) != TRANSFORM_VALUES:
        _fail(f"{label} must contain exactly {TRANSFORM_VALUES} values")
    if any(isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item) for item in value):
        _fail(f"{label} contains a non-finite value")
    return value


def _reject_native(value: object, path: str = "scene") -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"native", "native_handle", "runtime_handle", "entity_ref", "world_epoch"}:
                _fail(f"{path}.{key} is runtime-only")
            _reject_native(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _reject_native(child, f"{path}[{index}]")


def _definition_map(document: dict) -> dict[int, dict]:
    definitions = document.get("definitions")
    if not isinstance(definitions, list) or not 0 < len(definitions) <= MAX_DEFINITIONS:
        _fail(f"definitions must contain 1..{MAX_DEFINITIONS} entries")
    result: dict[int, dict] = {}
    for definition in definitions:
        if not isinstance(definition, dict):
            _fail("definition must be an object")
        prefab_id = _positive(definition.get("prefab_id"), "prefab_id")
        if prefab_id in result:
            _fail("duplicate prefab_id")
        nodes = definition.get("nodes")
        if not isinstance(nodes, list) or not 0 < len(nodes) <= MAX_NODES:
            _fail(f"nodes must contain 1..{MAX_NODES} entries")
        ids: set[int] = set()
        rows: dict[int, dict] = {}
        for node in nodes:
            if not isinstance(node, dict):
                _fail("node must be an object")
            authoring_id = _positive(node.get("authoring_id"), "authoring_id")
            if authoring_id in ids:
                _fail("duplicate authoring_id")
            parent_id = node.get("parent_id")
            if isinstance(parent_id, bool) or not isinstance(parent_id, int) or parent_id < 0:
                _fail("parent_id must be zero or a positive integer")
            _transform(node.get("local"), f"node {authoring_id}.local")
            ids.add(authoring_id)
            rows[authoring_id] = node
        for authoring_id, node in rows.items():
            parent_id = node["parent_id"]
            if parent_id != 0 and parent_id not in ids:
                _fail(f"node {authoring_id} references a missing parent")
            cursor = parent_id
            seen: set[int] = set()
            while cursor != 0:
                if cursor in seen or cursor == authoring_id:
                    _fail(f"node {authoring_id} participates in a parent cycle")
                seen.add(cursor)
                cursor = rows[cursor]["parent_id"]
        result[prefab_id] = definition
    return result


def validate(document: dict) -> dict:
    if not isinstance(document, dict):
        _fail("scene document must be an object")
    _reject_native(document)
    if document.get("version") != CURRENT_VERSION:
        _fail(f"unsupported scene version: {document.get('version')!r}")
    definitions = _definition_map(document)
    links = document.get("links")
    if not isinstance(links, list) or not 0 < len(links) <= MAX_LINKS:
        _fail(f"links must contain 1..{MAX_LINKS} entries")
    link_ids: set[int] = set()
    instance_ids: set[int] = set()
    link_map: dict[int, dict] = {}
    for index, link in enumerate(links):
        if not isinstance(link, dict):
            _fail("link must be an object")
        link_id = _positive(link.get("link_id"), "link_id")
        instance_id = _positive(link.get("instance_id"), "instance_id")
        prefab_id = _positive(link.get("prefab_id"), "prefab_id")
        if link_id in link_ids or instance_id in instance_ids:
            _fail("link_id and instance_id must be unique")
        if prefab_id not in definitions:
            _fail(f"link {link_id} references a missing prefab")
        definition_ids = {node["authoring_id"] for node in definitions[prefab_id]["nodes"]}
        root_id = _positive(link.get("root_authoring_id"), "root_authoring_id")
        if root_id not in definition_ids:
            _fail(f"link {link_id} references a missing root authoring ID")
        parent_link_id = link.get("parent_link_id")
        if isinstance(parent_link_id, bool) or not isinstance(parent_link_id, int) or parent_link_id < -1:
            _fail("parent_link_id must be -1 or a positive integer")
        parent_authoring_id = link.get("parent_authoring_id")
        if parent_link_id == -1:
            if parent_authoring_id != 0:
                _fail(f"root link {link_id} must use parent_authoring_id zero")
        else:
            if parent_link_id not in link_map:
                _fail(f"link {link_id} must follow its parent link")
            parent_prefab = link_map[parent_link_id]["prefab_id"]
            parent_ids = {node["authoring_id"] for node in definitions[parent_prefab]["nodes"]}
            if parent_authoring_id not in parent_ids:
                _fail(f"link {link_id} references a missing parent authoring ID")
            cursor = parent_link_id
            seen: set[int] = set()
            while cursor != -1:
                if cursor in seen or cursor == link_id:
                    _fail(f"link {link_id} participates in a parent cycle")
                seen.add(cursor)
                cursor = link_map[cursor]["parent_link_id"]
        _transform(link.get("local"), f"link {link_id}.local")
        link_ids.add(link_id)
        instance_ids.add(instance_id)
        link_map[link_id] = link
    overrides = document.get("overrides", [])
    if not isinstance(overrides, list) or len(overrides) > MAX_LINKS:
        _fail(f"overrides must contain at most {MAX_LINKS} instances")
    seen_instances: set[int] = set()
    for override in overrides:
        if not isinstance(override, dict):
            _fail("override instance must be an object")
        instance_id = _positive(override.get("instance_id"), "override instance_id")
        prefab_id = _positive(override.get("prefab_id"), "override prefab_id")
        if instance_id in seen_instances or instance_id not in instance_ids:
            _fail("override instance_id is unknown or duplicated")
        if prefab_id not in definitions:
            _fail("override prefab_id is unknown")
        valid_ids = {node["authoring_id"] for node in definitions[prefab_id]["nodes"]}
        records = override.get("records")
        if not isinstance(records, list) or len(records) > MAX_OVERRIDES:
            _fail(f"override records must contain at most {MAX_OVERRIDES} entries")
        record_ids: set[int] = set()
        for record in records:
            if not isinstance(record, dict):
                _fail("override record must be an object")
            authoring_id = _positive(record.get("authoring_id"), "override authoring_id")
            if authoring_id in record_ids or authoring_id not in valid_ids:
                _fail("override authoring_id is unknown or duplicated")
            _transform(record.get("local"), f"override {authoring_id}.local")
            record_ids.add(authoring_id)
        seen_instances.add(instance_id)
    return document


def migrate(document: dict) -> dict:
    if not isinstance(document, dict):
        _fail("scene document must be an object")
    version = document.get("version", 0)
    if version == 0:
        migrated = copy.deepcopy(document)
        migrated["version"] = CURRENT_VERSION
        migrated.setdefault("overrides", [])
        return migrated
    if version != CURRENT_VERSION:
        _fail(f"unsupported scene version: {version!r}")
    return copy.deepcopy(document)


def save_scene(path: Path, document: dict) -> str:
    prepared = migrate(document)
    validate(prepared)
    return save(path, prepared)


def load_scene(path: Path) -> dict:
    try:
        return validate(migrate(load(path)))
    except SaveError as failure:
        raise SceneError(str(failure)) from failure


def _sample() -> dict:
    identity = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0]
    moved = [1.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0]
    return {
        "version": 1,
        "definitions": [{"prefab_id": 7, "nodes": [
            {"authoring_id": 10, "parent_id": 0, "local": identity},
            {"authoring_id": 20, "parent_id": 10, "local": moved},
        ]}],
        "links": [{"link_id": 100, "prefab_id": 7, "instance_id": 1000,
                   "root_authoring_id": 10, "parent_link_id": -1,
                   "parent_authoring_id": 0, "local": identity}],
        "overrides": [{"prefab_id": 7, "instance_id": 1000,
                       "records": [{"authoring_id": 20, "local": moved}]}],
    }


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="elisa-scene-") as directory:
        target = Path(directory) / "scene.save"
        document = _sample()
        digest = save_scene(target, document)
        assert len(digest) == 64 and load_scene(target) == document
        old = copy.deepcopy(document)
        old.pop("overrides")
        old["version"] = 0
        assert migrate(old)["version"] == CURRENT_VERSION
        cycle = copy.deepcopy(document)
        cycle["definitions"][0]["nodes"][0]["parent_id"] = 20
        try:
            validate(cycle)
        except SceneError as failure:
            assert "cycle" in str(failure)
        else:
            raise AssertionError("scene parent cycle was accepted")
        native = copy.deepcopy(document)
        native["links"][0]["native_handle"] = 4
        try:
            validate(native)
        except SceneError as failure:
            assert "runtime-only" in str(failure)
        else:
            raise AssertionError("native handle crossed scene boundary")
        invalid_float = copy.deepcopy(document)
        invalid_float["links"][0]["local"][0] = float("nan")
        try:
            validate(invalid_float)
        except SceneError as failure:
            assert "non-finite" in str(failure)
        else:
            raise AssertionError("non-finite transform was accepted")
        print("scene file: canonical save, migration, topology, override, and native-boundary checks passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("--self-test is required")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
