/*
 * libstored, distributed debuggable data stores.
 * Copyright (C) 2020-2023  Jochem Rutgers
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "TestStore.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stored>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#ifdef __AFL_FUZZ_TESTCASE_LEN
#	define HAVE_AFL

#	pragma clang diagnostic ignored "-Wunused-but-set-variable"
#	pragma clang diagnostic ignored "-Wshorten-64-to-32"

__AFL_FUZZ_INIT();
#endif

static void help(char const* exe)
{
	printf("Usage: %s [-h|-i|<input file>]\n", exe);

	printf("\nwhere\n");
	printf("   -h   Show this help and exit.\n");
	printf("   -i   Generate input files in the current directory and exit.\n");
	printf("   <input file>\n");
	printf("        The input file to run (without using AFL++).\n");

	printf("\nWithout parameters, the program expects to be controlled by AFL++ for "
	       "fuzzing.\n");
}

static void generate(std::initializer_list<char const*> msgs)
{
	std::string buf;

	for(auto const* msg : msgs) {
		size_t len = std::min<size_t>(0xff, strlen(msg));
		buf.push_back((char)len);
		buf.append(msg, len);
	}

	static size_t count;
	std::array<char, 64> filename{};
	snprintf(filename.data(), filename.size(), "fuzz_debug_%03zu.bin", count++);

	FILE* f = fopen(filename.data(), "wb");
	if(!f) {
		printf("Cannot open %s for writing; %s\n", filename.data(), strerror(errno));
		exit(1);
	}

	auto written = fwrite(buf.data(), buf.size(), 1, f);
	fclose(f);

	if(written != 1) {
		printf("Cannot write %s\n", filename.data());
		exit(1);
	}

	printf("Generated %s\n", filename.data());
}

static void generate()
{
	generate({"?", "i", "v", "l"});
	generate({"r/default int8", "a1/default int8", "r1", "w101", "r1"});
	generate({"w0123456789abcdef/f read/write", "r/f read-", "r/init float 1"});
	generate({"mA|e0|e1|e2", "A", "mA"});
	generate({"mt|r/default uint32", "ttt", "st", "t"});
}

static void test(void const* buf, size_t len)
{
	static std::vector<uint8_t> msg;

	stored::TestStore store;
	stored::Debugger debugger;
	debugger.map(store);

	// buf contains messages, which are a byte with the length and then one message of that
	// length.
	uint8_t const* buf_ = static_cast<uint8_t const*>(buf);

	while(len > 0) {
		size_t l = std::min<size_t>(--len, *buf_++);

		msg.resize(l);
		memcpy(msg.data(), buf_, l);
		debugger.decode(msg.data(), l);

		buf_ += l;
		len -= l;
	}
}

static int test(char const* file)
{
	int res = 1;
	int fd = -1;
	void* buf = nullptr;
	size_t len = 0;
	struct stat s {};

	printf("Reading %s...\n", file);

	fd = open(file, O_RDONLY);

	if(fd == -1) {
		printf("Cannot open file; %s\n", strerror(errno));
		goto done;
	}

	if(fstat(fd, &s) == -1) {
		printf("Cannot stat file; %s\n", strerror(errno));
		goto close;
	}

	len = (size_t)s.st_size;
	buf = mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);

	if(buf == MAP_FAILED) {
		printf("Cannot mmap; %s\n", strerror(errno));
		goto close;
	}

	test(buf, len);
	res = 0;

	// unmap:
	munmap(buf, len);
close:
	close(fd);
done:
	return res;
}

#ifdef HAVE_AFL
#	ifdef STORED_COMPILER_CLANG
#		pragma clang optimize off
#	endif
#	ifdef STORED_COMPILER_GCC
#		pragma GCC optimize("O0")
#	endif
#endif
int main(int argc, char** argv)
{
	printf("%s\n\n", stored::banner());
	printf("Debug fuzzer\n\n");

	switch(argc) {
	case 0:
	case 1:
		break;
	case 2:
		if(strcmp(argv[1], "-h") == 0) {
			help(argv[0]);
			return 0;
		} else if(strcmp(argv[1], "-i") == 0) {
			generate();
			return 0;
		}

		return test(argv[1]);
	default:
		help(argv[0]);
		return 1;
	}

#ifndef HAVE_AFL
	printf("Compile this program with afl-clang-fast++ to do fuzzing.\n");
	return 1;
#else

	printf("Ready. Waiting for afl-fuzz for instructions...\n");

	__AFL_INIT();

	unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;

	while(__AFL_LOOP(1000000)) {
		auto len = __AFL_FUZZ_TESTCASE_LEN;
		test(buf, (size_t)len);
	}
#endif
}
