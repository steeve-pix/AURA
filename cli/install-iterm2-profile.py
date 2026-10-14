#!/usr/bin/env python3
"""Install Aura's native-selection and wheel iTerm2 profile, inheriting the default appearance."""
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile
import uuid

binary = Path(sys.argv[1]).resolve()
folder = Path.home() / 'Library/Application Support/iTerm2/DynamicProfiles'
folder.mkdir(parents=True, exist_ok=True)
path = folder / 'aura-cli.json'
guid = str(uuid.uuid4())
keyboard_map = {}
if path.exists():
    previous = json.loads(path.read_text())
    profiles = previous.get('Profiles', [])
    if len(profiles) != 1 or profiles[0].get('Name') != 'Aura CLI':
        raise SystemExit('Existing aura-cli.json is not an Aura profile; leaving it unchanged.')
    guid = profiles[0]['Guid']
    keyboard_map = dict(profiles[0].get('Keyboard Map', {}))
# Explicit mappings keep Mac editing independent of the inherited key preset.
keyboard_map.update({
    '0xd-0x20000': {'Action': 10, 'Text': '[13;2u'},
    '0xf702-0x80000': {'Action': 10, 'Text': '[1;3D'},
    '0xf703-0x80000': {'Action': 10, 'Text': '[1;3C'},
    '0xf702-0xa0000': {'Action': 10, 'Text': '[1;4D'},
    '0xf703-0xa0000': {'Action': 10, 'Text': '[1;4C'},
    '0xf702-0x20000': {'Action': 10, 'Text': '[1;2D'},
    '0xf703-0x20000': {'Action': 10, 'Text': '[1;2C'},
    '0x7f-0x80000': {'Action': 10, 'Text': '\x7f'},
    '0xf728-0x80000': {'Action': 10, 'Text': '[3;3~'},
    '0xf702-0x100000': {'Action': 10, 'Text': '[1;9D'},
    '0xf703-0x100000': {'Action': 10, 'Text': '[1;9C'},
    '0xf702-0x120000': {'Action': 10, 'Text': '[1;10D'},
    '0xf703-0x120000': {'Action': 10, 'Text': '[1;10C'},
    '0x7f-0x100000': {'Action': 10, 'Text': '[127;9u'},
    '0x61-0x100000': {'Action': 10, 'Text': '[97;9u'},
    '0x63-0x100000': {'Action': 69, 'Text': ''},
    '0x7a-0x100000': {'Action': 10, 'Text': '[122;9u'},
    '0x7a-0x120000': {'Action': 10, 'Text': '[122;10u'},
    '0x7a-0x60000': {'Action': 10, 'Text': '[122;6u'},
})
profile = {
    'Keyboard Map': keyboard_map,
    'Name': 'Aura CLI', 'Guid': guid,
    'Custom Command': 'Yes',
    'Command': '/usr/bin/env AURA_MOUSE=wheel ' + shlex.quote(str(binary)),
    'Mouse Reporting': True,
    'Mouse Reporting allow mouse wheel': True,
    'Mouse Reporting allow clicks and drags': False,
    'Allow Alternate Mouse Scroll': False,
    'Automatically Enable Alternate Mouse Scroll': False,
    'Restrict Mouse Reporting to Alternate Screen Mode': True,
    'Disable Smcup Rmcup': False,
    'Allow Paste Bracketing': True,
    'Selection Color': {'Red Component': 184/255, 'Green Component': 178/255, 'Blue Component': 204/255, 'Color Space': 'sRGB'},
    'Selected Text Color': {'Red Component': 25/255, 'Green Component': 25/255, 'Blue Component': 28/255, 'Color Space': 'sRGB'},
    'Use Selected Text Color': True,
    'Drag to Scroll in Alternate Screen Mode Disabled': True,
}
# Omitted appearance settings inherit the user's default iTerm2 profile.
with tempfile.NamedTemporaryFile(mode='w', dir=folder, suffix='.json', delete=False) as stream:
    temporary = Path(stream.name)
    json.dump({'Profiles': [profile]}, stream, indent=2)
    stream.write('\n')
os.replace(temporary, path)
print('iTerm2 profile installed. Open Profiles > Aura CLI for wheel scrolling and native drag selection (Cmd+C copies, Ctrl+O opens Details).')
