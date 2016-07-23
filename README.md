# Chalcocite

Command line video/audio player using FFmpeg.
Chalcocite is written with the help of this
[tutorial](http://dranger.com/ffmpeg/ffmpeg.html).

## Usage

Currently, Chalcocite can only play videos that have an audio channel. To play
a video, execute
```
Chalcocite <media-file>
```
Alternatively, Chalcocite has an interactive console:
```
$ Chalcocite
(chal) test <media-file>
```
`quit` terminates Chalcocite from the interactive console.

## Building

Chalcocite depends on SDL, FFmpeg, and GNU Readline. It is recommended to do an
out-of-source build. Execute
```
make build
cd build
cmake ..
```
