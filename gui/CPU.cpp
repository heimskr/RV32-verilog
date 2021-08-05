#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "CPU.h"
#include "Util.h"

namespace RVGUI {
	CPU::Options::Options(const std::string &program_filename, size_t memory_size):
		programFilename(program_filename), memorySize(memory_size) {}

	CPU::Options & CPU::Options::setDataFilename(const std::string &value) {
		dataFilename = value;
		return *this;
	}

	CPU::Options & CPU::Options::setDataFilename(const std::string &value, Word offset) {
		dataFilename = value;
		dataOffset = offset;
		return *this;
	}

	CPU::Options & CPU::Options::setDataOffset(Word value) {
		dataOffset = value;
		return *this;
	}

	CPU::Options & CPU::Options::setSeparateInstructions(bool value) {
		separateInstructions = value;
		return *this;
	}

	CPU::Options & CPU::Options::setTimeOffset(int32_t value) {
		timeOffset = value;
		useTimeOffset = true;
		return *this;
	}

	CPU::Options & CPU::Options::setWidth(uint32_t value) {
		width = value;
		return *this;
	}

	CPU::Options & CPU::Options::setHeight(uint32_t value) {
		height = value;
		return *this;
	}

	CPU::Options & CPU::Options::setDimensions(uint32_t width_, uint32_t height_) {
		width = width_;
		height = height_;
		return *this;
	}

	CPU::Options & CPU::Options::setMMIOOffset(Word value) {
		mmioOffset = value;
		return *this;
	}

	CPU::CPU(const Options &options_): options(options_) {
		init();
	}

	bool CPU::tick() {
		if (!vcpu)
			throw std::runtime_error("CPU isn't initialized");

		if (!memory)
			throw std::runtime_error("CPU memory isn't initialized");

		if (options.separateInstructions && !instructions)
			throw std::runtime_error("CPU instructions array isn't initialized");

		if (start == 0)
			start = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();

		if (options.useTimeOffset)
			*reinterpret_cast<uint32_t *>(memory.get() + options.timeOffset) =
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::high_resolution_clock::now().time_since_epoch()).count();

		vcpu->i_clk = 0;

		if (options.separateInstructions) {
			vcpu->i_inst = instructions[vcpu->o_pc / sizeof(Word)];
		} else {
			vcpu->i_inst = reinterpret_cast<Word *>(memory.get())[vcpu->o_pc / sizeof(Word)];
		}

		vcpu->eval();

		if (vcpu->o_load) {
			if (options.mmioOffset <= vcpu->o_addr) {
				const uintptr_t ptrsum = (uintptr_t) (vcpu->o_addr - options.mmioOffset);
				const uintptr_t fbstart = (uintptr_t) framebuffer.get(), fbend = fbstart + framebufferSize;
				if (!(0 <= ptrsum && ptrsum + 3 < framebufferSize)) {
					std::cerr << "Framebuffer: [" << toHex(fbstart) << ", " << toHex(fbend) << ")\n";;
					throw std::out_of_range("Framebuffer read of size 4 out of range (" + toHex(fbstart + ptrsum)
						+ ")");
				}
				std::memcpy(&vcpu->i_mem, framebuffer.get() + vcpu->o_addr - options.mmioOffset, sizeof(Word));
			} else {
				const uintptr_t ptr = vcpu->o_addr % options.memorySize;
				const uintptr_t memstart = (uintptr_t) memory.get(), memend = memstart + options.memorySize;
				if (!(0 <= ptr && ptr + 3 < options.memorySize)) {
					std::cerr << "Memory: [" << toHex(memstart) << ", " << toHex(memend) << ")\n";;
					throw std::out_of_range("Memory read of size 4 out of range (" + toHex(memstart + ptr) + ")");
				}
				std::memcpy(&vcpu->i_mem, memory.get() + vcpu->o_addr % options.memorySize, sizeof(Word));
			}
		}

		vcpu->eval();
		vcpu->i_clk = 1;
		vcpu->eval();

		uint8_t *pointer;
		Word address;

		if (options.mmioOffset <= vcpu->o_addr) {
			pointer = framebuffer.get();
			address = vcpu->o_addr - options.mmioOffset;
		} else {
			pointer = memory.get();
			address = vcpu->o_addr;
		}

