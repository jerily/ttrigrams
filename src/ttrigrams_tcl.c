#include <tcl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ttrigrams.h"

#if TCL_MAJOR_VERSION < 9
typedef int Tcl_Size;
#endif

static int error(Tcl_Interp *interp, const char *message) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(message, -1));
    return TCL_ERROR;
}

static const char *get_text(Tcl_Interp *interp, Tcl_Obj *object, Tcl_Size limit, int allow_empty) {
    Tcl_Size length;
    const char *text = Tcl_GetStringFromObj(object, &length);
    if ((!allow_empty && !length) || length > limit || memchr(text, '\0', (size_t)length)) {
        error(interp, "invalid string length or embedded NUL");
        return NULL;
    }
    for (Tcl_Size i = 0; i + 1 < length; ++i) {
        if ((unsigned char)text[i] == 0xc0 && (unsigned char)text[i + 1] == 0x80) {
            error(interp, "embedded NUL is not supported");
            return NULL;
        }
    }
    return text;
}

static void delete_index(void *data) { ttrigrams_destroy(data); }

static int append_match(const ttrigrams_match *match, void *context) {
    Tcl_Obj *fields[] = {
        Tcl_NewStringObj("document", -1), Tcl_NewWideIntObj((Tcl_WideInt)match->document),
        Tcl_NewStringObj("distance", -1), Tcl_NewIntObj(match->distance),
        Tcl_NewStringObj("shared_trigrams", -1), Tcl_NewWideIntObj((Tcl_WideInt)match->shared_trigrams),
        Tcl_NewStringObj("query_trigrams", -1), Tcl_NewWideIntObj((Tcl_WideInt)match->query_trigrams)
    };
    return Tcl_ListObjAppendElement(NULL, context, Tcl_NewListObj(8, fields));
}

static int index_command(void *data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *operations[] = {"search", "destroy", "serialize", "count", NULL};
    enum {SEARCH, DESTROY, SERIALIZE, COUNT};
    int operation;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "search|destroy|serialize|count ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], operations, "operation", TCL_EXACT, &operation) != TCL_OK) {
        return TCL_ERROR;
    }
    if (operation == COUNT) {
        if (objc != 2) { Tcl_WrongNumArgs(interp, 2, objv, NULL); return TCL_ERROR; }
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)ttrigrams_count(data)));
        return TCL_OK;
    }
    if (operation == SERIALIZE) {
        if (objc != 2) { Tcl_WrongNumArgs(interp, 2, objv, NULL); return TCL_ERROR; }
        size_t length;
        unsigned char *bytes = ttrigrams_serialize(data, &length);
        if (!bytes) { return error(interp, "cannot serialize index"); }
        if (length > INT_MAX) { free(bytes); return error(interp, "snapshot exceeds Tcl size limit"); }
        Tcl_SetObjResult(interp, Tcl_NewByteArrayObj(bytes, (Tcl_Size)length));
        free(bytes);
        return TCL_OK;
    }
    if (operation == DESTROY) {
        if (objc != 2) { Tcl_WrongNumArgs(interp, 2, objv, NULL); return TCL_ERROR; }
        return Tcl_DeleteCommand(interp, Tcl_GetString(objv[0]));
    }
    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "query ?max_distance?");
        return TCL_ERROR;
    }
    const char *query = get_text(interp, objv[2], TTRIGRAMS_MAX_QUERY_LENGTH, 0);
    if (!query) { return TCL_ERROR; }
    int distance = 2;
    if (objc == 4 && Tcl_GetIntFromObj(interp, objv[3], &distance) != TCL_OK) { return TCL_ERROR; }
    if (distance < 0 || distance > 2) { return error(interp, "max_distance must be 0, 1 or 2"); }
    Tcl_Obj *matches = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(matches);
    int status = ttrigrams_search(data, query, distance, append_match, matches);
    if (!status) { Tcl_SetObjResult(interp, matches); }
    Tcl_DecrRefCount(matches);
    return status ? error(interp, "trigram search failed") : TCL_OK;
}

static int create_command(void *data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    (void)data;
    if (objc != 3) { Tcl_WrongNumArgs(interp, 1, objv, "command_name texts"); return TCL_ERROR; }
    const char *name = get_text(interp, objv[1], 255, 0);
    if (!name) { return TCL_ERROR; }
    Tcl_CmdInfo info;
    if (Tcl_GetCommandInfo(interp, name, &info)) { return error(interp, "command already exists"); }
    Tcl_Size count;
    Tcl_Obj **objects;
    if (Tcl_ListObjGetElements(interp, objv[2], &count, &objects) != TCL_OK) { return TCL_ERROR; }
    if ((size_t)count > SIZE_MAX / sizeof(char *)) { return error(interp, "too many documents"); }
    const char **texts = count ? malloc((size_t)count * sizeof(char *)) : NULL;
    if (count && !texts) { return error(interp, "cannot allocate document list"); }
    for (Tcl_Size i = 0; i < count; ++i) {
        texts[i] = get_text(interp, objects[i], TTRIGRAMS_MAX_TEXT_LENGTH, 1);
        if (!texts[i]) { free(texts); return TCL_ERROR; }
    }
    ttrigrams *index = ttrigrams_create(texts, (size_t)count);
    free(texts);
    if (!index) { return error(interp, "cannot allocate trigram index"); }
    Tcl_Command command = Tcl_CreateObjCommand(interp, name, index_command, index, delete_index);
    if (!command) { ttrigrams_destroy(index); return TCL_ERROR; }
    Tcl_Obj *full_name = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, command, full_name);
    Tcl_SetObjResult(interp, full_name);
    return TCL_OK;
}

static int load_command(void *data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    (void)data;
    if (objc != 3) { Tcl_WrongNumArgs(interp, 1, objv, "command_name bytes"); return TCL_ERROR; }
    const char *name = get_text(interp, objv[1], 255, 0);
    if (!name) { return TCL_ERROR; }
    Tcl_CmdInfo info;
    if (Tcl_GetCommandInfo(interp, name, &info)) { return error(interp, "command already exists"); }
    Tcl_Size length;
    const unsigned char *bytes = Tcl_GetByteArrayFromObj(objv[2], &length);
    ttrigrams *index = ttrigrams_load(bytes, (size_t)length);
    if (!index) { return error(interp, "invalid index snapshot or allocation failure"); }
    Tcl_Command command = Tcl_CreateObjCommand(interp, name, index_command, index, delete_index);
    if (!command) { ttrigrams_destroy(index); return TCL_ERROR; }
    Tcl_Obj *full_name = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, command, full_name);
    Tcl_SetObjResult(interp, full_name);
    return TCL_OK;
}

DLLEXPORT int Ttrigrams_Init(Tcl_Interp *interp) {
#if TCL_MAJOR_VERSION >= 9
    if (!Tcl_InitStubs(interp, "9.0", 0)) { return TCL_ERROR; }
#else
    if (!Tcl_InitStubs(interp, "8.6", 0)) { return TCL_ERROR; }
#endif
    if (!Tcl_FindNamespace(interp, "::ttrigrams", NULL, 0) &&
        !Tcl_CreateNamespace(interp, "::ttrigrams", NULL, NULL)) { return TCL_ERROR; }
    Tcl_CreateObjCommand(interp, "::ttrigrams::create", create_command, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::ttrigrams::load", load_command, NULL, NULL);
    return Tcl_PkgProvide(interp, "ttrigrams", TTRIGRAMS_VERSION);
}
