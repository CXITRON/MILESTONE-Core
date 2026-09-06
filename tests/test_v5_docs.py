import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
header = (root / 'v5/libraries/MilestoneV5Core/src/MilestoneV5Version.h').read_text()
version = re.search(r'FIRMWARE_VERSION\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"', header).group(1)
agents = (root / 'AGENTS.md').read_text()
context = (root / 'MILESTONE_PROJECT_CONTEXT.md').read_text()
readme = (root / 'README.md').read_text()
development = readme.count(f'## v{version} 개발 현황')
released = readme.count(f'## v{version} 업데이트 안내')
assert development + released == 1
current = f'- Current firmware baseline: `{version}`' in agents
if current:
    assert f'- V5 firmware baseline: `{version}`' in agents
    assert f'> V5 firmware baseline: {version}' in context
else:
    assert f'- V5 development baseline: `{version}`' in agents
    assert f'> V5 development baseline: {version}' in context
assert (released == 1) if current else (development == 1)
versions = [tuple(map(int, value.split('.'))) for value in re.findall(r'^## v([0-9.]+) 업데이트 안내$', readme, re.M)]
assert versions == sorted(versions, reverse=True)
assert (root / 'v5/MilestoneV5Main/partitions.csv').read_bytes() == (root / 'v5/MilestoneV5Safe/partitions.csv').read_bytes()
release_v5 = (root / 'tools/release-v5.sh').read_text()
assets_v5 = (root / 'tools/v5-release-assets.py').read_text()
assert '0xe000 "$task_root/main/boot_app0.bin"' in release_v5
assert "Initial MAIN image does not select OTA slot A" in assets_v5
assert '계획한 v5 소프트웨어 경로는 모두 소스에 연결됐다' in (root / 'v5/IMPLEMENTATION_STATUS.md').read_text()
print(f'v5 documentation and shared partition contract passed ({version})')
