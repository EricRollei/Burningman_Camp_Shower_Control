# YouTube audio downloader

This local tool downloads YouTube videos as MP3 files into `audio/`. It remembers
downloaded YouTube video IDs in `audio/.youtube-downloads.json`, so repeated or
alternate URLs for the same video are skipped.

The web app also inventories every supported audio file already in `audio/`
(including nested folders), whether or not it was originally downloaded by this
tool. Its wishlist is stored in the same manifest, so pending tracks survive a
browser refresh or server restart.

If YouTube rejects its normal media URL with HTTP 403, the downloader
automatically retries using HLS-capable YouTube clients. Some restricted videos
may still require a PO-token provider or browser cookies; neither is enabled by
this tool because those options require additional setup and account-risk
considerations.

Requirements: Python 3.10+, `yt-dlp`, and `ffmpeg`. On macOS:

```sh
brew install yt-dlp ffmpeg
```

## CSV mode

Create a CSV with `url` and optional `name` columns:

```csv
url,name
https://youtu.be/VIDEO_ID,Opening song
https://www.youtube.com/watch?v=ANOTHER_ID,Closing song
```

Run it from the repository root:

```sh
python3 tools/youtube_audio.py songs.csv
```

A headerless CSV is also accepted, with URL first and name second.

## Web app

Start the local server:

```sh
python3 tools/youtube_audio.py --serve
```

Then open <http://127.0.0.1:8765>. Paste one URL per line, optionally using
`Track name | URL`, and add the tracks to the wishlist. Select any wishlist
items and click **Download selected**. Successful downloads move out of the
wishlist and appear in the complete, searchable audio library. Stop the server
with Ctrl-C.

The library recognizes AAC, FLAC, M4A, MP3, OGG, Opus, PCM, and WAV files.

Only download media you have permission to use, and follow YouTube's terms.
