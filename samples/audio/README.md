# Running audio sample on macOS

Requires macOS 14.2+ for system audio capture, Xcode Command Line Tools,
Homebrew, and the Vulkan SDK. Source the SDK's `setup-env.sh` before building.

*Make sure to run `configure.sh` to install all necessary brew packages.*

From the repository root:

```bash
./configure.sh
cmake -S . -B build -G Ninja -DEV2_BUILD_SAMPLES=ON
ninja -C build
open build/bin/audio_viz.app # open is necessary for macOS to read the Info.plist and grant audio access
```

Use `open` to launch the app bundle so macOS associates audio permissions with
the sample. Allow system audio recording when prompted, then play audio in
another app. Permissions can be managed in **System Settings > Privacy & Security**.

For microphone input, quit the sample and launch it with:

```bash
open build/bin/audio_viz.app --args --mic
```

Allow microphone access when prompted.
