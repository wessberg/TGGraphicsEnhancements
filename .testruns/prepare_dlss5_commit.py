from pathlib import Path
import difflib
import subprocess

def head_text(path):
    return subprocess.check_output(['git', 'show', 'HEAD:' + path]).decode('utf-8').replace('\r\n', '\n')

def work_text(path):
    return Path(path).read_text(encoding='utf-8')

changes = []
path = 'BACKLOG.md'
before = head_text(path)
current = work_text(path)
entry = next(line for line in current.splitlines() if line.startswith('- 2026-10-07 (round-07):'))
continuation = current.split(entry + '\n', 1)[1].splitlines()[0]
anchor = 'Frame generation, upscaling, bridge:\n'
assert before.count(anchor) == 1
after = before.replace(anchor, anchor + entry + '\n' + continuation + '\n', 1)
changes.append((path, before, after))

path = 'ROUNDS.md'
before = head_text(path)
row = next(line for line in work_text(path).splitlines() if line.startswith('## [Round 7]'))
anchor = 'Each round\'s file ends with a "▶ Starter prompt" for the next session. A round is one working session or day.\n'
assert before.count(anchor) == 1
after = before.replace(anchor, anchor + '\n' + row + '\n', 1)
changes.append((path, before, after))

path = 'docs/PROJECT-CONTEXT.md'
before = head_text(path)
current = work_text(path)
anchor = '- DLSS 5 with tracing:'
old_start = before.index(anchor)
old_end = before.index('\n\n', old_start)
new_start = current.index(anchor)
new_end = current.index('\n\n', new_start)
after = before[:old_start] + current[new_start:new_end] + before[old_end:]
changes.append((path, before, after))

patch = ''.join(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), fromfile='a/' + path, tofile='b/' + path)) for path, old, new in changes)
Path('.testruns/dlss5-docs.patch').write_bytes(patch.encode('utf-8'))
print(patch)
