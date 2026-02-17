#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "builtin.h"
#ifdef VIBES_ENABLED
#include "repository.h"
#include "parse-options.h"
#include "libvibes/agents/agents.h"

/*
 * git vibes-decompose: dual-purpose command.
 *
 * Normal mode: alias for git vibes-commit (analyze staged changes,
 * create atomic commits). The name "decompose" emphasizes retroactive splitting.
 *
 * Worker mode (--worker): entry point for agent worker processes
 * spawned by the orchestrator via fork/exec. Receives task assignment
 * via command-line arguments and communicates progress via IPC socket.
 */

int cmd_vibes_commit(int argc, const char **argv, const char *prefix,
		     struct repository *repo);

int cmd_vibes_decompose(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	const char *task_id = NULL;
	const char *agent_id = NULL;
	const char *db_path = NULL;
	int ipc_fd = -1;
	int worker_mode = 0;
	int i;

	/*
	 * Check for --worker flag before parse-options, since
	 * the worker args are specific to agent mode.
	 */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--worker")) {
			worker_mode = 1;
			break;
		}
	}

	if (!worker_mode)
		return cmd_vibes_commit(argc, argv, prefix, repo);

	/* Parse worker-specific arguments */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--task-id") && i + 1 < argc)
			task_id = argv[++i];
		else if (!strcmp(argv[i], "--agent-id") && i + 1 < argc)
			agent_id = argv[++i];
		else if (!strcmp(argv[i], "--db-path") && i + 1 < argc)
			db_path = argv[++i];
		else if (!strcmp(argv[i], "--ipc-fd") && i + 1 < argc)
			ipc_fd = atoi(argv[++i]);
	}

	if (!task_id || !agent_id || !db_path)
		die("gitvibes: --worker requires --task-id, --agent-id, "
		    "and --db-path");

	return vibes_agent_worker_main(task_id, agent_id, db_path, ipc_fd);
}
#else
int cmd_vibes_decompose(int argc, const char **argv, const char *prefix,
			struct repository *repo)
{
	die("gitvibes: not compiled with VIBES_ENABLED");
}
#endif /* VIBES_ENABLED */
