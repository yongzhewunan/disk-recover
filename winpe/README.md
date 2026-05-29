# WinPE Deployment Package

This directory contains the WinPE deployment package builder for Disk Recover.

## Build Instructions

1. Build the project in Release mode:
   ```bash
   cmake --build build --config Release
   ctest --test-dir build -V
   ```

2. Run the package builder:
   ```cmd
   cd winpe
   build_pe_package.bat
   ```

3. Copy the `winpe_package` folder to your WinPE boot media.

## WinPE Requirements

WinPE (Windows Preinstallation Environment) is a minimal Windows OS used for deployment and recovery. This tool requires:

- WinPE 10 (Windows 10 ADK)
- Basic WinPE with no optional components needed
- The tool runs on stock WinPE without additional packages

## Usage in WinPE

1. Boot into WinPE
2. Navigate to the winpe_package directory
3. Run the GUI or CLI:
   ```
   disk-recover.exe          # GUI mode
   disk-recover-cli.exe      # CLI mode
   ```

## CLI Commands

```bash
disk-recover-cli.exe list-disks                     # List physical disks
disk-recover-cli.exe scan "\\.\PhysicalDrive0" --mode quick --output scan_result
disk-recover-cli.exe recover scan_result --output X:\recovered
disk-recover-cli.exe progress scan_result           # Show scan progress
```

## Files in Package

| File | Description |
|------|-------------|
| disk-recover.exe | GUI application |
| disk-recover-cli.exe | CLI application |
| avcodec-*.dll | FFmpeg codec library |
| avformat-*.dll | FFmpeg format library |
| avutil-*.dll | FFmpeg utility library |
| swscale-*.dll | FFmpeg scaling library |
| swresample-*.dll | FFmpeg resampling library |
| sqlite3.dll | SQLite database library |

## Limitations in WinPE

- No persistent storage - scan results must be saved to external drive
- Limited display drivers - GUI may have reduced functionality
- No network by default - cannot save to network shares without enabling WinPE networking

## Troubleshooting

If the application fails to start:
1. Verify all DLLs are present
2. Check WinPE architecture matches (x64 required)
3. Ensure sufficient memory is available