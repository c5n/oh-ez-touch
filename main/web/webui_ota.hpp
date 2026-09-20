/**
 * @file webui_ota.hpp
 *
 * The firmware upload, as two web handlers. See webui_ota.cpp.
 */
#ifndef WEBUI_OTA_HPP
#define WEBUI_OTA_HPP

#include "webui_transport.h"

/** GET /update: the upload form. */
void webui_ota_handle_form(webui_request_t *req);

/** POST /update: the multipart body, streamed into the inactive app slot. */
void webui_ota_handle_upload(webui_request_t *req);

#endif /* WEBUI_OTA_HPP */
