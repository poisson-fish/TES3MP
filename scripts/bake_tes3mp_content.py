#!/usr/bin/env python3
"""Bake a deterministic, manifest-addressed TES3MP content pack.

The baker treats the resolved OpenMW content files, canonical server catalogs
or a bounded derived-pack recipe, and client record mappings as one identity.
It validates mapped and selected records against the winning TES3 loadout
before publishing an immutable pack and atomically advancing CURRENT.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import pathlib
import shutil
import struct
import sys
import tempfile
from dataclasses import dataclass
from typing import Iterable, Sequence


FORMAT = "TES3MP_CONTENT_PACK_V2"
HASH_DOMAIN = b"TES3MP_CONTENT_PACK_V2\0"
MANIFEST_PLACEHOLDER = "<tes3mp-content-manifest>"
MAX_CONFIG_BYTES = 2 * 1024 * 1024
MAX_CATALOG_BYTES = 8 * 1024 * 1024
MAX_CONTENT_FILES = 256
MAX_DATA_DIRECTORIES = 256
MAX_RECORD_BYTES = 256 * 1024 * 1024
MAX_MASTER_NAME_BYTES = 1024
MAX_DERIVED_RECIPE_BYTES = 256 * 1024
MAX_SCRIPT_MODULE_BYTES = 64 * 1024
DERIVED_RECIPE_FORMAT = "TES3MP_DERIVED_VANILLA_V1"
DERIVED_CATALOG_KEYS = {
    "collision_content_file",
    "actor_content_file",
    "inventory_content_file",
    "combat_content_file",
}

CATALOGS = {
    "collision_content_file": ("TES3MP_COLLISION_V1", True),
    "actor_content_file": ("TES3MP_ACTORS_V1", True),
    "interactive_object_content_file": ("TES3MP_INTERACTIVE_OBJECTS_V2", False),
    "inventory_content_file": ("TES3MP_INVENTORY_V2", False),
    "combat_content_file": ("TES3MP_COMBAT_V10", False),
    "character_content_file": ("TES3MP_CHARACTERS_V2", False),
    "world_content_file": ("TES3MP_WORLD_V4", True),
    "script_package_file": ("TES3MP_SCRIPT_PACKAGES_V2", False),
}

SERVER_CONTENT_KEYS = (
    "cell_spaces",
    "allowed_cells",
    "spawn_cell",
    "spawn_positions",
    "default_appearance_id",
    "movement_profile",
    *CATALOGS.keys(),
)

CLIENT_SINGLE_KEYS = {"tes3mp-content-appearance-record"}
CLIENT_MAPPING_KEYS = {
    "tes3mp-content-cell-space-map",
    "tes3mp-content-actor-prototype-map",
    "tes3mp-content-interactive-object-map",
    "tes3mp-content-item-prototype-map",
    "tes3mp-content-container-map",
    "tes3mp-content-quest-map",
    "tes3mp-content-dialogue-choice-map",
    "tes3mp-content-weather-region-map",
    "tes3mp-content-weather-map",
    "tes3mp-content-spell-map",
}

ITEM_RECORD_TYPES = {
    "ALCH", "APPA", "ARMO", "BOOK", "CLOT", "INGR",
    "LIGH", "LOCK", "MISC", "PROB", "REPA", "WEAP",
}


class BakeError(RuntimeError):
    pass


@dataclass(frozen=True)
class Assignment:
    key: str
    value: str
    line: int


@dataclass(frozen=True)
class LoadoutFile:
    name: str
    path: pathlib.Path
    size: int
    sha256: str
    masters: tuple[str, ...]


@dataclass(frozen=True)
class Catalog:
    key: str
    path: pathlib.Path
    output_name: str
    normalized: bytes
    records: tuple[tuple[str, ...], ...]


@dataclass(frozen=True)
class ScriptModuleArtifact:
    name: str
    path: pathlib.Path
    data: bytes
    sha256: str


@dataclass(frozen=True)
class Tes3Record:
    kind: str
    name: str
    deleted: bool
    subrecords: tuple[tuple[str, bytes], ...]


@dataclass(frozen=True)
class DerivedActor:
    actor_id: int
    entity_id: int
    record: str
    cell: str
    position: tuple[int, int, int]
    orientation: tuple[int, int, int]


@dataclass(frozen=True)
class DerivedSolid:
    cell: str
    minimum: tuple[int, int, int]
    maximum: tuple[int, int, int]


@dataclass(frozen=True)
class DerivedStartingItem:
    record: str
    count: int
    slot: int | None


@dataclass(frozen=True)
class DerivedRecipe:
    path: pathlib.Path
    seed: int
    player_record: str
    items: tuple[str, ...]
    actors: tuple[DerivedActor, ...]
    solids: tuple[DerivedSolid, ...]
    starting_items: tuple[DerivedStartingItem, ...]


def _read_bounded(path: pathlib.Path, limit: int, description: str) -> bytes:
    try:
        if not path.is_file():
            raise BakeError(f"{description} is unavailable: {path}")
        size = path.stat().st_size
        if size > limit:
            raise BakeError(f"{description} exceeds {limit} bytes: {path}")
        return path.read_bytes()
    except OSError as exc:
        raise BakeError(f"could not read {description}: {path}: {exc}") from exc


def _decode_text(data: bytes, description: str) -> str:
    if b"\0" in data:
        raise BakeError(f"{description} contains a NUL byte")
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise BakeError(f"{description} is not UTF-8") from exc


def _unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return value[1:-1]
    return value


def _assignments(text: str, description: str) -> list[Assignment]:
    result: list[Assignment] = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise BakeError(f"{description}:{number}: expected key=value")
        key, value = line.split("=", 1)
        key = key.strip()
        value = _unquote(value)
        if not key or not value:
            raise BakeError(f"{description}:{number}: empty key or value")
        result.append(Assignment(key, value, number))
    return result


def _one(values: Sequence[Assignment], key: str, required: bool = True) -> str | None:
    found = [entry.value for entry in values if entry.key == key]
    if len(found) > 1:
        raise BakeError(f"duplicate {key} assignment")
    if required and not found:
        raise BakeError(f"missing {key} assignment")
    return found[0] if found else None


def _resolve_path(value: str, base: pathlib.Path, description: str) -> pathlib.Path:
    if value.startswith("?") or "${" in value:
        raise BakeError(f"unresolved path token in {description}: {value}")
    path = pathlib.Path(value)
    if not path.is_absolute():
        path = base / path
    try:
        return path.resolve(strict=False)
    except OSError as exc:
        raise BakeError(f"invalid path in {description}: {value}: {exc}") from exc


def _find_case_insensitive(directory: pathlib.Path, name: str) -> pathlib.Path | None:
    exact = directory / name
    if exact.is_file():
        return exact
    try:
        matches = [entry for entry in directory.iterdir() if entry.is_file() and entry.name.casefold() == name.casefold()]
    except OSError:
        return None
    if len(matches) > 1:
        raise BakeError(f"ambiguous case-insensitive content file {name} in {directory}")
    return matches[0] if matches else None


def _sha256_file(path: pathlib.Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                digest.update(chunk)
    except OSError as exc:
        raise BakeError(f"could not hash content file {path}: {exc}") from exc
    return size, digest.hexdigest()


def _tes3_masters(path: pathlib.Path) -> tuple[str, ...]:
    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            header = stream.read(16)
            if len(header) != 16:
                raise BakeError(f"truncated TES3 header in {path}")
            raw_type, payload_size, _unknown, _flags = struct.unpack("<4sIII", header)
            if raw_type != b"TES3":
                raise BakeError(f"unsupported non-TES3 content file in v1 baker: {path.name}")
            if payload_size > MAX_RECORD_BYTES or payload_size > size - len(header):
                raise BakeError(f"invalid TES3 header size in {path}")
            payload = stream.read(payload_size)
            if len(payload) != payload_size:
                raise BakeError(f"truncated TES3 header in {path}")

        masters: list[str] = []
        seen: set[str] = set()
        offset = 0
        while offset < len(payload):
            if len(payload) - offset < 8:
                raise BakeError(f"truncated TES3 header subrecord in {path}")
            raw_subtype, subsize = struct.unpack_from("<4sI", payload, offset)
            offset += 8
            if subsize > len(payload) - offset:
                raise BakeError(f"invalid TES3 header subrecord size in {path}")
            value = payload[offset:offset + subsize]
            offset += subsize
            if raw_subtype != b"MAST":
                continue
            raw_name = value.split(b"\0", 1)[0]
            if not raw_name or len(raw_name) > MAX_MASTER_NAME_BYTES:
                raise BakeError(f"invalid TES3 master name in {path}")
            name = raw_name.decode("cp1252")
            if pathlib.PureWindowsPath(name).name != name or "/" in name or "\\" in name:
                raise BakeError(f"TES3 master must be a filename in {path}: {name}")
            normalized = name.casefold()
            if normalized in seen:
                raise BakeError(f"duplicate TES3 master in {path}: {name}")
            seen.add(normalized)
            masters.append(name)
        return tuple(masters)
    except (OSError, UnicodeDecodeError, struct.error) as exc:
        if isinstance(exc, BakeError):
            raise
        raise BakeError(f"could not inspect TES3 masters in {path}: {exc}") from exc


def _validate_master_order(loadout: Sequence[LoadoutFile]) -> None:
    positions = {entry.name.casefold(): index for index, entry in enumerate(loadout)}
    for index, entry in enumerate(loadout):
        for master in entry.masters:
            master_index = positions.get(master.casefold())
            if master_index is None:
                raise BakeError(f"missing TES3 master: {entry.name} requires {master}")
            if master_index >= index:
                raise BakeError(f"TES3 master is not ordered before dependent: {entry.name} requires {master}")


def resolve_loadout(config_paths: Sequence[pathlib.Path]) -> list[LoadoutFile]:
    if not config_paths:
        raise BakeError("at least one OpenMW config is required")
    data_directories: list[pathlib.Path] = []
    local_directories: list[pathlib.Path] = []
    content_names: list[str] = []
    for config_path in config_paths:
        path = config_path.resolve(strict=False)
        text = _decode_text(_read_bounded(path, MAX_CONFIG_BYTES, "OpenMW config"), str(path))
        entries = _assignments(text, str(path))
        for entry in entries:
            if entry.key == "data":
                data_directories.append(_resolve_path(entry.value, path.parent, "data"))
            elif entry.key == "data-local":
                local_directories.append(_resolve_path(entry.value, path.parent, "data-local"))
            elif entry.key == "content":
                if pathlib.Path(entry.value).name != entry.value or "/" in entry.value or "\\" in entry.value:
                    raise BakeError(f"content must be a filename, not a path: {entry.value}")
                content_names.append(entry.value)
    data_directories.extend(local_directories)
    if not data_directories or len(data_directories) > MAX_DATA_DIRECTORIES:
        raise BakeError("OpenMW loadout has no data directory or exceeds the directory bound")
    if not content_names or len(content_names) > MAX_CONTENT_FILES:
        raise BakeError("OpenMW loadout has no content file or exceeds the file bound")
    if len({name.casefold() for name in content_names}) != len(content_names):
        raise BakeError("OpenMW loadout contains a duplicate content filename")

    result: list[LoadoutFile] = []
    for name in content_names:
        resolved = next(
            (candidate for directory in reversed(data_directories)
             if (candidate := _find_case_insensitive(directory, name)) is not None),
            None,
        )
        if resolved is None:
            raise BakeError(f"content file is unavailable in configured data directories: {name}")
        size, digest = _sha256_file(resolved)
        resolved = resolved.resolve()
        result.append(LoadoutFile(name, resolved, size, digest, _tes3_masters(resolved)))
    _validate_master_order(result)
    return result


def _catalog_lines(data: bytes, expected_header: str, description: str) -> tuple[bytes, tuple[tuple[str, ...], ...]]:
    text = _decode_text(data, description)
    lines = text.splitlines()
    if not lines or lines[0] != expected_header:
        raise BakeError(f"{description}: expected {expected_header} header")
    manifest_count = 0
    normalized: list[str] = []
    records: list[tuple[str, ...]] = []
    for number, raw in enumerate(lines, 1):
        line = raw.rstrip()
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            fields = tuple(stripped.split())
            if fields[0] == "manifest":
                if len(fields) != 2:
                    raise BakeError(f"{description}:{number}: malformed manifest record")
                manifest_count += 1
                line = f"manifest {MANIFEST_PLACEHOLDER}"
            elif number != 1:
                records.append(fields)
        normalized.append(line)
    if manifest_count != 1:
        raise BakeError(f"{description}: expected exactly one manifest record")
    return ("\n".join(normalized) + "\n").encode("utf-8"), tuple(records)


def load_catalogs(server_config: pathlib.Path, entries: Sequence[Assignment],
    omitted: set[str] | None = None) -> list[Catalog]:
    result: list[Catalog] = []
    output_names: set[str] = set()
    omitted = omitted or set()
    for key, (header, required) in CATALOGS.items():
        if key in omitted:
            continue
        value = _one(entries, key, required)
        if value is None:
            continue
        path = _resolve_path(value, server_config.parent, key)
        output_name = path.name
        if not output_name or output_name.casefold() in output_names:
            raise BakeError(f"catalog output filename is empty or duplicated: {output_name}")
        output_names.add(output_name.casefold())
        data = _read_bounded(path, MAX_CATALOG_BYTES, key)
        normalized, records = _catalog_lines(data, header, str(path))
        result.append(Catalog(key, path, output_name, normalized, records))
    return result


def stable_record_id(value: str) -> int:
    try:
        encoded = value.lower().encode("ascii")
    except UnicodeEncodeError as exc:
        raise BakeError(f"record ID is not ASCII and cannot have a canonical v1 identity: {value}") from exc
    result = 14695981039346656037
    for byte in encoded:
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def _optional_stable_record_id(value: str) -> int | None:
    try:
        return stable_record_id(value)
    except BakeError:
        # TES3 permits legacy-encoded IDs. They cannot be named by the current
        # ASCII canonical-ID contract, but unrelated records must not prevent a
        # bounded pack from being baked.
        return None


def _tes3_record_data(path: pathlib.Path) -> Iterable[Tes3Record]:
    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            offset = 0
            while offset < size:
                header = stream.read(16)
                if len(header) != 16:
                    raise BakeError(f"truncated TES3 record header in {path}")
                raw_type, payload_size, _unknown, _flags = struct.unpack("<4sIII", header)
                offset += 16
                if payload_size > MAX_RECORD_BYTES or payload_size > size - offset:
                    raise BakeError(f"invalid TES3 record size in {path}")
                payload = stream.read(payload_size)
                if len(payload) != payload_size:
                    raise BakeError(f"truncated TES3 record in {path}")
                offset += payload_size
                record_type = raw_type.decode("ascii", "strict")
                name: str | None = None
                deleted = False
                subrecords: list[tuple[str, bytes]] = []
                suboffset = 0
                while suboffset < len(payload):
                    if len(payload) - suboffset < 8:
                        raise BakeError(f"truncated TES3 subrecord header in {path}")
                    raw_subtype, subsize = struct.unpack_from("<4sI", payload, suboffset)
                    suboffset += 8
                    if subsize > len(payload) - suboffset:
                        raise BakeError(f"invalid TES3 subrecord size in {path}")
                    value = payload[suboffset:suboffset + subsize]
                    suboffset += subsize
                    subtype = raw_subtype.decode("ascii", "strict")
                    subrecords.append((subtype, value))
                    if subtype == "NAME" and name is None:
                        raw_name = value.split(b"\0", 1)[0]
                        name = raw_name.decode("cp1252").casefold()
                    elif subtype == "DELE":
                        deleted = True
                if name is None and record_type in {"SKIL", "MGEF"}:
                    indexes = [value for subtype, value in subrecords if subtype == "INDX"]
                    if len(indexes) == 1 and len(indexes[0]) == 4:
                        name = str(struct.unpack("<i", indexes[0])[0])
                if name:
                    yield Tes3Record(record_type, name, deleted, tuple(subrecords))
    except (OSError, UnicodeDecodeError, struct.error) as exc:
        if isinstance(exc, BakeError):
            raise
        raise BakeError(f"could not inspect TES3 records in {path}: {exc}") from exc


def _tes3_records(path: pathlib.Path) -> Iterable[tuple[str, str, bool]]:
    for value in _tes3_record_data(path):
        yield value.kind, value.name, value.deleted


def winning_tes3_record_data(loadout: Sequence[LoadoutFile]) -> dict[tuple[str, str], Tes3Record]:
    records: dict[tuple[str, str], Tes3Record] = {}
    for entry in loadout:
        with entry.path.open("rb") as stream:
            magic = stream.read(4)
        if magic != b"TES3":
            raise BakeError(f"unsupported non-TES3 content file in v1 baker: {entry.name}")
        for record_value in _tes3_record_data(entry.path):
            records[(record_value.kind, record_value.name)] = record_value
    return records


def winning_tes3_records(loadout: Sequence[LoadoutFile]) -> dict[tuple[str, str], bool]:
    return {key: not value.deleted for key, value in winning_tes3_record_data(loadout).items()}


def verify_loadout_unchanged(loadout: Sequence[LoadoutFile]) -> None:
    for entry in loadout:
        size, digest = _sha256_file(entry.path)
        if size != entry.size or digest != entry.sha256:
            raise BakeError(f"content file changed while it was being baked: {entry.name}")


def _json_integer(value: object, description: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum or value > maximum:
        raise BakeError(f"derived recipe has invalid {description}")
    return value


def _json_vector(value: object, description: str, minimum: int, maximum: int) -> tuple[int, int, int]:
    if not isinstance(value, list) or len(value) != 3:
        raise BakeError(f"derived recipe has invalid {description}")
    return tuple(_json_integer(item, description, minimum, maximum) for item in value)  # type: ignore[return-value]


def load_derived_recipe(path: pathlib.Path) -> DerivedRecipe:
    path = path.resolve(strict=False)
    try:
        value = json.loads(_decode_text(_read_bounded(path, MAX_DERIVED_RECIPE_BYTES,
            "derived pack recipe"), str(path)))
    except json.JSONDecodeError as exc:
        raise BakeError(f"invalid derived pack recipe: {exc}") from exc
    expected = {"format", "seed", "player_record", "items", "actors", "collision_solids", "starting_items"}
    if not isinstance(value, dict) or set(value) != expected or value.get("format") != DERIVED_RECIPE_FORMAT:
        raise BakeError("derived pack recipe has an unsupported shape or format")
    seed = _json_integer(value["seed"], "seed", 0, 0xFFFFFFFFFFFFFFFF)
    player = value["player_record"]
    if not isinstance(player, str) or not player or len(player.encode("utf-8")) > 255:
        raise BakeError("derived pack recipe has an invalid player record")
    raw_items = value["items"]
    if not isinstance(raw_items, list) or not raw_items or len(raw_items) > 65536:
        raise BakeError("derived pack recipe has an invalid item set")
    items: list[str] = []
    for item in raw_items:
        if not isinstance(item, str) or not item or len(item.encode("utf-8")) > 255:
            raise BakeError("derived pack recipe has an invalid item record")
        stable_record_id(item)
        items.append(item)
    if len({item.casefold() for item in items}) != len(items):
        raise BakeError("derived pack recipe has duplicate item records")

    raw_actors = value["actors"]
    if not isinstance(raw_actors, list) or not raw_actors or len(raw_actors) > 4096:
        raise BakeError("derived pack recipe has an invalid actor set")
    actors: list[DerivedActor] = []
    for raw in raw_actors:
        keys = {"actor_id", "entity_id", "record", "cell", "position", "orientation"}
        if not isinstance(raw, dict) or set(raw) != keys:
            raise BakeError("derived pack recipe has an invalid actor declaration")
        record_name = raw["record"]
        cell = raw["cell"]
        if not isinstance(record_name, str) or not record_name or not isinstance(cell, str) or not cell:
            raise BakeError("derived pack recipe has an invalid actor record or cell")
        stable_record_id(record_name)
        actors.append(DerivedActor(
            _json_integer(raw["actor_id"], "actor identity", 1, 0xFFFFFFFFFFFFFFFF),
            _json_integer(raw["entity_id"], "actor entity identity", 1, 0xFFFFFFFFFFFFFFFF),
            record_name, cell,
            _json_vector(raw["position"], "actor position", -2147483647, 2147483647),
            _json_vector(raw["orientation"], "actor orientation", 0, 0xFFFFFFFF)))
    if len({actor.actor_id for actor in actors}) != len(actors) \
            or len({actor.entity_id for actor in actors}) != len(actors):
        raise BakeError("derived pack recipe has duplicate actor or entity identities")

    raw_solids = value["collision_solids"]
    if not isinstance(raw_solids, list) or len(raw_solids) > 32768:
        raise BakeError("derived pack recipe has an invalid collision solid set")
    solids: list[DerivedSolid] = []
    for raw in raw_solids:
        if not isinstance(raw, dict) or set(raw) != {"cell", "minimum", "maximum"} \
                or not isinstance(raw["cell"], str) or not raw["cell"]:
            raise BakeError("derived pack recipe has an invalid collision solid")
        minimum = _json_vector(raw["minimum"], "collision minimum", -2147483647, 2147483647)
        maximum = _json_vector(raw["maximum"], "collision maximum", -2147483647, 2147483647)
        if any(low >= high for low, high in zip(minimum, maximum)):
            raise BakeError("derived pack recipe collision minimum is not below its maximum")
        solids.append(DerivedSolid(raw["cell"], minimum, maximum))
    if len(set(solids)) != len(solids):
        raise BakeError("derived pack recipe has duplicate collision solids")

    raw_starting = value["starting_items"]
    if not isinstance(raw_starting, list) or len(raw_starting) > 64:
        raise BakeError("derived pack recipe has an invalid starting item set")
    starting: list[DerivedStartingItem] = []
    for raw in raw_starting:
        if not isinstance(raw, dict) or set(raw) != {"record", "count", "slot"} \
                or not isinstance(raw["record"], str) or not raw["record"]:
            raise BakeError("derived pack recipe has an invalid starting item")
        slot = raw["slot"]
        if slot is not None:
            slot = _json_integer(slot, "starting item slot", 0, 18)
        starting.append(DerivedStartingItem(raw["record"],
            _json_integer(raw["count"], "starting item count", 1, 0xFFFFFFFF), slot))
    if any(item.record.casefold() not in {name.casefold() for name in items} for item in starting):
        raise BakeError("derived pack recipe starting item is absent from its item set")
    return DerivedRecipe(path, seed, player, tuple(items), tuple(actors), tuple(solids), tuple(starting))


def _subrecord(record_value: Tes3Record, name: str, required: bool = True) -> bytes | None:
    values = [value for kind, value in record_value.subrecords if kind == name]
    if len(values) > 1 or (required and not values):
        raise BakeError(f"{record_value.kind} {record_value.name} has missing or duplicate {name} data")
    return values[0] if values else None


def _winning_record(records: dict[tuple[str, str], Tes3Record], name: str,
    kinds: set[str], description: str) -> Tes3Record:
    folded = name.casefold()
    candidates = [record_value for (kind, record_name), record_value in records.items()
                  if record_name == folded and kind in kinds]
    if len(candidates) > 1:
        raise BakeError(f"ambiguous {description} record: {name}")
    if not candidates:
        if any(record_name == folded and not record_value.deleted
               for (_kind, record_name), record_value in records.items()):
            raise BakeError(f"unsupported {description} record type: {name}")
        raise BakeError(f"missing {description} record: {name}")
    if candidates[0].deleted:
        raise BakeError(f"deleted winning {description} record: {name}")
    return candidates[0]


def _unpack(record_value: Tes3Record, subrecord_name: str, format_string: str) -> tuple[object, ...]:
    payload = _subrecord(record_value, subrecord_name)
    assert payload is not None
    if len(payload) != struct.calcsize(format_string):
        raise BakeError(f"unsupported {record_value.kind} {record_value.name} {subrecord_name} layout")
    try:
        return struct.unpack(format_string, payload)
    except struct.error as exc:
        raise BakeError(f"malformed {record_value.kind} {record_value.name} {subrecord_name} data") from exc


def _float_text(value: float) -> str:
    if not math.isfinite(value):
        raise BakeError("derived record contains a non-finite numeric value")
    return format(value, ".9g")


def _weight_units(weight: float, record_name: str) -> int:
    if not math.isfinite(weight) or weight < 0:
        raise BakeError(f"item {record_name} has an invalid weight")
    result = round(weight * 10.0)
    if result < 0 or result > 0xFFFFFFFF:
        raise BakeError(f"item {record_name} weight exceeds the canonical bound")
    return result


def _item_values(record_value: Tes3Record) -> tuple[int, int, int, int, int, bool, int | None, float]:
    """Return category, weight units, value, max condition, slot mask, stacking, key, and tool quality."""
    kind = record_value.kind
    max_condition = 0
    slot_mask = 0
    stackable = True
    key_id: int | None = None
    tool_quality = 0.0
    if kind == "WEAP":
        values = _unpack(record_value, "WPDT", "<fihHffH2B2B2Bi")
        weight, value, weapon_type, health = float(values[0]), int(values[1]), int(values[2]), int(values[3])
        if weapon_type < 0 or weapon_type > 8:
            raise BakeError(f"unsupported non-melee weapon record: {record_value.name}")
        max_condition, slot_mask, stackable = health, 1 << 16, False
        category = 11
    elif kind == "ALCH":
        weight, value, flags = _unpack(record_value, "ALDT", "<fii")
        if int(flags) & 1:
            raise BakeError(f"unsupported autocalculated potion record: {record_value.name}")
        category = 0
    elif kind == "APPA":
        _type, _quality, weight, value = _unpack(record_value, "AADT", "<iffi")
        category = 1
    elif kind == "ARMO":
        armor_type, weight, value, health, _enchant, _armor = _unpack(record_value, "AODT", "<ifiiii")
        armor_slots = {0: 0, 1: 1, 2: 3, 3: 4, 4: 2, 5: 7, 6: 5, 7: 6, 8: 17, 9: 5, 10: 6}
        if int(armor_type) not in armor_slots:
            raise BakeError(f"unsupported armor type: {record_value.name}")
        category, max_condition, slot_mask, stackable = 2, int(health), 1 << armor_slots[int(armor_type)], False
    elif kind == "BOOK":
        weight, value, _scroll, _skill, _enchant = _unpack(record_value, "BKDT", "<fiiii")
        category = 3
    elif kind == "CLOT":
        clothing_type, weight, value, _enchant = _unpack(record_value, "CTDT", "<ifHH")
        clothing_slots = {0: 9, 1: 7, 2: 8, 3: 15, 4: 11, 5: 6, 6: 5, 7: 10, 9: 14}
        if int(clothing_type) == 8:
            slot_mask = (1 << 12) | (1 << 13)
        elif int(clothing_type) in clothing_slots:
            slot_mask = 1 << clothing_slots[int(clothing_type)]
        else:
            raise BakeError(f"unsupported clothing type: {record_value.name}")
        category, stackable = 4, False
    elif kind == "INGR":
        payload = _subrecord(record_value, "IRDT")
        assert payload is not None
        if len(payload) != 56:
            raise BakeError(f"unsupported ingredient layout: {record_value.name}")
        weight, value = struct.unpack_from("<fi", payload)
        category = 5
    elif kind == "LIGH":
        weight, value, _time, _radius, _color, flags = _unpack(record_value, "LHDT", "<fiiiIi")
        category = 6
        if int(flags) & 2:
            slot_mask, stackable = 1 << 17, False
    elif kind in {"LOCK", "PROB"}:
        weight, value, quality, uses = _unpack(record_value, "LKDT" if kind == "LOCK" else "PBDT", "<fifi")
        category, max_condition, stackable, tool_quality = (7 if kind == "LOCK" else 9), int(uses), False, float(quality)
    elif kind == "MISC":
        weight, value, flags = _unpack(record_value, "MCDT", "<fii")
        category = 8
        if int(flags) & 1:
            key_id = stable_record_id(record_value.name)
    elif kind == "REPA":
        weight, value, uses, _quality = _unpack(record_value, "RIDT", "<fiif")
        category, max_condition = 10, int(uses)
    else:
        raise BakeError(f"unsupported item record type: {record_value.kind}")
    numeric_value = int(value)
    if numeric_value < 0 or numeric_value > 0xFFFFFFFF or max_condition < 0 or max_condition > 0xFFFFFFFF \
            or not math.isfinite(tool_quality) or tool_quality < 0:
        raise BakeError(f"item {record_value.name} has an out-of-range value or condition")
    return category, _weight_units(float(weight), record_value.name), numeric_value, max_condition, slot_mask, stackable, key_id, tool_quality


def _weapon_values(record_value: Tes3Record) -> tuple[int, tuple[float, ...], bool]:
    values = _unpack(record_value, "WPDT", "<fihHffH2B2B2Bi")
    weight, weapon_type, reach, flags = float(values[0]), int(values[2]), float(values[5]), int(values[13])
    skills = {0: 0, 1: 1, 2: 1, 3: 2, 4: 2, 5: 2, 6: 4, 7: 3, 8: 3}
    if weapon_type not in skills or not math.isfinite(reach) or reach <= 0:
        raise BakeError(f"unsupported melee weapon data: {record_value.name}")
    damages = tuple(float(item) for item in values[7:13])
    return skills[weapon_type], (*damages, weight, reach), (flags & 3) == 0


def _npc_values(record_value: Tes3Record, description: str) -> tuple[int, tuple[int, ...], tuple[int, ...], int, int, int]:
    values = _unpack(record_value, "NPDT", "<h8B27Bx3H3Bxi")
    level = int(values[0])
    attributes = tuple(int(value) for value in values[1:9])
    skills = tuple(int(value) for value in values[9:36])
    health, magicka, fatigue = int(values[36]), int(values[37]), int(values[38])
    if level < 1 or health < 1 or magicka < 0 or fatigue < 0:
        raise BakeError(f"{description} has unsupported NPC stats: {record_value.name}")
    return level, attributes, skills, health, magicka, fatigue


COMBAT_GMSTS = (
    "fCombatInvisoMult", "fFatigueAttackBase", "fFatigueAttackMult", "fWeaponFatigueMult",
    "fWeaponDamageMult", "fDamageStrengthBase", "fDamageStrengthMult", "fMinHandToHandMult",
    "fMaxHandToHandMult", "fHandtoHandHealthPer", "fCombatCriticalStrikeMult", "fCombatKODamageMult",
    "fFatigueBase", "fFatigueMult", "fFatigueReturnBase", "fFatigueReturnMult", "fEndFatigueMult",
    "fDifficultyMult", "fCombatBlockLeftAngle", "fCombatBlockRightAngle", "fSwingBlockMult",
    "fSwingBlockBase", "fBlockStillBonus", "iBlockMinChance", "iBlockMaxChance",
    "fFatigueBlockBase", "fFatigueBlockMult", "fWeaponFatigueBlockMult",
    "iBaseArmorSkill", "fUnarmoredBase1", "fUnarmoredBase2", "fCombatArmorMinMult",
)

PROGRESSION_GMSTS = (
    "fMiscSkillBonus", "fMinorSkillBonus", "fMajorSkillBonus", "fSpecialSkillBonus",
)

PROGRESSION_SKILL_INDEXES = (0, 20, 5, 4, 6, 7, 26, 21, 2, 3, 17, 18, 11, 13, 10, 12, 14, 15, 9)

ARMOR_WEIGHT_GMSTS = {
    0: "iHelmWeight", 1: "iCuirassWeight", 2: "iPauldronWeight", 3: "iPauldronWeight",
    4: "iGreavesWeight", 5: "iBootsWeight", 6: "iGauntletWeight", 7: "iGauntletWeight",
    8: "iShieldWeight", 9: "iGauntletWeight", 10: "iGauntletWeight",
}

DIRECT_EFFECT_KINDS = {
    14: "fire", 15: "shock", 16: "frost", 23: "health", 24: "magicka",
    25: "fatigue", 27: "poison", 75: "restore_health", 76: "restore_magicka",
    77: "restore_fatigue", 57: "dispel",
}

MAGIC_SCHOOLS = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5}

PASSIVE_EFFECT_FIELDS = {
    4: 8, 5: 9, 6: 10, 90: 2, 91: 4, 92: 3, 94: 6, 95: 7, 97: 5,
}


def _record_reference(payload: bytes, description: str) -> str:
    raw = payload.split(b"\0", 1)[0]
    if not raw or len(raw) > 255:
        raise BakeError(f"invalid {description} reference")
    try:
        return raw.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise BakeError(f"invalid {description} reference") from exc


def _effect_values(record_value: Tes3Record) -> tuple[tuple[int, int, int, int, int, int], ...]:
    result = []
    for kind, payload in record_value.subrecords:
        if kind != "ENAM":
            continue
        if len(result) >= 8:
            raise BakeError(f"invalid direct magic effect count: {record_value.name}")
        if len(payload) != struct.calcsize("<hbbiiiii"):
            raise BakeError(f"unsupported magic effect layout: {record_value.name}")
        effect, _skill, _attribute, effect_range, area, duration, minimum, maximum = \
            struct.unpack("<hbbiiiii", payload)
        if effect_range < 0 or effect_range > 2 or area < 0 or area > 256 \
                or duration < 0 or duration > 3600 or minimum < 0 or maximum < minimum:
            raise BakeError(f"unsupported direct magic effect: {record_value.name}")
        result.append((effect, effect_range, duration, minimum, maximum, area))
    if not result or len(result) > 8:
        raise BakeError(f"invalid direct magic effect count: {record_value.name}")
    return tuple(result)


def _direct_effect_tokens(record_value: Tes3Record, force_other: bool = False,
                          force_instant: bool = False) -> tuple[str, ...]:
    result = []
    for effect, effect_range, duration, minimum, maximum, area in _effect_values(record_value):
        effect_kind = DIRECT_EFFECT_KINDS.get(effect)
        if effect_kind is None:
            raise BakeError(f"unsupported direct magic effect: {record_value.name}")
        target = "other" if force_other or effect_range != 0 else "self"
        duration_ticks = 0 if force_instant else duration * 63
        area_quanta = 0 if force_instant else area * 1024
        result.extend((target, effect_kind, str(minimum), str(maximum), str(duration_ticks),
                       str(area_quanta), "refresh"))
    return tuple(result)


def _passive_magic_defense(record_value: Tes3Record) -> tuple[float, ...]:
    result = [0.0] * 11
    for effect, effect_range, _duration, minimum, maximum, _area in _effect_values(record_value):
        field = PASSIVE_EFFECT_FIELDS.get(effect)
        if field is None or effect_range != 0 or minimum != maximum:
            raise BakeError(f"unsupported passive magic effect: {record_value.name}")
        result[field] += float(minimum)
    return tuple(result)


def _item_magic(record_value: Tes3Record, records: dict[tuple[str, str], Tes3Record]) \
        -> tuple[int, tuple[str, ...] | None, tuple[float, ...] | None]:
    if record_value.kind not in {"ARMO", "BOOK", "CLOT", "WEAP"}:
        if any(kind == "ENAM" for kind, _payload in record_value.subrecords):
            raise BakeError(f"unsupported direct-use item magic: {record_value.name}")
        return 0, None, None
    reference = _subrecord(record_value, "ENAM", False)
    if reference is None:
        return 0, None, None
    enchantment = _winning_record(records, _record_reference(reference, "enchantment"), {"ENCH"}, "enchantment")
    enchant_type, cost, charge, _flags = (int(value) for value in _unpack(enchantment, "ENDT", "<4i"))
    if enchant_type == 1:
        if record_value.kind != "WEAP":
            raise BakeError(f"unsupported on-strike item type: {record_value.name}")
        if cost <= 0 or charge <= 0 or cost > charge:
            raise BakeError(f"invalid on-strike enchantment charge: {enchantment.name}")
        return charge, ("strike", str(cost), *_instant_direct_effect_tokens(enchantment, records)), None
    if enchant_type == 2:
        if cost <= 0 or charge <= 0 or cost > charge:
            raise BakeError(f"invalid when-used enchantment charge: {enchantment.name}")
        return charge, ("use", str(cost), *_timed_area_effect_tokens(enchantment, records)), None
    if enchant_type == 3:
        return 0, None, _passive_magic_defense(enchantment)
    raise BakeError(f"unsupported item enchantment type: {enchantment.name}")


def _magic_effect_data(records: dict[tuple[str, str], Tes3Record], effect: int) -> tuple[int, float, int]:
    record_value = _winning_record(records, str(effect), {"MGEF"}, "magic effect")
    school, base_cost, flags, _red, _green, _blue, _unknown1, _speed, _unknown2 = \
        _unpack(record_value, "MEDT", "<if4i3f")
    if int(school) not in MAGIC_SCHOOLS or not math.isfinite(float(base_cost)) or float(base_cost) < 0:
        raise BakeError(f"unsupported magic effect metadata: {effect}")
    return MAGIC_SCHOOLS[int(school)], float(base_cost), int(flags)


def _instant_direct_effect_tokens(record_value: Tes3Record,
        records: dict[tuple[str, str], Tes3Record]) -> tuple[str, ...]:
    del records
    for _effect, effect_range, _duration, _minimum, _maximum, area in _effect_values(record_value):
        if effect_range == 2:
            raise BakeError(f"projectile magic is deferred: {record_value.name}")
    return _direct_effect_tokens(record_value, force_instant=True)


def _timed_area_effect_tokens(record_value: Tes3Record,
        records: dict[tuple[str, str], Tes3Record]) -> tuple[str, ...]:
    del records
    for _effect, effect_range, _duration, _minimum, _maximum, _area in _effect_values(record_value):
        if effect_range == 2:
            raise BakeError(f"projectile magic is deferred: {record_value.name}")
    return _direct_effect_tokens(record_value)


def _spell_profile(record_value: Tes3Record, records: dict[tuple[str, str], Tes3Record],
                   player_skills: tuple[int, ...]) -> tuple[str, ...]:
    spell_type, cost, flags = (int(value) for value in _unpack(record_value, "SPDT", "<3i"))
    if spell_type not in {0, 5} or cost < 0:
        raise BakeError(f"unsupported castable spell: {record_value.name}")
    candidates = []
    for effect, effect_range, duration, minimum, maximum, area in _effect_values(record_value):
        if effect_range == 2:
            raise BakeError(f"projectile spell is deferred: {record_value.name}")
        school, base_cost, effect_flags = _magic_effect_data(records, effect)
        adjusted_duration = float(duration) if effect_flags & 0x1000 else max(1.0, float(duration))
        difficulty = adjusted_duration * 0.1 * base_cost * 0.5 * float(minimum + maximum)
        difficulty *= _gmst_value(records, "fEffectCostMult")
        candidates.append((2.0 * float(player_skills[PROGRESSION_SKILL_INDEXES[12 + school]]) - difficulty,
                           school, difficulty))
    _score, school, difficulty = min(candidates)
    effects = _direct_effect_tokens(record_value)
    return (str(school), str(cost), _float_text(difficulty), "1" if (flags & 1) else "0",
            str(len(effects) // 7), *effects)


def _actor_magic(record_value: Tes3Record, records: dict[tuple[str, str], Tes3Record],
        willpower: float, destruction: float) -> tuple[tuple[float, ...], tuple[tuple[int, str, tuple[str, ...]], ...]]:
    defense = [willpower, destruction, *([0.0] * 9)]
    diseases = []
    for kind, payload in record_value.subrecords:
        if kind != "NPCS":
            continue
        spell = _winning_record(records, _record_reference(payload, "actor spell"), {"SPEL"}, "actor spell")
        spell_type, _cost, _flags = (int(value) for value in _unpack(spell, "SPDT", "<3i"))
        if spell_type in {0, 5}:
            continue
        if spell_type == 1:
            for index, value in enumerate(_passive_magic_defense(spell)):
                defense[index] += value
        elif spell_type in {2, 3}:
            if len(diseases) >= 16:
                raise BakeError(f"invalid actor disease set: {record_value.name}")
            diseases.append((stable_record_id(spell.name), "blight" if spell_type == 2 else "common",
                             _direct_effect_tokens(spell, True, True)))
        else:
            raise BakeError(f"unsupported initial actor spell type: {spell.name}")
    if len(diseases) > 16 or len({value[0] for value in diseases}) != len(diseases):
        raise BakeError(f"invalid actor disease set: {record_value.name}")
    if any(not math.isfinite(value) or value < -1000 or value > 1000 for value in defense):
        raise BakeError(f"actor magic defense is out of range: {record_value.name}")
    return tuple(defense), tuple(sorted(diseases))


def _trap_magic_profiles(records: dict[tuple[str, str], Tes3Record],
                         trap_ids: set[int]) -> tuple[tuple[int, tuple[str, ...]], ...]:
    profiles = []
    for trap_id in sorted(trap_ids):
        candidates = [record_value for (kind, name), record_value in records.items()
                      if kind == "SPEL" and not record_value.deleted
                      and _optional_stable_record_id(name) == trap_id]
        if len(candidates) != 1:
            raise BakeError(f"interactive trap identity does not name one live spell: {trap_id}")
        profiles.append((trap_id, _direct_effect_tokens(candidates[0], True, True)))
    return tuple(profiles)


def _skill_progression_rule(records: dict[tuple[str, str], Tes3Record], index: int,
                            use_index: int = 0) -> tuple[int, float]:
    record_value = _winning_record(records, str(index), {"SKIL"}, "skill")
    attribute, specialization, *use_values = _unpack(record_value, "SKDT", "<ii4f")
    if int(attribute) < 0 or int(attribute) >= 8 or int(specialization) < 0 or int(specialization) > 2:
        raise BakeError(f"skill {index} has unsupported progression metadata")
    gain = float(use_values[use_index])
    if not math.isfinite(gain) or gain < 0:
        raise BakeError(f"skill {index} has invalid use gain")
    return int(specialization), gain


def _gmst_value(records: dict[tuple[str, str], Tes3Record], name: str) -> float:
    record_value = _winning_record(records, name, {"GMST"}, "combat setting")
    values = [(kind, value) for kind, value in record_value.subrecords if kind in {"FLTV", "INTV"}]
    if len(values) != 1 or len(values[0][1]) != 4:
        raise BakeError(f"unsupported combat setting value: {name}")
    result = float(struct.unpack("<f" if values[0][0] == "FLTV" else "<i", values[0][1])[0])
    if not math.isfinite(result):
        raise BakeError(f"combat setting is non-finite: {name}")
    return result


def _cell_tokens(value: str) -> tuple[str, ...]:
    fields = value.split(":")
    try:
        if len(fields) == 2 and fields[0] == "interior" and int(fields[1]) > 0:
            return tuple(fields)
        if len(fields) == 4 and fields[0] == "exterior" and int(fields[1]) > 0:
            int(fields[2]); int(fields[3])
            return tuple(fields)
    except ValueError:
        pass
    raise BakeError(f"invalid derived recipe cell: {value}")


def _inside_box(position: tuple[int, int, int], solid: DerivedSolid) -> bool:
    return all(low < value < high for value, low, high in zip(position, solid.minimum, solid.maximum))


def _catalog(key: str, path: pathlib.Path, output_name: str, lines: Sequence[str]) -> Catalog:
    normalized, records = _catalog_lines(("\n".join(lines) + "\n").encode("utf-8"), CATALOGS[key][0], str(path))
    return Catalog(key, path, output_name, normalized, records)


def derive_catalogs(recipe: DerivedRecipe, server_entries: Sequence[Assignment],
    client_entries: Sequence[Assignment], records: dict[tuple[str, str], Tes3Record],
    retained_catalogs: Sequence[Catalog]) -> tuple[list[Catalog], list[Assignment]]:
    allowed_raw = _one(server_entries, "allowed_cells")
    assert allowed_raw is not None
    allowed_cells = tuple(part for part in allowed_raw.split(";") if part)
    if not allowed_cells or len(set(allowed_cells)) != len(allowed_cells):
        raise BakeError("invalid or duplicate allowed_cells in server config")
    allowed = set(allowed_cells)
    for cell in allowed_cells:
        _cell_tokens(cell)
    if any(actor.cell not in allowed for actor in recipe.actors) or any(solid.cell not in allowed for solid in recipe.solids):
        raise BakeError("derived actor or collision solid references an undeclared cell")
    if any(_inside_box(actor.position, solid) for actor in recipe.actors for solid in recipe.solids
           if actor.cell == solid.cell):
        raise BakeError("derived actor starts inside a collision solid")

    player_record = _winning_record(records, recipe.player_record, {"NPC_"}, "player-stat")
    _level, player_attributes, player_skills, _health, player_magicka, player_fatigue = _npc_values(
        player_record, "player template")
    gmsts = tuple(_gmst_value(records, name) for name in COMBAT_GMSTS)
    progression_factors = tuple(_gmst_value(records, name) for name in PROGRESSION_GMSTS)
    if any(value <= 0 for value in progression_factors):
        raise BakeError("derived skill progression factor is invalid")
    progression_rules = tuple(_skill_progression_rule(records, index, 1 if index == 18 else 0)
                              for index in PROGRESSION_SKILL_INDEXES)
    security_disarm_gain = _skill_progression_rule(records, 18, 0)[1]
    fatigue_base = gmsts[12]
    strength, agility, luck = player_attributes[0], player_attributes[3], player_attributes[7]
    weapon_skill_values = (player_skills[22], player_skills[5], player_skills[4], player_skills[6], player_skills[7])
    maximum_weight = strength * 50
    if maximum_weight <= 0:
        raise BakeError("derived player maximum encumbrance is invalid")

    item_records: dict[str, Tes3Record] = {}
    item_declarations: list[tuple[int, tuple[int, int, int, int, int, bool, int | None], Tes3Record, int]] = []
    weapon_profiles: list[tuple[int, int, tuple[float, ...], bool]] = []
    enchantment_profiles: list[tuple[int, tuple[str, ...]]] = []
    equipment_magic_profiles: list[tuple[int, tuple[float, ...]]] = []
    for item_name in recipe.items:
        record_value = _winning_record(records, item_name, ITEM_RECORD_TYPES, "item")
        identifier = stable_record_id(item_name)
        item_records[item_name.casefold()] = record_value
        maximum_charge, enchantment, equipment_magic = _item_magic(record_value, records)
        item_declarations.append((identifier, _item_values(record_value), record_value, maximum_charge))
        if enchantment is not None:
            enchantment_profiles.append((identifier, enchantment))
        if equipment_magic is not None:
            equipment_magic_profiles.append((identifier, equipment_magic))
        if record_value.kind == "WEAP":
            skill, weapon_values, normal = _weapon_values(record_value)
            weapon_profiles.append((identifier, skill, weapon_values, normal))
    if len({identifier for identifier, _values, _record, _charge in item_declarations}) != len(item_declarations):
        raise BakeError("derived item identities collide")

    actor_values: list[tuple[DerivedActor, Tes3Record, float, float, float, float, tuple[float, ...],
                            tuple[float, ...], tuple[tuple[int, str, tuple[str, ...]], ...]]] = []
    for actor in recipe.actors:
        record_value = _winning_record(records, actor.record, {"NPC_", "CREA"}, "actor")
        if record_value.kind == "NPC_":
            _level, attributes, actor_skills, health, magicka, fatigue = _npc_values(record_value, "actor")
            combat_skill = float(actor_skills[26])
            destruction_skill = float(actor_skills[10])
            attacks = (0.0,) * 6
        else:
            values = _unpack(record_value, "NPDT", "<24i")
            attributes = tuple(int(value) for value in values[2:10])
            health, fatigue = int(values[10]), int(values[12])
            magicka = int(values[11])
            if health < 1 or fatigue < 0:
                raise BakeError(f"actor has unsupported creature stats: {actor.record}")
            combat_skill = float(values[14])
            destruction_skill = 0.0
            attacks = tuple(float(value) for value in values[17:23])
        evasion = (attributes[3] / 5.0 + attributes[7] / 10.0) * fatigue_base
        attack = (float(attributes[3]), float(attributes[7]), float(attributes[0]), fatigue_base,
                  combat_skill, float(fatigue), *attacks, 1.0, float(attributes[5]))
        magic_defense, diseases = _actor_magic(
            record_value, records, float(attributes[2]), destruction_skill)
        actor_values.append((actor, record_value, float(health), float(fatigue), float(magicka), evasion, attack,
                             magic_defense, diseases))
    if len({stable_record_id(actor.record) for actor in recipe.actors}) != len(
            {actor.record.casefold() for actor in recipe.actors}):
        raise BakeError("derived actor prototype identities collide")

    collision_lines = [CATALOGS["collision_content_file"][0], f"manifest {MANIFEST_PLACEHOLDER}"]
    for cell in allowed_cells:
        collision_lines.append("cell " + " ".join(_cell_tokens(cell)))
    for solid in sorted(recipe.solids, key=lambda value: (value.cell, value.minimum, value.maximum)):
        collision_lines.append("solid " + " ".join((*_cell_tokens(solid.cell),
            *(str(value) for value in solid.minimum), *(str(value) for value in solid.maximum))))

    actor_lines = [CATALOGS["actor_content_file"][0], f"manifest {MANIFEST_PLACEHOLDER}"]
    for actor, _record, _health, _fatigue, _magicka, _evasion, _attack, _magic, _diseases in sorted(
            actor_values, key=lambda value: value[0].actor_id):
        actor_lines.append(" ".join(("actor", str(actor.actor_id), str(actor.entity_id),
            str(stable_record_id(actor.record)), *_cell_tokens(actor.cell),
            *(str(value) for value in actor.position), *(str(value) for value in actor.orientation), "idle")))

    inventory_lines = [CATALOGS["inventory_content_file"][0], f"manifest {MANIFEST_PLACEHOLDER}"]
    for identifier, values, _record, maximum_charge in sorted(item_declarations):
        category, weight, item_value, condition, slots, stackable, key_id, tool_quality = values
        inventory_lines.append(" ".join(("prototype", str(identifier), str(category), str(weight),
            str(item_value), str(condition), str(maximum_charge), str(slots), "1" if stackable else "0",
            str(key_id) if key_id is not None else "none", _float_text(tool_quality))))

    combat_lines = [CATALOGS["combat_content_file"][0], f"manifest {MANIFEST_PLACEHOLDER}",
                    f"seed {recipe.seed}",
                    "settings " + " ".join((*(_float_text(value) for value in gmsts), "0", "0"))]
    combat_lines.append("security_settings " + " ".join((_float_text(_gmst_value(records, "fPickLockMult")),
        _float_text(_gmst_value(records, "fTrapCostMult")), _float_text(security_disarm_gain))))
    combat_lines.append("magic_settings " + " ".join(_float_text(_gmst_value(records, name))
        for name in ("fElementalShieldMult", "fDiseaseXferChance")))
    player_magic = (float(player_attributes[2]), float(player_skills[10]), *([0.0] * 9))
    combat_lines.append("player_magic " + " ".join(_float_text(value) for value in player_magic))
    progression_fields = (*progression_factors,
                          *(value for rule in progression_rules for value in rule))
    combat_lines.append("progression " + " ".join(_float_text(float(value)) for value in progression_fields))
    # OpenMW restores these resources per in-game hour while resting. Until the
    # canonical time/rest domain lands, vNext uses the same formulas at the
    # stock 30x time scale only while the player has no live same-cell aggressor.
    health_recovery = float(player_attributes[5]) * 0.1 / 120.0
    rest_magic_multiplier = _gmst_value(records, "fRestMagicMult")
    magicka_recovery = float(player_attributes[1]) * rest_magic_multiplier / 120.0
    player_fields = (float(agility), float(luck), float(strength), fatigue_base, 0.0, 0.0,
                     *(float(value) for value in weapon_skill_values), float(player_skills[26]), float(player_fatigue),
                     float(player_attributes[5]), float(player_skills[0]), float(player_attributes[1]),
                     float(player_magicka), health_recovery, magicka_recovery,
                     float(player_skills[21]), float(player_skills[2]), float(player_skills[3]),
                     float(player_skills[17]), float(player_skills[18]),
                     float(player_skills[11]), float(player_skills[13]), float(player_skills[10]),
                     float(player_skills[12]), float(player_skills[14]), float(player_skills[15]),
                     float(player_skills[9]))
    combat_lines.append("player " + " ".join((*(_float_text(value) for value in player_fields),
        str(maximum_weight), "0")))
    supported_spells: list[tuple[int, str, tuple[str, ...]]] = []
    character_catalog = _catalog_by_key(retained_catalogs, "character_content_file")
    referenced_spell_ids: set[int] = set()
    if character_catalog is not None:
        for declaration in character_catalog.records:
            if declaration[0] == "race":
                referenced_spell_ids.update(int(value) for value in declaration[46:])
            elif declaration[0] == "birthsign":
                referenced_spell_ids.update(int(value) for value in declaration[3:])
    for spell_id in sorted(referenced_spell_ids):
        candidates = [record_value for (kind, _name), record_value in records.items()
                      if kind == "SPEL" and not record_value.deleted
                      and _optional_stable_record_id(record_value.name) == spell_id]
        if len(candidates) != 1:
            raise BakeError(f"character spell identity does not name one live spell: {spell_id}")
        try:
            profile = _spell_profile(candidates[0], records, player_skills)
        except BakeError:
            continue
        supported_spells.append((spell_id, candidates[0].name, profile))
        combat_lines.append("spell " + " ".join((str(spell_id), *profile)))
    for actor, _record, health, fatigue, magicka, evasion, attack, magic_defense, diseases in sorted(
            actor_values, key=lambda value: value[0].actor_id):
        combat_lines.append("actor " + " ".join((str(actor.actor_id), _float_text(health),
            _float_text(fatigue), _float_text(evasion), "0", "0", "0", "0", _float_text(magicka),
            "0", "0", "0", "0",
            "1" if _record.kind == "CREA" else "0")))
        combat_lines.append("actor_attack " + " ".join((str(actor.actor_id),
            *(_float_text(value) for value in attack))))
        combat_lines.append("actor_magic " + " ".join((str(actor.actor_id),
            *(_float_text(value) for value in magic_defense))))
        for spell_id, disease_kind, effects in diseases:
            combat_lines.append("disease " + " ".join((str(actor.actor_id), str(spell_id), disease_kind,
            str(len(effects) // 7), *effects)))
    for identifier, skill, values, normal in sorted(weapon_profiles):
        combat_lines.append("weapon " + " ".join((str(identifier), str(skill),
            *(_float_text(value) for value in values), "1" if normal else "0")))
    for identifier, values in sorted(enchantment_profiles):
        combat_lines.append("enchantment " + " ".join((str(identifier), values[0], values[1],
            str((len(values) - 2) // 7), *values[2:])))
    for identifier, defense in sorted(equipment_magic_profiles):
        combat_lines.append("equipment_magic " + " ".join((str(identifier),
            *(_float_text(value) for value in defense))))
    object_catalog = _catalog_by_key(retained_catalogs, "interactive_object_content_file")
    for identifier, effects in _trap_magic_profiles(records, _interactive_object_trap_ids(object_catalog)):
        combat_lines.append("trap " + " ".join((str(identifier), str(len(effects) // 7), *effects)))
    light_multiplier = _gmst_value(records, "fLightMaxMod")
    medium_multiplier = _gmst_value(records, "fMedMaxMod")
    for identifier, _values, record_value, _charge in sorted(item_declarations):
        if record_value.kind != "ARMO":
            continue
        armor_type, weight, _value, _health, _enchant, base_armor = _unpack(record_value, "AODT", "<ifiiii")
        weight_gmst = ARMOR_WEIGHT_GMSTS.get(int(armor_type))
        if weight_gmst is None:
            raise BakeError(f"unsupported armor type: {record_value.name}")
        type_weight = math.floor(_gmst_value(records, weight_gmst))
        light_max = type_weight * light_multiplier + 0.0005
        medium_max = type_weight * medium_multiplier + 0.0005
        armor_skill = 0 if float(weight) <= light_max else 1 if float(weight) <= medium_max else 2
        combat_lines.append(f"armor {identifier} {armor_skill} {int(base_armor)}")

    generated = [
        _catalog("collision_content_file", recipe.path, "vanilla-collision.txt", collision_lines),
        _catalog("actor_content_file", recipe.path, "vanilla-actors.txt", actor_lines),
        _catalog("inventory_content_file", recipe.path, "vanilla-inventory.txt", inventory_lines),
        _catalog("combat_content_file", recipe.path, "vanilla-combat.txt", combat_lines),
    ]
    catalogs = list(retained_catalogs) + generated
    character = _catalog_by_key(catalogs, "character_content_file")
    if recipe.starting_items and character is None:
        raise BakeError("derived starting items require character content")
    if character is not None:
        additions = []
        for item in recipe.starting_items:
            identifier = stable_record_id(item.record)
            declaration = next((value for value in item_declarations if value[0] == identifier), None)
            if declaration is None:
                raise BakeError("derived starting item identity is absent from inventory content")
            slots = declaration[1][4]
            if item.slot is not None and (slots & (1 << item.slot)) == 0:
                raise BakeError("derived starting item cannot occupy its requested equipment slot")
            additions.append(("starting_item", str(identifier), str(item.count),
                              str(item.slot) if item.slot is not None else "-1"))
        # Starting inventory belongs to the derived recipe. Strip any prior
        # generated lines so rebaking the checked-in default pack is idempotent
        # and never accepts that gameplay state as trusted catalog input.
        retained_lines = [line for line in character.normalized.decode("utf-8").splitlines()
                          if not line.strip().startswith("starting_item ")]
        normalized = ("\n".join(retained_lines) + "\n").encode("utf-8") \
            + b"".join((" ".join(value) + "\n").encode("utf-8") for value in additions)
        replacement = Catalog(character.key, character.path, character.output_name,
                              normalized, tuple(value for value in character.records if value[0] != "starting_item")
                              + tuple(additions))
        catalogs[catalogs.index(character)] = replacement

    # Actor and item catalogs are generated, so their client mappings are too.
    # Discard stale checked-in/generated values before rebuilding the exact set.
    augmented = [entry for entry in client_entries
                 if entry.key not in {"tes3mp-content-actor-prototype-map",
                                      "tes3mp-content-item-prototype-map",
                                      "tes3mp-content-spell-map"}]
    line = max((entry.line for entry in augmented), default=0) + 1
    existing_actor_names: dict[int, str] = {}
    for actor in recipe.actors:
        existing_actor_names[stable_record_id(actor.record)] = actor.record
    for identifier, record_name in sorted(existing_actor_names.items()):
        augmented.append(Assignment("tes3mp-content-actor-prototype-map", f"{identifier}={record_name}", line)); line += 1
    for identifier, _values, record_value, _charge in sorted(item_declarations):
        original_name = next(name for name in recipe.items if name.casefold() == record_value.name)
        augmented.append(Assignment("tes3mp-content-item-prototype-map", f"{identifier}={original_name}", line)); line += 1
    for identifier, record_name, _profile in supported_spells:
        augmented.append(Assignment("tes3mp-content-spell-map", f"{identifier}={record_name}", line)); line += 1
    return catalogs, augmented


def _records_of_type(records: dict[tuple[str, str], bool], types: set[str]) -> set[str]:
    return {name for (record_type, name), present in records.items() if present and record_type in types}


def _mapping_values(client_entries: Sequence[Assignment], key: str) -> dict[int, str]:
    result: dict[int, str] = {}
    for entry in client_entries:
        if entry.key != key:
            continue
        if "=" not in entry.value:
            raise BakeError(f"client mappings:{entry.line}: malformed {key}")
        raw_id, value = entry.value.split("=", 1)
        try:
            identifier = int(raw_id, 10)
        except ValueError as exc:
            raise BakeError(f"client mappings:{entry.line}: invalid numeric ID") from exc
        if identifier <= 0 or identifier > 0xFFFFFFFFFFFFFFFF or not value or identifier in result:
            raise BakeError(f"client mappings:{entry.line}: invalid or duplicate {key} ID")
        result[identifier] = value
    return result


def _record_ids(catalog: Catalog | None, kind: str, field: int) -> set[int]:
    if catalog is None:
        return set()
    result: set[int] = set()
    for record in catalog.records:
        if record[0] != kind:
            continue
        try:
            value = int(record[field], 10)
        except (IndexError, ValueError) as exc:
            raise BakeError(f"malformed {kind} record in {catalog.path}") from exc
        if value <= 0 or value > 0xFFFFFFFFFFFFFFFF:
            raise BakeError(f"invalid {kind} identity in {catalog.path}")
        result.add(value)
    return result


def _interactive_object_trap_ids(catalog: Catalog | None) -> set[int]:
    if catalog is None:
        return set()
    result: set[int] = set()
    for record in catalog.records:
        if record[0] != "object":
            continue
        try:
            trap_field = 13 if record[3] == "interior" else 15 if record[3] == "exterior" else -1
        except IndexError as exc:
            raise BakeError(f"malformed object record in {catalog.path}") from exc
        if trap_field < 0:
            raise BakeError(f"malformed object record in {catalog.path}")
        try:
            value = record[trap_field]
        except IndexError as exc:
            raise BakeError(f"malformed object record in {catalog.path}") from exc
        if value == "none":
            continue
        try:
            identifier = int(value, 10)
        except ValueError as exc:
            raise BakeError(f"invalid trap identity in {catalog.path}") from exc
        if identifier <= 0 or identifier > 0xFFFFFFFFFFFFFFFF:
            raise BakeError(f"invalid trap identity in {catalog.path}")
        result.add(identifier)
    return result


def _require_exact_mapping(mapping: dict[int, str], expected: set[int], description: str) -> None:
    if set(mapping) != expected:
        missing = sorted(expected - set(mapping))
        extra = sorted(set(mapping) - expected)
        raise BakeError(f"{description} mapping does not match the catalog (missing={missing}, extra={extra})")


def _require_hashed_mapping(mapping: dict[int, str], available: set[str], description: str) -> None:
    for identifier, record in mapping.items():
        if stable_record_id(record) != identifier:
            raise BakeError(f"{description} mapping ID does not match record ID hash: {identifier}={record}")
        if record.casefold() not in available:
            raise BakeError(f"{description} mapping references a missing winning record: {record}")


def _catalog_by_key(catalogs: Sequence[Catalog], key: str) -> Catalog | None:
    return next((catalog for catalog in catalogs if catalog.key == key), None)


def _validate_script_packages(catalog: Catalog | None) -> None:
    if catalog is None:
        return
    packages: dict[int, tuple[int, int, int]] = {}
    variables: set[tuple[int, int]] = set()
    per_package: dict[int, int] = {}
    string_bytes = 0
    for record in catalog.records:
        try:
            if record[0] == "package":
                if len(record) != 10 or len(packages) == 64:
                    raise BakeError(f"malformed or oversized package record in {catalog.path}")
                package_id, version, load_order, api, abi = map(int, record[1:6])
                artifact, digest, entrypoint, budget_text = record[6:10]
                if package_id <= 0 or package_id > 0xFFFFFFFFFFFFFFFF or version <= 0 \
                        or version > 0xFFFFFFFF or load_order < 0 or load_order > 0xFFFFFFFF or api != 6 \
                        or abi != 1 or package_id in packages \
                        or not artifact or len(artifact.encode("ascii", errors="ignore")) != len(artifact) \
                        or len(artifact) > 128 or pathlib.PurePath(artifact).name != artifact \
                        or artifact.startswith(".") \
                        or any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-"
                               for character in artifact) \
                        or len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest) \
                        or not entrypoint or len(entrypoint) > 64 \
                        or any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                               for character in entrypoint):
                    raise BakeError(f"invalid or duplicate script package in {catalog.path}")
                budget = int(budget_text)
                if budget <= 0 or budget > 4096:
                    raise BakeError(f"invalid script execution budget in {catalog.path}")
                packages[package_id] = (version, load_order, api)
            elif record[0] == "variable":
                if len(record) != 5 or len(variables) == 16384:
                    raise BakeError(f"malformed or oversized script variable in {catalog.path}")
                package_id, variable_id = int(record[1]), int(record[2])
                key = (package_id, variable_id)
                if package_id <= 0 or package_id > 0xFFFFFFFFFFFFFFFF or variable_id <= 0 \
                        or variable_id > 0xFFFFFFFFFFFFFFFF or key in variables:
                    raise BakeError(f"invalid or duplicate script variable in {catalog.path}")
                kind, value = record[3], record[4]
                if kind == "boolean":
                    if value not in {"true", "false"}:
                        raise BakeError(f"invalid script boolean in {catalog.path}")
                elif kind == "integer":
                    parsed = int(value)
                    if parsed < -0x8000000000000000 or parsed > 0x7FFFFFFFFFFFFFFF:
                        raise BakeError(f"script integer is out of range in {catalog.path}")
                elif kind == "float":
                    if not math.isfinite(float(value)):
                        raise BakeError(f"invalid script float in {catalog.path}")
                elif kind == "string_hex":
                    if value == "-":
                        length = 0
                    else:
                        if not value or len(value) % 2 or any(byte not in "0123456789abcdef" for byte in value):
                            raise BakeError(f"invalid script string encoding in {catalog.path}")
                        length = len(value) // 2
                    if length > 4096:
                        raise BakeError(f"script string is too large in {catalog.path}")
                    string_bytes += length
                    if string_bytes > 1024 * 1024:
                        raise BakeError(f"script catalog strings are too large in {catalog.path}")
                else:
                    raise BakeError(f"unknown script variable type in {catalog.path}")
                variables.add(key)
                per_package[package_id] = per_package.get(package_id, 0) + 1
                if per_package[package_id] > 256:
                    raise BakeError(f"script package has too many variables in {catalog.path}")
            else:
                raise BakeError(f"unknown script package record in {catalog.path}")
        except ValueError as exc:
            raise BakeError(f"malformed script package record in {catalog.path}") from exc
    if any(package_id not in packages for package_id, _variable_id in variables):
        raise BakeError(f"script variable references an unknown package in {catalog.path}")


def load_script_modules(catalog: Catalog | None) -> list[ScriptModuleArtifact]:
    if catalog is None:
        return []
    result: list[ScriptModuleArtifact] = []
    names: set[str] = set()
    for record in catalog.records:
        if record[0] != "package":
            continue
        name, expected, entrypoint = record[6], record[7], record[8]
        normalized = name.casefold()
        if normalized in names:
            raise BakeError(f"duplicate script module artifact in {catalog.path}: {name}")
        names.add(normalized)
        path = _resolve_path(name, catalog.path.parent, "script module artifact")
        data = _read_bounded(path, MAX_SCRIPT_MODULE_BYTES, "script module artifact")
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected:
            raise BakeError(f"script module artifact digest mismatch: {name}")
        text = _decode_text(data, str(path))
        lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
        if not lines or lines[0] != "TES3MP_SCRIPT_MODULE_V1" or f"abi 1" not in lines \
                or f"api 6" not in lines or f"entry {entrypoint}" not in lines:
            raise BakeError(f"invalid script module artifact: {name}")
        result.append(ScriptModuleArtifact(name, path, data, digest))
    return result


def validate_consistency(server_entries: Sequence[Assignment], client_entries: Sequence[Assignment],
    catalogs: Sequence[Catalog], records: dict[tuple[str, str], bool]) -> None:
    allowed_client_keys = CLIENT_SINGLE_KEYS | CLIENT_MAPPING_KEYS
    unknown = sorted({entry.key for entry in client_entries if entry.key not in allowed_client_keys})
    if unknown:
        raise BakeError(f"unknown client content mapping keys: {', '.join(unknown)}")

    actor_catalog = _catalog_by_key(catalogs, "actor_content_file")
    object_catalog = _catalog_by_key(catalogs, "interactive_object_content_file")
    inventory_catalog = _catalog_by_key(catalogs, "inventory_content_file")
    combat_catalog = _catalog_by_key(catalogs, "combat_content_file")
    character_catalog = _catalog_by_key(catalogs, "character_content_file")
    _validate_script_packages(_catalog_by_key(catalogs, "script_package_file"))

    actor_prototypes = _record_ids(actor_catalog, "actor", 3)
    objects = _record_ids(object_catalog, "object", 1)
    item_prototypes = _record_ids(inventory_catalog, "prototype", 1)
    containers = _record_ids(inventory_catalog, "container", 1)
    actor_ids = _record_ids(actor_catalog, "actor", 1)
    combat_actor_ids = _record_ids(combat_catalog, "actor", 1)
    weapon_ids = _record_ids(combat_catalog, "weapon", 1)
    object_trap_ids = _interactive_object_trap_ids(object_catalog)
    combat_trap_ids = _record_ids(combat_catalog, "trap", 1)
    combat_spell_ids = _record_ids(combat_catalog, "spell", 1)
    if combat_catalog and inventory_catalog is None:
        raise BakeError("combat content requires inventory content")
    if combat_actor_ids != actor_ids:
        raise BakeError("combat actor identities do not exactly match the actor catalog")
    if not weapon_ids.issubset(item_prototypes):
        raise BakeError("combat weapon identities are absent from the inventory catalog")
    if combat_trap_ids != object_trap_ids:
        raise BakeError("combat trap identities do not exactly match the interactive object catalog")

    actor_mapping = _mapping_values(client_entries, "tes3mp-content-actor-prototype-map")
    object_mapping = _mapping_values(client_entries, "tes3mp-content-interactive-object-map")
    item_mapping = _mapping_values(client_entries, "tes3mp-content-item-prototype-map")
    container_mapping = _mapping_values(client_entries, "tes3mp-content-container-map")
    dialogue_choice_mapping = _mapping_values(client_entries, "tes3mp-content-dialogue-choice-map")
    weather_region_mapping = _mapping_values(client_entries, "tes3mp-content-weather-region-map")
    weather_mapping = _mapping_values(client_entries, "tes3mp-content-weather-map")
    spell_mapping = _mapping_values(client_entries, "tes3mp-content-spell-map")
    cell_mapping = _mapping_values(client_entries, "tes3mp-content-cell-space-map")
    _require_exact_mapping(actor_mapping, actor_prototypes, "actor prototype")
    _require_exact_mapping(object_mapping, objects, "interactive object")
    _require_exact_mapping(item_mapping, item_prototypes, "item prototype")
    _require_exact_mapping(container_mapping, containers, "container")
    world_catalog = _catalog_by_key(catalogs, "world_content_file")
    dialogue_choices = _record_ids(world_catalog, "dialogue_choice", 1)
    _require_exact_mapping(dialogue_choice_mapping, dialogue_choices, "dialogue choice")
    weather_regions = _record_ids(world_catalog, "weather_region", 1)
    weather_ids = _record_ids(world_catalog, "weather", 1)
    _require_exact_mapping(weather_region_mapping, weather_regions, "weather region")
    _require_exact_mapping(weather_mapping, weather_ids, "weather")
    _require_exact_mapping(spell_mapping, combat_spell_ids, "spell")
    local_dialogue_choices: set[int] = set()
    for value in dialogue_choice_mapping.values():
        try:
            local_choice = int(value, 10)
        except ValueError as exc:
            raise BakeError("dialogue choice mapping contains an invalid local choice") from exc
        if local_choice < -0x80000000 or local_choice > 0x7fffffff or local_choice in local_dialogue_choices:
            raise BakeError("dialogue choice mapping contains an out-of-range or duplicate local choice")
        local_dialogue_choices.add(local_choice)

    cell_spaces = _one(server_entries, "cell_spaces")
    expected_spaces: set[int] = set()
    assert cell_spaces is not None
    for declaration in cell_spaces.split(";"):
        fields = declaration.split(":")
        if len(fields) != 2 or fields[0] not in {"interior", "exterior"}:
            raise BakeError("invalid cell_spaces in server config")
        try:
            identifier = int(fields[1], 10)
        except ValueError as exc:
            raise BakeError("invalid cell space identity in server config") from exc
        if identifier <= 0 or identifier in expected_spaces:
            raise BakeError("invalid or duplicate cell space identity in server config")
        expected_spaces.add(identifier)
    _require_exact_mapping(cell_mapping, expected_spaces, "cell space")

    actor_records = _records_of_type(records, {"NPC_", "CREA"})
    item_records = _records_of_type(records, ITEM_RECORD_TYPES)
    npc_records = _records_of_type(records, {"NPC_"})
    cell_records = _records_of_type(records, {"CELL"})
    region_records = _records_of_type(records, {"REGN"})
    _require_hashed_mapping(actor_mapping, actor_records, "actor prototype")
    _require_hashed_mapping(item_mapping, item_records, "item prototype")
    _require_hashed_mapping(spell_mapping, _records_of_type(records, {"SPEL"}), "spell")
    appearance = _one(client_entries, "tes3mp-content-appearance-record")
    assert appearance is not None
    if appearance.casefold() not in npc_records:
        raise BakeError(f"appearance mapping references a missing winning NPC record: {appearance}")
    for identifier, value in cell_mapping.items():
        declaration = next(part for part in cell_spaces.split(";") if int(part.split(":")[1]) == identifier)
        if declaration.startswith("interior:") and value.casefold() not in cell_records:
            raise BakeError(f"interior cell mapping references a missing winning cell: {value}")
    for value in weather_region_mapping.values():
        if value.casefold() not in region_records:
            raise BakeError(f"weather region mapping references a missing winning region: {value}")

    if character_catalog:
        hashed_by_type: dict[str, set[int]] = {}
        for record_type in {"RACE", "BODY", "CLAS", "BSGN", "SPEL"} | ITEM_RECORD_TYPES:
            hashed_by_type[record_type] = {
                identifier for (kind, name), present in records.items()
                if present and kind == record_type
                for identifier in [_optional_stable_record_id(name)] if identifier is not None
            }
        for record in character_catalog.records:
            kind = record[0]
            try:
                if kind == "race":
                    if int(record[1]) not in hashed_by_type["RACE"]:
                        raise BakeError("character race identity is absent from the loadout")
                    spell_count = int(record[45])
                    spell_ids = [int(value) for value in record[46:]]
                    if spell_count != len(spell_ids) or any(value not in hashed_by_type["SPEL"] for value in spell_ids):
                        raise BakeError("character race spells are inconsistent with the loadout")
                elif kind == "appearance":
                    if any(int(value) not in hashed_by_type["BODY"] for value in record[2:4]):
                        raise BakeError("character appearance identity is absent from the loadout")
                elif kind == "class" and int(record[1]) not in hashed_by_type["CLAS"]:
                    raise BakeError("character class identity is absent from the loadout")
                elif kind == "birthsign":
                    if int(record[1]) not in hashed_by_type["BSGN"]:
                        raise BakeError("character birthsign identity is absent from the loadout")
                    count = int(record[2])
                    spell_ids = [int(value) for value in record[3:]]
                    if count != len(spell_ids) or any(value not in hashed_by_type["SPEL"] for value in spell_ids):
                        raise BakeError("character birthsign spells are inconsistent with the loadout")
                elif kind == "starting_item" and int(record[1]) not in item_prototypes:
                    raise BakeError("character starting item is absent from the inventory catalog")
            except (IndexError, ValueError) as exc:
                raise BakeError(f"malformed {kind} record in {character_catalog.path}") from exc


def _canonical_server_semantics(entries: Sequence[Assignment]) -> bytes:
    lines: list[str] = []
    for key in SERVER_CONTENT_KEYS:
        required = CATALOGS[key][1] if key in CATALOGS else True
        value = _one(entries, key, required)
        if value is not None:
            # Catalog payloads are hashed separately. Bind presence here, not an
            # authoring-machine path that has no gameplay meaning.
            lines.append(f"{key}={'present' if key in CATALOGS else value}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def _canonical_client_semantics(entries: Sequence[Assignment]) -> bytes:
    lines: list[str] = []
    for key in sorted(CLIENT_SINGLE_KEYS):
        lines.append(f"{key}={_one(entries, key)}")
    for key in sorted(CLIENT_MAPPING_KEYS):
        for identifier, value in sorted(_mapping_values(entries, key).items()):
            lines.append(f"{key}={identifier}={value}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def compute_manifest(loadout: Sequence[LoadoutFile], server_entries: Sequence[Assignment],
    client_entries: Sequence[Assignment], catalogs: Sequence[Catalog],
    script_modules: Sequence[ScriptModuleArtifact] = ()) -> str:
    digest = hashlib.sha256(HASH_DOMAIN)

    def add(label: str, payload: bytes) -> None:
        encoded_label = label.encode("utf-8")
        digest.update(struct.pack("<I", len(encoded_label)))
        digest.update(encoded_label)
        digest.update(struct.pack("<Q", len(payload)))
        digest.update(payload)

    for index, entry in enumerate(loadout):
        add(f"loadout/{index}/{entry.name.casefold()}", bytes.fromhex(entry.sha256))
    add("server", _canonical_server_semantics(server_entries))
    add("client", _canonical_client_semantics(client_entries))
    for catalog in sorted(catalogs, key=lambda value: value.key):
        add(f"catalog/{catalog.key}", catalog.normalized)
    for module in sorted(script_modules, key=lambda value: value.name.casefold()):
        add(f"script_module/{module.name.casefold()}", module.data)
    return digest.hexdigest()


def _replace_manifest(data: bytes, manifest: str) -> bytes:
    marker = MANIFEST_PLACEHOLDER.encode("ascii")
    if data.count(marker) != 1:
        raise BakeError("internal normalized catalog manifest invariant failed")
    return data.replace(marker, manifest.encode("ascii"))


def _render_server_config(source: pathlib.Path, source_text: str, entries: Sequence[Assignment],
    catalogs: Sequence[Catalog], manifest: str) -> bytes:
    catalog_names = {catalog.key: catalog.output_name for catalog in catalogs}
    rewritten: list[str] = []
    seen_manifest = False
    seen_catalogs: set[str] = set()
    for raw in source_text.splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#") or "=" not in raw:
            rewritten.append(raw.rstrip())
            continue
        key, _value = raw.split("=", 1)
        normalized_key = key.strip()
        if normalized_key == "content_manifest_id":
            rewritten.append(f"content_manifest_id = {manifest}")
            seen_manifest = True
        elif normalized_key in catalog_names:
            rewritten.append(f"{normalized_key} = {catalog_names[normalized_key]}")
            seen_catalogs.add(normalized_key)
        elif normalized_key == "join_password_file":
            # Packs live at <output>/packs/<manifest>. Keep secrets outside the
            # immutable pack and do not leak authoring-machine paths.
            rewritten.append("join_password_file = ../../join-password.txt")
        elif normalized_key == "player_identity_file":
            rewritten.append("player_identity_file = ../../players.txt")
        else:
            rewritten.append(raw.rstrip())
    if not seen_manifest:
        raise BakeError("server config is missing content_manifest_id")
    for key in CATALOGS:
        if key in catalog_names and key not in seen_catalogs:
            rewritten.append(f"{key} = {catalog_names[key]}")
    return ("\n".join(rewritten) + "\n").encode("utf-8")


def _render_client_config(server_entries: Sequence[Assignment], client_entries: Sequence[Assignment],
    manifest: str) -> bytes:
    server_to_client = {
        "cell_spaces": "tes3mp-content-cell-spaces",
        "allowed_cells": "tes3mp-content-allowed-cells",
        "default_appearance_id": "tes3mp-content-appearance-id",
        "movement_profile": "tes3mp-content-movement-profile",
    }
    lines = ["# Generated by bake_tes3mp_content.py; load as an OpenMW config layer.",
             f"tes3mp-content-manifest-id={manifest}"]
    for server_key, client_key in server_to_client.items():
        lines.append(f"{client_key}={_one(server_entries, server_key)}")
    for key in sorted(CLIENT_SINGLE_KEYS):
        lines.append(f"{key}={_one(client_entries, key)}")
    for key in sorted(CLIENT_MAPPING_KEYS):
        for identifier, value in sorted(_mapping_values(client_entries, key).items()):
            lines.append(f"{key}={identifier}={value}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def _tree_bytes(path: pathlib.Path) -> dict[str, bytes]:
    return {item.relative_to(path).as_posix(): item.read_bytes() for item in sorted(path.rglob("*")) if item.is_file()}


def _write(path: pathlib.Path, data: bytes) -> None:
    try:
        path.write_bytes(data)
    except OSError as exc:
        raise BakeError(f"could not write {path}: {exc}") from exc


def bake(config_paths: Sequence[pathlib.Path], server_config: pathlib.Path, client_mappings: pathlib.Path,
    output_root: pathlib.Path, derived_recipe: pathlib.Path | None = None) -> tuple[str, pathlib.Path]:
    server_config = server_config.resolve(strict=False)
    client_mappings = client_mappings.resolve(strict=False)
    server_text = _decode_text(_read_bounded(server_config, MAX_CONFIG_BYTES, "server config"), str(server_config))
    client_text = _decode_text(_read_bounded(client_mappings, MAX_CONFIG_BYTES, "client mappings"), str(client_mappings))
    server_entries = _assignments(server_text, str(server_config))
    client_entries = _assignments(client_text, str(client_mappings))
    _one(server_entries, "content_manifest_id")
    loadout = resolve_loadout(config_paths)
    record_data = winning_tes3_record_data(loadout)
    records = {key: not value.deleted for key, value in record_data.items()}
    recipe = load_derived_recipe(derived_recipe) if derived_recipe is not None else None
    catalogs = load_catalogs(server_config, server_entries, DERIVED_CATALOG_KEYS if recipe else None)
    if recipe:
        catalogs, client_entries = derive_catalogs(recipe, server_entries, client_entries, record_data, catalogs)
        server_entries = [entry for entry in server_entries if entry.key not in DERIVED_CATALOG_KEYS]
        next_line = max((entry.line for entry in server_entries), default=0) + 1
        for catalog in catalogs:
            if catalog.key in DERIVED_CATALOG_KEYS:
                server_entries.append(Assignment(catalog.key, catalog.output_name, next_line))
                next_line += 1
    verify_loadout_unchanged(loadout)
    validate_consistency(server_entries, client_entries, catalogs, records)
    script_modules = load_script_modules(_catalog_by_key(catalogs, "script_package_file"))
    manifest = compute_manifest(loadout, server_entries, client_entries, catalogs, script_modules)

    output_root = output_root.resolve(strict=False)
    packs = output_root / "packs"
    try:
        packs.mkdir(parents=True, exist_ok=True)
        temporary = pathlib.Path(tempfile.mkdtemp(prefix=".bake-", dir=output_root))
    except OSError as exc:
        raise BakeError(f"could not prepare output directory {output_root}: {exc}") from exc
    try:
        artifacts: dict[str, str] = {}
        rendered: dict[str, bytes] = {
            "server.cfg": _render_server_config(server_config, server_text, server_entries, catalogs, manifest),
            "openmw.cfg": _render_client_config(server_entries, client_entries, manifest),
        }
        for catalog in catalogs:
            rendered[catalog.output_name] = _replace_manifest(catalog.normalized, manifest)
        for module in script_modules:
            if module.name in rendered:
                raise BakeError(f"script module output filename is duplicated: {module.name}")
            rendered[module.name] = module.data
        for name, data in sorted(rendered.items()):
            _write(temporary / name, data)
            artifacts[name] = hashlib.sha256(data).hexdigest()
        metadata = {
            "format": FORMAT,
            "manifest_id": manifest,
            "content_files": [
                {"name": entry.name, "size": entry.size, "sha256": entry.sha256,
                 "masters": list(entry.masters)} for entry in loadout
            ],
            "artifacts": artifacts,
        }
        _write(temporary / "pack.json",
               (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode("utf-8"))
        final = packs / manifest
        if final.exists():
            if not final.is_dir() or _tree_bytes(final) != _tree_bytes(temporary):
                raise BakeError(f"immutable pack collision at {final}")
            shutil.rmtree(temporary)
        else:
            os.replace(temporary, final)
        descriptor, pointer_name = tempfile.mkstemp(prefix=".CURRENT.", dir=output_root)
        pointer = pathlib.Path(pointer_name)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write((manifest + "\n").encode("ascii"))
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(pointer, output_root / "CURRENT")
        finally:
            if pointer.exists():
                pointer.unlink()
        return manifest, final
    except Exception:
        if temporary.exists():
            shutil.rmtree(temporary, ignore_errors=True)
        raise


def _verify_content_metadata(value: object) -> None:
    if not isinstance(value, list) or not value or len(value) > MAX_CONTENT_FILES:
        raise BakeError("pack has an invalid content file table")
    earlier: set[str] = set()
    for entry in value:
        if not isinstance(entry, dict) or set(entry) != {"name", "size", "sha256", "masters"}:
            raise BakeError("pack has an invalid content file entry")
        name, size, digest, masters = (entry[key] for key in ("name", "size", "sha256", "masters"))
        if not isinstance(name, str) or not name or pathlib.PureWindowsPath(name).name != name \
                or "/" in name or "\\" in name:
            raise BakeError("pack has an invalid content filename")
        normalized = name.casefold()
        if normalized in earlier:
            raise BakeError("pack has a duplicate content filename")
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise BakeError(f"pack has an invalid content size: {name}")
        if not isinstance(digest, str) or len(digest) != 64 \
                or any(character not in "0123456789abcdef" for character in digest):
            raise BakeError(f"pack has an invalid content digest: {name}")
        if not isinstance(masters, list) or len(masters) > MAX_CONTENT_FILES:
            raise BakeError(f"pack has an invalid TES3 master table: {name}")
        seen_masters: set[str] = set()
        for master in masters:
            if not isinstance(master, str) or not master or pathlib.PureWindowsPath(master).name != master \
                    or "/" in master or "\\" in master:
                raise BakeError(f"pack has an invalid TES3 master name: {name}")
            try:
                encoded = master.encode("cp1252")
            except UnicodeEncodeError as exc:
                raise BakeError(f"pack has an invalid TES3 master name: {name}") from exc
            master_normalized = master.casefold()
            if len(encoded) > MAX_MASTER_NAME_BYTES or master_normalized in seen_masters:
                raise BakeError(f"pack has an invalid TES3 master table: {name}")
            if master_normalized not in earlier:
                raise BakeError(f"pack TES3 master is missing or misordered: {name} requires {master}")
            seen_masters.add(master_normalized)
        earlier.add(normalized)


def verify_pack(path: pathlib.Path) -> str:
    path = path.resolve(strict=False)
    metadata_path = path / "pack.json"
    try:
        metadata = json.loads(_decode_text(
            _read_bounded(metadata_path, MAX_CONFIG_BYTES, "pack metadata"), str(metadata_path)))
    except json.JSONDecodeError as exc:
        raise BakeError(f"invalid pack metadata: {exc}") from exc
    if not isinstance(metadata, dict) or metadata.get("format") != FORMAT:
        raise BakeError("unsupported content pack format")
    manifest = metadata.get("manifest_id")
    artifacts = metadata.get("artifacts")
    _verify_content_metadata(metadata.get("content_files"))
    if not isinstance(manifest, str) or len(manifest) != 64 or any(c not in "0123456789abcdef" for c in manifest):
        raise BakeError("invalid pack manifest identity")
    if not isinstance(artifacts, dict) or not artifacts:
        raise BakeError("pack has no artifact digest table")
    for name, expected in artifacts.items():
        if not isinstance(name, str) or pathlib.PurePosixPath(name).name != name:
            raise BakeError("invalid artifact name")
        if not isinstance(expected, str) or len(expected) != 64:
            raise BakeError(f"invalid artifact digest for {name}")
        data = _read_bounded(path / name, MAX_CATALOG_BYTES, f"pack artifact {name}")
        if hashlib.sha256(data).hexdigest() != expected:
            raise BakeError(f"artifact digest mismatch: {name}")
        if name == "server.cfg" and f"content_manifest_id = {manifest}".encode() not in data:
            raise BakeError("server config manifest does not match pack")
        if name == "openmw.cfg" and f"tes3mp-content-manifest-id={manifest}".encode() not in data:
            raise BakeError("OpenMW config manifest does not match pack")
        if name.endswith(".t3sm"):
            if not data.startswith(b"TES3MP_SCRIPT_MODULE_V1\n"):
                raise BakeError(f"invalid script module artifact: {name}")
        elif name not in {"server.cfg", "openmw.cfg"} and f"manifest {manifest}".encode() not in data:
            raise BakeError(f"catalog manifest does not match pack: {name}")
    return manifest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    bake_parser = subparsers.add_parser("bake", help="bake and publish a content pack")
    bake_parser.add_argument("--openmw-config", type=pathlib.Path, action="append", required=True,
                             help="OpenMW config layer, in increasing priority order; repeatable")
    bake_parser.add_argument("--server-config", type=pathlib.Path, required=True,
                             help="TES3MP server config and catalog source")
    bake_parser.add_argument("--client-mappings", type=pathlib.Path, required=True,
                             help="client appearance and record mapping assignments")
    bake_parser.add_argument("--output", type=pathlib.Path, required=True,
                             help="manifest-addressed content pack root")
    bake_parser.add_argument("--derived-pack-recipe", type=pathlib.Path,
                             help="bounded record-selection recipe used to derive vanilla combat catalogs")
    verify_parser = subparsers.add_parser("verify", help="verify a previously baked pack")
    verify_parser.add_argument("pack", type=pathlib.Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "bake":
            manifest, path = bake(args.openmw_config, args.server_config, args.client_mappings, args.output,
                                  args.derived_pack_recipe)
            print(f"manifest {manifest}")
            print(f"pack {path}")
        else:
            manifest = verify_pack(args.pack)
            print(f"verified {manifest}")
        return 0
    except BakeError as exc:
        print(f"content bake failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
