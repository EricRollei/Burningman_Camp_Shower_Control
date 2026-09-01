#!/usr/bin/env python3
"""Interactive uploader for the camp's six PlatformIO firmware profiles."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
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
  station_id: int | None = None
  role: int | None = None


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


@dataclass(frozen=True)
class FirmwareArtifact:
  path: Path
  environment: str
  station_id: int
  role: int
  commit: str
  dirty: bool
  branch: str
  size: int
  sha256: str


@dataclass(frozen=True)
class OtaController:
  station_id: int
  role: int
  state: str
  message: str
  max_image_bytes: int
  uptime_ms: int
  boot_id: int
  boot_pending: bool
  commit: str
  dirty: bool


PROFILES: tuple[Profile, ...] = (
    Profile("shower1", "Shower 1", "M5Stack Tough", "firmware/shower-controller", "shower1", 1, 0),
    Profile("shower2", "Shower 2", "M5Stack Tough", "firmware/shower-controller", "shower2", 2, 0),
    Profile("water_fill", "Water Fill", "M5Stack Tough", "firmware/shower-controller", "water_fill", 3, 1),
    Profile("rv_fill", "RV Fill", "M5Stack Tough", "firmware/shower-controller", "rv_fill", 4, 2),
    Profile("door1", "Door 1 (matches Shower 1)", "M5NanoC6", "firmware/door-display", "door1"),
    Profile("door2", "Door 2 (matches Shower 2)", "M5NanoC6", "firmware/door-display", "door2"),
)

TOUGH_PROFILES = PROFILES[:4]
DEFAULT_OTA_HOST = "192.168.4.1"

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


def build_command(pio_executable: str, repo: Path, profile: Profile) -> list[str]:
  return [
      pio_executable,
      "run",
      "--project-dir",
      str(repo / profile.project),
      "--environment",
      profile.environment,
  ]


def load_firmware_artifact(repo: Path, profile: Profile) -> FirmwareArtifact:
  build_dir = repo / profile.project / ".pio" / "build" / profile.environment
  metadata_path = build_dir / "firmware-metadata.json"
  try:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as exc:
    raise UploaderError(f"Could not read built firmware metadata: {metadata_path}") from exc
  if not isinstance(metadata, dict):
    raise UploaderError("Built firmware metadata has an unexpected format.")
  firmware_name = metadata.get("firmware")
  if not isinstance(firmware_name, str) or Path(firmware_name).name != firmware_name:
    raise UploaderError("Built firmware metadata contains an invalid filename.")
  firmware_path = build_dir / firmware_name
  try:
    size = firmware_path.stat().st_size
    sha256 = hashlib.sha256(firmware_path.read_bytes()).hexdigest()
  except OSError as exc:
    raise UploaderError(f"Could not read built firmware image: {firmware_path}") from exc
  expected = (profile.environment, profile.station_id, profile.role)
  actual = (metadata.get("environment"), metadata.get("stationId"), metadata.get("role"))
  if actual != expected:
    raise UploaderError(
        f"Built firmware identity {actual!r} does not match selected profile {expected!r}."
    )
  commit = metadata.get("commit")
  branch = metadata.get("branch")
  dirty = metadata.get("dirty")
  if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{10}", commit):
    raise UploaderError("Built firmware metadata contains an invalid Git commit.")
  if not isinstance(branch, str) or not isinstance(dirty, bool):
    raise UploaderError("Built firmware metadata contains an invalid Git version.")
  return FirmwareArtifact(
      firmware_path, profile.environment, profile.station_id, profile.role,
      commit, dirty, branch, size, sha256,
  )


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


def stream_build(command: Sequence[str], repo: Path, stream: TextIO = sys.stdout) -> int:
  stream.write("Building selected firmware before connecting to the controller Wi-Fi...\n")
  stream.flush()
  try:
    process = subprocess.Popen(
        list(command),
        cwd=repo,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
  except OSError as exc:
    raise UploaderError(f"Could not start PlatformIO: {exc}") from exc
  assert process.stdout is not None
  try:
    for line in process.stdout:
      stream.write(line)
      stream.flush()
  except KeyboardInterrupt:
    process.terminate()
    try:
      process.wait(timeout=5)
    except subprocess.TimeoutExpired:
      process.kill()
      process.wait()
    raise
  finally:
    process.stdout.close()
  return process.wait()


def ota_url(host: str, path: str) -> str:
  base = host if "://" in host else f"http://{host}"
  parsed = urllib.parse.urlsplit(base)
  if parsed.scheme != "http" or not parsed.netloc or parsed.path not in ("", "/"):
    raise UploaderError("OTA host must be a plain HTTP host or host:port.")
  return urllib.parse.urlunsplit(("http", parsed.netloc, path, "", ""))


def request_json(
    host: str,
    path: str,
    *,
    method: str = "GET",
    fields: dict[str, object] | None = None,
    timeout: float = 5.0,
) -> dict[str, object]:
  body = None
  headers: dict[str, str] = {}
  if fields is not None:
    body = urllib.parse.urlencode(fields).encode("ascii")
    headers["Content-Type"] = "application/x-www-form-urlencoded"
  request = urllib.request.Request(ota_url(host, path), data=body, headers=headers, method=method)
  try:
    with urllib.request.urlopen(request, timeout=timeout) as response:
      payload = response.read().decode("utf-8")
  except urllib.error.HTTPError as exc:
    try:
      detail = json.loads(exc.read().decode("utf-8")).get("message")
    except (UnicodeDecodeError, json.JSONDecodeError, AttributeError):
      detail = None
    raise UploaderError(str(detail or f"Controller returned HTTP {exc.code}.")) from exc
  except (urllib.error.URLError, TimeoutError, socket.timeout, OSError) as exc:
    raise UploaderError(f"Could not reach the Tough at {host}: {exc}") from exc
  try:
    decoded = json.loads(payload)
  except json.JSONDecodeError as exc:
    raise UploaderError("Controller returned an unreadable response.") from exc
  if not isinstance(decoded, dict):
    raise UploaderError("Controller returned an unexpected response.")
  return decoded


def parse_ota_controller(payload: dict[str, object]) -> OtaController:
  firmware = payload.get("firmware")
  if not isinstance(firmware, dict):
    raise UploaderError("Connected controller does not report OTA firmware identity.")
  try:
    controller = OtaController(
        station_id=int(payload["stationId"]),
        role=int(payload["role"]),
        state=str(payload["state"]),
        message=str(payload.get("message") or ""),
        max_image_bytes=int(payload["maxImageBytes"]),
        uptime_ms=int(payload["uptimeMs"]),
        boot_id=int(payload["bootId"]),
        boot_pending=bool(payload.get("bootPending")),
        commit=str(firmware["commit"]),
        dirty=bool(firmware["dirty"]),
    )
  except (KeyError, TypeError, ValueError) as exc:
    raise UploaderError("Connected controller returned incomplete OTA status.") from exc
  return controller


def read_ota_controller(host: str, timeout: float = 5.0) -> OtaController:
  return parse_ota_controller(request_json(host, "/api/ota/status", timeout=timeout))


def verify_ota_target(profile: Profile, artifact: FirmwareArtifact, target: OtaController) -> None:
  if profile.station_id is None or profile.role is None:
    raise UploaderError(f"{profile.label} does not support OTA updates.")
  if (target.station_id, target.role) != (profile.station_id, profile.role):
    raise UploaderError(
        f"Refusing update: selected {profile.label}, but the connected Tough reports "
        f"station {target.station_id}, role {target.role}."
    )
  if artifact.size > target.max_image_bytes:
    raise UploaderError(
        f"Firmware is {artifact.size} bytes but the inactive OTA slot allows "
        f"{target.max_image_bytes} bytes."
    )
  if target.state not in ("idle", "error"):
    raise UploaderError(f"Controller is not ready for OTA: {target.state} — {target.message}")


def prepare_ota(host: str, artifact: FirmwareArtifact) -> str:
  response = request_json(
      host,
      "/api/ota/prepare",
      method="POST",
      fields={
          "stationId": artifact.station_id,
          "role": artifact.role,
          "size": artifact.size,
          "sha256": artifact.sha256,
          "commit": artifact.commit,
          "dirty": 1 if artifact.dirty else 0,
      },
  )
  token = response.get("token")
  if not isinstance(token, str) or not re.fullmatch(r"[0-9a-f]{32}", token):
    raise UploaderError("Controller returned an invalid OTA preparation token.")
  deadline = time.monotonic() + 10.0
  while time.monotonic() < deadline:
    status = read_ota_controller(host)
    if status.state == "armed":
      return token
    if status.state == "error":
      raise UploaderError(f"Controller could not prepare for OTA: {status.message}")
    time.sleep(0.25)
  raise UploaderError("Controller did not enter OTA-ready state within 10 seconds.")


def upload_ota(
    host: str,
    artifact: FirmwareArtifact,
    token: str,
    stream: TextIO = sys.stdout,
) -> None:
  boundary = f"----tawdry-{artifact.sha256[:24]}"
  prefix = (
      f"--{boundary}\r\n"
      f'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
      "Content-Type: application/octet-stream\r\n\r\n"
  ).encode("ascii")
  suffix = f"\r\n--{boundary}--\r\n".encode("ascii")
  parsed = urllib.parse.urlsplit(ota_url(host, "/api/ota/upload"))
  connection = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=30)
  renderer = UploadRenderer(stream)
  renderer.log("Uploading firmware to the inactive OTA slot...")
  try:
    connection.putrequest("POST", f"/api/ota/upload?token={urllib.parse.quote(token)}")
    connection.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
    connection.putheader("Content-Length", str(len(prefix) + artifact.size + len(suffix)))
    connection.endheaders()
    connection.send(prefix)
    sent = 0
    with artifact.path.open("rb") as firmware:
      while True:
        chunk = firmware.read(16384)
        if not chunk:
          break
        connection.send(chunk)
        sent += len(chunk)
        renderer.progress(round(sent * 100 / artifact.size))
    connection.send(suffix)
    response = connection.getresponse()
    payload = response.read().decode("utf-8")
    try:
      decoded = json.loads(payload)
    except json.JSONDecodeError as exc:
      raise UploaderError("Controller returned an unreadable upload result.") from exc
    if response.status != 200 or not isinstance(decoded, dict) or not decoded.get("ok"):
      detail = decoded.get("message") if isinstance(decoded, dict) else None
      raise UploaderError(str(detail or f"OTA upload failed with HTTP {response.status}."))
  except (OSError, http.client.HTTPException, socket.timeout) as exc:
    raise UploaderError(f"OTA connection failed during upload: {exc}") from exc
  finally:
    renderer.finish()
    connection.close()


def wait_for_ota(
    host: str,
    profile: Profile,
    artifact: FirmwareArtifact,
    previous_boot_id: int,
    stream: TextIO = sys.stdout,
) -> OtaController:
  stream.write("Waiting for the Tough to reboot and pass boot health checks...\n")
  stream.flush()
  deadline = time.monotonic() + 90.0
  while time.monotonic() < deadline:
    try:
      target = read_ota_controller(host, timeout=2.0)
    except UploaderError:
      time.sleep(1.0)
      continue
    if (target.station_id, target.role) != (profile.station_id, profile.role):
      raise UploaderError(
          f"Laptop reconnected to station {target.station_id}, role {target.role}, "
          f"instead of {profile.label}. Rejoin the intended controller Wi-Fi."
      )
    rebooted = target.boot_id != previous_boot_id
    if rebooted and target.commit == artifact.commit and target.dirty == artifact.dirty:
      if not target.boot_pending:
        return target
    elif rebooted:
      raise UploaderError(
          f"The controller rolled back to {target.commit} after the new image failed boot health checks."
      )
    time.sleep(1.0)
  raise UploaderError("The controller did not return with verified firmware within 90 seconds.")


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


def select_operation() -> str | None:
  print("\nChoose operation:")
  selected = prompt_choice(
      "Operation",
      ("ota", "usb"),
      lambda item: "Update nearby Tough over Wi-Fi" if item == "ota" else "Flash controller over USB",
  )
  return selected if isinstance(selected, str) else None


def select_profile(profiles: Sequence[Profile] = PROFILES) -> Profile | None:
  print("\nChoose the firmware profile:")
  selected = prompt_choice(
      "Profile",
      profiles,
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


def confirm_ota(profile: Profile, artifact: FirmwareArtifact, target: OtaController) -> bool:
  dirty = " + uncommitted changes" if artifact.dirty else ""
  print("\nConfirm Wi-Fi OTA update")
  print(f"  Target: {profile.label} (station {target.station_id}, role {target.role})")
  print(f"  Current firmware: {target.commit}{' + uncommitted changes' if target.dirty else ''}")
  print(f"  New firmware: {artifact.branch} @ {artifact.commit}{dirty}")
  print(f"  Image: {artifact.size / (1024 * 1024):.2f} MiB")
  print("  Safety state: controller reports idle; relays will be commanded off before upload")
  return input("Upload this firmware over Wi-Fi? [y/N]: ").strip().lower() == "y"


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Build and upload one of the six camp firmware profiles.",
  )
  parser.add_argument(
      "--dry-run",
      action="store_true",
      help="show the selected PlatformIO command without building or uploading",
  )
  parser.add_argument(
      "--ota-host",
      default=DEFAULT_OTA_HOST,
      help=f"controller host for Wi-Fi OTA (default: {DEFAULT_OTA_HOST})",
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
      operation = select_operation()
      if operation is None:
        return 0
      profile = select_profile(TOUGH_PROFILES if operation == "ota" else PROFILES)
      if profile is None:
        return 0

      if operation == "ota":
        command = build_command(pio_executable, repo, profile)
        if args.dry_run:
          print("\nCommand preview:")
          print(shlex.join(command))
          print(f"Then verify and upload to {ota_url(args.ota_host, '/api/ota/status')}")
          result = 0
        else:
          result = stream_build(command, repo)
          if result != 0:
            print(f"\nBuild failed with PlatformIO exit code {result}.", file=sys.stderr)
          else:
            artifact = load_firmware_artifact(repo, profile)
            print("\nBuild complete. Connect this laptop to the controller Wi-Fi.")
            input("Press Enter when connected: ")
            target = read_ota_controller(args.ota_host)
            verify_ota_target(profile, artifact, target)
            if not confirm_ota(profile, artifact, target):
              print("Cancelled; nothing was uploaded.")
              continue
            token = prepare_ota(args.ota_host, artifact)
            upload_ota(args.ota_host, artifact, token)
            verified = wait_for_ota(
                args.ota_host, profile, artifact, target.boot_id,
            )
            print(f"\nOTA update complete: {profile.label} is running {verified.commit}.")

        answer = input("\nPress Enter to choose another device, or [q] to quit: ").strip().lower()
        if answer == "q":
          return result
        continue

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
