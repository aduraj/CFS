COLAFileSystem
===


Adrian Duraj

Simple filesystem based on COLA data structure, using FUSE

Directory listing, file and directory creation

Max path length: 30 characters

Compilation: "gcc -lfuse -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 cfs.c -o cfs"

Sample run: "./cfs ~/data /tmp/fuse"

First parameter: the file containing the filesystem, second one: mount dir


Edit:

added FAT, reading from and writing to files, max file size - more than one block
