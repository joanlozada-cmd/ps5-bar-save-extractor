# ps5-bar-tool

PS5 BAR Save Extractor is a fork of ps5-bar-tool focused on recovering PS4 and PS5 save data from official PS5 Backup & Restore archives (archive.dat) stored on USB. It allows save files to be extracted without performing a full system restore or formatting/replacing the current console state also without losing the jailbreak state of the ps5.

## Credits

[john-tornblom](https://github.com/john-tornblom): PS5 SDK (https://github.com/ps5-payload-dev/sdk)

## Own compilation

First install the ps5-payload-sdk

```sh
make
```

## How to Use

Place the PS5 BAR file in /mnt/usb0/PS5/EXPORT/BACKUP/archive.dat

```
USB0 Root
└── 📁 PS5
    └── 📁 EXPORT
        └── 📁 BACKUP
            └── 📄 archive.dat
            └── 📄 archive0001.dat (if multi file)
            └── 📄 archive0002.dat (if multi file)
            └── 📄 archiveXXXX.dat
            └── 📁 DUMP (output)
            └── 📄 ps5-bar-tool.log (output)
```

Send the payload

```sh
socat -t 99999 - TCP:YOUR_PS5_IP:9021 < ps5-bar-tool.elf
```

Wait until it finishes.

You'll find a log file in the folder.
