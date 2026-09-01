import io
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

import firmware_uploader as uploader


class ProfileTests(unittest.TestCase):
  def test_all_six_profiles_map_to_expected_projects(self):
    self.assertEqual(
        [profile.environment for profile in uploader.PROFILES],
        ["shower1", "shower2", "water_fill", "rv_fill", "door1", "door2"],
    )
    self.assertTrue(all("shower-controller" in profile.project for profile in uploader.PROFILES[:4]))
    self.assertTrue(all("door-display" in profile.project for profile in uploader.PROFILES[4:]))

  def test_upload_command_uses_explicit_environment_and_port(self):
    command = uploader.upload_command("/usr/local/bin/pio", Path("/repo"), uploader.PROFILES[5], "/dev/cu.test")
    self.assertEqual(command[0:2], ["/usr/local/bin/pio", "run"])
    self.assertIn("/repo/firmware/door-display", command)
    self.assertEqual(command[-6:], ["--environment", "door2", "--target", "upload", "--upload-port", "/dev/cu.test"])

  def test_build_command_uses_selected_environment_without_upload_target(self):
    command = uploader.build_command("pio", Path("/repo"), uploader.PROFILES[2])
    self.assertEqual(command[-2:], ["--environment", "water_fill"])
    self.assertNotIn("upload", command)

  def test_tough_profiles_carry_station_identity(self):
    self.assertEqual(
        [(profile.station_id, profile.role) for profile in uploader.TOUGH_PROFILES],
        [(1, 0), (2, 0), (3, 1), (4, 2)],
    )

  def test_validate_profile_finds_declared_environment(self):
    with tempfile.TemporaryDirectory() as directory:
      repo = Path(directory)
      project = repo / "firmware" / "shower-controller"
      project.mkdir(parents=True)
      (project / "platformio.ini").write_text("[env:shower1]\n", encoding="utf-8")
      uploader.validate_profile(repo, uploader.PROFILES[0])

  def test_validate_profile_rejects_missing_environment(self):
    with tempfile.TemporaryDirectory() as directory:
      repo = Path(directory)
      project = repo / "firmware" / "shower-controller"
      project.mkdir(parents=True)
      (project / "platformio.ini").write_text("[env:something_else]\n", encoding="utf-8")
      with self.assertRaises(uploader.UploaderError):
        uploader.validate_profile(repo, uploader.PROFILES[0])


class DeviceTests(unittest.TestCase):
  def test_parse_and_filter_platformio_devices(self):
    payload = json.dumps([
        {"port": "/dev/cu.Bluetooth-Incoming-Port", "description": "n/a", "hwid": "n/a"},
        {"port": "/dev/cu.usbserial-123", "description": "USB Serial", "hwid": "USB VID:PID=1A86:55D4"},
        {"port": "/dev/cu.usbmodem456", "description": "Nano", "hwid": "USB VID:PID=303A:1001"},
    ])
    devices = [device for device in uploader.parse_device_list(payload) if uploader.is_usb_device(device)]
    self.assertEqual([device.port for device in devices], ["/dev/cu.usbserial-123", "/dev/cu.usbmodem456"])

  def test_parse_rejects_non_json(self):
    with self.assertRaises(uploader.UploaderError):
      uploader.parse_device_list("not json")

  @mock.patch("firmware_uploader.discover_usb_devices")
  def test_select_device_automatically_uses_only_usb_device(self, discover):
    expected = uploader.Device("/dev/cu.usbserial-1", "Tough", "USB VID:PID=1A86:55D4")
    discover.return_value = [expected]
    with redirect_stdout(io.StringIO()):
      self.assertEqual(uploader.select_device("pio"), expected)

  @mock.patch("firmware_uploader.input", side_effect=["2"])
  @mock.patch("firmware_uploader.discover_usb_devices")
  def test_select_device_prompts_when_multiple_are_connected(self, discover, _input):
    devices = [
        uploader.Device("/dev/cu.usbserial-1", "Tough", "first"),
        uploader.Device("/dev/cu.usbmodem-2", "Nano", "second"),
    ]
    discover.return_value = devices
    with redirect_stdout(io.StringIO()):
      self.assertEqual(uploader.select_device("pio"), devices[1])

  @mock.patch("firmware_uploader.input", side_effect=["m", "/dev/cu.custom"])
  @mock.patch("firmware_uploader.discover_usb_devices", return_value=[])
  def test_select_device_allows_manual_port_when_none_are_detected(self, _discover, _input):
    with redirect_stdout(io.StringIO()):
      device = uploader.select_device("pio")
    self.assertEqual(device.port, "/dev/cu.custom")
    self.assertEqual(device.description, "Manually entered port")

  @mock.patch("firmware_uploader.subprocess.run")
  def test_discover_reports_platformio_failure(self, run):
    run.return_value = subprocess.CompletedProcess([], 2, stdout="", stderr="port failure")
    with self.assertRaisesRegex(uploader.UploaderError, "port failure"):
      uploader.discover_usb_devices("pio")


