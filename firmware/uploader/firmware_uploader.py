#!/usr/bin/env python3
"""Interactive uploader for the camp's six PlatformIO firmware profiles."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence, TextIO


@dataclass(frozen=True)
class Profile:
  key: str
  label: str
  hardware: str
  project: str
  environment: str


@dataclass(frozen=True)
class Device:
  port: str
  description: str
  hardware_id: str


@dataclass(frozen=True)
class GitVersion:
  branch: str
  commit: str
  dirty: bool

  def display(self) -> str:
    suffix = " + uncommitted changes" if self.dirty else ""
    return f"{self.branch} @ {self.commit}{suffix}"


PROFILES: tuple[Profile, ...] = (
    Profile("shower1", "Shower 1", "M5Stack Tough", "firmware/shower-controller", "shower1"),
    Profile("shower2", "Shower 2", "M5Stack Tough", "firmware/shower-controller", "shower2"),
    Profile("water_fill", "Water Fill", "M5Stack Tough", "firmware/shower-controller", "water_fill"),
    Profile("rv_fill", "RV Fill", "M5Stack Tough", "firmware/shower-controller", "rv_fill"),
    Profile("door1", "Door 1 (matches Shower 1)", "M5NanoC6", "firmware/door-display", "door1"),
    Profile("door2", "Door 2 (matches Shower 2)", "M5NanoC6", "firmware/door-display", "door2"),
)

ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
UPLOAD_PERCENT = re.compile(
    r"writing at .*?(\d{1,3}(?:\.\d+)?)\s*%",
    re.IGNORECASE,
)


class UploaderError(RuntimeError):
  """An expected problem that should be shown without a traceback."""


def repository_root(script_path: Path | None = None) -> Path:
  path = (script_path or Path(__file__)).resolve()
  for parent in (path.parent, *path.parents):
    if (parent / ".git").exists():
      return parent
  raise UploaderError("Could not find the repository root from the uploader location.")


def run_git(repo: Path, *args: str) -> str:
  completed = subprocess.run(
      ["git", "-C", str(repo), *args],
      check=True,
      capture_output=True,
      text=True,
  )
  return completed.stdout.strip()


def git_version(repo: Path) -> GitVersion:
  try:
    branch = run_git(repo, "branch", "--show-current") or "detached HEAD"
    commit = run_git(repo, "rev-parse", "--short=10", "HEAD")
    dirty = bool(run_git(repo, "status", "--porcelain", "--untracked-files=normal"))
  except (OSError, subprocess.CalledProcessError) as exc:
    raise UploaderError(f"Could not read the firmware Git version: {exc}") from exc
  return GitVersion(branch, commit, dirty)


def parse_device_list(payload: str) -> list[Device]:
  try:
    items = json.loads(payload)
  except json.JSONDecodeError as exc:
    raise UploaderError("PlatformIO returned an unreadable device list.") from exc
  if not isinstance(items, list):
    raise UploaderError("PlatformIO returned an unexpected device list.")

  devices: list[Device] = []
  for item in items:
    if not isinstance(item, dict) or not isinstance(item.get("port"), str):
      continue
    devices.append(Device(
        port=item["port"],
        description=str(item.get("description") or "Unknown device"),
        hardware_id=str(item.get("hwid") or "Unknown hardware ID"),
    ))
  return devices


def is_usb_device(device: Device) -> bool:
  port = device.port.lower()
  metadata = f"{device.description} {device.hardware_id}".lower()
  return (
      port.startswith("/dev/cu.usb")
      or port.startswith("/dev/tty.usb")
      or port.startswith("com")
      or "vid:pid=" in metadata
      or " usb" in f" {metadata}"
  )


def discover_usb_devices(pio_executable: str) -> list[Device]:
  try:
    completed = subprocess.run(
        [pio_executable, "device", "list", "--json-output"],
        check=False,
        capture_output=True,
        text=True,
    )
  except OSError as exc:
    raise UploaderError(f"Could not run PlatformIO: {exc}") from exc
  if completed.returncode != 0:
    detail = completed.stderr.strip() or "unknown error"
    raise UploaderError(f"PlatformIO could not list serial devices: {detail}")
  return [device for device in parse_device_list(completed.stdout) if is_usb_device(device)]


def upload_command(pio_executable: str, repo: Path, profile: Profile, port: str) -> list[str]:
  return [
      pio_executable,
      "run",
      "--project-dir",
      str(repo / profile.project),
      "--environment",
      profile.environment,
      "--target",
      "upload",
      "--upload-port",
      port,
  ]


def validate_profile(repo: Path, profile: Profile) -> None:
  config = repo / profile.project / "platformio.ini"
  if not config.is_file():
    raise UploaderError(f"Missing PlatformIO project configuration: {config}")
  try:
    contents = config.read_text(encoding="utf-8")
  except OSError as exc:
    raise UploaderError(f"Could not read {config}: {exc}") from exc
  if f"[env:{profile.environment}]" not in contents:
    raise UploaderError(f"Environment '{profile.environment}' is not defined in {config}.")


class ProgressTracker:
  """Extract upload progress from PlatformIO/esptool output lines."""

  def __init__(self) -> None:
    self.stage = "build"
    self.percent: int | None = None

  def process(self, raw_line: str) -> tuple[str, int | None]:
    line = ANSI_ESCAPE.sub("", raw_line).strip()
    lower = line.lower()
    if "uploading" in lower or "esptool" in lower or "connecting" in lower:
      self.stage = "upload"
    match = UPLOAD_PERCENT.search(line)
    if match:
      self.stage = "upload"
      self.percent = max(0, min(100, round(float(match.group(1)))))
    return line, self.percent if match else None


class UploadRenderer:
  def __init__(self, stream: TextIO = sys.stdout, width: int = 34) -> None:
    self.stream = stream
    self.width = width
    self.progress_visible = False

  def progress(self, percent: int) -> None:
    filled = round(self.width * percent / 100)
    bar = "#" * filled + "-" * (self.width - filled)
    self.stream.write(f"\rUploading [{bar}] {percent:3d}%")
    self.stream.flush()
    self.progress_visible = True

  def log(self, line: str) -> None:
    if not line:
      return
    if self.progress_visible:
      self.stream.write("\n")
      self.progress_visible = False
    self.stream.write(f"{line}\n")
    self.stream.flush()

  def finish(self) -> None:
    if self.progress_visible:
      self.stream.write("\n")
      self.stream.flush()
      self.progress_visible = False


def stream_upload(command: Sequence[str], repo: Path, stream: TextIO = sys.stdout) -> int:
  tracker = ProgressTracker()
  renderer = UploadRenderer(stream)
  renderer.log("Building selected firmware (PlatformIO will upload after the build)...")
  try:
    process = subprocess.Popen(
        list(command),
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=0,
    )
  except OSError as exc:
    raise UploaderError(f"Could not start PlatformIO: {exc}") from exc

  assert process.stdout is not None
  pending: list[str] = []
  try:
    while True:
      char = process.stdout.read(1)
      if char == "" and process.poll() is not None:
        break
      if char in ("\r", "\n"):
        if pending:
          line, percent = tracker.process("".join(pending))
          pending.clear()
          if percent is not None:
            renderer.progress(percent)
          else:
            renderer.log(line)
      elif char:
        pending.append(char)
    if pending:
      line, percent = tracker.process("".join(pending))
      if percent is not None:
        renderer.progress(percent)
      else:
        renderer.log(line)
  except KeyboardInterrupt:
    renderer.finish()
    process.terminate()
    try:
      process.wait(timeout=5)
    except subprocess.TimeoutExpired:
      process.kill()
      process.wait()
    raise
  finally:
    renderer.finish()
    process.stdout.close()
  return process.wait()


def prompt_choice(
    prompt: str,
    choices: Sequence[object],
    label: Callable[[object], str],
    *,
    allow_refresh: bool = False,
) -> object | None:
  for index, choice in enumerate(choices, start=1):
    print(f"  {index}. {label(choice)}")
  if allow_refresh:
    print("  r. Refresh device list")
  print("  q. Quit")
  while True:
    answer = input(f"{prompt}: ").strip().lower()
    if answer == "q":
      return None
    if allow_refresh and answer == "r":
      return "refresh"
    try:
      index = int(answer) - 1
    except ValueError:
      index = -1
    if 0 <= index < len(choices):
      return choices[index]
    print("Please enter one of the choices shown.")


def select_profile() -> Profile | None:
  print("\nChoose the firmware profile:")
  selected = prompt_choice(
      "Profile",
      PROFILES,
      lambda item: f"{item.label} — {item.hardware}",
  )
  return selected if isinstance(selected, Profile) else None


def manual_device() -> Device | None:
  port = input("Serial port path (blank to cancel): ").strip()
  if not port:
    return None
  return Device(port, "Manually entered port", "Not reported by PlatformIO")


def select_device(pio_executable: str) -> Device | None:
  while True:
    devices = discover_usb_devices(pio_executable)
    if len(devices) == 1:
      device = devices[0]
      print(f"\nDetected USB device: {device.port} ({device.description})")
      return device
    if not devices:
      print("\nNo USB serial devices were detected.")
      answer = input("[r]efresh, enter a [m]anual port, or [q]uit: ").strip().lower()
      if answer == "q":
        return None
      if answer == "m":
        device = manual_device()
        if device:
          return device
      continue

    print("\nChoose the connected USB device:")
    selected = prompt_choice(
        "Device",
        devices,
        lambda item: f"{item.port} — {item.description} ({item.hardware_id})",
        allow_refresh=True,
    )
    if selected is None:
      return None
    if selected == "refresh":
      continue
    return selected if isinstance(selected, Device) else None


def confirm(profile: Profile, device: Device, version: GitVersion, dry_run: bool) -> bool:
  action = "Preview" if dry_run else "Upload"
  print("\nConfirm selection")
  print(f"  Firmware: {profile.label}")
  print(f"  Expected hardware: {profile.hardware}")
  print(f"  Source version: {version.display()}")
  print(f"  USB port: {device.port}")
  print(f"  Device: {device.description}")
  print("\nThe profile is authoritative; USB metadata cannot verify the station label.")
  return input(f"{action} this profile? [y/N]: ").strip().lower() == "y"


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Build and upload one of the six camp firmware profiles.",
  )
  parser.add_argument(
      "--dry-run",
      action="store_true",
      help="show the selected PlatformIO command without building or uploading",
  )
  return parser.parse_args(argv)


def ensure_prerequisites(repo: Path) -> str:
  pio_executable = shutil.which("pio")
  if not pio_executable:
    raise UploaderError("PlatformIO ('pio') is not on PATH. Install it before using the uploader.")
  for profile in PROFILES:
    validate_profile(repo, profile)
  return pio_executable


def main(argv: Sequence[str] | None = None) -> int:
  args = parse_args(argv)
  try:
    repo = repository_root()
    pio_executable = ensure_prerequisites(repo)
    starting_version = git_version(repo)
  except UploaderError as exc:
    print(f"Error: {exc}", file=sys.stderr)
    return 1

  print("Camp Firmware Uploader")
  print(f"Source: {starting_version.display()}")
  if args.dry_run:
    print("DRY RUN: no firmware will be built or uploaded.")

  while True:
    try:
      profile = select_profile()
      if profile is None:
        return 0
      device = select_device(pio_executable)
      if device is None:
        return 0
      version = git_version(repo)
      if not confirm(profile, device, version, args.dry_run):
        print("Cancelled; nothing was uploaded.")
        continue

      command = upload_command(pio_executable, repo, profile, device.port)
      if args.dry_run:
        print("\nCommand preview:")
        print(shlex.join(command))
        result = 0
      else:
        result = stream_upload(command, repo)

      if result == 0:
        message = "Dry run complete." if args.dry_run else f"Upload complete: {profile.label}."
        print(f"\n{message}")
      else:
        print(f"\nUpload failed with PlatformIO exit code {result}.", file=sys.stderr)

      answer = input("\nPress Enter to choose another device, or [q] to quit: ").strip().lower()
      if answer == "q":
        return result
    except UploaderError as exc:
      print(f"\nError: {exc}", file=sys.stderr)
      if input("Press Enter to try again, or [q] to quit: ").strip().lower() == "q":
        return 1
    except (EOFError, KeyboardInterrupt):
      print("\nCancelled; nothing else will be uploaded.")
      return 130


if __name__ == "__main__":
  raise SystemExit(main())
