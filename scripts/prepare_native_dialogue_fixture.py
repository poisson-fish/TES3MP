"""Prepare a fresh bounded V85 desktop dialogue campaign with stock dialogue filters.

The supplied peaceful NPC fixture, stock config and two profiles are local inputs;
content, credentials and campaigns stay under ignored build/.
"""
import argparse
import hashlib
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
from native_combat_families_capture import sub
from run_phase12_movement_capture import free_port

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--substitutions', action='store_true', help='Exercise private character/speaker/cell response expansion')
parser.add_argument('--disposition', action='store_true', help='Exercise stock disposition >=55/<55 instead of stat thresholds')
parser.add_argument('--encounter', action='store_true', help='V86 source-only Disable; base fixture must contain one dry static interior/NPC')
parser.add_argument('--return-path', action='store_true', help='Fresh V87 encounter with bounded shared-exploration return')
parser.add_argument('--enable-source', action='store_true', help='Fresh V88 stock source Enable at safe return')
parser.add_argument('--relocate-source', action='store_true', help='Fresh V89 inert relocation; --return-path selects V90 dormant destination return')
parser.add_argument('--life-events', action='store_true', help='Fresh V92 scheduled source health/OnDeath banks and wait UI')
parser.add_argument('--fair-opportunities', action='store_true', help='Fresh V93 next-life reservation and recovery proof')
for name in ('base-content', 'stock-config', 'profiles', 'content', 'runtime', 'binary', 'settings'):
    parser.add_argument('--' + name, type=lambda p: Path(p).resolve(), required=True)
args = parser.parse_args()
args.life_events |= args.fair_opportunities
if args.life_events and (args.return_path or args.enable_source or args.relocate_source):
    raise ValueError('Life-event proof requires the shared single-source scope')
if args.relocate_source and args.enable_source:
    raise ValueError('Relocation with source Enable is not admitted')
args.return_path |= args.enable_source
args.encounter |= args.return_path or args.relocate_source or args.life_events
content, runtime = args.content, args.runtime
content.mkdir()
shutil.copytree(args.base_content / 'meshes', content / 'meshes')
base_plugin = args.base_content / 'NpcDoors.esp'
if base_plugin.stat().st_size > 4 * 1024 * 1024:
    raise ValueError('Dialogue base fixture exceeds its byte budget')
blob = bytearray(base_plugin.read_bytes())
def record(tag, payload):
    return struct.pack('<4sIII', tag, len(payload), 0, 0) + payload
def string(tag, text):
    return sub(tag, text.encode() + b'\0')
if args.encounter:
    # The headless fixture omits lighting. Bind static ambient light so the
    # rendered visibility proof is readable; gameplay records remain stock.
    lit = bytearray()
    offset = 0
    while offset < len(blob):
        tag, size, unused, flags = struct.unpack_from('<4sIII', blob, offset)
        payload = blob[offset+16:offset+16+size]
        if len(payload) != size:
            raise ValueError('Truncated base fixture record')
        witness = tag == b'NPC_' and payload[:4] == b'NAME' and payload[8:8+struct.unpack_from('<I', payload, 4)[0]].rstrip(b'\0') == b'npc_door_actor'
        if tag == b'CELL' or witness:
            fields, cursor, ambient = bytearray(), 0, False
            while cursor < size:
                field, length = struct.unpack_from('<4sI', payload, cursor)
                value = payload[cursor+8:cursor+8+length]
                if len(value) != length:
                    raise ValueError('Truncated base fixture cell field')
                if tag == b'CELL' and field == b'AMBI':
                    value = struct.pack('<IIIf', 0x909090, 0, 0, .1)
                    ambient = True
                if witness and field == b'FNAM':
                    value = b'Encounter Witness\0'
                if witness and field == b'NPDT' and args.life_events:
                    if len(value) != 52:
                        raise ValueError('Life-event fixture needs explicit stock NPC stats')
                    value = bytearray(value)
                    struct.pack_into('<H', value, 38, 20)
                fields += sub(field, value)
                cursor += 8 + length
            if tag == b'CELL' and not ambient:
                fields += sub(b'AMBI', struct.pack('<IIIf', 0x909090, 0, 0, .1))
            payload = fields
        lit += struct.pack('<4sIII', tag, len(payload), unused, flags) + payload
        offset += 16 + size
    blob = lit
def script(name, source):
    return record(b'SCPT', sub(b'SCHD', name.encode().ljust(32, b'\0') + struct.pack('<IIIII', 0, 0, 0, 0, 0))
        + string(b'SCTX', source))