class ProgressTests(unittest.TestCase):
  def test_upload_percentage_is_extracted_and_clamped(self):
    tracker = uploader.ProgressTracker()
    line, percent = tracker.process("\x1b[32mWriting at 0x001 (37 %)\x1b[0m")
    self.assertEqual(line, "Writing at 0x001 (37 %)")
    self.assertEqual(percent, 37)
    self.assertEqual(tracker.stage, "upload")
    _, percent = tracker.process("Writing at 0x002 (120 %)")
    self.assertEqual(percent, 100)

  def test_esptool_five_decimal_progress_is_extracted(self):
    tracker = uploader.ProgressTracker()
    _, percent = tracker.process("Writing at 0x00012000 [==========>]  42.4% 8192/1936649 bytes...")
    self.assertEqual(percent, 42)

  def test_dependency_download_percentage_does_not_drive_upload_bar(self):
    tracker = uploader.ProgressTracker()
    _, percent = tracker.process("Unpacking 0% 10% 20% 30% 40% 50% 100%")
    self.assertIsNone(percent)

  def test_non_progress_line_is_preserved(self):
    tracker = uploader.ProgressTracker()
    line, percent = tracker.process("Compiling src/main.cpp")
    self.assertEqual(line, "Compiling src/main.cpp")
    self.assertIsNone(percent)
    self.assertEqual(tracker.stage, "build")

  def test_renderer_draws_bar_and_finishes_line(self):
    stream = io.StringIO()
    renderer = uploader.UploadRenderer(stream, width=10)
    renderer.progress(50)
    renderer.finish()
    self.assertEqual(stream.getvalue(), "\rUploading [#####-----]  50%\n")

  def test_stream_upload_handles_carriage_return_progress(self):
    stream = io.StringIO()
    child = (
        "import sys; "
        "sys.stdout.write('Compiling src/main.cpp\\n'); "
        "sys.stdout.write('Writing at 0x1000 (42 %)\\r'); "
        "sys.stdout.write('Writing at 0x2000 (100 %)\\r')"
    )
    result = uploader.stream_upload([sys.executable, "-c", child], Path.cwd(), stream)
    self.assertEqual(result, 0)
    self.assertIn("Compiling src/main.cpp", stream.getvalue())
    self.assertIn("42%", stream.getvalue())
    self.assertIn("100%", stream.getvalue())


class VersionTests(unittest.TestCase):
  def test_version_marks_dirty_checkout(self):
    version = uploader.GitVersion("main", "abc123", True)
    self.assertEqual(version.display(), "main @ abc123 + uncommitted changes")

  @mock.patch("firmware_uploader.run_git")
  def test_git_version_reads_branch_commit_and_status(self, run_git):
    run_git.side_effect = ["feature", "0123456789", " M file"]
    version = uploader.git_version(Path("/repo"))
    self.assertEqual(version, uploader.GitVersion("feature", "0123456789", True))


