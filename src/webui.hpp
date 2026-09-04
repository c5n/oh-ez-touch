#ifndef WEBUI_HPP
#define WEBUI_HPP

#include "config.hpp"

class WebServer;

/* Register the configuration pages on an existing server. The server is
 * passed in rather than owned here for as long as AutoConnect still creates
 * it; once it is gone this takes the Config alone. */
void webui_setup(Config *config, WebServer *server);

/* Install the catch-all redirect. Separate from webui_setup() only because
 * AutoConnect's Portal.begin() installs an onNotFound of its own and would
 * clobber ours; call this after it. Folds back into webui_setup() once
 * AutoConnect is gone. */
void webui_install_not_found(void);

#endif // WEBUI_HPP
