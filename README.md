# Chalcocite

Command line video/audio player using FFmpeg based on FFplay.

## Usage

To execute a test routine:
```
Chalcocite --test
```
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

## Developing

All C codes are formatted with astyle and the configuration
```
--style=allman
--align-pointer=type	
--close-templates
--convert-tabs
--indent-preproc-block
--indent-preproc-define
--indent=tab=2
--pad-header
--unpad-paren
```

`typedef struct` should only be used when the struct is opaque. That is, the
user is not allowed to access its members. Similar applies for
`typedef union`. `typedef enum` is forbidden.
