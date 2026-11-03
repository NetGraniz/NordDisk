# NordDisk

Compact Windows x64 monitor for physical disk temperature and activity.

Current version: **0.3.0**

- Single-file native executable; no runtime resources or helper processes.
- Integrated storage engine discovers SATA, NVMe, USB/UASP and supported RAID devices.
- Per-disk activity is sampled directly from `PhysicalDriveN`.
- Administrator elevation is required for hardware SMART access.

## Architecture

NordDisk is one native application. Its UI, monitoring loop, activity sampler and
hardware discovery engine are built by the same project and shipped as one executable.
The low-level storage implementation lives in `src/storage_engine`; it is an internal
module, not a separately built or launched application.

## Build

Open a Visual Studio 2022 developer shell with the MFC component installed, then run:

```powershell
msbuild NordDisk.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

## Third-party notices

Parts of the integrated storage engine are derived from CrystalDiskInfo 9.9.3,
Copyright (c) hiyohiyo, under the MIT License. The required copyright and license
notice is preserved at `src/storage_engine/LICENSE.txt`. This attribution describes
code provenance only; CrystalDiskInfo is not included or shipped as a separate product.

