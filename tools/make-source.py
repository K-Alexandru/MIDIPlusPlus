"""Zips the source that builds QuartzMIDI.exe, and nothing else.

    python tools/make-source.py            # HEAD
    python tools/make-source.py 9fa4605    # a commit

Output: build/release/QuartzMIDI-source-<commit>.zip, with its SHA256.

What goes in: ui/, MIDI++/, third_party/, x64/Release/config.json and
LICENSE, from `git archive`, so nothing untracked can leak. What stays out:
every document, the tests, the tools (this one included), the old binary.
Comments that cite the working docs by name are reworded, and a scan for
account names, assistant names and document names must find nothing or
the zip is not written.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
commit = subprocess.check_output(['git', 'rev-parse', '--short', sys.argv[1] if len(sys.argv) > 1 else 'HEAD'],
                                 cwd=repo, text=True).strip()
out = os.path.join(repo, 'build', 'release', 'QuartzMIDI-source-' + commit + '.zip')

work = tempfile.mkdtemp(prefix='quartz-source-')
root = os.path.join(work, 'tree')
os.makedirs(root)
archive = os.path.join(work, 'tree.tar')
with open(archive, 'wb') as file:
    subprocess.run(['git', 'archive', '--format=tar', commit], cwd=repo, stdout=file, check=True)
with tarfile.open(archive) as tar:
    tar.extractall(root, filter='data')

keep_dirs = ('ui', 'MIDI++', 'third_party', 'x64')
keep_files = ('LICENSE',)
for name in os.listdir(root):
    if name in keep_dirs or name in keep_files:
        continue
    path = os.path.join(root, name)
    shutil.rmtree(path) if os.path.isdir(path) else os.remove(path)
release = os.path.join(root, 'x64', 'Release')
for name in os.listdir(release):
    if name != 'config.json':
        os.remove(os.path.join(release, name))
for name in os.listdir(os.path.join(root, 'ui')):
    if name.endswith('.md'):
        os.remove(os.path.join(root, 'ui', name))


def rewrite(rel, pairs):
    path = os.path.join(root, rel)
    text = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert text.count(a) == 1, (rel, a)
        text = text.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(text)


rewrite(os.path.join('MIDI++', 'PlaybackCore.cpp'), [
    ('// Found by the panel seat on 2026-09-07 while making OutRange reachable.',
     '// Found on 2026-09-07 while making OutRange reachable.'),
])
rewrite(os.path.join('third_party', 'imgui', '.gitignore'), [('.claude\n', '')])

# Comments cite the working docs by name and section; the docs are not in
# the zip, so the citations become a plain phrase.
doc_ref = re.compile(r'(?:HANDOFF(?:\.md)?|CONTINUE-HERE\.md)(?: section \d+)?')
for top in ('MIDI++', 'ui'):
    for folder, _, files in os.walk(os.path.join(root, top)):
        for name in files:
            if os.path.splitext(name)[1].lower() not in ('.cpp', '.hpp', '.h', '.rc', '.py', '.ps1'):
                continue
            path = os.path.join(folder, name)
            raw = open(path, 'rb').read()
            encoding = 'utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8'
            try:
                text = raw.decode(encoding)
            except UnicodeDecodeError:
                encoding = 'cp1252'
                text = raw.decode(encoding)
            new = text.replace('HANDOFF.md section 4 forbids', 'the design notes forbid')
            new = doc_ref.sub('the design notes', new)
            if new != text:
                open(path, 'wb').write(new.encode(encoding))

forbidden = re.compile(r'k-alexandru|kailash|viner|\bKAV\b|claude|anthropic|astra\b|codex|gpt-|panel seat|engine seat|'
                       r'\bassistant\b|co-authored|CONTINUE-HERE|HANDOFF|\bSol 5', re.I)
hits = []
for folder, _, files in os.walk(root):
    for name in files:
        path = os.path.join(folder, name)
        if os.path.splitext(name)[1].lower() in ('.ttf', '.ico', '.png', '.exe', '.mid'):
            continue
        try:
            text = open(path, encoding='utf-8').read()
        except UnicodeDecodeError:
            text = open(path, encoding='latin-1').read()
        for match in forbidden.finditer(text):
            line = text.count('\n', 0, match.start()) + 1
            hits.append((os.path.relpath(path, root), line, text.splitlines()[line - 1].strip()[:120]))
for hit in hits:
    print('HIT', *hit)
if hits:
    shutil.rmtree(work, ignore_errors=True)
    sys.exit('The scan found the lines above; the zip was not written.')

os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(folder, name)
            bundle.write(path, 'QuartzMIDI-source/' + os.path.relpath(path, root).replace(os.sep, '/'))
shutil.rmtree(work, ignore_errors=True)
digest = hashlib.sha256(open(out, 'rb').read()).hexdigest().upper()
print('Zip   ', out, '({:,} bytes)'.format(os.path.getsize(out)))
print('Commit', commit)
print('SHA256', digest)