		if (vcpu->o_write) {
			const uint8_t *ptrsum = pointer + address;
			const uint8_t *memstart = memory.get(), *memend = memstart + options.memorySize;
			const uint8_t *fbstart = framebuffer.get(), *fbend = fbstart + framebufferSize;
			switch (vcpu->o_memsize) {
				case 1:
					if (!(memstart <= ptrsum && ptrsum < memend) && !(fbstart <= ptrsum && ptrsum < fbend))
						throw std::out_of_range("Write of size 1 out of range (" + toHex(ptrsum) + ")");
					std::memcpy(pointer + address, &vcpu->o_mem, 1);
					break;
				case 2:
					if (!(memstart <= ptrsum && ptrsum + 1 < memend) && !(fbstart <= ptrsum && ptrsum + 1 < fbend))
						throw std::out_of_range("Write of size 2 out of range (" + toHex(ptrsum) + ")");
					std::memcpy(pointer + address, &vcpu->o_mem, 2);
					break;
				case 3:
					if (!(memstart <= ptrsum && ptrsum + 3 < memend) && !(fbstart <= ptrsum && ptrsum + 3 < fbend))
						throw std::out_of_range("Write of size 4 out of range (" + toHex(ptrsum) + ")");
					std::memcpy(pointer + address, &vcpu->o_mem, 4);
					break;
				default:
					break;
			}
		}

		++count;

		if (vcpu->i_inst == 0x6f) { // Jump to self
			 end = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			return false;
		}

		return true;
	}

	void CPU::resetMemory() {
		memory.reset(new uint8_t[options.memorySize]());
	}

	void CPU::loadProgram() {
		if (options.programFilename.empty())
			throw std::runtime_error("Program filename is empty");

		std::ifstream file;
		file.open(options.programFilename, std::ios::in | std::ios::binary);

		const auto filesize = std::filesystem::file_size(options.programFilename);

		if (!file.is_open())
			throw std::runtime_error("Failed to open program for reading");

		instructionCount = filesize / sizeof(Word);
		if (options.separateInstructions) {
			instructions.reset(new Word[instructionCount]);
			for (size_t i = 0; !file.eof() && i < filesize / sizeof(Word); ++i)
				file.read(reinterpret_cast<char *>(&instructions[i]), sizeof(Word));
		} else {
			instructions.reset();
			for (size_t i = 0; !file.eof() && i < filesize; ++i)
				file.read(reinterpret_cast<char *>(&memory[i]), sizeof(uint8_t));
		}
	}

	void CPU::loadData(void *data, size_t size, size_t offset) {
		if (!memory)
			throw std::runtime_error("CPU memory isn't initialized");
		std::memcpy(memory.get() + offset, data, size);
	}

	void CPU::loadData() {
		if (options.dataFilename.empty())
			return;
		const auto datasize = std::filesystem::file_size(options.dataFilename);
		std::ifstream data;
		data.open(options.dataFilename, std::ios::in | std::ios::binary);

		std::cout << "Loading data...\n";
		data.read(reinterpret_cast<char *>(memory.get()) + options.dataOffset, datasize);
		std::cout << "Data loaded.\n";
	}

	CPU::Word CPU::getPC() const {
		return vcpu? vcpu->o_pc : 0;
	}

	void CPU::setPC(Word new_pc) {
		if (vcpu) {
			vcpu->i_clk = 0;
			vcpu->i_pcload = 1;
			vcpu->i_pc = new_pc;
			vcpu->eval();
			vcpu->i_clk = 1;
			vcpu->eval();
			vcpu->i_pcload = 0;
		}
	}

	const CPU::Word * CPU::getInstructions() const {
		if (options.separateInstructions)
			return instructions.get();
		return reinterpret_cast<Word *>(memory.get());
	}

	const uint8_t * CPU::getMemory() const {
		return memory.get();
	}

	void CPU::init() {
		resetMemory();
		initFramebuffer(3);
		loadProgram();
		loadData();
		initVCPU();
	}

	void CPU::initFramebuffer(int channels) {
		if (options.width == 0 && options.height == 0) {
			framebuffer.reset();
			framebufferSize = 0;
		} else if (options.width != 0 && options.height != 0) {
			framebuffer.reset(new uint8_t[framebufferSize = options.width * options.height * channels]());
		} else
			throw std::invalid_argument("Exactly one of width and height is zero");
	}

	void CPU::initVCPU() {
		if (!memory)
			throw std::runtime_error("CPU memory isn't initialized");

		if (options.separateInstructions && !instructions)
			throw std::runtime_error("CPU instructions array isn't initialized");

		vcpu = std::make_unique<VCPU>();
		vcpu->i_clk = 0;
		vcpu->i_inst = 0x6f;
		vcpu->i_daddr = 0x2;
		vcpu->i_dload = 0x1;
		vcpu->i_ddata = options.memorySize - 1;
		vcpu->eval();
		vcpu->i_clk = 1;
		vcpu->eval();
		vcpu->i_clk = 0;
		vcpu->i_daddr = 0;
		vcpu->i_dload = 0;
		vcpu->i_ddata = 0;
		if (options.separateInstructions)
			vcpu->i_inst = instructions[0];
		else
			vcpu->i_inst = ((Word *) memory.get())[0];
		vcpu->i_mem = memory[0];
		vcpu->eval();
	}
}
