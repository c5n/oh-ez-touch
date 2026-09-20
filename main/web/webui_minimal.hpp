/**
 * @file webui_minimal.hpp
 *
 * The recovery firmware's web interface: a status page, the REST status the
 * fleet manager probes, a restart, and the /update upload. Registered and
 * started by webui_minimal_setup(), which is the whole of the interface.
 */
#ifndef WEBUI_MINIMAL_HPP
#define WEBUI_MINIMAL_HPP

void webui_minimal_setup(void);

#endif
