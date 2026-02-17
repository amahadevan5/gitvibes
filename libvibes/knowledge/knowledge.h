#ifndef LIBVIBES_KNOWLEDGE_KNOWLEDGE_H
#define LIBVIBES_KNOWLEDGE_KNOWLEDGE_H
#ifdef VIBES_ENABLED

#include "strbuf.h"
#include "string-list.h"
#include <stdint.h>

struct vibes_db;
struct repository;

/*
 * Knowledge graph node types.
 */
enum kg_node_type {
	KG_NODE_FILE = 0,
	KG_NODE_MODULE,
	KG_NODE_FUNCTION,
	KG_NODE_CLASS,
	KG_NODE_TYPE,
	KG_NODE_IMPORT,
	KG_NODE_TEST,
};

/*
 * Knowledge graph edge types.
 */
enum kg_edge_type {
	KG_EDGE_IMPORTS = 0,
	KG_EDGE_CALLS,
	KG_EDGE_EXTENDS,
	KG_EDGE_IMPLEMENTS,
	KG_EDGE_TESTS,
	KG_EDGE_DEPENDS_ON,
};

/*
 * Risk level for impact analysis.
 */
enum risk_level {
	RISK_LOW = 0,
	RISK_MEDIUM,
	RISK_HIGH,
	RISK_CRITICAL,
};

/*
 * A node in the knowledge graph.
 */
struct kg_node {
	int64_t id;
	enum kg_node_type type;
	char *name;
	char *file_path;
	int start_line;
	int end_line;
	char *language;
};

/*
 * An edge in the knowledge graph.
 */
struct kg_edge {
	int64_t source_id;
	int64_t target_id;
	enum kg_edge_type type;
};

/*
 * Impact analysis result.
 */
struct impact_analysis {
	struct string_list direct;
	struct string_list transitive;
	struct string_list tests;
	enum risk_level risk;
	char *reasoning;
};

/*
 * Conflict prediction result.
 */
struct conflict_prediction {
	double probability;
	struct string_list shared_files;
	char *recommendation;
};

/* --- Tree-sitter parser (ts-parser.c) --- */

int vibes_ts_init(void);
void vibes_ts_shutdown(void);
const char *vibes_ts_detect_language(const char *path);

/*
 * Extract symbols (functions, classes, imports) from a file.
 * Appends extracted nodes to the database.
 * Returns number of symbols extracted, or -1 on error.
 */
int vibes_ts_extract_symbols(struct vibes_db *db,
			     const char *file_path,
			     const char *language);

/* --- Knowledge graph (graph.c) --- */

int vibes_kg_index_repo(struct vibes_db *db, struct repository *repo,
			int max_files);
int vibes_kg_index_file(struct vibes_db *db, const char *file_path);
int vibes_kg_get_dependents(struct vibes_db *db, const char *file_path,
			    struct string_list *out);
int vibes_kg_get_dependencies(struct vibes_db *db, const char *file_path,
			      struct string_list *out);
int vibes_kg_get_related_tests(struct vibes_db *db, const char *file_path,
			       struct string_list *out);
int vibes_kg_search(struct vibes_db *db, const char *symbol_name,
		    struct string_list *out);

/* --- Impact analysis (impact.c) --- */

int vibes_impact_analyze(struct vibes_db *db,
			 const struct string_list *file_paths,
			 struct impact_analysis *result);
void vibes_impact_free(struct impact_analysis *result);

/* --- Conflict prediction (conflict-predict.c) --- */

int vibes_predict_conflicts(struct vibes_db *db,
			    const char *task_a_id,
			    const char *task_b_id,
			    struct conflict_prediction *result);
void vibes_conflict_prediction_free(struct conflict_prediction *result);

/* Utility */
const char *kg_node_type_str(enum kg_node_type t);
const char *kg_edge_type_str(enum kg_edge_type t);
const char *risk_level_str(enum risk_level r);

#endif /* VIBES_ENABLED */
#endif /* LIBVIBES_KNOWLEDGE_KNOWLEDGE_H */
