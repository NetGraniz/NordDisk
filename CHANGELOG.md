# Changelog

## 0.3.0 - 2026-11-03

- Removed the numeric disk activity percentage from each row.
- Kept the activity bar as the compact visual load indicator.
- Reduced the base window width from 372 to 336 logical pixels.
- Expanded the disk model area by reusing the removed percentage column.

## 0.2.0 - 2026-11-03

- Added a dedicated NordDisk application icon and embedded it in the Windows executable.
- Removed decorative drive tiles and reduced the window and row dimensions.
- Replaced truncated disk model names with an overflow-only marquee.
- Simplified the header status text while retaining temperature and activity readings.

## 0.1.0 - 2026-11-03

First integrated NordDisk release.

- Unified the application and low-level storage discovery code in one build.
- Added SATA, NVMe, USB/UASP and supported RAID discovery paths.
- Added per-physical-disk activity sampling with a PDH fallback.
- Added a native Windows interface and a command-line diagnostic probe.
- Preserved required third-party copyright and MIT license notices.

Known limitation: RAID, Storage Spaces and external USB enclosure behavior still
needs validation across additional hardware configurations.
