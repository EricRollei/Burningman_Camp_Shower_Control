"""Embed and emit firmware identity for Tawdry OTA verification."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # PlatformIO/SCons injects this.


def git_output(repo: Path, *args: str) -> str:
  completed = subprocess.run(
      ["git", "-C", str(repo), *args],
      check=True,
      capture_output=True,
      text=True,
  )
  return completed.stdout.strip()


project_dir = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
repo = project_dir.parents[1]
commit = git_output(repo, "rev-parse", "--short=10", "HEAD")
branch = git_output(repo, "branch", "--show-current") or "detached HEAD"
dirty = bool(git_output(repo, "status", "--porcelain", "--untracked-files=normal"))

env.Append(  # type: ignore[name-defined]
    CPPDEFINES=[
        ("FIRMWARE_GIT_COMMIT", env.StringifyMacro(commit)),  # type: ignore[name-defined]
        ("FIRMWARE_GIT_DIRTY", 1 if dirty else 0),
    ]
)


def write_metadata(source, target, env) -> None:
  environment = env.subst("$PIOENV")
  profiles = {
      "shower1": (1, 0),
      "shower2": (2, 0),
      "water_fill": (3, 1),
      "rv_fill": (4, 2),
  }
  station_id, role = profiles[environment]
  firmware = Path(env.subst("$BUILD_DIR")) / f"{env.subst('$PROGNAME')}.bin"
  metadata = {
      "schema": 1,
      "environment": environment,
      "stationId": station_id,
      "role": role,
      "commit": commit,
      "dirty": dirty,
      "branch": branch,
      "firmware": firmware.name,
  }
  output = Path(env.subst("$BUILD_DIR")) / "firmware-metadata.json"
  output.write_text(json.dumps(metadata, sort_keys=True) + "\n", encoding="utf-8")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", write_metadata)  # type: ignore[name-defined]
