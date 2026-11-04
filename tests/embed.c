#include <tcl.h>
extern int Ttrigrams_Init(Tcl_Interp *interp);
static int initialize(Tcl_Interp *interp) {
    return Tcl_Init(interp);
}
int main(int argc, char **argv) {
#if TCL_MAJOR_VERSION >= 9
    TclZipfs_AppHook(&argc, &argv);
    Tcl_StaticLibrary(NULL, "Ttrigrams", Ttrigrams_Init, NULL);
#else
    Tcl_FindExecutable(argv[0]);
    Tcl_StaticPackage(NULL, "Ttrigrams", Ttrigrams_Init, NULL);
#endif
    Tcl_Main(argc, argv, initialize);
    return 0;
}
