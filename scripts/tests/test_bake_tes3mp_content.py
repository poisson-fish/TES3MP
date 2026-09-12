import hashlib
import json
import pathlib
import struct
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import bake_tes3mp_content as baker


ZERO_MANIFEST = "0" * 64


def subrecord(name: str, value: bytes) -> bytes:
    return struct.pack("<4sI", name.encode("ascii"), len(value)) + value


def record(name: str, *subrecords: bytes) -> bytes:
    payload = b"".join(subrecords)
    return struct.pack("<4sIII", name.encode("ascii"), len(payload), 0, 0) + payload


def named_record(kind: str, name: str, deleted: bool = False) -> bytes:
    values = [subrecord("NAME", name.encode("cp1252") + b"\0")]
    if deleted:
        values.append(subrecord("DELE", b"\0\0\0\0"))
    return record(kind, *values)


class ContentBakerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.data = self.root / "Data Files"
        self.source = self.root / "authoring"
        self.output = self.root / "output"
        self.data.mkdir()
        self.source.mkdir()

        self.actor_record = "rat"
        self.item_record = "iron dagger"
        self.actor_prototype = baker.stable_record_id(self.actor_record)
        self.item_prototype = baker.stable_record_id(self.item_record)
        self.esm = self.data / "Base.esm"
        self._write_esm()
        self.openmw_config = self.root / "openmw.cfg"
        self.openmw_config.write_text(
            f'data="{self.data}"\ncontent=Base.esm\n', encoding="utf-8"
        )

        (self.source / "collision.txt").write_text(
            "TES3MP_COLLISION_V1\n"
            f"manifest {ZERO_MANIFEST}\n"
            "cell interior 1\n",
            encoding="utf-8",
        )
        (self.source / "actors.txt").write_text(
            "TES3MP_ACTORS_V1\n"
            f"manifest {ZERO_MANIFEST}\n"
            f"actor 1 2 {self.actor_prototype} interior 1 0 0 0 0 0 0 idle\n",
            encoding="utf-8",
        )
        (self.source / "inventory.txt").write_text(
            "TES3MP_INVENTORY_V2\n"
            f"manifest {ZERO_MANIFEST}\n"
            f"prototype {self.item_prototype} 11 10 5 100 0 16 0 none 0\n",
            encoding="utf-8",
        )
        (self.source / "combat.txt").write_text(
            "TES3MP_COMBAT_V8\n"
            f"manifest {ZERO_MANIFEST}\n"
            "seed 1234\n"
            "settings 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 -90 90 1 1 1 0 100 1 1 1 30 .01 .01 .25 0 0\n"
            "magic_settings .1 .1\n"
            "security_settings -1 -1 1\n"
            "player_magic 50 10 0 0 0 0 0 0 0 0 0\n"
            "progression 1 1 1 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1\n"
            "player 50 50 50 1 0 0 20 20 20 20 20 25 100 50 20 50 100 0.1 0.2 20 20 20 20 500 0 20\n"
            "actor 1 20 20 10 0 0 0 0 0 0 0 0 1\n"
            f"weapon {self.item_prototype} 0 1 5 1 5 1 5 10 1 1\n",
            encoding="utf-8",
        )
        (self.source / "characters.txt").write_text(
            "TES3MP_CHARACTERS_V2\n"
            f"manifest {ZERO_MANIFEST}\n"
            f"starting_item {self.item_prototype} 2 -1\n",
            encoding="utf-8",
        )
        (self.source / "world.txt").write_text(
            "TES3MP_WORLD_V4\n"
            f"manifest {ZERO_MANIFEST}\n"
            "time 16 6 427 32400000 30000\n"
            "global 1 short 0\n"
            "weather_seed 1234\nweather 1\nweather_region 1 1 9000 300 1\n",
            encoding="utf-8",
        )
        (self.source / "vanilla-script-module.t3sm").write_bytes((
            "TES3MP_SCRIPT_MODULE_V1\n"
            "abi 1\n"
            "api 6\n"
            "entry joined\n"
            "callback 0 session_joined\n"
            "increment_integer 1 1\n"
            "end\n").encode("utf-8"))
        (self.source / "scripts.txt").write_text(
            "TES3MP_SCRIPT_PACKAGES_V2\n"
            f"manifest {ZERO_MANIFEST}\n"
            "package 1 1 0 6 1 vanilla-script-module.t3sm "
            "ff389bbabd319cd7e79b6b3ea4136f43fb70afbc04a30755bee9aeb8ebf1f24b joined 32\n"
            "variable 1 1 integer 0\n",
            encoding="utf-8",
        )
        self.password = self.root / "join-password.txt"
        self.password.write_text("", encoding="utf-8")
        self.server_config = self.source / "server.cfg"
        self.server_config.write_text(
            "bind_address = 127.0.0.1\n"
            "port = 25565\n"
            "tick_interval_ms = 16\n"
            "disconnect_grace_ms = 30000\n"
            f"join_password_file = {self.password}\n"
            f"content_manifest_id = {ZERO_MANIFEST}\n"
            "cell_spaces = interior:1\n"
            "allowed_cells = interior:1\n"
            "spawn_cell = interior:1\n"
            "spawn_positions = 0:0:0\n"
            "default_appearance_id = 100\n"
            "movement_profile = sneak:1;walk:2;run:3;jump:4\n"
            "collision_content_file = collision.txt\n"
            "actor_content_file = actors.txt\n"
            "inventory_content_file = inventory.txt\n"
            "combat_content_file = combat.txt\n"
            "character_content_file = characters.txt\n"
            "world_content_file = world.txt\n"
            "script_package_file = scripts.txt\n"
            f"player_identity_file = {self.root / 'players.txt'}\n",
            encoding="utf-8",
        )
        self.client_mappings = self.source / "client-mappings.cfg"
        self._write_client_mappings()

    def tearDown(self):
        self.temporary.cleanup()

    def _write_esm(self, extra: bytes = b"") -> None:
        self.esm.write_bytes(
            record("TES3", subrecord("HEDR", bytes(300)))
            + named_record("CELL", "Room")
            + named_record("CREA", self.actor_record)
            + named_record("NPC_", "player")
            + named_record("WEAP", self.item_record)
            + named_record("MISC", "wulfharth’s cup")
            + named_record("REGN", "Bitter Coast Region")
            + extra
        )

    def _write_plugin(self, name: str, *masters: str, extra: bytes = b"") -> pathlib.Path:
        path = self.data / name
        header = [subrecord("HEDR", bytes(300))]
        for master in masters:
            header.extend((subrecord("MAST", master.encode("cp1252") + b"\0"),
                           subrecord("DATA", bytes(8))))
        path.write_bytes(record("TES3", *header) + extra)
        return path

    def _write_client_mappings(self, item_id=None) -> None:
        if item_id is None:
            item_id = self.item_prototype
        self.client_mappings.write_text(
            "tes3mp-content-appearance-record=player\n"
            "tes3mp-content-cell-space-map=1=Room\n"
            f"tes3mp-content-actor-prototype-map={self.actor_prototype}={self.actor_record}\n"
            f"tes3mp-content-item-prototype-map={item_id}={self.item_record}\n"
            "tes3mp-content-weather-region-map=1=Bitter Coast Region\n"
            "tes3mp-content-weather-map=1=Clear\n",
            encoding="utf-8",
        )

    def _write_derived_esm(self, extra: bytes = b"", item_enchantment: str | None = None,
                           actor_spells: tuple[str, ...] = ()) -> None:
        attributes = [40, 30, 30, 40, 40, 40, 30, 40]
        skills = [5] * 27
        skills[22], skills[5], skills[4], skills[6], skills[7], skills[26] = 21, 22, 23, 24, 25, 26
        npc_data = struct.pack("<h8B27Bx3H3Bxi", 1, *attributes, *skills, 100, 0, 160, 50, 0, 0, 0)
        creature_data = struct.pack("<24i", 0, 1, 10, 5, 5, 20, 30, 10, 5, 10,
                                    23, 10, 60, 10, 30, 5, 20, 1, 2, 1, 2, 1, 2, 0)
        weapon_data = struct.pack("<fihHffH2B2B2Bi", 3.0, 10, 0, 400, 2.5, 1.0, 20,
                                  4, 5, 4, 5, 5, 5, 0)
        settings = {
            "fCombatInvisoMult": .2, "fFatigueAttackBase": 2., "fFatigueAttackMult": 0.,
            "fWeaponFatigueMult": .25, "fWeaponDamageMult": .1, "fDamageStrengthBase": .5,
            "fDamageStrengthMult": .1, "fMinHandToHandMult": .1, "fMaxHandToHandMult": .5,
            "fHandtoHandHealthPer": .1, "fCombatCriticalStrikeMult": 4.,
            "fCombatKODamageMult": 1.5, "fFatigueBase": 1.25, "fFatigueMult": .5,
            "fFatigueReturnBase": .02, "fFatigueReturnMult": .04, "fEndFatigueMult": .1,
            "fDifficultyMult": 5., "fCombatBlockLeftAngle": -60., "fCombatBlockRightAngle": 60.,
            "fSwingBlockMult": 1., "fSwingBlockBase": 1., "fBlockStillBonus": 1.25,
            "iBlockMinChance": 10., "iBlockMaxChance": 50., "fFatigueBlockBase": 2.,
            "fFatigueBlockMult": 3., "fWeaponFatigueBlockMult": .25,
            "iBaseArmorSkill": 30., "fUnarmoredBase1": .01, "fUnarmoredBase2": .01,
            "fCombatArmorMinMult": .25,
            "iShieldWeight": 15., "fLightMaxMod": .6, "fMedMaxMod": .9,
            "fMiscSkillBonus": 1., "fMinorSkillBonus": .75, "fMajorSkillBonus": .5,
            "fSpecialSkillBonus": .8, "fRestMagicMult": .15,
            "fElementalShieldMult": .1, "fDiseaseXferChance": 10.,
            "fPickLockMult": -1., "fTrapCostMult": -1.,
        }
        gmsts = b"".join(record("GMST", subrecord("NAME", name.encode() + b"\0"),
                                  subrecord("FLTV", struct.pack("<f", value)))
                          for name, value in settings.items())
        skill_records = b"".join(
            record("SKIL", subrecord("INDX", struct.pack("<i", index)),
                   subrecord("SKDT", struct.pack("<ii4f", 0, 0, 1.0 + index, 0, 0, 0)))
            for index in baker.PROGRESSION_SKILL_INDEXES
        )
        self.esm.write_bytes(record("TES3", subrecord("HEDR", bytes(300)))
                             + named_record("CELL", "Room")
                             + record("CREA", subrecord("NAME", b"rat\0"),
                                      subrecord("NPDT", creature_data),
                                      *(subrecord("NPCS", value.encode() + b"\0") for value in actor_spells),
                                      subrecord("FLAG", struct.pack("<i", 0x48)))
                             + record("NPC_", subrecord("NAME", b"player\0"),
                                      subrecord("NPDT", npc_data), subrecord("FLAG", struct.pack("<i", 8)))
                             + record("WEAP", subrecord("NAME", b"iron dagger\0"),
                                      subrecord("WPDT", weapon_data),
                                      *(tuple([subrecord("ENAM", item_enchantment.encode() + b"\0")])
                                        if item_enchantment else ()))
                             + named_record("REGN", "Bitter Coast Region")
                             + gmsts + skill_records + extra)

    def _write_recipe(self, **changes) -> pathlib.Path:
        recipe = {
            "format": baker.DERIVED_RECIPE_FORMAT,
            "seed": 1234,
            "player_record": "player",
            "items": ["iron dagger"],
            "actors": [{"actor_id": 1, "entity_id": 2, "record": "rat", "cell": "interior:1",
                        "position": [0, 0, 0], "orientation": [0, 0, 0]}],
            "collision_solids": [{"cell": "interior:1", "minimum": [100, 100, 100],
                                  "maximum": [200, 200, 200]}],
            "starting_items": [],
        }
        recipe.update(changes)
        path = self.source / "derived.json"
        path.write_text(json.dumps(recipe), encoding="utf-8")
        return path

    def _write_minimal_client_mappings(self) -> None:
        self.client_mappings.write_text(
            "tes3mp-content-appearance-record=player\n"
            "tes3mp-content-cell-space-map=1=Room\n"
            "tes3mp-content-weather-region-map=1=Bitter Coast Region\n"
            "tes3mp-content-weather-map=1=Clear\n", encoding="utf-8")

    def _bake(self):
        return baker.bake(
            [self.openmw_config], self.server_config, self.client_mappings, self.output
        )

    def test_repeatable_bake_publishes_one_immutable_verified_pack(self):
        manifest, path = self._bake()
        first = baker._tree_bytes(path)
        second_manifest, second_path = self._bake()

        self.assertEqual(manifest, second_manifest)
        self.assertEqual(path, second_path)
        self.assertEqual(first, baker._tree_bytes(second_path))
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(baker.verify_pack(path), manifest)
        metadata = __import__("json").loads(path.joinpath("pack.json").read_text())
        self.assertEqual(metadata["content_files"][0]["sha256"], hashlib.sha256(self.esm.read_bytes()).hexdigest())
        self.assertEqual(metadata["content_files"][0]["masters"], [])
        for name in ("server.cfg", "openmw.cfg", "collision.txt", "actors.txt", "inventory.txt", "combat.txt",
                     "characters.txt", "scripts.txt", "vanilla-script-module.t3sm"):
            self.assertIn(name, metadata["artifacts"])
        self.assertIn(f"content_manifest_id = {manifest}", path.joinpath("server.cfg").read_text())
        self.assertIn(f"tes3mp-content-manifest-id={manifest}", path.joinpath("openmw.cfg").read_text())

    def test_malformed_script_package_rejects_before_publication(self):
        (self.source / "scripts.txt").write_text(
            "TES3MP_SCRIPT_PACKAGES_V2\n"
            f"manifest {ZERO_MANIFEST}\n"
            "package 1 1 0 4 1 vanilla-script-module.t3sm "
            "8f8e49a1b09145a3ca84c70bc21838b85761db293e3afe476db513bad37627ca joined 32\n",
            encoding="utf-8",
        )

        with self.assertRaisesRegex(baker.BakeError, "script package"):
            self._bake()

        self.assertFalse(self.output.joinpath("CURRENT").exists())

    def test_script_package_upgrade_changes_manifest(self):
        first_manifest, _first_path = self._bake()
        scripts = self.source / "scripts.txt"
        scripts.write_text(scripts.read_text().replace("package 1 1 0 6", "package 1 2 0 6"), encoding="utf-8")

        second_manifest, second_path = self._bake()

        self.assertNotEqual(first_manifest, second_manifest)
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), second_manifest)
        self.assertEqual(baker.verify_pack(second_path), second_manifest)

    def test_layered_mod_loadout_records_validated_master_graph(self):
        plugin = self._write_plugin("Expansion.esp", "Base.esm")
        self.openmw_config.write_text(
            f'data="{self.data}"\ncontent=Base.esm\ncontent=Expansion.esp\n', encoding="utf-8"
        )

        _manifest, path = self._bake()

        metadata = json.loads(path.joinpath("pack.json").read_text())
        self.assertEqual(metadata["content_files"][1], {
            "name": "Expansion.esp",
            "size": plugin.stat().st_size,
            "sha256": hashlib.sha256(plugin.read_bytes()).hexdigest(),
            "masters": ["Base.esm"],
        })

    def test_missing_mod_master_rejects_before_publication(self):
        self._write_plugin("Expansion.esp", "Missing.esm")
        self.openmw_config.write_text(
            f'data="{self.data}"\ncontent=Base.esm\ncontent=Expansion.esp\n', encoding="utf-8"
        )

        with self.assertRaisesRegex(baker.BakeError, "missing TES3 master"):
            self._bake()

        self.assertFalse(self.output.joinpath("CURRENT").exists())

    def test_misordered_mod_master_rejects_before_publication(self):
        self._write_plugin("Expansion.esp", "Base.esm")
        self.openmw_config.write_text(
            f'data="{self.data}"\ncontent=Expansion.esp\ncontent=Base.esm\n', encoding="utf-8"
        )

        with self.assertRaisesRegex(baker.BakeError, "not ordered before dependent"):
            self._bake()

        self.assertFalse(self.output.joinpath("CURRENT").exists())

    def test_loadout_change_creates_a_new_pack_and_advances_current(self):
        first_manifest, first_path = self._bake()
        self._write_esm(named_record("MISC", "new record"))
        second_manifest, second_path = self._bake()

        self.assertNotEqual(first_manifest, second_manifest)
        self.assertTrue(first_path.is_dir())
        self.assertTrue(second_path.is_dir())
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), second_manifest)

    def test_invalid_mapping_fails_before_publication(self):
        manifest, path = self._bake()
        packs_before = sorted(item.name for item in path.parent.iterdir())
        self._write_client_mappings(self.item_prototype + 1)

        with self.assertRaises(baker.BakeError):
            self._bake()

        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(sorted(item.name for item in path.parent.iterdir()), packs_before)

    def test_dialogue_choice_mapping_is_exact_and_locally_unique(self):
        world = self.source / "world.txt"
        world.write_text(world.read_text(encoding="utf-8") + "dialogue_choice 40 unrestricted\n",
                         encoding="utf-8")
        self.client_mappings.write_text(
            self.client_mappings.read_text(encoding="utf-8")
            + "tes3mp-content-dialogue-choice-map=40=7\n", encoding="utf-8")
        manifest, path = self._bake()
        self.assertIn("tes3mp-content-dialogue-choice-map=40=7", path.joinpath("openmw.cfg").read_text())

        world.write_text(world.read_text(encoding="utf-8") + "dialogue_choice 41 unrestricted\n",
                         encoding="utf-8")
        self.client_mappings.write_text(
            self.client_mappings.read_text(encoding="utf-8")
            + "tes3mp-content-dialogue-choice-map=41=7\n", encoding="utf-8")
        with self.assertRaisesRegex(baker.BakeError, "duplicate local choice"):
            self._bake()
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)

    def test_weather_mappings_are_exact_and_reference_local_regions(self):
        mappings = self.client_mappings.read_text(encoding="utf-8")
        self.client_mappings.write_text(
            mappings.replace("tes3mp-content-weather-map=1=Clear\n", ""), encoding="utf-8")
        with self.assertRaisesRegex(baker.BakeError, "weather mapping does not match"):
            self._bake()
        self.assertFalse(self.output.joinpath("CURRENT").exists())

        self.client_mappings.write_text(
            mappings.replace("1=Bitter Coast Region", "1=Missing Region"), encoding="utf-8")
        with self.assertRaisesRegex(baker.BakeError, "missing winning region"):
            self._bake()
        self.assertFalse(self.output.joinpath("CURRENT").exists())

    def test_deleted_winning_record_rejects_a_stale_mapping(self):
        override = self.data / "Override.esp"
        override.write_bytes(
            record("TES3", subrecord("HEDR", bytes(300)))
            + named_record("WEAP", self.item_record, deleted=True)
        )
        self.openmw_config.write_text(
            f'data="{self.data}"\ncontent=Base.esm\ncontent=Override.esp\n', encoding="utf-8"
        )
        with self.assertRaisesRegex(baker.BakeError, "missing winning record"):
            self._bake()
        self.assertFalse(self.output.joinpath("CURRENT").exists())

    def test_verify_detects_artifact_tampering(self):
        _manifest, path = self._bake()
        path.joinpath("openmw.cfg").write_text("tampered\n", encoding="utf-8")
        with self.assertRaisesRegex(baker.BakeError, "digest mismatch"):
            baker.verify_pack(path)

    def test_verify_rejects_tampered_master_graph(self):
        _manifest, path = self._bake()
        metadata_path = path / "pack.json"
        metadata = json.loads(metadata_path.read_text())
        metadata["content_files"][0]["masters"] = ["Missing.esm"]
        metadata_path.write_text(json.dumps(metadata), encoding="utf-8")

        with self.assertRaisesRegex(baker.BakeError, "missing or misordered"):
            baker.verify_pack(path)

    def test_derived_pack_decodes_winning_tes3_values_and_generates_catalogs(self):
        self._write_derived_esm()
        recipe = self._write_recipe(starting_items=[{"record": "iron dagger", "count": 1, "slot": 16}])

        manifest, path = baker.bake(
            [self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)

        inventory = path.joinpath("vanilla-inventory.txt").read_text()
        combat = path.joinpath("vanilla-combat.txt").read_text()
        actors = path.joinpath("vanilla-actors.txt").read_text()
        collision = path.joinpath("vanilla-collision.txt").read_text()
        client = path.joinpath("openmw.cfg").read_text()
        server = path.joinpath("server.cfg").read_text()
        characters = path.joinpath("characters.txt").read_text()
        self.assertIn(f"manifest {manifest}", inventory)
        self.assertIn(f"prototype {self.item_prototype} 11 30 10 400 0 65536 0 none", inventory)
        self.assertIn("settings 0.200000003 2 0 0.25 0.100000001 0.5 0.100000001 0.100000001 0.5 0.100000001 4 1.5 1.25 0.5 0.0199999996 0.0399999991 0.100000001 5 -60 60 1 1 1.25 10 50 2 3 0.25 30 0.00999999978 0.00999999978 0.25 0 0", combat)
        self.assertIn("progression 1 0.75 0.5 0.800000012 0 1 0 21 0 6 0 5 0 7 0 8 0 27 0 22 0 3 0 4 0 18", combat)
        self.assertIn("security_settings -1 -1 19", combat)
        self.assertIn("player 40 40 40 1.25 0 0 21 22 23 24 25 26 160 40 5 30 0 0.0333333333 0.0375000015 5 5 5 5 5 2000 0", combat)
        self.assertIn("magic_settings 0.100000001 10", combat)
        self.assertIn("player_magic 30 5 0 0 0 0 0 0 0 0 0", combat)
        self.assertIn(f"weapon {self.item_prototype} 0 4 5 4 5 5 5 3 1 1", combat)
        self.assertIn("actor 1 23 60 6.25 0 0 0 0 0 0 0 0 1", combat)
        self.assertIn("actor_attack 1 20 10 10 1.25 30 60 1 2 1 2 1 2 1 10", combat)
        self.assertIn("actor_magic 1 5 0 0 0 0 0 0 0 0 0 0", combat)
        self.assertIn(f"actor 1 2 {self.actor_prototype} interior 1 0 0 0 0 0 0 idle", actors)
        self.assertIn("solid interior 1 100 100 100 200 200 200", collision)
        self.assertIn(f"tes3mp-content-item-prototype-map={self.item_prototype}=iron dagger", client)
        self.assertEqual(characters.count("starting_item "), 1)
        self.assertIn(f"starting_item {self.item_prototype} 1 16", characters)
        self.assertIn("inventory_content_file = vanilla-inventory.txt", server)
        self.assertIn("combat_content_file = vanilla-combat.txt", server)
        self.assertEqual(baker.verify_pack(path), manifest)

    def test_derived_pack_classifies_shield_feedback_from_openmw_weight_rules(self):
        shields = (("light shield", 8.0, 0), ("medium shield", 11.0, 1),
                   ("heavy shield", 14.0, 2))
        armor_records = b"".join(
            record("ARMO", subrecord("NAME", name.encode() + b"\0"),
                   subrecord("AODT", struct.pack("<ifiiii", 8, weight, 20, 100, 0, 10)))
            for name, weight, _skill in shields)
        self._write_derived_esm(armor_records)
        recipe = self._write_recipe(items=["iron dagger", *(name for name, _weight, _skill in shields)])

        _manifest, path = baker.bake(
            [self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)

        combat = path.joinpath("vanilla-combat.txt").read_text()
        inventory = path.joinpath("vanilla-inventory.txt").read_text()
        for name, weight, skill in shields:
            prototype = baker.stable_record_id(name)
            self.assertIn(f"armor {prototype} {skill} 10", combat)
            self.assertIn(f"prototype {prototype} 2 {round(weight * 10)} 20 100 0 131072 0 none", inventory)

    def test_derived_pack_bakes_on_strike_enchantment_and_actor_disease(self):
        enchantment = "fire bite"
        disease = "rat fever"
        effect = lambda effect_id, effect_range, magnitude: subrecord(
            "ENAM", struct.pack("<hbbiiiii", effect_id, -1, -1, effect_range, 0, 1, magnitude, magnitude))
        enchantment_record = record("ENCH", subrecord("NAME", enchantment.encode() + b"\0"),
                                    subrecord("ENDT", struct.pack("<4i", 1, 5, 40, 0)),
                                    effect(14, 1, 7))
        disease_record = record("SPEL", subrecord("NAME", disease.encode() + b"\0"),
                                subrecord("SPDT", struct.pack("<3i", 3, 0, 0)),
                                effect(23, 0, 3))
        self._write_derived_esm(enchantment_record + disease_record, enchantment, (disease,))

        _manifest, path = baker.bake([self.openmw_config], self.server_config, self.client_mappings,
                                     self.output, self._write_recipe())

        inventory = path.joinpath("vanilla-inventory.txt").read_text()
        combat = path.joinpath("vanilla-combat.txt").read_text()
        self.assertIn(f"prototype {self.item_prototype} 11 30 10 400 40 65536 0 none", inventory)
        self.assertIn(f"enchantment {self.item_prototype} 5 1 other fire 7 7", combat)
        self.assertIn(f"disease 1 {baker.stable_record_id(disease)} common 1 other health 3 3", combat)

    def test_interactive_trap_ids_are_extracted_for_exact_combat_coverage(self):
        interior = ("object", "1", "standard", "interior", "1", "0", "0", "0", "0", "0", "0",
                    "0", "none", "41")
        exterior = ("object", "2", "standard", "exterior", "2", "-1", "3", "0", "0", "0", "0",
                    "0", "0", "0", "0", "42")
        catalog = baker.Catalog("interactive_object_content_file", self.source / "objects.txt", "objects.txt", b"",
                                (interior, exterior))
        self.assertEqual(baker._interactive_object_trap_ids(catalog), {41, 42})

    def test_interactive_trap_profiles_are_derived_from_manifest_spells(self):
        name = "burning hand"
        effect = subrecord("ENAM", struct.pack("<hbbiiiii", 14, -1, -1, 1, 0, 1, 50, 50))
        spell = baker.Tes3Record("SPEL", name, False, (("NAME", name.encode() + b"\0"),
                                                       ("ENAM", effect[8:])))
        identifier = baker.stable_record_id(name)
        profiles = baker._trap_magic_profiles({("SPEL", name): spell}, {identifier})
        self.assertEqual(profiles, ((identifier, ("other", "fire", "50", "50")),))

    def test_derived_pack_missing_record_preserves_current_pointer(self):
        self._write_derived_esm()
        self._write_minimal_client_mappings()
        manifest, path = baker.bake([self.openmw_config], self.server_config, self.client_mappings,
                                    self.output, self._write_recipe())
        packs_before = sorted(item.name for item in path.parent.iterdir())
        recipe = self._write_recipe(items=["missing blade"])
        with self.assertRaisesRegex(baker.BakeError, "missing item record"):
            baker.bake([self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(sorted(item.name for item in path.parent.iterdir()), packs_before)

    def test_derived_pack_deleted_record_preserves_current_pointer(self):
        self._write_derived_esm()
        self._write_minimal_client_mappings()
        manifest, path = baker.bake([self.openmw_config], self.server_config, self.client_mappings,
                                    self.output, self._write_recipe())
        packs_before = sorted(item.name for item in path.parent.iterdir())
        self._write_derived_esm(named_record("WEAP", "iron dagger", deleted=True))
        recipe = self._write_recipe()
        with self.assertRaisesRegex(baker.BakeError, "deleted winning item record"):
            baker.bake([self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(sorted(item.name for item in path.parent.iterdir()), packs_before)

    def test_derived_pack_ambiguous_actor_preserves_current_pointer(self):
        self._write_derived_esm()
        self._write_minimal_client_mappings()
        manifest, path = baker.bake([self.openmw_config], self.server_config, self.client_mappings,
                                    self.output, self._write_recipe())
        packs_before = sorted(item.name for item in path.parent.iterdir())
        npc_data = struct.pack("<h8B27Bx3H3Bxi", 1, *([10] * 8), *([5] * 27), 20, 0, 40, 50, 0, 0, 0)
        self._write_derived_esm(record("NPC_", subrecord("NAME", b"rat\0"),
                                       subrecord("NPDT", npc_data), subrecord("FLAG", struct.pack("<i", 8))))
        recipe = self._write_recipe()
        with self.assertRaisesRegex(baker.BakeError, "ambiguous actor record"):
            baker.bake([self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(sorted(item.name for item in path.parent.iterdir()), packs_before)

    def test_derived_pack_unsupported_record_preserves_current_pointer(self):
        self._write_derived_esm()
        self._write_minimal_client_mappings()
        manifest, path = baker.bake([self.openmw_config], self.server_config, self.client_mappings,
                                    self.output, self._write_recipe())
        packs_before = sorted(item.name for item in path.parent.iterdir())
        self._write_derived_esm(named_record("CONT", "crate"))
        recipe = self._write_recipe(items=["crate"])
        with self.assertRaisesRegex(baker.BakeError, "unsupported item record"):
            baker.bake([self.openmw_config], self.server_config, self.client_mappings, self.output, recipe)
        self.assertEqual(self.output.joinpath("CURRENT").read_text().strip(), manifest)
        self.assertEqual(sorted(item.name for item in path.parent.iterdir()), packs_before)


if __name__ == "__main__":
    unittest.main()
