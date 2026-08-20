# MediaVol

MediaVol is a selective, reproducible restore system for a Nobara/Fedora KDE Plasma desktop. It installs curated packages and assets, deploys portable user configuration, and can restore either an adaptive Plasma setup or a guarded exact machine profile.

It is intentionally not a raw `$HOME` backup. Secrets, caches, build output, application sessions, game data, and hardware identifiers are excluded from the default profile.

## Commands

```bash
./install.sh                    # interactive installer
./bin/mediavol install --dry-run
./bin/mediavol snapshot --dry-run
./bin/mediavol verify
./bin/mediavol rollback <transaction-id>
```

## Safety model

- Existing files are preserved unless replacement is explicitly selected.
- Every managed replacement is backed up under `${XDG_STATE_HOME:-$HOME/.local/state}/mediavol/transactions/`.
- Plasma layouts are generated from the outputs available at apply time.
- Cider credentials and login/session state are never stored here.
- Hardware features are conditional and opt-in.

## Restore order

1. Bootstrap core tools and repositories.
2. Install curated packages and Flatpaks.
3. Install active visual assets and Plasma add-ons.
4. Apply portable Plasma theme, icon, cursor, decoration, and Kvantum settings.
5. Create one generic MediaVol-owned KDE panel, then deploy automation, launchers, and user services.
6. Run verification and complete any required logout/reboot.

## Portable Plasma baseline

A normal `mediavol install` applies the complete portable KDE baseline: bundled themes, icons, cursor, decoration, wallpaper, Kvantum theme, KWin effects, KZones, and KWin scripts. It creates one bottom MediaVol panel on every display currently active on the target machine, with Kickoff, spacers, Icons-Only Task Manager, PlasMusic Toolbar, System Tray, and Digital Clock. Reapplying replaces only panels marked as MediaVol-managed and leaves other panels untouched. Use `--layout skip` to apply selected packages/assets/settings without creating or changing MediaVol panels.

Launching `./install.sh` from a terminal opens a guided Gum interface for the recommended baseline or feature customization. Scripted installs can use explicit flags such as `--non-interactive`, `--yes`, `--features`, `--layout`, and `--conflict`; those bypass the matching interactive choices.

MediaVol deliberately does not store or restore monitor topology, KScreen data, EDIDs, GPU or USB state, display-specific containment IDs, hardware widgets, source-home paths, desktop folder mappings, or captured tray state. Exact layouts are unsupported by design.

## Focused application volume shortcuts

Shift+F1 and Shift+F2 use the same xremap and repeat-dispatch setup as the working Cider Shift+F9/F10 bindings. They lower or raise the focused application's playback volume and show Plasma's native volume OSD with the application's name, icon, and updated percentage. They do not open the separate Gamebar audio overlay; that is a pavucontrol playback mixer for manual stream management.

The focused-volume daemon is the fast path. If KWin has not provided it with focus context, the shortcut automatically falls back to the direct KWin/PulseAudio matcher, so it can still adjust the focused app and show the same OSD.

The Forgejo remote is intentionally not configured by the installer. Add and push a private remote only after `mediavol verify` reports no secrets or unexpected host paths.
