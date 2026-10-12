#!/usr/bin/env python3
"""Install Aura's wheel and Details iTerm2 profile, inheriting the default appearance."""
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
if path.exists():
    previous = json.loads(path.read_text())
    profiles = previous.get('Profiles', [])
    if len(profiles) != 1 or profiles[0].get('Name') != 'Aura CLI':
        raise SystemExit('Existing aura-cli.json is not an Aura profile; leaving it unchanged.')
    guid = profiles[0]['Guid']
profile = {
    'Name': 'Aura CLI', 'Guid': guid,
    'Custom Command': 'Yes',
    'Command': '/usr/bin/env AURA_MOUSE=click ' + shlex.quote(str(binary)),
    'Mouse Reporting': True,
    'Mouse Reporting allow mouse wheel': True,
    'Mouse Reporting allow clicks and drags': True,
    'Allow Alternate Mouse Scroll': False,
    'Automatically Enable Alternate Mouse Scroll': False,
    'Restrict Mouse Reporting to Alternate Screen Mode': True,
    'Disable Smcup Rmcup': False,
    'Allow Paste Bracketing': True,
    'Selection Color': {'Red Component': 196/255, 'Green Component': 196/255, 'Blue Component': 204/255, 'Color Space': 'sRGB'},
    'Selected Text Color': {'Red Component': 23/255, 'Green Component': 23/255, 'Blue Component': 25/255, 'Color Space': 'sRGB'},
    'Use Selected Text Color': True,
    'Drag to Scroll in Alternate Screen Mode Disabled': True,
}
# Omitted appearance settings inherit the user's default iTerm2 profile.
with tempfile.NamedTemporaryFile(mode='w', dir=folder, suffix='.json', delete=False) as stream:
    temporary = Path(stream.name)
    json.dump({'Profiles': [profile]}, stream, indent=2)
    stream.write('\n')
os.replace(temporary, path)
print('iTerm2 profile installed. Open Profiles > Aura CLI for wheel scrolling and clickable Details (Option-drag selects text).')
