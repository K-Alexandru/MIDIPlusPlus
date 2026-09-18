"""Zips the source that builds QuartzMIDI.exe, and nothing else.

    python tools/make-source.py            # HEAD
    python tools/make-source.py 9fa4605    # a commit
    python tools/make-source.py --no-build # skip building the result

Output: build/release/QuartzMIDI-source-<commit>.zip, with its SHA256. The
zip is then unpacked elsewhere and built through its own QuartzMIDI.sln; a
zip that does not build is removed.

What goes in: ui/, MIDI++/, third_party/, x64/Release/config.json and
LICENSE, from `git archive`, so nothing untracked can leak, plus a
QuartzMIDI.sln and a BUILD.txt written here. What stays out: every document,
the tests, the tools (this one included), the old binary, and the original
window's project file, which is not what builds QuartzMIDI.
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
revisions = [argument for argument in sys.argv[1:] if not argument.startswith('--')]
commit = subprocess.check_output(['git', 'rev-parse', '--short', revisions[0] if revisions else 'HEAD'],
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

# The original window's project stays out. With no solution in the zip it was
# the file a tester opened, by its name, and Visual Studio opens a bare
# project on its first configuration: Debug|Win32, which has no /std:c++20
# and fails on its first header. QuartzMIDI needs MIDI++/'s sources, not its
# project. One solution at the top, holding the one project, is what opens.
for name in ('MIDI++.vcxproj', 'MIDI++.vcxproj.filters', 'MIDI++.vcxproj.user'):
    path = os.path.join(root, 'MIDI++', name)
    if os.path.exists(path):
        os.remove(path)

project_guid = re.search(r'<ProjectGuid>(\{[^}]+\})</ProjectGuid>',
                         open(os.path.join(root, 'ui', 'MIDIShell.vcxproj'), encoding='utf-8-sig').read()).group(1).upper()
with open(os.path.join(root, 'QuartzMIDI.sln'), 'w', encoding='utf-8-sig', newline='\r\n') as file:
    file.write('\n'.join([
        '',
        'Microsoft Visual Studio Solution File, Format Version 12.00',
        '# Visual Studio Version 17',
        'VisualStudioVersion = 17.0.31903.59',
        'MinimumVisualStudioVersion = 10.0.40219.1',
        'Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "QuartzMIDI", "ui\\MIDIShell.vcxproj", "' + project_guid + '"',
        'EndProject',
        'Global',
        '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution',
        '\t\tRelease|x64 = Release|x64',
        '\tEndGlobalSection',
        '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution',
        '\t\t' + project_guid + '.Release|x64.ActiveCfg = Release|x64',
        '\t\t' + project_guid + '.Release|x64.Build.0 = Release|x64',
        '\tEndGlobalSection',
        '\tGlobalSection(SolutionProperties) = preSolution',
        '\t\tHideSolutionNode = FALSE',
        '\tEndGlobalSection',
        'EndGlobal',
        '']))
with open(os.path.join(root, 'BUILD.txt'), 'w', encoding='utf-8', newline='\r\n') as file:
    file.write('\n'.join([
        'Building QuartzMIDI',
        '',
        '1. Install Visual Studio 2022 or later with the "Desktop development with C++" workload.',
        '2. Open QuartzMIDI.sln.',
        '3. Build. The configuration is Release x64.',
        '4. Run build\\shell\\QuartzMIDI.exe.',
        '',
        'It is built and tested with the v143 toolset (Visual Studio 2022).',
        'A later Visual Studio uses its own toolset unless v143 is installed:',
        'Individual components, "MSVC v143 - VS 2022 C++ x64/x86 build tools".',
        '',
        'From a command prompt: msbuild QuartzMIDI.sln /p:Configuration=Release /p:Platform=x64',
        '']))


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

# The zip is what a tester has, so the zip is what gets built: unpacked
# somewhere that is not this repository, through the solution they open. It
# was checked once by hand and then shipped three times unchecked.
if '--no-build' not in sys.argv:
    msbuild = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    vswhere = os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    if not os.path.exists(msbuild) and os.path.exists(vswhere):
        found = subprocess.run([vswhere, '-latest', '-products', '*', '-requires', 'Microsoft.Component.MSBuild',
                                '-find', r'MSBuild\**\Bin\MSBuild.exe'], capture_output=True, text=True).stdout.split('\n')
        msbuild = found[0].strip() if found and found[0].strip() else msbuild
    if not os.path.exists(msbuild):
        sys.exit('MSBuild was not found, so the zip was written but not built. Pass --no-build to accept that.')
    check = tempfile.mkdtemp(prefix='quartz-source-check-')
    with zipfile.ZipFile(out) as bundle:
        bundle.extractall(check)
    built = subprocess.run([msbuild, os.path.join(check, 'QuartzMIDI-source', 'QuartzMIDI.sln'), '/p:Configuration=Release',
                            '/p:Platform=x64', '/m', '/v:minimal', '/nologo'], capture_output=True, text=True)
    exe = os.path.join(check, 'QuartzMIDI-source', 'build', 'shell', 'QuartzMIDI.exe')
    ok = built.returncode == 0 and os.path.exists(exe)
    if not ok:
        print(built.stdout[-4000:])
    shutil.rmtree(check, ignore_errors=True)
    if not ok:
        os.remove(out)
        sys.exit('The zip did not build on its own, so it was removed.')
    print('Built  QuartzMIDI.exe from the zip, through QuartzMIDI.sln')

digest = hashlib.sha256(open(out, 'rb').read()).hexdigest().upper()
print('Zip   ', out, '({:,} bytes)'.format(os.path.getsize(out)))
print('Commit', commit)
print('SHA256', digest)
