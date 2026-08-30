#!/usr/bin/env python3
"""Download named YouTube videos as MP3s from CSV or a local web UI."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import subprocess
import sys
import threading
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_AUDIO_DIR = REPO_ROOT / "audio"
APP_FILE = Path(__file__).with_name("youtube_audio_app.html")
MANIFEST_NAME = ".youtube-downloads.json"
MAX_REQUEST_BYTES = 1_000_000
MAX_ITEMS = 200
VIDEO_ID_RE = re.compile(r"^[A-Za-z0-9_-]{6,20}$")
BAD_FILENAME_CHARS_RE = re.compile(r"[<>:\"/\\|?*\x00-\x1f]")
AUDIO_EXTENSIONS = {".aac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".pcm", ".wav"}
YOUTUBE_HOSTS = {
    "youtube.com",
    "m.youtube.com",
    "music.youtube.com",
    "youtu.be",
    "youtube-nocookie.com",
}


@dataclass
class DownloadResult:
    url: str
    name: str
    status: str
    message: str
    filename: str | None = None


def youtube_video_id(raw_url: str) -> tuple[str, str]:
    """Return (video ID, normalized URL), rejecting non-YouTube URLs."""
    url = raw_url.strip()
    if not url:
        raise ValueError("URL is empty")
    if "://" not in url:
        url = "https://" + url

    parsed = urlsplit(url)
    host = (parsed.hostname or "").lower().removeprefix("www.")
    if parsed.scheme not in {"http", "https"} or host not in YOUTUBE_HOSTS:
        raise ValueError("Only youtube.com and youtu.be video URLs are supported")

    video_id = ""
    parts = [part for part in parsed.path.split("/") if part]
    if host == "youtu.be" and parts:
        video_id = parts[0]
    elif parsed.path == "/watch":
        video_id = parse_qs(parsed.query).get("v", [""])[0]
    elif len(parts) >= 2 and parts[0] in {"shorts", "embed", "live"}:
        video_id = parts[1]

    if not VIDEO_ID_RE.fullmatch(video_id):
        raise ValueError("URL does not contain a recognizable YouTube video ID")
    return video_id, f"https://www.youtube.com/watch?v={video_id}"


def safe_name(name: str, fallback: str) -> str:
    cleaned = BAD_FILENAME_CHARS_RE.sub("_", name.strip())
    cleaned = re.sub(r"\s+", " ", cleaned).strip(" .")
    return (cleaned or fallback)[:120].rstrip(" .")


class AudioDownloader:
    def __init__(self, audio_dir: Path):
        self.audio_dir = audio_dir.resolve()
        self.manifest_path = self.audio_dir / MANIFEST_NAME
        self._lock = threading.Lock()

    def _load_manifest(self) -> dict:
        if not self.manifest_path.exists():
            return {"version": 2, "downloads": {}, "wishlist": []}
        try:
            data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
            if not isinstance(data.get("downloads"), dict):
                raise ValueError
            if not isinstance(data.get("wishlist", []), list):
                raise ValueError
            data.setdefault("wishlist", [])
            data["version"] = 2
            return data
        except (json.JSONDecodeError, OSError, ValueError):
            raise RuntimeError(f"Cannot read manifest: {self.manifest_path}")

    def _save_manifest(self, manifest: dict) -> None:
        temp_path = self.manifest_path.with_suffix(".tmp")
        temp_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        temp_path.replace(self.manifest_path)

    def library(self) -> list[dict]:
        """Return every audio file on disk, enriched with manifest metadata."""
        with self._lock:
            downloads = self._load_manifest()["downloads"]
            by_filename = {
                item.get("filename"): item
                for item in downloads.values()
                if isinstance(item, dict) and item.get("filename")
            }
            if not self.audio_dir.exists():
                return []

            tracks = []
            for path in self.audio_dir.rglob("*"):
                if not path.is_file() or path.name.startswith(".") or path.suffix.lower() not in AUDIO_EXTENSIONS:
                    continue
                relative_path = path.relative_to(self.audio_dir).as_posix()
                metadata = by_filename.get(relative_path) or by_filename.get(path.name) or {}
                stat = path.stat()
                tracks.append(
                    {
                        "filename": path.name,
                        "path": relative_path,
                        "format": path.suffix.removeprefix(".").upper(),
                        "size_bytes": stat.st_size,
                        "modified_at": datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
                        "name": metadata.get("name") or path.stem,
                        "url": metadata.get("url"),
                        "video_id": metadata.get("video_id"),
                        "downloaded_at": metadata.get("downloaded_at"),
                    }
                )
            return sorted(tracks, key=lambda item: (item["name"].casefold(), item["path"].casefold()))

    def history(self) -> list[dict]:
        """Compatibility endpoint data, including only files that still exist."""
        library = self.library()
        library_names = {track["filename"] for track in library}
        library_paths = {track["path"] for track in library}
        with self._lock:
            downloads = self._load_manifest()["downloads"]
            entries = [
                item
                for item in downloads.values()
                if item.get("filename") in library_names or item.get("filename") in library_paths
            ]
            return sorted(entries, key=lambda item: item.get("downloaded_at", ""), reverse=True)

    def wishlist(self) -> list[dict]:
        with self._lock:
            return list(self._load_manifest()["wishlist"])

    def replace_wishlist(self, items: list[dict]) -> list[dict]:
        """Validate, deduplicate, and persist the complete wishlist."""
        if len(items) > MAX_ITEMS:
            raise ValueError(f"items must contain at most {MAX_ITEMS} entries")

        normalized = []
        seen = set()
        now = datetime.now(timezone.utc).isoformat()
        with self._lock:
            manifest = self._load_manifest()
            previous = {
                item.get("video_id"): item
                for item in manifest["wishlist"]
                if isinstance(item, dict) and item.get("video_id")
            }
            for item in items:
                if not isinstance(item, dict):
                    raise ValueError("Every wishlist item must contain a URL and optional name")
                video_id, url = youtube_video_id(str(item.get("url", "")))
                if video_id in seen:
                    continue
                seen.add(video_id)
                normalized.append(
                    {
                        "video_id": video_id,
                        "url": url,
                        "name": safe_name(str(item.get("name", "")), video_id) if item.get("name") else "",
                        "added_at": previous.get(video_id, {}).get("added_at", now),
                    }
                )
            manifest["wishlist"] = normalized
            self._save_manifest(manifest)
        return normalized

    def download(self, raw_url: str, requested_name: str = "") -> DownloadResult:
        try:
            video_id, url = youtube_video_id(raw_url)
        except ValueError as exc:
            return DownloadResult(raw_url, requested_name, "error", str(exc))

        name = safe_name(requested_name, video_id)
        with self._lock:
            try:
                manifest = self._load_manifest()
            except RuntimeError as exc:
                return DownloadResult(url, name, "error", str(exc))

            previous = manifest["downloads"].get(video_id)
            if previous and (self.audio_dir / str(previous.get("filename", ""))).is_file():
                filename = previous.get("filename")
                return DownloadResult(
                    url,
                    name,
                    "skipped",
                    "Already downloaded",
                    filename,
                )
            if previous:
                # A stale manifest entry must not prevent restoring a deleted file.
                del manifest["downloads"][video_id]

            self.audio_dir.mkdir(parents=True, exist_ok=True)
            stem = name
            destination = self.audio_dir / f"{stem}.mp3"
            if destination.exists():
                stem = safe_name(f"{name} [{video_id}]", video_id)

            base_command = [
                shutil.which("yt-dlp") or "yt-dlp",
                "--ignore-config",
                "--no-playlist",
                "--extract-audio",
                "--audio-format",
                "mp3",
                "--audio-quality",
                "0",
                "--no-overwrites",
                "--print",
                "after_move:filepath",
                "--output",
                str(self.audio_dir / f"{stem}.%(ext)s"),
            ]

            # YouTube increasingly requires PO tokens for direct media URLs. If
            # that path returns 403, retry via HLS clients/formats, which do not
            # currently require a GVS PO token for many public videos.
            attempts = [
                [],
                [
                    "--extractor-args",
                    "youtube:player_client=web_safari,web_embedded,default",
                    "--format",
                    "bestaudio[protocol=m3u8_native]/best[protocol=m3u8_native]/bestaudio/best",
                ],
            ]
            completed = None
            for attempt_number, extra_args in enumerate(attempts):
                try:
                    completed = subprocess.run(
                        base_command + extra_args + [url],
                        capture_output=True,
                        text=True,
                        timeout=30 * 60,
                        check=False,
                    )
                except FileNotFoundError:
                    return DownloadResult(
                        url,
                        name,
                        "error",
                        "yt-dlp is not installed (try: brew install yt-dlp ffmpeg)",
                    )
                except subprocess.TimeoutExpired:
                    return DownloadResult(url, name, "error", "Download timed out after 30 minutes")

                if completed.returncode == 0:
                    break
                if attempt_number == 0 and "403" in completed.stderr:
                    print(f"[download] {video_id}: direct media request returned 403; retrying with HLS")
                    continue
                break

            assert completed is not None
            if completed.returncode != 0:
                detail = [line.strip() for line in completed.stderr.splitlines() if line.strip()]
                useful = [line for line in detail if line.startswith(("ERROR:", "WARNING:"))]
                message_lines = (useful or detail)[-3:]
                message = " | ".join(message_lines) if message_lines else "yt-dlp failed"
                print(f"[download] {video_id}: {message}")
                return DownloadResult(url, name, "error", message)

            output_lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
            output_path = Path(output_lines[-1]) if output_lines else self.audio_dir / f"{stem}.mp3"
            filename = output_path.name
            manifest["downloads"][video_id] = {
                "video_id": video_id,
                "url": url,
                "name": name,
                "filename": filename,
                "downloaded_at": datetime.now(timezone.utc).isoformat(),
            }
            manifest["wishlist"] = [
                item for item in manifest["wishlist"] if item.get("video_id") != video_id
            ]
            self._save_manifest(manifest)
            return DownloadResult(url, name, "downloaded", "Downloaded", filename)

    def download_many(self, items: list[dict]) -> list[DownloadResult]:
        return [self.download(str(item.get("url", "")), str(item.get("name", ""))) for item in items]


def read_csv_items(csv_path: Path) -> list[dict]:
    with csv_path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return []

    header = [cell.strip().lower() for cell in rows[0]]
    url_aliases = {"url", "youtube_url", "youtube url", "link"}
    name_aliases = {"name", "title", "filename", "file_name"}
    has_header = any(cell in url_aliases for cell in header)
    if has_header:
        url_index = next(i for i, cell in enumerate(header) if cell in url_aliases)
        name_index = next((i for i, cell in enumerate(header) if cell in name_aliases), None)
        data_rows = rows[1:]
    else:
        url_index, name_index, data_rows = 0, 1, rows

    items = []
    for row_number, row in enumerate(data_rows, start=2 if has_header else 1):
        if not row or not any(cell.strip() for cell in row):
            continue
        if url_index >= len(row):
            raise ValueError(f"Missing URL on CSV row {row_number}")
        items.append(
            {
                "url": row[url_index].strip(),
                "name": row[name_index].strip() if name_index is not None and name_index < len(row) else "",
            }
        )
    return items


def make_handler(downloader: AudioDownloader):
    class Handler(BaseHTTPRequestHandler):
        def _json(self, payload: object, status: HTTPStatus = HTTPStatus.OK) -> None:
            body = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/":
                body = APP_FILE.read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/api/history":
                try:
                    self._json({"downloads": downloader.history()})
                except RuntimeError as exc:
                    self._json({"error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
            elif self.path == "/api/library":
                try:
                    self._json({"tracks": downloader.library()})
                except (OSError, RuntimeError) as exc:
                    self._json({"error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
            elif self.path == "/api/wishlist":
                try:
                    self._json({"items": downloader.wishlist()})
                except RuntimeError as exc:
                    self._json({"error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
            else:
                self.send_error(HTTPStatus.NOT_FOUND)

        def do_POST(self) -> None:  # noqa: N802
            if self.path not in {"/api/download", "/api/wishlist"}:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > MAX_REQUEST_BYTES:
                    raise ValueError("Invalid request size")
                payload = json.loads(self.rfile.read(length))
                items = payload.get("items")
                if not isinstance(items, list) or len(items) > MAX_ITEMS:
                    raise ValueError(f"items must be a list of at most {MAX_ITEMS} entries")
                if not all(isinstance(item, dict) for item in items):
                    raise ValueError("Every item must contain a URL and optional name")
            except (ValueError, json.JSONDecodeError) as exc:
                self._json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return

            if self.path == "/api/wishlist":
                try:
                    self._json({"items": downloader.replace_wishlist(items)})
                except (RuntimeError, ValueError) as exc:
                    self._json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
            else:
                results = [asdict(result) for result in downloader.download_many(items)]
                self._json({"results": results})

        def log_message(self, format: str, *args: object) -> None:
            print(f"[web] {self.address_string()} - {format % args}")

    return Handler


def print_results(results: list[DownloadResult]) -> int:
    errors = 0
    for result in results:
        marker = {"downloaded": "+", "skipped": "=", "error": "!"}[result.status]
        target = f" -> {result.filename}" if result.filename else ""
        print(f"[{marker}] {result.name or result.url}: {result.message}{target}")
        errors += result.status == "error"
    downloaded = sum(result.status == "downloaded" for result in results)
    skipped = sum(result.status == "skipped" for result in results)
    print(f"\n{downloaded} downloaded, {skipped} skipped, {errors} failed")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("csv_file", nargs="?", type=Path, help="CSV with url and optional name columns")
    mode.add_argument("--serve", action="store_true", help="Run the local HTML app")
    parser.add_argument("--host", default="127.0.0.1", help="Web app bind address")
    parser.add_argument("--port", type=int, default=8765, help="Web app port")
    parser.add_argument("--audio-dir", type=Path, default=DEFAULT_AUDIO_DIR, help="MP3 output directory")
    args = parser.parse_args()

    downloader = AudioDownloader(args.audio_dir)
    if args.serve:
        if not APP_FILE.exists():
            parser.error(f"Missing web app: {APP_FILE}")
        server = ThreadingHTTPServer((args.host, args.port), make_handler(downloader))
        print(f"YouTube audio app: http://{args.host}:{args.port}")
        print(f"MP3 output: {downloader.audio_dir}")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped")
        finally:
            server.server_close()
        return 0

    try:
        items = read_csv_items(args.csv_file)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if not items:
        print("CSV contains no downloads")
        return 0
    return print_results(downloader.download_many(items))


if __name__ == "__main__":
    sys.exit(main())
