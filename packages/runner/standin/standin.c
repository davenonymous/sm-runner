/* Copyright 2026 davenonymous
 *
 * A stand-in for a game binary (server_srv.so, engine_srv.so). SourceMod's
 * game config parser looks such binaries up by their CreateInterface export
 * before it scans them for signatures; with a stand-in the scan finds
 * nothing, as for a game without those functions, instead of reporting an
 * unknown library. It offers no interfaces.
 */
__attribute__((visibility("default"))) void* CreateInterface(const char* name, int* returnCode) {
	(void)name;
	if (returnCode) {
		*returnCode = 1; /* IFACE_FAILED */
	}
	return 0;
}