def info(name, previous, following, kind, text, result='', selects=(), status=None, disposition=0):
    data = struct.pack('<iibbbb', kind, disposition, -1, -1, -1, 0)
    payload = string(b'INAM', name) + string(b'PNAM', previous) + string(b'NNAM', following)
    payload += sub(b'DATA', data) + string(b'NAME', text)
    if kind == 0:
        payload += string(b'ONAM', 'npc_door_actor')
    for index, (rule, value) in enumerate(selects):
        payload += string(b'SCVR', str(index) + rule) + sub(b'INTV', struct.pack('<i', value))
    if result:
        payload += string(b'BNAM', result)
    if status:
        payload += sub(status, b'\1')
    return record(b'INFO', payload)
shared = 'dialogue_clock_91'
text_prefix = '%PCName (%PCRace, %PCClass), %name greets you in %cell. ' if args.substitutions else ''
blob += script(shared, f'begin {shared}\nend {shared}\n')
stories = []
for number, direction in (((91, 'north'),) if args.life_events else ((91, 'north'), (92, 'south'))):
    bank, topic, quest = f'ledger_{number}', f'{direction}_passage_{number}', f'{direction}_journey_{number}'
    if args.life_events:
        relay, credit = f'relay_{number}', f'credit_{number}'
        blob += record(b'GLOB', string(b'NAME', credit) + sub(b'FNAM', b'l') + sub(b'FLTV', struct.pack('<f', 0)))
        blob += script(bank, f'begin {bank}\nlong calls\nfloat timer\nfloat health\nfloat ratio\n'
            f'set calls to calls + 1\nset timer to timer + GetSecondsPassed\n'
            f'if OnDeath == 1\nset {credit} to {credit} + 1\nendif\n'
            f'set health to GetHealth\nset ratio to GetHealthGetRatio\nend {bank}\n')
        blob += script(relay, f'begin {relay}\nlong calls\nfloat health\nfloat ratio\n'
            f'set calls to calls + 1\nset health to npc_door_actor->GetHealth\n'
            f'set ratio to npc_door_actor->GetHealthGetRatio\nend {relay}\n')
        blob += record(b'DIAL', string(b'NAME', topic) + sub(b'DATA', b'\0'))
        blob += info(f'{direction}_credit', '', '', 0, f'Credited encounters: %{credit}.', 'set calls to calls')
        stories += [f'story {number} scripts 2 script "{bank}" running 1 script "{relay}" running 1 '
            f'globals 1 "{credit}" dialogues 1 "{topic}"']
        continue
    blob += script(bank, f'begin {bank}\nlong calls\nend {bank}\n')
    blob += record(b'DIAL', string(b'NAME', topic) + sub(b'DATA', b'\0'))
    names = [f'{direction}_{suffix}' for suffix in ('high', 'low', 'take', 'wait')]
    # Native SCVR function 10 = PC Strength; 21 = PC Destruction.
    stat, function, threshold = ('Strength', 10, 40) if number == 91 else ('Destruction', 21, 20)
    for i, (comparison, description) in enumerate((('3', 'at least'), ('4', 'below'))):
        if args.disposition:
            stat, threshold = 'disposition', 55
        blob += info(names[i], names[i-1] if i else '', names[i+1], 0,
            text_prefix + f'Your {stat} is {description} {threshold}. The {direction} passage awaits.',
            f'set calls to calls + 1\nJournal {quest} 10\nMessageBox "Private {direction} notice"\nChoice "Take {direction}" 7 "Wait {direction}" 9',
            [('4JX0' + quest, 0)] + ([] if args.disposition else [(f'1{function:02d}{comparison}', threshold)]),
            disposition=55 if args.disposition and i == 0 else 0)
    for i, (choice, stage, outcome) in enumerate(((7, 20, 'taken'), (9, 30, 'declined')), 2):
        blob += info(names[i], names[i-1], names[i+1] if i+1 < len(names) else '', 0,
            text_prefix + f'Your {direction} passage is {outcome}.', f'set calls to calls + 1\nJournal {quest} {stage}'
            + ('\nnpc_door_actor->PositionCell 200 -32 1 5400 "NPC Door Contact Test"'
               if args.relocate_source and choice == 7 else '\nDisable' if args.encounter and choice == 7 else ''),
            [('4JX0' + quest, 10), ('1500', choice)])
    blob += record(b'DIAL', string(b'NAME', quest) + sub(b'DATA', b'\4'))
    for i, stage in enumerate((10, 20, 30)):
        payload = string(b'INAM', f'{direction}_journal_{stage}') + string(b'PNAM', f'{direction}_journal_{(10,20,30)[i-1]}' if i else '')
        payload += string(b'NNAM', f'{direction}_journal_{(10,20,30)[i+1]}' if i < 2 else '')
        payload += sub(b'DATA', struct.pack('<iibbbb', 4, stage, -1, -1, -1, 0)) + string(b'NAME', 'A personal milestone.')
        if stage != 10: payload += sub(b'QSTF', b'\1')
        blob += record(b'INFO', payload)
    stories += [f'story {number} scripts 1 script "{bank}" running 0 globals 0 dialogues 2 "{topic}" "{quest}"']
