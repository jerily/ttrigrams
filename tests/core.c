#include "ttrigrams.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { ttrigrams_match values[16]; size_t count; } results;
static int collect(const ttrigrams_match *match, void *context) {
    results *found = context;
    assert(found->count < 16);
    found->values[found->count++] = *match;
    return 0;
}
static int stop(const ttrigrams_match *match, void *context) {
    (void)match; (void)context;
    return 42;
}
int main(void) {
    const char *texts[] = {"web server for tcl", "encryption toolkit", "aaaaaa", "abc----bcd----cde", "", "tk"};
    ttrigrams *index = ttrigrams_create(texts, 6);
    assert(index);
    results found = {0};
    assert(ttrigrams_search(index, "web", 2, collect, &found) == 0);
    assert(found.count == 1 && found.values[0].document == 0 && found.values[0].distance == 0);
    assert(found.values[0].shared_trigrams == 1 && found.values[0].query_trigrams == 1);
    found.count = 0;
    assert(ttrigrams_search(index, "encrytion", 2, collect, &found) == 0);
    assert(found.count == 1 && found.values[0].document == 1 && found.values[0].distance == 1);
    found.count = 0;
    assert(ttrigrams_search(index, "aaaa", 0, collect, &found) == 0);
    assert(found.count == 1 && found.values[0].shared_trigrams == 1 && found.values[0].query_trigrams == 1);
    found.count = 0;
    assert(ttrigrams_search(index, "abcde", 0, collect, &found) == 0 && found.count == 0);
    assert(ttrigrams_search(index, "tk", 2, collect, &found) == 0);
    assert(found.count == 1 && found.values[0].document == 5 && found.values[0].distance == 0);
    assert(ttrigrams_search(index, "web", 2, stop, NULL) == 42);
    assert(ttrigrams_search(index, "", 2, collect, &found) == -1);
    assert(ttrigrams_search(index, "web", 3, collect, &found) == -1);
    assert(ttrigrams_search(NULL, "web", 2, collect, &found) == -1);
    ttrigrams_destroy(index);
    index = ttrigrams_create(NULL, 0);
    assert(index);
    found.count = 0;
    assert(ttrigrams_search(index, "web", 2, collect, &found) == 0 && !found.count);
    ttrigrams_destroy(index);
    assert(!ttrigrams_create(NULL, 1));
    assert(!ttrigrams_create(texts, SIZE_MAX));
    char text[TTRIGRAMS_MAX_TEXT_LENGTH + 2];
    memset(text, 'a', sizeof(text)); text[sizeof(text) - 1] = 0;
    const char *boundary[] = {text};
    assert(!ttrigrams_create(boundary, 1));
    text[TTRIGRAMS_MAX_TEXT_LENGTH] = 0;
    index = ttrigrams_create(boundary, 1);
    assert(index);
    text[0] = 'b'; // The index owns a copy.
    found.count = 0;
    assert(ttrigrams_search(index, "aaa", 0, collect, &found) == 0 && found.count == 1);
    ttrigrams_destroy(index);
    ttrigrams_destroy(NULL);
    puts("Trigram candidates, verification, ownership and limits passed");
    return 0;
}
