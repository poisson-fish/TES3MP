#!/usr/bin/env python3
"""Exercise class dialogs with MyGUI mouse injection in a fresh V78 runtime.

Name/race setup uses coordinator choices; class/back/question/next buttons use
hit testing and ordinary GUI events. Does not reset saves or existing clients.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def capture(args):
    args.output.mkdir(parents=True, exist_ok=False)
    settings = dict(line.split('=', 1) for line in (args.runtime / 'server.cfg').read_text().splitlines()
                    if '=' in line and not line.startswith('#'))
    env = os.environ.copy()
    deps = Path(__file__).resolve().parents[1] / 'deps/installed/x64-windows/bin'
    env['PATH'] = os.pathsep.join((str(args.binary), str(deps), env.get('PATH', '')))
    env['OSG_LIBRARY_PATH'] = str(deps / 'osgPlugins-3.6.5')
    startup = args.output / 'startup.txt'
    startup.write_text('StopScript CharGen\nset CharGenState to -1\nEnablePlayerControls\n')
    evidence = args.output / 'Alice.ndjson'
    processes, logs = [], []
    sequence = 0

    def launch(role, command):
        handles = [(args.output / (role + '.' + stream + '.log')).open('wb') for stream in ('stdout', 'stderr')]
        logs.extend(handles)
        processes.append(subprocess.Popen(command, cwd=args.binary, env=env, stdout=handles[0], stderr=handles[1],
                         creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0))

    def rows(event):
        if not evidence.exists():
            return []
        return [v for line in evidence.read_text().split('\n')[:-1] if line
                if (v := json.loads(line)).get('event') == event]

    def wait(predicate, description, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            if any(p.poll() is not None for p in processes) or any(v.get('success') is False for v in rows('phase8_desktop_complete')):
                raise RuntimeError('Process failed: ' + description)
            time.sleep(.1)
        raise RuntimeError('Timed out: ' + description)

    def command(action, event='traversal_ui'):
        nonlocal sequence
        sequence += 1
        pending = args.output / 'control.pending'
        pending.write_text(f'{sequence} {action}\n')
        for attempt in range(100):
            try:
                pending.replace(Path(str(evidence) + '.control'))
                break
            except PermissionError:
                if attempt == 99:
                    raise
                time.sleep(.01)
        result = wait(lambda: next((v for v in rows(event) if v.get('sequence') == sequence), None), action)
        time.sleep(.15)
        return result

    def click(caption):
        state = command('uistate')
        matches = [b for b in state['buttons'] if caption.lower() in b['caption'].lower()]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one button {caption!r}: {state}')
        b = matches[0]
        return command(f'uiclick {b["x"]} {b["y"]}')

    try:
        launch('server', [str(args.server), str(args.runtime / 'server.cfg')])
        wait(lambda: b'server started' in (args.output / 'server.stdout.log').read_bytes(), 'server readiness', 180)
        user = args.runtime / 'Alice-user'
        launch('Alice', [str(args.binary / 'openmw.exe'), '--tes3mp-enable=1', '--tes3mp-host=127.0.0.1',
            f'--tes3mp-port={settings["port"].strip()}', '--tes3mp-timeout-ms=15000',
            f'--tes3mp-password-file={args.runtime / "join-password.txt"}',
            f'--tes3mp-player-credential-file={args.runtime / "Alice.credential"}', f'--config={user}',
            '--replace=config', f'--user-data={user}', f'--resources={args.binary / "resources"}',
            f'--start={args.cell}', '--skip-menu=1', '--new-game=0', '--no-grab=1', '--no-sound=1',
            '--script-console=1', f'--script-run={startup}', '--tes3mp-automation-role=native-traversal',
            f'--tes3mp-automation-output={evidence}'])
        first = wait(lambda: rows('character_profile_confirmed'), 'fresh profile')[-1]
        if first['lifecycle'] != 1:
            raise RuntimeError('Requires fresh profile; refuses to change an established character')
        for action, phase in [('name "UI test"', 2), ('race "Breton" 0 "b_n_breton_f_head_01" "b_n_breton_f_hair_01"', 3)]:
            command(action, 'character_choice_submitted')
            wait(lambda: rows('character_profile_confirmed')[-1]['phase'] == phase, 'setup confirmation')
        # Keep a review mode underneath, matching the manual playtest launcher.
        command('uimode review')
        command('uimode class')
        click('create')
        if args.route == 'create':
            command('uitext "UI custom class"')
        command('uirefresh')
        if args.route != 'create':
            click('Back')
        if args.route == 'pick':
            click('pick')
        elif args.route == 'generate':
            click('answer')
            for _ in range(10):
                command('uirefresh')
                state = command('uistate')
                buttons = [b for b in state['buttons'] if b['caption']]
                # Question responses are dynamic, but all have normal mouse targets.
                if len(buttons) != 3:
                    raise RuntimeError(f'Question has stray/inaccessible dialog buttons: {state}')
                b = buttons[0]
                command(f'uiclick {b["x"]} {b["y"]}')
        command('uirefresh')
        click('Accept this Class' if args.route == 'generate' else 'Next')
        wait(lambda: rows('character_profile_confirmed')[-1]['phase'] == 4, 'class confirmation')
        time.sleep(.5)
        state = command('uistate')
        if state['mode'] != 23:
            raise RuntimeError(f'Class acknowledgement did not leave stable birthsign mode: {state}')
        # A click after multiple frames/refreshes must still reach the active modal.
        command('uirefresh')
        click('Next')
        wait(lambda: rows('character_profile_confirmed')[-1]['lifecycle'] == 3, 'birthsign and completion')
        summary = dict(success=True, route=args.route, class_gui_mouse_clicks=True, custom_back=args.route != 'create',
                       visibility_refresh=True, birthsign_gui_mouse_click=True, established=True,
                       physical_os_mouse=False)
        (args.output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(f'PASS chargen UI {args.route}: {args.output}')
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        for handle in logs:
            handle.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'binary', 'server', 'output'):
        parser.add_argument('--' + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument('--cell', required=True)
    parser.add_argument('--route', choices=('pick', 'generate', 'create'), default='pick')
    capture(parser.parse_args())
