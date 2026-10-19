98ripper
========

This is a commandline tool for extracting files from various
PC-98 binary disk images.

There are some images that use weird custom filesystems, particularly
pre-1990 games. It may not be possible to rip those images.
Also be aware that disk images, particularly old ones, may have been
made using damaged disks, which means the image has unavoidable data
corruption and may only be partially rippable.

To compile 98ripper on Linux, run `make`. It only depends on the C++
standard library, which contains iconv. 98ripper does require C++17 support.

To compile on Windows, your best bet is to use WSL or Cygwin.
Alternatively, it is possible to do a simple build in the MSYS2 environment,
using its mingw32/64 compilers. You can get those from
[the MSYS2 website](https://www.msys2.org/), although the instructions
can be a bit intimidating. You need a fairly recent version of MSYS2 to have
the necessary C++17 support, which means you must have a fairly recent Windows.
(So running MSYS2 on Windows 7 or older may not build 98ripper well.)
You'll also need to get libiconv somehow. With all that in place, run MSYS2's
mingw32 environment, go to the directory where you keep the 98ripper source
files, and run `make`.

Here's a prebuilt 32-bit Windows binary for 98ripper v6:
<https://mooncore.eu/filus/98ripper.exe>

*Caution*: Due to encoding differences, the Windows build of 98ripper can't
work correctly with disk images containing non-ASCII filepaths. In this
case, please use EditDisk, L3DiskEx, FIVEC, or 98Bridge instead; or it may be possible
to get UTF-8 encoding working as expected on modern Windowses if, instead of
using a normal command prompt, you run 98ripper in Windows Terminal and have
the latest Powershell.

Special thanks to Ryo-Cokey of PC98.org for format documentation!
The Windows GUI tool [FIVEC](https://www.pc98.org/project/fivec.html)
is in some ways even more capable than 98ripper, well worth a try.

----------------------------------------------------------

	$ 98ripper --help
	
	98ripper - version 7 - Rip files from PC-98 disk images
	
	Usage: 98ripper [options] <image files...>
	       98ripper -r [options] <directory>
	
	Options:
	  -o --output=<path>   Set the output path, default=<working directory>
	                       Ripped images automatically go into unique subfolders.
	  -i --id              Identify image type and data errors without extracting
	  -l --list            List image contents without extracting
	  -d --dump            Dump raw image without extracting
	  -r --recursive       Rip everything under input folder and its subfolders
	  -u --undelete        Try to dump deleted files too
	  -t --table=<index>   Select file table to use, default=0
	  -f --filter=<text>   Only list/extract files matching this, wildcards *?
	  -v --verbose         Verbose output
	
	Supported formats:
	  fat12  fat16  fat8  fat-do  makodos  elfdos
	  .hdm  .iso  .dcp  .dcu  .dim  .nhd  .d88  .fdd  .fdi  .hdi  .dip  .nfd  .fim
