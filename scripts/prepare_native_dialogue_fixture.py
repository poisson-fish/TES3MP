"""Prepare a fresh bounded V85 desktop dialogue campaign with stock stat filters.

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
for name in ('base-content', 'stock-config', 'profiles', 'content', 'runtime', 'binary', 'settings'):
    parser.add_argument('--' + name, type=lambda p: Path(p).resolve(), required=True)
args = parser.parse_args()
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
def script(name, source):
    return record(b'SCPT', sub(b'SCHD', name.encode().ljust(32, b'\0') + struct.pack('<IIIII', 0, 0, 0, 0, 0))
        + string(b'SCTX', source))
def info(name, previous, following, kind, text, result='', selects=(), status=None):
    data = struct.pack('<iibbbb', kind, 0, -1, -1, -1, 0)
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
for number, direction in ((91, 'north'), (92, 'south')):
    bank, topic, quest = f'ledger_{number}', f'{direction}_passage_{number}', f'{direction}_journey_{number}'
    blob += script(bank, f'begin {bank}\nlong calls\nend {bank}\n')
    blob += record(b'DIAL', string(b'NAME', topic) + sub(b'DATA', b'\0'))
    names = [f'{direction}_{suffix}' for suffix in ('high', 'low', 'take', 'wait')]
    # Native SCVR function 10 = PC Strength; 21 = PC Destruction.
    stat, function, threshold = ('Strength', 10, 40) if number == 91 else ('Destruction', 21, 20)
    for i, (comparison, description) in enumerate((('3', 'at least'), ('4', 'below'))):
        blob += info(names[i], names[i-1] if i else '', names[i+1], 0,
            text_prefix + f'Your {stat} is {description} {threshold}. The {direction} passage awaits.',
            f'set calls to calls + 1\nJournal {quest} 10\nMessageBox "Private {direction} notice"\nChoice "Take {direction}" 7 "Wait {direction}" 9',
            [('4JX0' + quest, 0), (f'1{function:02d}{comparison}', threshold)])
    for i, (choice, stage, outcome) in enumerate(((7, 20, 'taken'), (9, 30, 'declined')), 2):
        blob += info(names[i], names[i-1], names[i+1] if i+1 < len(names) else '', 0,
            text_prefix + f'Your {direction} passage is {outcome}.', f'set calls to calls + 1\nJournal {quest} {stage}',
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
    'npc_hit_female', 'npc_hit_beast', '1', '60:-100:1', str(args.settings),
    'player-physics', 'shared_script', shared, 'globals', '0'], check=True, cwd=binary)
native = (runtime / 'native.txt').read_text().replace('native-inventory-80', 'native-inventory-85')
native += 'personal_scripts 2 ' + ' '.join(stories) + '\n'
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
print('Prepared fresh V85 dialogue fixture: Strength >=40/<40; Destruction >=20/<20')
