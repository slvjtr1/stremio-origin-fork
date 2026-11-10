![Stremio Origin](docs/header-rounded.png)

<p align="center">
  <img src="docs/logo-pill.png" width="200" alt="Stremio Origin">
</p>

<h1 align="center">Stremio Origin</h1>

<p align="center"><b>A native webOS app for Stremio addons, written in C: fast, quiet, and fully in English or Arabic.</b></p>

<p align="center">
  <a href="https://github.com/slvjtr1/stremio-origin-fork/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/slvjtr1/stremio-origin-fork?label=release&color=7c5cff"></a>
  <img alt="LG webOS 3 and later" src="https://img.shields.io/badge/LG%20webOS-3%2B-1f1d2b">
  <img alt="Licence GPL-3.0" src="https://img.shields.io/badge/licence-GPL--3.0-1f1d2b">
</p>

<p align="center"><sub>Stremio™ is a trademark of its owner. Stremio Origin is independent and unofficial, and is not affiliated with, endorsed by or sponsored by Stremio. The name only describes compatibility.<br>You bring your own Stremio account and your own addons. The app ships empty, hosts and indexes nothing, and plays only the stream links your addons return.</sub></p>

---

## Highlights

| | |
|---|---|
| **Subtitles your way** | Outline and shadow, six fonts and bold, sizes and colours, and one press to bring them back in sync. Arabic subtitles join and read right to left. |
| **Skip intro, recap and credits** | A button shows up at the right moment. Press OK, or let it skip by itself. |
| **Trailers on the whole screen** | A title's trailer plays behind its page, and after five seconds it fills the screen. Any key brings the page back at once. |
| **Clean search** | Real, released films and series only. Clips, fan uploads, adult titles and unwanted themes are left out, and results arrive sooner. |
| **Arabic from end to end** | The whole app in Arabic or English. Titles and descriptions follow the app or a language you pick, built in: nothing to set up. |
| **No spoilers** | Blur unwatched episodes hides their stills and stories on the title page, in the player and in reviews until you watch them. |
| **Your Home, your rows** | Choose 8, 12, 16 or 24 catalogs on Home. Your addons come first; Cinemeta's lists fill any space left. |
| **The best stream, in one press** | Picked for your TV and your connection, with quality tags on every source. |
| **Your library, everywhere** | Library, watched episodes and Continue Watching stay in sync with your Stremio account. |
| **Made for the remote** | Smooth focus and motion, the pointer hides when you use the arrows, and a 12-hour or 24-hour clock. |

Also inside: DTS sound converted on the TV, Dolby Vision on TVs that support it, torrent playback with a buffer sized to your TV, people and reviews on every title, and updates from inside the app.

## Status

- **LG webOS:** where the work is now.
- **Google TV and Android TV:** paused for a short while. Version 2.1.0 stays available and keeps working; no new APK is being made for the moment.
- **Lightweight (beta, LG webOS):** a simpler edition with one look and a short list of settings. To try it: Settings › About › Beta updates, then Check for updates.

## Install

For LG TVs running webOS 3 and later (ARM). Tested on webOS 4.4; see [compatibility](docs/compatibility.md).

**Google TV and Android TV (9 or later):** in the [Downloader](https://www.aftvnews.com/downloader/) app, enter the code **2141492**: it downloads the latest APK (2.1.0 while Android work is paused). Or download [`StremioOrigin-AndroidTV.apk`](https://github.com/slvjtr1/stremio-origin-fork/releases/latest/download/StremioOrigin-AndroidTV.apk) yourself. Allow installs from Downloader when Android asks.

**Homebrew Channel (rooted TVs):** add the repository `https://slvjtr1.github.io/stremio-origin-fork/apps.json`, then install Stremio Origin.

**Developer Mode:** download `tv.stremioorigin.app_<version>_arm.ipk` from the [latest release](https://github.com/slvjtr1/stremio-origin-fork/releases/latest) and install it with [webOS Dev Manager](https://github.com/webosbrew/dev-manager-desktop) or `ares-install`.

New versions install from inside the app: **Settings › About**. Everything else (install steps, updates, help) is on the [site](https://slvjtr1.github.io/stremio-origin-fork/).

## Good to know

- **Language:** Settings › Language. The app follows the TV's language until you choose one.
- **Titles and descriptions:** Settings › Language. Choose *Same as the app*, *English* or *Arabic*.
- **Links from addons** show a code: scan it and the site opens the link on your phone.
- **Something wrong?** Settings › About › Share logs, then scan the code: the site fills in the report.
- **Blur unwatched episodes:** Settings › Playback.
- **Catalogs on Home:** Settings › Appearance.

## Privacy

- Sign-in goes directly to Stremio. Your account stays on the TV.
- No analytics. Nothing is sent to the developer.
- Logs stay on the TV. Sharing them is your choice: the app seals them and shows a short code.

## Building

```sh
tools/build.sh webos        # the app
tools/release.sh X.Y.Z      # version, package and checksum in dist/, the site's Homebrew files in docs/
```

## Credits

- Built on [Stremio Origin](https://github.com/stremio-origin/webos) by simussu.
- Interface sounds: [Interface Sounds](https://kenney.nl/assets/interface-sounds) by Kenney (CC0).
- The glass look: [liquid-glass-react](https://github.com/rdev/liquid-glass-react) by rdev; the app ships a modified version.
- Torrents: [Nuvio Engine](https://github.com/NuvioMedia/nuvio-engine) (GPL-3.0); the app ships a modified version.
- Subtitle fonts: Atkinson Hyperlegible Next, Cairo, Lora, Readex Pro and Roboto (SIL Open Font Licence, in `assets/fonts`).
- Titles and descriptions: [TMDB](https://www.themoviedb.org). This product uses the TMDB API but is not endorsed or certified by TMDB.

## Licence

GPL-3.0, see [LICENSE](LICENSE). Third-party components and their licences: [third_party/licenses/README.txt](third_party/licenses/README.txt).
