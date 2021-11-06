#include <iostream>
#include <fstream>
#include <cstdint>
#include <string>
#include <algorithm>
// jesus fuck i forgot how bad iostream formatting was, give me printf or give me death
#include <stdio.h>

// Device-under-test model generated by CXXRTL:
#include "dut.cpp"
#include <backends/cxxrtl/cxxrtl_vcd.h>

static const unsigned int MEM_SIZE = 16 * 1024 * 1024;
uint8_t mem[MEM_SIZE];

static const unsigned int IO_BASE = 0x80000000;
enum {
	IO_PRINT_CHAR = 0x000,
	IO_PRINT_U32  = 0x004,
	IO_EXIT       = 0x008,
	IO_MTIME      = 0x100,
	IO_MTIMEH     = 0x104,
	IO_MTIMECMP   = 0x108,
	IO_MTIMECMPH  = 0x10c
};

const char *help_str =
"Usage: tb binfile [vcdfile] [--dump start end] [--cycles n]\n"
"    binfile          : Binary to load into start of memory\n"
"    vcdfile          : Path to dump waveforms to\n"
"    --dump start end : Print out memory contents between start and end (exclusive)\n"
"                       after execution finishes. Can be passed multiple times.\n"
"    --cycles n       : Maximum number of cycles to run before exiting.\n"
;

void exit_help(std::string errtext = "") {
	std::cerr << errtext << help_str;
	exit(-1);
}

