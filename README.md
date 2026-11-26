# CrossMoe

> **A personal fork of [CrossInk](https://github.com/uxjulia/CrossInk)**, which is itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). CrossMoe keeps everything CrossInk v2.0.0 does and adds a few features I wanted for reading fantasy books on my Xteink X3.

### Supported devices

- **Xteink X3**: built and tested here.
- **Xteink X4**: uses the same firmware file but is untested. Auto-Rotate is hidden there because the X4 has no motion sensor.

Other CrossInk devices (X4 Pro, X4 Classic, Sticky, EEGO A4, Picco) are not built by this fork. Use [CrossInk](https://github.com/uxjulia/CrossInk) for those.

## What's different in this fork

### Zoom Image

Opens the biggest picture on the current page full screen, so you can actually read fantasy maps and diagrams.

- **Confirm** zooms in: fitted, 2x, then 3x.
- **Back** zooms out one level at a time. From the fitted view it returns to the book.
- **Left/Right** (front buttons) and **Up/Down** (side buttons) move around the zoomed picture, in portrait or landscape.
- While zoomed in, the button hints along the bottom show what each button does: Zoom Out, Zoom In, Move Left, Move Right.
- The first time you open a zoom level you'll see **Loading** for a few seconds while the picture is saved in gray to a temporary file on the SD card. Moving around that level is then quick. The temporary files are deleted when you leave.
- Works with JPEG and PNG pictures, including in Adobe DRM (Libby) books.

### Search Book

Finds every place a name or phrase appears in the book, a bit like Kindle's X-Ray.

1. Start **Search Book**. The word picker opens on the current page.
2. Pick a word, or hold **Confirm** to select a phrase.
3. A progress bar shows while the book is searched. **Back** cancels.
4. Choose a result. The reader opens the page that shows it, whatever your font and size settings, and draws a box around the word with a counter like `3/12`.
5. **Left/Right** jump to the previous and next match in the book. **Back** clears the box and you keep reading from there.

### Auto-Rotate

Lets EPUB and TXT books follow the way you hold the reader, using the X3's motion sensor. Turn it on in **Settings > Reader > Page Layout > Auto-Rotate**, or from the Reader Menu's **Layout** tab while reading:

- **Flip only**: turning the reader upside down flips the page, for example when you switch hands.
- **Flip and sideways**: also switches between portrait and landscape. Landscape lays the chapter out again, so expect a few seconds of **Loading** the first time.
- **Rotation Delay** (Short / Normal / Long) sets how long the reader must rest in its new position first.

It starts working as soon as a book opens or the reader wakes up. It ignores the reader while it's lying flat or moving. If a sideways turn ever comes out upside down, use **Rotate Page 180°** within 20 seconds and it remembers the correction. Turn on **Orientation Aware** for the buttons in **Settings > Controls** so page turns follow the rotated screen.

### Stay Awake When Plugged In

**Settings > System > Device > Stay Awake When Plugged In** stops the reader from going to sleep on its own while it's on USB power, which is handy while copying books or flashing firmware. The **Time to Sleep** timer starts over when you unplug. The X3 can only tell it's plugged in while the battery is charging, so on a charger with a full battery it may still go to sleep.

### Adobe DRM activation fix

On the X3/X4, CrossInk v2.0.0 can't activate an Adobe ID for Libby and other `.acsm` books; activation fails with `pkcs12 rc=-133`. CrossMoe enables the RC2 cipher that Adobe's activation files need, so activation works.

### Where to find the new features

Zoom Image and Search Book are on the **More** tab of the Reader Menu, next to Look Up Word. You can also add them to **Quick Actions** or assign them to a button under **Settings > Controls** (power button, side buttons, front long-press, or long-press Back/Menu).

See [Reader Features](./docs/reader-features.md) for the full details.

---

## Installation

CrossMoe has no releases. Firmware is built by GitHub Actions each time the `release/zoom-search` branch changes.

1. Open the [Actions tab](../../actions/workflows/release_candidate.yml) and pick the newest run with a green check.
2. Under **Artifacts**, download **firmware-x3-x4** and unzip it. You need to be signed in to GitHub, and artifacts are deleted after 90 days.
3. Connect the reader to your computer with a USB cable and flash the `.bin` file with Inky, CrossInk's web flasher: https://inky.crossink.dev/#flash-tools

[Installation](./docs/installation.md) has step-by-step flashing and revert instructions. They are written for CrossInk but work the same here. To go back to stock CrossInk, flash a firmware file from the [CrossInk releases page](https://github.com/uxjulia/CrossInk/releases).

---

## Everything else

All of CrossInk's other features are unchanged: fonts, reading stats, bookmarks, clippings, sync, plugins, themes, and the rest. See the [CrossInk README](https://github.com/uxjulia/CrossInk#readme) and the guides at [crossink.dev](https://www.crossink.dev).

CrossInk's tips for a smooth experience apply here too:

- Keep folders under about 200 files.
- Prefer text-first EPUBs under about 20 MB.
- Use a reliable SD card with some free space.

---

## Building it yourself

CrossMoe builds the same way as CrossInk, with [PlatformIO](https://platformio.org/). See [Getting Started](./docs/development/getting-started.md) for prerequisites.

```sh
git clone https://github.com/thefamousmoe/crossmoe
cd crossmoe
git checkout release/zoom-search
git submodule update --init --recursive
pio run -e default --target upload   # X3/X4 firmware, flashed over USB
```

---

## Credits and license

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), the firmware this all started from.
- [CrossInk](https://github.com/uxjulia/CrossInk) by uxjulia, which CrossMoe builds on. If CrossMoe is useful to you, please consider [supporting CrossInk on Ko-fi](https://ko-fi.com/Q5Q01M6S7).

CrossMoe is a personal project with no support or schedule, so report CrossInk issues to CrossInk, not here. Released under the [MIT License](./LICENSE), like the projects it is based on.
