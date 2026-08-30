import json
import tempfile
import unittest
from pathlib import Path

from tools.youtube_audio import AudioDownloader, MANIFEST_NAME


class AudioDownloaderDataTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.audio_dir = Path(self.temp_dir.name)
        self.downloader = AudioDownloader(self.audio_dir)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_library_includes_manifest_and_untracked_nested_audio(self):
        (self.audio_dir / "Known.mp3").write_bytes(b"1234")
        (self.audio_dir / "pcm").mkdir()
        (self.audio_dir / "pcm" / "CH1.PCM").write_bytes(b"12")
        (self.audio_dir / "notes.txt").write_text("not audio", encoding="utf-8")
        (self.audio_dir / MANIFEST_NAME).write_text(
            json.dumps(
                {
                    "version": 1,
                    "downloads": {
                        "abcdefghijk": {
                            "video_id": "abcdefghijk",
                            "url": "https://www.youtube.com/watch?v=abcdefghijk",
                            "name": "Friendly title",
                            "filename": "Known.mp3",
                            "downloaded_at": "2026-01-01T00:00:00+00:00",
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        tracks = self.downloader.library()

        self.assertEqual([track["path"] for track in tracks], ["pcm/CH1.PCM", "Known.mp3"])
        known = next(track for track in tracks if track["path"] == "Known.mp3")
        self.assertEqual(known["name"], "Friendly title")
        self.assertEqual(known["video_id"], "abcdefghijk")

    def test_wishlist_is_normalized_deduplicated_and_persistent(self):
        items = self.downloader.replace_wishlist(
            [
                {"name": " First / song ", "url": "youtu.be/abcdefghijk"},
                {"name": "Duplicate", "url": "https://youtube.com/watch?v=abcdefghijk"},
                {"name": "Second", "url": "https://www.youtube.com/shorts/lmnopqrstuv"},
            ]
        )

        self.assertEqual(len(items), 2)
        self.assertEqual(items[0]["name"], "First _ song")
        self.assertEqual(items[0]["url"], "https://www.youtube.com/watch?v=abcdefghijk")
        self.assertEqual(self.downloader.wishlist(), items)

        manifest = json.loads((self.audio_dir / MANIFEST_NAME).read_text(encoding="utf-8"))
        self.assertEqual(manifest["version"], 2)
        self.assertEqual(manifest["wishlist"], items)

    def test_wishlist_rejects_non_youtube_urls(self):
        with self.assertRaisesRegex(ValueError, "Only youtube.com"):
            self.downloader.replace_wishlist([{"url": "https://example.com/video"}])


if __name__ == "__main__":
    unittest.main()
