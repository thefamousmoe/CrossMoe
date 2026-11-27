# CrossMoe

> **A personal fork of [CrossInk](https://github.com/uxjulia/CrossInk)**, which is itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). CrossMoe keeps everything CrossInk v2.0.0 does and adds a few features I wanted for reading fantasy books on my Xteink X3.

### Supported devices

- **Xteink X3**: built and tested here.
- **Xteink X4**: uses the same firmware file but is untested. Auto-Rotate is hidden there because the X4 has no motion sensor.

Other CrossInk devices (X4 Pro, X4 Classic, Sticky, EEGO A4, Picco) are not built by this fork. Use [CrossInk](https://github.com/uxjulia/CrossInk) for those.

## What's different in this fork

### Zoom Image

Opens the biggest picture on the current page at 2x zoom, so you can actually read fantasy maps and diagrams.

- It opens at **2x**. **Confirm** zooms in to 3x, then 4x.
- **Back** zooms out one level at a time. From 2x it returns to the book.
- **Left/Right** (front buttons) and **Up/Down** (side buttons) move around the zoomed picture, in portrait or landscape.
- While zoomed in, button hints show what each button does: Out, In, Left and Right on the front, Up and Down beside the side buttons.
- The first time you open a zoom level you'll see **Loading** for a few seconds while the picture is saved in gray to a temporary file on the SD card. Moving around that level is then quick. The temporary files are deleted when you leave.
- Works with JPEG and PNG pictures, including in Adobe DRM (Libby) books.

### Search Book

Finds every place a name or phrase appears in the book, a bit like Kindle's X-Ray.

1. Start **Search Book**. The word picker opens on the current page.
2. Pick a word, or hold **Confirm** to select a phrase.
3. A progress bar shows while the book is searched. **Back** cancels.
4. The results list shows each passage over up to two lines, so the word and a few words on either side always show. Choose one, and the reader opens the page that shows it, whatever your font and size settings, and draws a box around the word.
5. **Left/Right** (**Prev/Next**) jump to the previous and next match in the book. The match number, like `4/17`, sits beside the Next button.
6. Your place in the book stays visible: in portrait your bottom status bar moves to the top of the page while the search buttons show, with the text fitting between the two; in landscape both status bars stay where they are.
7. **Back** returns to the results list to pick another passage, and **Back** again takes you to the page you started from. **Confirm** (**Stay**) ends the search and keeps you reading on the page you're on.

### Auto-Rotate

Lets EPUB and TXT books follow the way you hold the reader, using the X3's motion sensor. Turn it on in **Settings > Reader > Page Layout > Auto-Rotate**, or while reading: press Confirm, then the layout tab (the lines-of-text icon, second from the left):

- **Flip**: turning the reader upside down flips the page, for example when you switch hands.
- **Sideways**: switches between portrait and landscape (either way round), but never shows portrait upside down. Landscape lays the chapter out again, so expect a few seconds of **Loading** the first time.
- **Flip + Sideways**: both.
- **Rotation Delay** (Short / Normal / Long) sets how long the reader must rest in its new position first. Short is a quarter second.
- A **Rotating** notice shows while the page turns.

It starts working as soon as a book opens or the reader wakes up, and learns which way is up, so a book closed in landscape opens in portrait if that's how you pick it up. It ignores the reader while it's lying flat or moving.

To switch it off and on quickly (for example, reading lying down), add **Auto-Rotate** to Quick Actions or a button shortcut. It comes back on in the mode it was in before, with an **Auto-Rotate: ON/OFF** notice. Whenever it comes back on, or after Quick Lock is unlocked, it checks how you're holding the reader and turns the page to match. If a sideways turn ever comes out upside down, use **Rotate Page 180°** within 20 seconds and it remembers the correction. Turn on **Orientation Aware** for the buttons in **Settings > Controls** so page turns follow the rotated screen.

### Stay Awake When Plugged In

**Settings > System > Device > Stay Awake When Plugged In** stops the reader from going to sleep on its own while it's on USB power, which is handy while copying books or flashing firmware. The **Time to Sleep** timer starts over when you unplug. On the X3 it counts as plugged in while the battery is charging or while it's connected to a computer, so only a wall charger with a full battery may still let it sleep.

### Where to find the new features

In a book, press Confirm to open the reader menu. Its tabs are icons; Zoom Image and Search Book are on the **⋯ (three dots)** tab, next to Look Up Word. You can also add them (and the Auto-Rotate on/off switch) to **Settings > Controls > Quick Actions**, or assign them to a button under **Settings > Controls > Power Button**, **Front Buttons**, or **Side Buttons**.

See [Reader Features](./docs/reader-features.md) for the full details.

---

## Installation

1. Download **`firmware-x3-x4.bin`** from the latest release on the [Releases page](../../releases/latest).
2. Connect the reader to your computer with a USB cable and flash the file with Inky, CrossInk's web flasher: https://inky.crossink.dev/#flash-tools (choose the custom `.bin` option).

Already on CrossMoe 1.0.1 or later? Use **Settings > System > Check for Updates** instead. (1.0 has to be flashed as above.)

Development builds of newer changes are made by GitHub Actions each time the `release/zoom-search` branch changes: open the [Actions tab](../../actions/workflows/release_candidate.yml), pick the newest green run, and download **firmware-x3-x4** under **Artifacts** (sign-in required; artifacts expire after 90 days).

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
