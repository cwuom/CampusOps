# Summarises the profilegpu dumps in a log: python gpuprof.py <log> [max depth] [min ms] [filter]
# For each dump and queue, prints the events down to the given depth that take at least min ms (inclusive);
# with a filter, only events whose name contains it (at any depth).
import re, sys

log = sys.argv[1]
max_depth = int(sys.argv[2]) if len(sys.argv) > 2 else 6
min_ms = float(sys.argv[3]) if len(sys.argv) > 3 else 0.15
only = sys.argv[4] if len(sys.argv) > 4 else None
sys.stdout.reconfigure(encoding="utf-8")
queue = ""
base = None
with open(log, encoding="utf-8-sig", errors="replace") as f:
	for line in f:
		if "LogRHI" not in line:
			continue
		m = re.search(r"Display: (\w+) pipeline", line)
		if m:
			queue = m.group(1)
			base = None
			print(f"--- {queue} {line[1:24]}")
			continue
		parts = line.rstrip("\r\n").split("\u2503")
		if len(parts) < 5:
			continue
		t = re.search(r"([0-9.]+) ms\s*$", parts[2])
		if not t:
			continue
		incl = float(t.group(1))
		name = parts[3].rstrip()
		indent = len(name) - len(name.lstrip())
		name = name.strip()
		if base is None:
			base = indent
		depth = (indent - base) // 3
		if only:
			if only in name and incl >= min_ms:
				print(f"{'  ' * depth}{incl:7.3f}  {name}")
		elif depth <= max_depth and incl >= min_ms:
			print(f"{'  ' * depth}{incl:7.3f}  {name}")
