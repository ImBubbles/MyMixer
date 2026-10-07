# Why

I've wanted to use VoiceMeeter on Linux for awhile now but there's not support for it. I have been using a PipeWire configuration that has virtual nodes but I keep having to manually connect the nodes to my headphones and it's bothersome enough where I'm developing this now.

# Roadmap
## Completed
* Logging
* Creating and connecting VirtualChannels
* Some other PipeWire API wrapping
* Setup configuration support
* Add UI
* Setup autostart / autoload
## Todo
* Officially release
* Fix discord -> virtual channel causing audio stuttering

# Build
## Generate system files
```bash
cmake -S . -B build
```
## Build
```bash
cmake --build build
```