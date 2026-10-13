/* Holds the ICCCM/EWMH atoms that JUCE's Linux backend looks up with
   XInternAtom (onlyIfExist=True) when it realises its first window.

   Why this exists: when no window manager has ever run on an X server, atoms like
   WM_TAKE_FOCUS or _NET_WM_PING do not exist, so JUCE gets None back and writes a
   zero atom into WM_PROTOCOLS on the first window. The X server then raises BadAtom,
   Xlib's default error handler calls exit(), and any JUCE app that creates windows —
   the test runner, pluginval, the standalone — dies mid-run.

   Atoms normally live for as long as the server does, but some X servers (Xvfb among
   them) garbage-collect atoms once the client that interned them disconnects. So
   this program doesn't exit: it interns the atoms, stays connected, and is killed by
   the script that starts it (scripts/linux/run-tests.sh). On a desktop session a
   window manager has created these atoms long ago, and nothing needs this.

   Usage: intern-x-atoms [seconds] — with an argument it exits by itself after that
   many seconds, which is handy for manual runs; with no argument it holds forever.
*/
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

static volatile sig_atomic_t stopRequested = 0;

static void stopOnSignal (int)
{
    stopRequested = 1;
}

int main (int argc, char** argv)
{
    const long holdSeconds = (argc > 1) ? strtol (argv[1], NULL, 10) : -1;

    signal (SIGTERM, stopOnSignal);
    signal (SIGINT, stopOnSignal);

    Display* display = XOpenDisplay (0);

    if (display == NULL)
    {
        fputs ("intern-x-atoms: cannot open the X display\n", stderr);
        return 1;
    }

    static const char* const atomNames[] = {
        "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING",
        "WM_CHANGE_STATE", "WM_STATE",
        "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN",
        "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT",
        "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO",
        "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_ABOVE"
    };

    for (unsigned i = 0; i < sizeof (atomNames) / sizeof (atomNames[0]); ++i)
        XInternAtom (display, atomNames[i], False);

    XFlush (display);

    for (long elapsed = 0; stopRequested == 0 && (holdSeconds < 0 || elapsed < holdSeconds); ++elapsed)
        if (holdSeconds < 0)
            pause();
        else
            sleep (1);

    XCloseDisplay (display);
    return 0;
}
