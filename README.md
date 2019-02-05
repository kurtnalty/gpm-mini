# gpm-mini
This is a pared down version of gpm (text console mouse) for linux

This is a subset of gpm-1.20.7, a console mouse driver for linux,
maily used for copy/paste operations and web browsing via elinks.
The mouse drivers have been removed, using the linux /dev/input/mice
or /dev/input/mousex devices instead. Repeater mode has been removed,
as has 'special' feature, which could bind programs to mouse buttons.

This program builds using the Build_gpm_mini script, and compiles
cleanly under gcc, and gcc/musl.

Thank-you to the various authors, and maintainers.

Kurt Nalty
4 February 2019
