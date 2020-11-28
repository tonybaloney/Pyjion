# Release notes

## 0.0.5

* Fixed a critical bug where method calls with large numbers of arguments, and the argument was a tuple could cause a segmentation fault on GC collection.
* Tested support for IPython REPL
* Fixed a bug where importing pyjion.dis after enabling the JIT would cause a stack overflow
* Has around 50% chance of working and not causing your computer to explode, or worse, segmentation fault

## 0.0.4

* Added a stack probe helper for Linux (will use JIT in more scenarios)
* Enabled support for running unit tests in Linux
* Fixed a bug where JIT would crash when a method call failed because of a bad-lookup
* Implemented helper method redirection for Linux to support PIC compiled symbols
* Has around 35% chance of working and not causing your computer to explode, or worse, segmentation fault
* Improved discovery of .NET libraries on Linux
* Fixed a bug where a garble-named log file would be generated (should be JIT timings log)

## 0.0.3

* Installable bdist_wheel for Ubuntu, Debian, macOS 10.15, 11 (10.16) and Windows x64
* Installable manylinux2014 wheel with clrjit.so bundled in
* Added multithreading/multiprocessing support
* Fixed a bug where the wheel would be broken if there are two distributions of Python 3.9 on the system
* Has around 30% chance of working and not causing your computer to explode, or worse, segmentation fault.

## 0.0.2

* Installable source distribution support for macOS, Windows and (barely) Linux.

## 0.0.1

* It compiles on my machine 