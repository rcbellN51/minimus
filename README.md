# minimus

**minimus** is a lightweight, terminal-based internet radio player developed for MINIX 3.

It provides an interactive station list, playback controls, stream metadata, and persistent configuration using `mpg123` as its audio playback backend.

## Features

- Interactive terminal interface with automatic resizing
- PLS playlist support
- Station selection by number or arrow keys
- MP3 internet radio playback through `mpg123`
- Display of stream metadata when available
- Playback, pause, stop, and volume controls
- Mute and unmute
- Loading playlists without restarting the application
- Persistent volume and default playlist settings
- Built-in help screen

## Requirements

- MINIX 3
- C compiler (`cc`)
- `make`
- `mpg123` with MINIX audio output support
- A functioning MINIX audio device
- Network connectivity for internet radio streams

## Building

From the source directory:

```sh
make
```

To remove compiled files:

```sh
make clean
```

## Installation

The default installation prefix is `/usr/local`.

Run with sufficient permissions:

```sh
make install
```

This installs:

- `/usr/local/bin/minimus` — executable
- `/usr/local/share/minimus/stations.pls` — example playlist

To install into a different directory:

```sh
make install PREFIX=/path/to/prefix
```

**Note:** The fallback playlist path is currently compiled as `/usr/local/share/minimus/stations.pls`. Changing the installation prefix does not automatically change that path.

## Usage

Start with the configured default playlist:

```sh
minimus
```

Or specify a PLS playlist:

```sh
minimus /path/to/stations.pls
```

### Keyboard controls

| Key or command | Action |
|---|---|
| `Enter` | Play highlighted station |
| `Up` / `Down` | Select station or scroll help |
| `1`–`99` | Play station by number |
| `p` | Pause or resume |
| `s` | Stop playback |
| `m` | Mute or unmute |
| `v N` | Set volume to N (0–100) |
| `:load pathname` | Load a PLS playlist |
| `:default` | Save the current playlist as default |
| `l` | Show station list |
| `h` or `?` | Toggle help |
| `q` | Quit |

Playlist pathnames containing spaces are supported by `:load` without requiring quotation marks.

### Loading playlists

To load another playlist while the application is running:

```text
:load /path/to/my stations.pls
```

Loading a playlist does not automatically change the saved default playlist.

To make the currently loaded playlist the startup default:

```text
:default
```

## Configuration

`minimus` saves its configuration in:

```text
~/.config/minimus/minimus.conf
```

The configuration directory follows `XDG_CONFIG_HOME` when that environment variable is set.

Saved settings include:

- Volume
- Default playlist pathname

### Playlist selection order

At startup, `minimus` selects a playlist using the following precedence:

1. Playlist supplied on the command line
2. Saved default playlist
3. Packaged fallback playlist at `/usr/local/share/minimus/stations.pls`

The included `stations.pls` provides a small selection of public MP3 internet radio streams. Stream availability depends on the respective providers.

## Development

`minimus` is an experimental project developed and tested on MINIX 3.

For information about the project's origins, AI-assisted development, and testing environment, see [DEVELOPMENT.md](DEVELOPMENT.md).

## License

minimus is distributed under the MIT License. See [LICENSE](LICENSE) for details.
