# UEFI CPU Temperature Monitor

A UEFI application that displays CPU temperature on screen.

## Usage

- **No argument**: Show temperature once and exit
- **Any argument** (e.g., `-r`): Enter resident mode, update temperature every second

## Supported CPUs

- Intel
- AMD

## Build

Build with EDK2 as a UEFI application.

## Files

- `cputemp.c` - Main source code
- `Cpuid.h` - CPUID utilities  
- `cputemp.inf` - EDK2 module definition