class OtaTests(unittest.TestCase):
  def controller(self, **overrides):
    values = {
        "station_id": 1,
        "role": 0,
        "state": "idle",
        "message": "Ready",
        "max_image_bytes": 6_000_000,
        "uptime_ms": 100_000,
        "boot_id": 1234,
        "boot_pending": False,
        "commit": "0123456789",
        "dirty": False,
    }
    values.update(overrides)
    return uploader.OtaController(**values)

  def artifact(self, path=Path("/tmp/firmware.bin"), **overrides):
    values = {
        "path": path,
        "environment": "shower1",
        "station_id": 1,
        "role": 0,
        "commit": "abcdef0123",
        "dirty": False,
        "branch": "main",
        "size": 2_000_000,
        "sha256": "a" * 64,
    }
    values.update(overrides)
    return uploader.FirmwareArtifact(**values)

  def test_parse_ota_controller(self):
    payload = {
        "stationId": 3,
        "role": 1,
        "state": "armed",
        "message": "Ready for firmware upload",
        "maxImageBytes": 6_400_000,
        "uptimeMs": 1234,
        "bootId": 4567,
        "bootPending": True,
        "firmware": {"commit": "0123456789", "dirty": False},
    }
    target = uploader.parse_ota_controller(payload)
    self.assertEqual((target.station_id, target.role), (3, 1))
    self.assertTrue(target.boot_pending)

  def test_wrong_station_is_refused(self):
    with self.assertRaisesRegex(uploader.UploaderError, "selected Shower 1"):
      uploader.verify_ota_target(
          uploader.PROFILES[0], self.artifact(), self.controller(station_id=2)
      )

  def test_busy_station_is_refused(self):
    with self.assertRaisesRegex(uploader.UploaderError, "not ready"):
      uploader.verify_ota_target(
          uploader.PROFILES[0], self.artifact(), self.controller(state="uploading")
      )

  def test_oversize_image_is_refused(self):
    with self.assertRaisesRegex(uploader.UploaderError, "inactive OTA slot"):
      uploader.verify_ota_target(
          uploader.PROFILES[0], self.artifact(size=7_000_000), self.controller()
      )

  def test_load_artifact_checks_identity_and_hashes_binary(self):
    with tempfile.TemporaryDirectory() as directory:
      repo = Path(directory)
      build = repo / "firmware/shower-controller/.pio/build/shower1"
      build.mkdir(parents=True)
      binary = b"firmware bytes"
      (build / "firmware.bin").write_bytes(binary)
      metadata = {
          "environment": "shower1",
          "stationId": 1,
          "role": 0,
          "commit": "0123456789",
          "dirty": False,
          "branch": "main",
          "firmware": "firmware.bin",
      }
      (build / "firmware-metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
      artifact = uploader.load_firmware_artifact(repo, uploader.PROFILES[0])
      self.assertEqual(artifact.sha256, hashlib.sha256(binary).hexdigest())
      self.assertEqual(artifact.size, len(binary))

  @mock.patch("firmware_uploader.time.sleep")
  @mock.patch("firmware_uploader.read_ota_controller")
  @mock.patch("firmware_uploader.request_json")
  def test_prepare_waits_until_controller_is_armed(self, request_json, read_status, _sleep):
    request_json.return_value = {"ok": True, "token": "a" * 32}
    read_status.side_effect = [self.controller(state="preparing"), self.controller(state="armed")]
    token = uploader.prepare_ota("controller", self.artifact())
    self.assertEqual(token, "a" * 32)

  @mock.patch("firmware_uploader.http.client.HTTPConnection")
  def test_upload_streams_multipart_firmware(self, connection_type):
    response = mock.Mock(status=200)
    response.read.return_value = b'{"ok":true}'
    connection = connection_type.return_value
    connection.getresponse.return_value = response
    with tempfile.TemporaryDirectory() as directory:
      binary = b"firmware-payload" * 2048
      path = Path(directory) / "firmware.bin"
      path.write_bytes(binary)
      stream = io.StringIO()
      uploader.upload_ota("controller", self.artifact(path=path, size=len(binary)), "a" * 32, stream)
    sent = b"".join(call.args[0] for call in connection.send.call_args_list)
    self.assertIn(binary, sent)
    self.assertIn(b'name="firmware"', sent)
    self.assertIn("100%", stream.getvalue())

  @mock.patch("firmware_uploader.time.sleep")
  @mock.patch("firmware_uploader.read_ota_controller")
  def test_wait_requires_boot_health_confirmation(self, read_status, _sleep):
    read_status.side_effect = [
        self.controller(commit="abcdef0123", boot_pending=True, boot_id=5678, uptime_ms=1000),
        self.controller(commit="abcdef0123", boot_pending=False, boot_id=5678, uptime_ms=7000),
    ]
    result = uploader.wait_for_ota(
        "controller", uploader.PROFILES[0], self.artifact(), 1234, io.StringIO()
    )
    self.assertFalse(result.boot_pending)

  @mock.patch("firmware_uploader.read_ota_controller")
  def test_wait_reports_rollback(self, read_status):
    read_status.return_value = self.controller(commit="0123456789", boot_id=5678, uptime_ms=1000)
    with self.assertRaisesRegex(uploader.UploaderError, "rolled back"):
      uploader.wait_for_ota(
          "controller", uploader.PROFILES[0], self.artifact(), 1234, io.StringIO()
      )


if __name__ == "__main__":
  unittest.main()
