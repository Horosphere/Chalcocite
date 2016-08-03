# Chalcocite

Command line video/audio player using FFmpeg based on FFplay.

## Usage

To display usage:
```
Chalcocite --help
```
To play a media file, execute
```
Chalcocite --file <media-file>
```
Alternatively, Chalcocite has an interactive console:
```
$ Chalcocite
(chal) play <media-file>
```
`quit` terminates Chalcocite from the interactive console.

## Building

Chalcocite depends on SDL2, FFmpeg, and GNU Readline. It is recommended to do
an out-of-source build. Execute
```
mkdir build
cd build
cmake ..
```
