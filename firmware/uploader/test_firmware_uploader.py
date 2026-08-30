import io
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


if __name__ == "__main__":
  unittest.main()
