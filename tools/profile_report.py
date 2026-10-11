#!/usr/bin/env python3
"""Map an sm50emu --profile histogram onto ELF symbols: profile_report.py prof.txt app.elf [N]"""
import bisect, subprocess, sys
prof, elf = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
nm = subprocess.run(["/opt/sh-elf/bin/sh-elf-nm", "-n", "-S", elf], capture_output=True, text=True).stdout
syms = []
for line in nm.splitlines():
    p = line.split()
    if len(p) >= 3 and p[-2] in "tT":
        syms.append((int(p[0], 16), p[-1]))
addrs = [a for a, _ in syms]
tot = {}
total = 0
for line in open(prof):
    a, c = line.split()
    a, c = int(a, 16), int(c)
    i = bisect.bisect_right(addrs, a) - 1
    name = syms[i][1] if i >= 0 else "?"
    tot[name] = tot.get(name, 0) + c
    total += c
for name, c in sorted(tot.items(), key=lambda x: -x[1])[:top]:
    print(f"{100.0 * c / total:6.2f}%  {c:12d}  {name}")
print(f"total {total}")
