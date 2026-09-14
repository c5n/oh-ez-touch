/**
 * @file testif.hpp
 *
 * The simulator's control interface: a UDP command channel on the loopback
 * interface, so that the panel can be driven and inspected from a script
 * instead of by hand.
 *
 * There has always been a way to arrange a *starting* state on the host --
 * OHEZ_ITEM walks to a control, OHEZ_SETTINGS opens a tab, OHEZ_THEME picks a
 * look -- and never a way to do anything after that, or to find out what
 * happened. This is the other half: send a touch, ask what is on screen, read
 * the telemetry, pull the framebuffer.
 *
 * Everything here runs on the task that calls testif_loop(), which is the one
 * task that owns LVGL (see the header of main.cpp). That is the whole reason
 * the socket is polled rather than served by a task of its own: a command is
 * carried out on the spot, by the only task allowed to carry it out, with no
 * queue and no deferral in between. It also sidesteps what shaped
 * webui_transport_linux.cpp -- on the FreeRTOS POSIX simulator only select()
 * is wrapped, so a blocking socket call parks the whole cooperative scheduler.
 * A non-blocking recvfrom() cannot.
 *
 * Simulator only. On the device every entry point below is an empty stub, so
 * no caller needs a target guard. What it would take to put it on a panel is
 * the last section of doc/test-interface.md.
 */
#ifndef TESTIF_HPP
#define TESTIF_HPP

#include <stdbool.h>

/**
 * Open the socket and register the synthetic pointer.
 *
 * After port_indev_init(), because the pointer it adds is a second input device
 * beside the SDL mouse that call creates, and both want a display to exist.
 *
 * Bound to 127.0.0.1 only. The channel has no authentication and can press
 * anything on the screen, so it is not offered to the network; the web
 * interface next to it has no authentication either, but that one cannot
 * operate the panel. OHEZ_TESTIF=0 turns it off and OHEZ_TESTIF_PORT moves it.
 */
void testif_setup(void);

/** Take one request, if one has arrived. Called from ohez_loop(). */
void testif_loop(void);

/**
 * True while a screenshot is being read out of the framebuffer.
 *
 * The capture streams straight out of LVGL's own frame buffer rather than
 * copying 150 KB to be safe, so for the few milliseconds that takes, the frame
 * has to stop changing underneath it -- main.cpp skips lv_timer_handler()
 * while this is true. The hold expires by itself after a moment, so a client
 * that dies mid-transfer cannot freeze the UI.
 *
 * False always on the device, where the compiler folds the call site away.
 */
bool testif_frame_hold(void);

#endif /* TESTIF_HPP */
