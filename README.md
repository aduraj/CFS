COLAFileSystem
===

Simple filesystem based on COLA data structure, using FUSE

Directory listing, file and directory creation

Max path length: 30 characters

Compilation: "gcc -lfuse -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 cfs.c -o cfs"

Sample run: "./cfs ~/data /tmp/fuse"

First parameter: the file containing the filesystem, second one: mount dir



Edit:
---

Added FAT, reading from and writing to files. Max file size is now more than one block.
