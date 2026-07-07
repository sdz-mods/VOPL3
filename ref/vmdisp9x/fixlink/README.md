# Fixlink

Small utility to bypass limitations of modern linkers to add some Windows 9x specific attributes to binary files. Determined for modification of final executable file.

## FAQ

Q: I do some retro programming for DOS/Windows 9x, do I need this utility?

A: No. Only when you plan to write the Windows driver. This utility fixing very specific cases and it is currently only useful for drivers. But there are my other projects [pthread9x](https://github.com/JHRobotics/pthread9x) and [nocrt](https://github.com/JHRobotics/nocrt) which are more useful for user-space programs programmers.

## Requirements

Some C89 compatible compiler.

## Build instructions

**GNU C Compiler**

```
gcc -std=c89 -Wall fixlink.c -o fixlink
```

(You can also replace `-std=c89` with newer standard, if you find it useful.)

**Open Watcom**

```
wcl386 -q fixlink.c -fe=fixlink.exe
```

**MS C compiler**

```
cl /nologo /MT fixlink.c /link /out:fixlink.exe
```

## Usage

```
Usage: fixlink <mode> [--dry-run] exe_file_to_fix
<mode> can be:
-40: set expect Windows version to 4.0 (NE target)
-vxd32: fix wrong paging and flags in wlink VXD (LE target)
-shared: fix EXE/DLL to load to shared memory (PE target)
-checksum: recalculate PE checksum (PE target)
```


Mode `-40` simulate missing `-40` option in Watcom resource compiler (`wrc`). Required for 16 bit display minidrivers to work well on Windows 95 and newer.

Mode `-vxd32` fixing bad segmentation in Windows 32-bit VXD created by Watcom linker (`wlink`). Also adds executable flags on BSS and CONST segments, because without this, VXD is not loadable. Required for Windows 9x drivers development in Watcom.

Mode `-shared` simulate missing MS `link.exe` `/shared` option in GNU LD and others modern linkers (please don't confuse with GCC/LD `-shared` option to produce DLL/SO files). Target executable must have image base above 2GB (`0x80000000`) to make this option works. Required for DirectDraw driver development.