(content / 'NpcDoors.esp').write_bytes(blob)
config = content / 'openmw'
config.mkdir()
(config / 'openmw.cfg').write_text(args.stock_config.read_text()
    + f'\ndata="{content.as_posix()}"\ncontent=NpcDoors.esp\n')
binary = args.binary
subprocess.run([str(binary / 'tes3mp_native_bootstrap.exe'), str(config), str(runtime), 'NPC Door Contact Test',
    *(['npc_door_actor'] * 2 if args.encounter else ['npc_hit_female', 'npc_hit_beast']), '1', '60:-100:1', str(args.settings),
    'player-physics', 'shared_script', shared, 'globals', '0'], check=True, cwd=binary)
native = (runtime / 'native.txt').read_text().replace('native-inventory-80',
    'native-inventory-93' if args.fair_opportunities else 'native-inventory-92' if args.life_events else 'native-inventory-90' if args.relocate_source and args.return_path else 'native-inventory-89' if args.relocate_source else 'native-inventory-88' if args.enable_source else 'native-inventory-87' if args.return_path else 'native-inventory-86' if args.encounter else 'native-inventory-85')
if args.life_events:
    native = native.replace('respawn 27000', 'respawn 900')
    native += 'starting_items 1\n"iron longsword" 1 16\n'
native += f'personal_scripts {len(stories)} ' + ' '.join(stories) + '\n'
prior = (runtime / 'manifest.txt').read_text().strip()
manifest = hashlib.sha256((prior + native).encode()).hexdigest()
for name in ('native.txt', 'server-content.cfg', 'client-content.cfg'):
    text = native if name == 'native.txt' else (runtime / name).read_text()
    (runtime / name).write_text(text.replace(prior, manifest))
(runtime / 'manifest.txt').write_text(manifest + '\n')
settings = (runtime / 'server-content.cfg').read_text()
settings += f'bind_address=127.0.0.1\nport={free_port()}\ntick_interval_ms=33\ndisconnect_grace_ms=30000\njoin_password_file=join-password.txt\nplayer_identity_file=players.txt\n'
(runtime / 'server.cfg').write_text(settings)
(runtime / 'join-password.txt').write_text(secrets.token_hex(24))
appearance = next(line.split('=',1)[1] for line in settings.splitlines() if line.startswith('default_appearance_id='))
rows = ['TES3MP_PLAYER_IDENTITIES_V5']
for i, role in enumerate(('Alice', 'Bob')):
    credential = secrets.token_bytes(32)
    (runtime / (role + '.credential')).write_bytes(credential)
    row = [str(i+1), str(i*2+1), appearance, manifest, hashlib.sha256(credential).hexdigest(), '0', '1', '1', '1', '-', '0', '0', '0']
    row += ['0'] * 35 + ['0', '0', role.encode().hex()]
    rows.append(' '.join(row))
    user = runtime / (role + '-user')
    user.mkdir()
    (user / 'openmw.cfg').write_text((config / 'openmw.cfg').read_text()+'\n'+(runtime / 'client-content.cfg').read_text())
(runtime / 'players.txt').write_text('\n'.join(rows)+'\n', newline='\n')
shutil.copyfile(args.profiles, runtime / 'profiles.json')
print('Prepared fresh ' + ('V93 reserved life' if args.fair_opportunities else 'V92 life events' if args.life_events else 'V90 relocation return' if args.relocate_source and args.return_path else 'V89 relocation encounter' if args.relocate_source else 'V88 Enable encounter' if args.enable_source else 'V87 return encounter' if args.return_path else 'V86 Disable encounter' if args.encounter else 'V85 dialogue fixture')
      + ': ' + ('stock health/OnDeath and durable waits' if args.life_events else 'disposition >=55/<55' if args.disposition else 'Strength >=40/<40; Destruction >=20/<20'))
