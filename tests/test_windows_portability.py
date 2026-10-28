from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
# POSIX-only os functions a converter may reach in the Windows bundle.
POSIX_ONLY = re.compile(r"\bos\.(nice|fork|getuid|geteuid|killpg|setsid|getpgid|setpgrp|mkfifo)\(")
MAC_ONLY = {"mac_slots.py"}   # the Mac's scheduling queue, off outside macOS


class WindowsPortabilityTests(unittest.TestCase):
    def test_posix_only_calls_are_guarded(self):
        # The Windows bundle runs every converter: a bare os.nice() in
        # a worker failed the rims step on a real Windows generation.
        unguarded = []
        for path in sorted((ROOT / "tools").glob("*.py")) + [ROOT / "build_game.py"]:
            if path.name in MAC_ONLY:
                continue
            lines = path.read_text(encoding="utf-8").splitlines()
            for i, line in enumerate(lines):
                m = POSIX_ONLY.search(line)
                if not m or line.lstrip().startswith("#"):
                    continue
                context = "\n".join(lines[max(0, i - 12):i + 1])
                name = m.group(1)
                guarded = (f"hasattr(os, '{name}')" in context or f'hasattr(os, "{name}")' in context
                           or "os.name" in context or "sys.platform" in context)
                if not guarded:
                    unguarded.append(f"{path.relative_to(ROOT)}:{i + 1}: {line.strip()}")
        self.assertEqual(unguarded, [])


if __name__ == "__main__":
    unittest.main()
