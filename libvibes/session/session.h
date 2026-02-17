#ifndef LIBVIBES_SESSION_SESSION_H
#define LIBVIBES_SESSION_SESSION_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include <stdint.h>

struct vibes_db;
struct repository;

/*
 * A coding session with context.
 */
struct vibes_session {
	char id[27];
	char *name;
	char *description;
	int64_t created_at;
	int64_t ended_at;
};

/* --- Session management (session.c) --- */

int vibes_session_start(struct vibes_db *db, const char *name,
			struct vibes_session *session);
int vibes_session_end(struct vibes_db *db, const char *session_id);
int vibes_session_list(struct vibes_db *db, struct string_list *out);
int vibes_session_get(struct vibes_db *db, const char *id,
		      struct vibes_session *session);
void vibes_session_free(struct vibes_session *session);

/* --- Context snapshots (context-snapshot.c) --- */

int vibes_snapshot_create(struct vibes_db *db, struct repository *repo,
			  const char *session_id);

/* --- Context restore (context-restore.c) --- */

int vibes_snapshot_restore(struct vibes_db *db, const char *session_id);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_SESSION_SESSION_H */
