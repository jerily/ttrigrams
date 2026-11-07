#ifndef TTRIGRAMS_H
#define TTRIGRAMS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTRIGRAMS_MAX_TEXT_LENGTH 4096
#define TTRIGRAMS_MAX_QUERY_LENGTH 255

typedef struct ttrigrams ttrigrams;
typedef struct {
    size_t document;
    int distance;
    size_t shared_trigrams;
    size_t query_trigrams;
} ttrigrams_match;
typedef int (*ttrigrams_visitor)(const ttrigrams_match *match, void *context);

/* Copies NUL-terminated byte strings. Empty documents and an empty index are valid.
 * Returns NULL for invalid input or allocation failure. Indexes are immutable. */
ttrigrams *ttrigrams_create(const char *const *texts, size_t count);
void ttrigrams_destroy(ttrigrams *index);
/* Portable snapshot of documents and sorted postings. Caller frees the returned
 * buffer with free(). Load restores postings without generating/sorting grams.
 * NULL means invalid input or allocation failure. Serialize clears length on
 * failure. Snapshot counts and document IDs are limited to UINT32_MAX. */
unsigned char *ttrigrams_serialize(const ttrigrams *index, size_t *length);
ttrigrams *ttrigrams_load(const void *bytes, size_t length);
size_t ttrigrams_count(const ttrigrams *index);
/* Finds literal substrings and suggestions within 0..2 edits of a substring.
 * Fuzzy candidates must share a query trigram. Queries shorter than three bytes
 * use literal matching only. Results follow input document order.
 * Case folding and ranking belong to callers. Returns -1 for invalid input or
 * allocation failure, 0 on success, or a nonzero visitor result to stop early. */
int ttrigrams_search(const ttrigrams *index, const char *query, int max_distance,
                     ttrigrams_visitor visitor, void *context);

#ifdef __cplusplus
}
#endif
#endif
