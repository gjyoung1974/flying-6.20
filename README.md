## xpool aka "Flying" port for modern Linux

A few minor edits where made to get this classic x11 game running under modern Linux

Original source code at:
https://www.ibiblio.org/pub/X11/contrib/games/multiplayer/



```
     ______      _
    / __/ /_  __(_)___  ____ _
   / /_/ / / / / / __ \/ __ `/            original source available at
  / __/ / /_/ / / / / / /_/ /             ftp.x.org:/contrib/games/multiplayer
 /_/ /_/\__, /_/_/ /_/\__, /  V6.20
       /____/        /____/
```

 To create the executable, you can either use one of the supplied makefiles
 or the Imakefile. You should take a look at the beginning of the Imakefile,
 if you need to change one of the parameters.

 After linking, you should be able to just use it as the manual page says.
 But if you want to install it to be use by more persons or if you're just
 curious, you might want to know a bit more about internals.


 Since the main purpose for me was not directly writing a game, but more to
 experiment with some algorithms, many things are configurable in the program.
 All the parameters that would change the executable are to be set in the file
 'global.h'. One important thing is to name a directory, where the program can
 later on read and write data-files for the pixmap-data, that is needed for the
 smooth ball animation in the deluxe-version of pool. If the data-files are not
 accessible, the program has to recompute it, which might take a lot of time.
 (For information, on my 1280 pixel wide screen, I need 37x37=1369 bitmaps for
  half balls and 75x37=2775 bitmaps for full balls in the default size. When
  too many bitmaps will get neccessary you might get memory problems, e.g.
  on X-Terminals.)

 An interresting thing to experiment with might be the Real-Class. Real is
 usually defined as float or double. But there is also a complete inline-class
 available for it. Unfortunately using the class isn't too fast, but this
 probably depends on the smartness of the compiler. (Anyway, does anybody have
 a similar Real-Class implemented by using integer? I could really use it on
 my slow 386).

 There is also the main DEBUG-flag, which enables debugging switches all over
 the source and should usually be unset, even so the switch don't consume too
 much time.

 Constant game parameters like object size and friction are all defined in
 'global.C'. But for not always re-compiling the program when changing them
 they can also be set dynamically in the file 'presets.txt'. The correct syntax
 and the current value can be seen with the option -presets when starting the
 game.


 After installation, I would be pleased if you send me a short note
 just for curiosity and tell me:
 - if there were any problems and what they were
 - if you like it and what you think is missing
 The more notes I get, the higher the chances that I will spend
 more time on that packages.


 Have fun.
```
        __  __     __                __     __  __                 _
       / / / /__  / /___ ___  __  __/ /_   / / / /___  ___  ____  (_)___ _
      / /_/ / _ \/ / __ `__ \/ / / / __/  / /_/ / __ \/ _ \/ __ \/ / __ `/
     / __  /  __/ / / / / / / /_/ / /_   / __  / /_/ /  __/ / / / / /_/ /
  _ /_/ /_/\___/_/_/ /_/ /_/\__,_/\__/  /_/ /_/\____/\___/_/ /_/_/\__, /
 /__________________________________e-mail: Helmut.Hoenig@hub.de______/
```


 Building on modern Linux (2026 port notes):
 ============================================

 Requirements: g++ (or clang++), make, and the X11 development headers
 (Debian/Ubuntu: libx11-dev libxext-dev; Fedora: libX11-devel libXext-devel).
 Then simply:

     make

 'Makefile' is the modern one; Makefile.Linux and Makefile.HP-UX are the
 original 1995 versions and are kept for reference only. Run as before:
 ./xpool, ./xsnooker, ./xcarrom, ./xhockey, ./xcurling, ./xcannon (or
 ./flying -pool etc.). The default is a full-screen override-redirect
 window; use '-size <pixels>' (i.e. -size 1920) for a window or '-no_override'
 for a managed full-screen window.

 What was changed for the port:
 - Old C++ cleaned up so that it compiles with a current compiler (implicit
   int, for-scope, const correctness, struct fd_set, delete[]).
 - The game was written for 8-bit PseudoColor displays with a writable
   colormap: ball colors live in the low bits of a pixel, background, shade
   and cursor layers in separate bit planes, and nearly all drawing uses
   GXxor with plane masks. Modern X servers only offer TrueColor visuals and
   no writable colormap cells, so now (xgraph.C/xgraph.h):
     * all drawing goes into a depth-8 pixmap ('win'), which keeps the
       original rendering code unchanged;
     * XAllocColorCells/XStoreColor are emulated in a 256 entry software
       palette;
     * PresentFrame() converts the pixmap through the palette and copies it
       into the visible TrueColor window ('real_win'), using MIT-SHM when
       available and only the rectangles that were drawn to (the Xlib
       drawing calls are wrapped in xgraph.h to track them).
 - The game is now selected from the basename of argv[0], so ./xpool works.
 - Source files and header files were moved into their respective directories


 History:
 ========

 V6.3  - just a few includes were changed to make the program more portable.
       - a SOLARIS switch was added
 V6.9  - new background colors / new balls / includes Xos.h /
         improved color handling / less ANSI C
 V6.11 - new flags: -nohints, -red, -green, -blue
         minor corrections
 V6.15 - enhanced (much faster on X-Terminals) ball animation
         waits with select(..) to reduce CPU-usage in idle periods
         interpixel-motion for fine adjustment of cursor
 V6.18 - corrections in setting up colors (fvwm now loses the colormap struggle :-)
         enhanced reset feature
         pool9-class
 V6.19 - update for gcc-2.7.0
 V6.20 - removement of the 64bit limitation?
       - Imakefile supplied