int main(int argc, char **argv) {

	if (argc < 2)
		exit_help();

	bool dump_waves = false;
	std::string waves_path;
	std::vector<std::pair<uint32_t, uint32_t>> dump_ranges;
	int64_t max_cycles = 100000;

	for (int i = 2; i < argc; ++i) {
		std::string s(argv[i]);
		if (i == 2 && s.rfind("--", 0) != 0) {
			// Optional positional argument: vcdfile
			dump_waves = true;
			waves_path = s;
		}
		else if (s == "--dump") {
			if (argc - i < 3)
				exit_help("Option --dump requires 2 arguments\n");
			dump_ranges.push_back(std::pair<uint32_t, uint32_t>(
				std::stoul(argv[i + 1], 0, 0),
				std::stoul(argv[i + 2], 0, 0)
			));;
			i += 2;
		}
		else if (s == "--cycles") {
			if (argc - i < 2)
				exit_help("Option --cycles requires an argument\n");
			max_cycles = std::stol(argv[i + 1], 0, 0);
			i += 1;
		}
		else {
			std::cerr << "Unrecognised argument " << s << "\n";
			exit_help("");
		}
	}

#ifdef DUAL_PORT
	cxxrtl_design::p_hazard3__cpu__2port top;
#else
	cxxrtl_design::p_hazard3__cpu__1port top;
#endif

	std::fill(std::begin(mem), std::end(mem), 0);

	std::ifstream fd(argv[1], std::ios::binary | std::ios::ate);
	std::streamsize bin_size = fd.tellg();
	if (bin_size > MEM_SIZE) {
		std::cerr << "Binary file (" << bin_size << " bytes) is larger than memory (" << MEM_SIZE << " bytes)\n";
		return -1;
	}
	fd.seekg(0, std::ios::beg);
	fd.read((char*)mem, bin_size);

	std::ofstream waves_fd;
	cxxrtl::vcd_writer vcd;
	if (dump_waves) {
		waves_fd.open(waves_path);
		cxxrtl::debug_items all_debug_items;
		top.debug_info(all_debug_items);
		vcd.timescale(1, "us");
		vcd.add(all_debug_items);
	}

	bool bus_trans = false;
	bool bus_write = false;
#ifdef DUAL_PORT
	bool bus_trans_i = false;
	uint32_t bus_addr_i = 0;
#endif
	uint32_t bus_addr = 0;
	uint8_t bus_size = 0;
	// Never generate bus stalls
#ifdef DUAL_PORT
	top.p_i__hready.set<bool>(true);
	top.p_d__hready.set<bool>(true);
#else
	top.p_ahblm__hready.set<bool>(true);
#endif

	uint64_t mtime = 0;
	uint64_t mtimecmp = 0;

	// Reset + initial clock pulse
	top.step();
	top.p_clk.set<bool>(true);
	top.step();
	top.p_clk.set<bool>(false);
	top.p_rst__n.set<bool>(true);
	top.step();
	top.step(); // workaround for github.com/YosysHQ/yosys/issues/2780

	for (int64_t cycle = 0; cycle < max_cycles; ++cycle) {
		top.p_clk.set<bool>(false);
		top.step();
		if (dump_waves)
			vcd.sample(cycle * 2);
		top.p_clk.set<bool>(true);
		top.step();
		top.step(); // workaround for github.com/YosysHQ/yosys/issues/2780

		// Default update logic for mtime, mtimecmp
		++mtime;
		top.p_timer__irq.set<bool>(mtime >= mtimecmp);

		// Handle current data phase, then move current address phase to data phase
		uint32_t rdata = 0;
		if (bus_trans && bus_write) {
#ifdef DUAL_PORT
			uint32_t wdata = top.p_d__hwdata.get<uint32_t>();
#else
			uint32_t wdata = top.p_ahblm__hwdata.get<uint32_t>();
#endif
			if (bus_addr <= MEM_SIZE - (1u << bus_size)) {
				unsigned int n_bytes = 1u << bus_size;
				// Note we are relying on hazard3's byte lane replication
				for (unsigned int i = 0; i < n_bytes; ++i) {
					mem[bus_addr + i] = wdata >> (8 * i) & 0xffu;
				}
			}
			else if (bus_addr == IO_BASE + IO_PRINT_CHAR) {
				putchar(wdata);
			}
			else if (bus_addr == IO_BASE + IO_PRINT_U32) {
				printf("%08x\n", wdata);
			}
			else if (bus_addr == IO_BASE + IO_EXIT) {
				printf("CPU requested halt. Exit code %d\n", wdata);
				printf("Ran for %ld cycles\n", cycle + 1);
				break;
			}
			else if (bus_addr == IO_BASE + IO_MTIME) {
				mtime = (mtime & 0xffffffff00000000u) | wdata;
			}
			else if (bus_addr == IO_BASE + IO_MTIMEH) {
				mtime = (mtime & 0x00000000ffffffffu) | ((uint64_t)wdata << 32);
			}
			else if (bus_addr == IO_BASE + IO_MTIMECMP) {
				mtimecmp = (mtimecmp & 0xffffffff00000000u) | wdata;
			}
			else if (bus_addr == IO_BASE + IO_MTIMECMPH) {
				mtimecmp = (mtimecmp & 0x00000000ffffffffu) | ((uint64_t)wdata << 32);
			}
		}
		else if (bus_trans && !bus_write) {
			bus_addr &= ~0x3u;
			if (bus_addr <= MEM_SIZE - 4) {
				rdata =
					(uint32_t)mem[bus_addr] |
					mem[bus_addr + 1] << 8 |
					mem[bus_addr + 2] << 16 |
					mem[bus_addr + 3] << 24;
			}
			else if (bus_addr == IO_BASE + IO_MTIME) {
				rdata = mtime;
			}
			else if (bus_addr == IO_BASE + IO_MTIMEH) {
				rdata = mtime >> 32;
			}
			else if (bus_addr == IO_BASE + IO_MTIMECMP) {
				rdata = mtimecmp;
			}
			else if (bus_addr == IO_BASE + IO_MTIMECMPH) {
				rdata = mtimecmp >> 32;
			}
		}
#ifdef DUAL_PORT
		top.p_d__hrdata.set<uint32_t>(rdata);
		if (bus_trans_i) {
			bus_addr_i &= ~0x3u;
			if (bus_addr_i <= MEM_SIZE - 4) {
				top.p_i__hrdata.set<uint32_t>(
					(uint32_t)mem[bus_addr_i] |
					mem[bus_addr_i + 1] << 8 |
					mem[bus_addr_i + 2] << 16 |
					mem[bus_addr_i + 3] << 24
				);
			}
			else {
				top.p_i__hrdata.set<uint32_t>(0);
			}
		}
#else
		top.p_ahblm__hrdata.set<uint32_t>(rdata);
#endif

#ifdef DUAL_PORT
		bus_trans = top.p_d__htrans.get<uint8_t>() >> 1;
		bus_write = top.p_d__hwrite.get<bool>();
		bus_size = top.p_d__hsize.get<uint8_t>();
		bus_addr = top.p_d__haddr.get<uint32_t>();
		bus_trans_i = top.p_i__htrans.get<uint8_t>() >> 1;
		bus_addr_i = top.p_i__haddr.get<uint32_t>();
#else
		bus_trans = top.p_ahblm__htrans.get<uint8_t>() >> 1;
		bus_write = top.p_ahblm__hwrite.get<bool>();
		bus_size = top.p_ahblm__hsize.get<uint8_t>();
		bus_addr = top.p_ahblm__haddr.get<uint32_t>();
#endif

		if (dump_waves) {
			// The extra step() is just here to get the bus responses to line up nicely
			// in the VCD (hopefully is a quick update)
			top.step();
			vcd.sample(cycle * 2 + 1);
			waves_fd << vcd.buffer;
			vcd.buffer.clear();
		}
	}

	for (auto r : dump_ranges) {
		printf("Dumping memory from %08x to %08x:\n", r.first, r.second);
		for (int i = 0; i < r.second - r.first; ++i)
			printf("%02x%c", mem[r.first + i], i % 16 == 15 ? '\n' : ' ');
		printf("\n");
	}

	return 0;
}
