#include "ttrigrams.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t gram; size_t document; } posting;
typedef struct { char *text; size_t length; } document;
struct ttrigrams {
    document *documents;
    size_t count;
    posting *postings;
    size_t posting_count;
};

static size_t bounded_length(const char *text, size_t limit) {
    if (!text) { return SIZE_MAX; }
    for (size_t i = 0; i <= limit; ++i) {
        if (!text[i]) { return i; }
    }
    return SIZE_MAX;
}

static int compare_grams(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int compare_postings(const void *a, const void *b) {
    const posting *x = a, *y = b;
    if (x->gram != y->gram) { return (x->gram > y->gram) - (x->gram < y->gram); }
    return (x->document > y->document) - (x->document < y->document);
}

static size_t unique_grams(const char *text, size_t length, uint32_t *grams) {
    if (length < 3) { return 0; }
    const unsigned char *bytes = (const unsigned char *)text;
    size_t count = length - 2;
    for (size_t i = 0; i < count; ++i) {
        grams[i] = ((uint32_t)bytes[i] << 16) | ((uint32_t)bytes[i + 1] << 8) | bytes[i + 2];
    }
    qsort(grams, count, sizeof(*grams), compare_grams);
    size_t unique = 1;
    for (size_t i = 1; i < count; ++i) {
        if (grams[i] != grams[unique - 1]) { grams[unique++] = grams[i]; }
    }
    return unique;
}

void ttrigrams_destroy(ttrigrams *index) {
    if (!index) { return; }
    for (size_t i = 0; i < index->count; ++i) { free(index->documents[i].text); }
    free(index->documents);
    free(index->postings);
    free(index);
}

ttrigrams *ttrigrams_create(const char *const *texts, size_t count) {
    if ((!texts && count) || count > SIZE_MAX / sizeof(document)) { return NULL; }
    ttrigrams *index = calloc(1, sizeof(*index));
    if (!index) { return NULL; }
    index->documents = count ? calloc(count, sizeof(document)) : NULL;
    if (count && !index->documents) { ttrigrams_destroy(index); return NULL; }
    index->count = count;
    size_t capacity = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = bounded_length(texts[i], TTRIGRAMS_MAX_TEXT_LENGTH);
        if (length == SIZE_MAX || (length >= 3 && capacity > SIZE_MAX / sizeof(posting) - (length - 2))) {
            ttrigrams_destroy(index);
            return NULL;
        }
        index->documents[i].length = length;
        index->documents[i].text = malloc(length + 1);
        if (!index->documents[i].text) { ttrigrams_destroy(index); return NULL; }
        memcpy(index->documents[i].text, texts[i], length + 1);
        if (length >= 3) { capacity += length - 2; }
    }
    index->postings = capacity ? malloc(capacity * sizeof(posting)) : NULL;
    if (capacity && !index->postings) { ttrigrams_destroy(index); return NULL; }
    uint32_t grams[TTRIGRAMS_MAX_TEXT_LENGTH];
    for (size_t i = 0; i < count; ++i) {
        size_t total = unique_grams(index->documents[i].text, index->documents[i].length, grams);
        for (size_t j = 0; j < total; ++j) {
            index->postings[index->posting_count++] = (posting){grams[j], i};
        }
    }
    if (index->posting_count) {
        qsort(index->postings, index->posting_count, sizeof(posting), compare_postings);
    }
    return index;
}

static size_t first_posting(const ttrigrams *index, uint32_t gram) {
    size_t low = 0, high = index->posting_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (index->postings[middle].gram < gram) { low = middle + 1; }
        else { high = middle; }
    }
    return low;
}

static unsigned short minimum(unsigned short a, unsigned short b) { return a < b ? a : b; }

static int substring_distance(const char *text, size_t text_length, const char *query, size_t query_length) {
    unsigned short previous[TTRIGRAMS_MAX_QUERY_LENGTH + 1];
    unsigned short current[TTRIGRAMS_MAX_QUERY_LENGTH + 1];
    for (size_t i = 0; i <= query_length; ++i) { previous[i] = (unsigned short)i; }
    int best = (int)query_length;
    for (size_t i = 0; i < text_length; ++i) {
        // A new match may start at any byte; text outside the window is free.
        current[0] = 0;
        for (size_t j = 1; j <= query_length; ++j) {
            current[j] = minimum(minimum(current[j - 1] + 1, previous[j] + 1),
                                 previous[j - 1] + (text[i] != query[j - 1]));
        }
        if (current[query_length] < best) { best = current[query_length]; }
        memcpy(previous, current, (query_length + 1) * sizeof(*previous));
    }
    return best;
}

int ttrigrams_search(const ttrigrams *index, const char *query, int max_distance,
                     ttrigrams_visitor visitor, void *context) {
    size_t length = bounded_length(query, TTRIGRAMS_MAX_QUERY_LENGTH);
    if (!index || !visitor || !length || length == SIZE_MAX || max_distance < 0 || max_distance > 2) {
        return -1;
    }
    uint32_t grams[TTRIGRAMS_MAX_QUERY_LENGTH];
    size_t total = unique_grams(query, length, grams);
    size_t *shared = index->count ? calloc(index->count, sizeof(size_t)) : NULL;
    if (index->count && !shared) { return -1; }
    for (size_t i = 0; i < total; ++i) {
        for (size_t j = first_posting(index, grams[i]); j < index->posting_count && index->postings[j].gram == grams[i]; ++j) {
            ++shared[index->postings[j].document];
        }
    }
    int status = 0;
    for (size_t i = 0; i < index->count; ++i) {
        if (total && !shared[i]) { continue; }
        int distance = 0;
        if (!strstr(index->documents[i].text, query)) {
            if (!total || !max_distance) { continue; }
            distance = substring_distance(index->documents[i].text, index->documents[i].length, query, length);
            if (distance > max_distance) { continue; }
        }
        ttrigrams_match match = {i, distance, shared[i], total};
        status = visitor(&match, context);
        if (status) { break; }
    }
    free(shared);
    return status;
}
