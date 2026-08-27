/**************************************************************************/
/*  godot_nx.cpp                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "main/main.h"
#include "os_nx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool romfs_mounted = false;

static const char *NX_LOG_PATH = "sdmc:/switch/godot_nx.log";

// Horizon puts no console behind stdout, so in a build without nxlink every
// print is discarded. What Godot writes before its own user:// logger exists is
// the only record a failed start leaves, so it goes to the SD card instead.
static void nx_redirect_stdio() {
	if (freopen(NX_LOG_PATH, "w", stdout) == nullptr) {
		return;
	}
	setvbuf(stdout, nullptr, _IOLBF, 0);
	dup2(fileno(stdout), STDERR_FILENO);
}

// Applet mode is what hbl gives a title it did not override, and what every
// emulator gives homebrew. The heap is a few hundred megabytes rather than the
// console's whole memory, so a large project can fail here; the size is printed
// because that failure is otherwise unreadable.
static void nx_print_environment() {
	u64 total = 0;
	u64 used = 0;
	svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
	svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
	printf("NX: applet type %d, heap %llu MiB total, %llu MiB used.\n",
			(int)appletGetAppletType(),
			(unsigned long long)(total >> 20),
			(unsigned long long)(used >> 20));
}

static void nx_services_init() {
	socketInitializeDefault();
#ifdef NXLINK_ENABLED
	nxlinkStdio();
#endif

#ifndef NXLINK_ENABLED
	nx_redirect_stdio();
#endif
	nx_print_environment();

	Result rc = romfsInit();
	romfs_mounted = R_SUCCEEDED(rc);
	if (!romfs_mounted) {
		printf("NX: romfsInit() failed (0x%08x), romfs:/game.pck will not be available.\n", rc);
		fflush(stdout);
	}

	setInitialize();
	csrngInitialize();
}

static void nx_services_exit() {
	csrngExit();
	setExit();
	if (romfs_mounted) {
		romfsExit();
	}
	socketExit();
}

static bool romfs_has_game_pck() {
	FILE *f = fopen("romfs:/game.pck", "rb");
	if (!f) {
		return false;
	}
	fclose(f);
	return true;
}

int main(int argc, char *argv[]) {
	nx_services_init();

	const char *execpath = (argc > 0 && argv[0]) ? argv[0] : "sdmc:/switch/godot.nro";
	const int arg_count = argc > 0 ? argc - 1 : 0;

	static char main_pack_flag[] = "--main-pack";
	static char main_pack_path[] = "romfs:/game.pck";

	char **args = arg_count > 0 ? &argv[1] : nullptr;
	int args_count = arg_count;
	char **romfs_args = nullptr;

	if (romfs_mounted && romfs_has_game_pck()) {
		romfs_args = (char **)malloc(sizeof(char *) * (arg_count + 2));
		if (romfs_args) {
			romfs_args[0] = main_pack_flag;
			romfs_args[1] = main_pack_path;
			for (int i = 0; i < arg_count; i++) {
				romfs_args[i + 2] = argv[i + 1];
			}
			args = romfs_args;
			args_count = arg_count + 2;
		}
	}

	OS_NX os(execpath);

	Error err = Main::setup(execpath, args_count, args);
	if (err != OK) {
		free(romfs_args);
		nx_services_exit();
		return err == ERR_HELP ? EXIT_SUCCESS : 255;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.set_exit_code(EXIT_SUCCESS);
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}

	Main::cleanup();

	const int exit_code = os.get_exit_code();

	free(romfs_args);
	nx_services_exit();

	return exit_code;
}
