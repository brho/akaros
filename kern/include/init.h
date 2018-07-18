/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

extern bool booting;

/**
 * @brief Fetches a given boot commond line parameter.
 *
 * @param base The base pointer from which to start the search. Should be set
 *             to NULL the first time is being called, and, for command line
 *             parameters with multiple instances, should be set to the value
 *             returned by the previous successful call to this API.
 * @param option The name of the option to retrieve. By convention, options
 *               should start with the '-' character.
 * @param param The pointer to the buffer where to store the eventual value of
 *              the parameter (in a "-option=value" format). Can be NULL if the
 *              caller is not interested in the parameter value (because of a
 *              value-less parameter (example "-enable-acpi").
 * @param max_param Maximum length of the @a param buffer, or 0 if @a param is
 *              NULL.
 *
 * @return Returns a pointer different from NULL if the given @a option has been
 *         successfully parsed, or NULL otherwise. The returned pointer should
 *         be treated as opaque value by the caller, and used only for the
 *         following calls to this API, in case of multiple instance parameters
 *         (example "-map=0x12345678,0x1000 -map=0x987654321,0x2000").
 */
const char *get_boot_option(const char *base, const char *option, char *param,
							size_t max_param);
