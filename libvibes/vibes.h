#ifndef VIBES_H
#define VIBES_H

/*
 * gitvibes - AI-native git extensions
 *
 * This is the public API header for libvibes.
 * All gitvibes functionality is gated behind VIBES_ENABLED.
 */

#ifdef VIBES_ENABLED

#include <stdint.h>
#include <time.h>

/* Forward declarations */
struct vibes_db;
struct vibes_intent;
struct vibes_task;
struct vibes_agent;
struct vibes_session;

/* ULID: 26 chars + null terminator */
#define VIBES_ULID_LEN 27

/* Generate a new ULID (Universally Unique Lexicographically Sortable Identifier) */
void vibes_ulid_generate(char out[VIBES_ULID_LEN]);

/* Get current timestamp in milliseconds since epoch */
int64_t vibes_timestamp_ms(void);

#endif /* VIBES_ENABLED */
#endif /* VIBES_H */
