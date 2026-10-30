#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include "ncurses_convenience.h"
#include "vis.h"
#include "libdevcheck.h"

void clear_body(void) {
    WINDOW *body = derwin(stdscr, LINES, COLS, 0, 0);
    wclear(body);
    wrefresh(body);
    delwin(body);

    WINDOW *footer = subwin(stdscr, 1, COLS, LINES-1, 0);
    wbkgd(footer, COLOR_PAIR(MY_COLOR_WHITE_ON_BLUE));
    wprintw(footer, " WHDD rev. " WHDD_VERSION);
    wrefresh(footer);
    delwin(footer);
}

/*
 * Mouse reporting is terminal state, not program state: tmux, vim or a shell rc
 * that turned it on leaves the terminal writing mouse reports into our stdin for
 * whoever reads next. An SGR mouse release report ends with a lowercase 'm', so a
 * plain click used to be taken for the "press m for menu" key.
 */
void ui_mouse_reporting_disable(void) {
    mousemask(0, NULL);
    printf("\033[?1000l\033[?1002l\033[?1003l\033[?1006l");
    fflush(stdout);
    flushinp();
}

static volatile sig_atomic_t ui_quit_requested;

static void ui_quit_signal_handler(int signo) {
    (void) signo;
    ui_quit_requested = 1;
}

/*
 * Park on the finished test's screen. Only Ctrl+C (quit) and Ctrl+M (back to the
 * menu) leave it; every other key, mouse report, resize or EOF keeps it on screen.
 */
void ui_wait_final_screen(void) {
    /* Input generated while the test was running is queued in the terminal and must
     * not be allowed to dismiss the screen the moment it appears. */
    flushinp();

    if (!isatty(STDIN_FILENO))
        return;

    struct sigaction act;
    struct sigaction old_act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = ui_quit_signal_handler;
    sigemptyset(&act.sa_mask);
    /* Without SA_RESTART, getch() returns ERR on signal so the flag gets noticed. */
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, &old_act))
        return;

    for (;;) {
        int ch;

        if (ui_quit_requested)
            break;
        ch = getch();
        if (ui_quit_requested)
            break;
        switch (ch) {
            case 3:  /* Ctrl+C */
            case 0x0d:  /* Ctrl+M */
            case 0x0a:  /* Enter, when nl() maps CR to LF */
            case KEY_ENTER:
                sigaction(SIGINT, &old_act, NULL);
                return;
            case KEY_MOUSE: {
                MEVENT ev;
                (void) getmouse(&ev);
                break;
            }
            case ERR:
                napms(50);
                break;
            default:
                break;
        }
    }

    /* cbreak() clears ISIG, so Ctrl+C usually reaches us as byte 0x03; the
     * sigaction above covers the case where it arrives as a signal instead. */
    exit(0);
}

