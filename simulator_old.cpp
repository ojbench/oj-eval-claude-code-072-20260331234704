#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>

using namespace std;

// Memory size (512KB)
const int MEMORY_SIZE = 524288;

// Simple ELF header structures for 32-bit RISC-V
struct Elf32_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

class RISCVSimulator {
private:
    uint32_t reg[32];           // 32 general-purpose registers
    uint32_t pc;                // Program counter
    uint8_t memory[MEMORY_SIZE]; // Memory
    bool finished;              // Execution finished flag

    // Sign extension helpers
    int32_t sign_extend(uint32_t val, int bits) {
        int32_t m = 1U << (bits - 1);
        return (val ^ m) - m;
    }

    uint32_t get_imm_I(uint32_t inst) {
        return sign_extend((inst >> 20) & 0xFFF, 12);
    }

    uint32_t get_imm_S(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1F) | (((inst >> 25) & 0x7F) << 5);
        return sign_extend(imm, 12);
    }

    uint32_t get_imm_B(uint32_t inst) {
        uint32_t imm = (((inst >> 8) & 0xF) << 1) |
                       (((inst >> 25) & 0x3F) << 5) |
                       (((inst >> 7) & 0x1) << 11) |
                       (((inst >> 31) & 0x1) << 12);
        return sign_extend(imm, 13);
    }

    uint32_t get_imm_U(uint32_t inst) {
        return inst & 0xFFFFF000;
    }

    uint32_t get_imm_J(uint32_t inst) {
        uint32_t imm = (((inst >> 21) & 0x3FF) << 1) |
                       (((inst >> 20) & 0x1) << 11) |
                       (((inst >> 12) & 0xFF) << 12) |
                       (((inst >> 31) & 0x1) << 20);
        return sign_extend(imm, 21);
    }

    uint32_t read_memory_word(uint32_t addr) {
        if (addr + 3 >= MEMORY_SIZE) return 0;
        return *(uint32_t*)(&memory[addr]);
    }

    uint16_t read_memory_half(uint32_t addr) {
        if (addr + 1 >= MEMORY_SIZE) return 0;
        return *(uint16_t*)(&memory[addr]);
    }

    uint8_t read_memory_byte(uint32_t addr) {
        if (addr >= MEMORY_SIZE) return 0;
        return memory[addr];
    }

    void write_memory_word(uint32_t addr, uint32_t val) {
        if (addr + 3 >= MEMORY_SIZE) return;
        *(uint32_t*)(&memory[addr]) = val;
    }

    void write_memory_half(uint32_t addr, uint16_t val) {
        if (addr + 1 >= MEMORY_SIZE) return;
        *(uint16_t*)(&memory[addr]) = val;
    }

    void write_memory_byte(uint32_t addr, uint8_t val) {
        if (addr >= MEMORY_SIZE) return;
        memory[addr] = val;
    }

    void execute_instruction(uint32_t inst) {
        uint32_t opcode = inst & 0x7F;
        uint32_t rd = (inst >> 7) & 0x1F;
        uint32_t funct3 = (inst >> 12) & 0x7;
        uint32_t rs1 = (inst >> 15) & 0x1F;
        uint32_t rs2 = (inst >> 20) & 0x1F;
        uint32_t funct7 = (inst >> 25) & 0x7F;

        // Always keep x0 as 0
        reg[0] = 0;

        switch (opcode) {
            case 0x37: // LUI
                reg[rd] = get_imm_U(inst);
                pc += 4;
                break;

            case 0x17: // AUIPC
                reg[rd] = pc + get_imm_U(inst);
                pc += 4;
                break;

            case 0x6F: // JAL
                reg[rd] = pc + 4;
                pc = pc + get_imm_J(inst);
                break;

            case 0x67: // JALR
                {
                    uint32_t t = pc + 4;
                    pc = (reg[rs1] + get_imm_I(inst)) & ~1;
                    reg[rd] = t;
                }
                break;

            case 0x63: // Branch instructions
                {
                    bool take_branch = false;
                    int32_t r1 = (int32_t)reg[rs1];
                    int32_t r2 = (int32_t)reg[rs2];
                    uint32_t ur1 = reg[rs1];
                    uint32_t ur2 = reg[rs2];

                    switch (funct3) {
                        case 0x0: take_branch = (ur1 == ur2); break; // BEQ
                        case 0x1: take_branch = (ur1 != ur2); break; // BNE
                        case 0x4: take_branch = (r1 < r2); break;    // BLT
                        case 0x5: take_branch = (r1 >= r2); break;   // BGE
                        case 0x6: take_branch = (ur1 < ur2); break;  // BLTU
                        case 0x7: take_branch = (ur1 >= ur2); break; // BGEU
                    }

                    if (take_branch) {
                        pc = pc + get_imm_B(inst);
                    } else {
                        pc += 4;
                    }
                }
                break;

            case 0x03: // Load instructions
                {
                    uint32_t addr = reg[rs1] + get_imm_I(inst);
                    switch (funct3) {
                        case 0x0: // LB
                            reg[rd] = sign_extend(read_memory_byte(addr), 8);
                            break;
                        case 0x1: // LH
                            reg[rd] = sign_extend(read_memory_half(addr), 16);
                            break;
                        case 0x2: // LW
                            reg[rd] = read_memory_word(addr);
                            break;
                        case 0x4: // LBU
                            reg[rd] = read_memory_byte(addr);
                            break;
                        case 0x5: // LHU
                            reg[rd] = read_memory_half(addr);
                            break;
                    }
                    pc += 4;
                }
                break;

            case 0x23: // Store instructions
                {
                    uint32_t addr = reg[rs1] + get_imm_S(inst);
                    switch (funct3) {
                        case 0x0: // SB
                            write_memory_byte(addr, reg[rs2] & 0xFF);
                            break;
                        case 0x1: // SH
                            write_memory_half(addr, reg[rs2] & 0xFFFF);
                            break;
                        case 0x2: // SW
                            write_memory_word(addr, reg[rs2]);
                            break;
                    }
                    pc += 4;
                }
                break;

            case 0x13: // Immediate arithmetic and logical instructions
                {
                    int32_t imm = get_imm_I(inst);
                    int32_t r1 = (int32_t)reg[rs1];

                    switch (funct3) {
                        case 0x0: // ADDI
                            reg[rd] = reg[rs1] + imm;
                            break;
                        case 0x2: // SLTI
                            reg[rd] = (r1 < imm) ? 1 : 0;
                            break;
                        case 0x3: // SLTIU
                            reg[rd] = (reg[rs1] < (uint32_t)imm) ? 1 : 0;
                            break;
                        case 0x4: // XORI
                            reg[rd] = reg[rs1] ^ imm;
                            break;
                        case 0x6: // ORI
                            reg[rd] = reg[rs1] | imm;
                            break;
                        case 0x7: // ANDI
                            reg[rd] = reg[rs1] & imm;
                            break;
                        case 0x1: // SLLI
                            reg[rd] = reg[rs1] << (rs2 & 0x1F);
                            break;
                        case 0x5: // SRLI/SRAI
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] >> (rs2 & 0x1F); // SRLI
                            } else {
                                reg[rd] = ((int32_t)reg[rs1]) >> (rs2 & 0x1F); // SRAI
                            }
                            break;
                    }
                    pc += 4;
                }
                break;

            case 0x33: // Register arithmetic and logical instructions
                {
                    int32_t r1 = (int32_t)reg[rs1];
                    int32_t r2 = (int32_t)reg[rs2];

                    switch (funct3) {
                        case 0x0:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] + reg[rs2]; // ADD
                            } else if (funct7 == 0x20) {
                                reg[rd] = reg[rs1] - reg[rs2]; // SUB
                            } else if (funct7 == 0x01) {
                                reg[rd] = (uint32_t)((int64_t)r1 * (int64_t)r2); // MUL
                            }
                            break;
                        case 0x1:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] << (reg[rs2] & 0x1F); // SLL
                            } else if (funct7 == 0x01) {
                                reg[rd] = (uint32_t)(((int64_t)r1 * (int64_t)r2) >> 32); // MULH
                            }
                            break;
                        case 0x2:
                            if (funct7 == 0x00) {
                                reg[rd] = (r1 < r2) ? 1 : 0; // SLT
                            } else if (funct7 == 0x01) {
                                reg[rd] = (uint32_t)(((int64_t)r1 * (uint64_t)reg[rs2]) >> 32); // MULHSU
                            }
                            break;
                        case 0x3:
                            if (funct7 == 0x00) {
                                reg[rd] = (reg[rs1] < reg[rs2]) ? 1 : 0; // SLTU
                            } else if (funct7 == 0x01) {
                                reg[rd] = (uint32_t)(((uint64_t)reg[rs1] * (uint64_t)reg[rs2]) >> 32); // MULHU
                            }
                            break;
                        case 0x4:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] ^ reg[rs2]; // XOR
                            } else if (funct7 == 0x01) {
                                if (reg[rs2] != 0) {
                                    reg[rd] = r1 / r2; // DIV
                                } else {
                                    reg[rd] = -1;
                                }
                            }
                            break;
                        case 0x5:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] >> (reg[rs2] & 0x1F); // SRL
                            } else if (funct7 == 0x20) {
                                reg[rd] = ((int32_t)reg[rs1]) >> (reg[rs2] & 0x1F); // SRA
                            } else if (funct7 == 0x01) {
                                if (reg[rs2] != 0) {
                                    reg[rd] = reg[rs1] / reg[rs2]; // DIVU
                                } else {
                                    reg[rd] = 0xFFFFFFFF;
                                }
                            }
                            break;
                        case 0x6:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] | reg[rs2]; // OR
                            } else if (funct7 == 0x01) {
                                if (reg[rs2] != 0) {
                                    reg[rd] = r1 % r2; // REM
                                } else {
                                    reg[rd] = reg[rs1];
                                }
                            }
                            break;
                        case 0x7:
                            if (funct7 == 0x00) {
                                reg[rd] = reg[rs1] & reg[rs2]; // AND
                            } else if (funct7 == 0x01) {
                                if (reg[rs2] != 0) {
                                    reg[rd] = reg[rs1] % reg[rs2]; // REMU
                                } else {
                                    reg[rd] = reg[rs1];
                                }
                            }
                            break;
                    }
                    pc += 4;
                }
                break;

            case 0x73: // ECALL, EBREAK
                if (inst == 0x00000073) { // ECALL
                    // System call - a7 (x17) contains syscall number
                    uint32_t syscall = reg[17];

                    // Common syscall numbers:
                    // 93 (or 10 in some implementations): exit
                    // 64 (or 1 in some implementations): write
                    // 63: read

                    if (syscall == 93 || syscall == 10) {
                        // Exit syscall - finish execution
                        finished = true;
                    } else if (syscall == 64 || syscall == 1) {
                        // Write syscall: a0=fd, a1=buffer, a2=count
                        uint32_t fd = reg[10];
                        uint32_t buf_addr = reg[11];
                        uint32_t count = reg[12];

                        // Only handle stdout (fd=1)
                        if (fd == 1) {
                            for (uint32_t i = 0; i < count && (buf_addr + i) < MEMORY_SIZE; i++) {
                                cout << (char)memory[buf_addr + i];
                            }
                            reg[10] = count; // Return number of bytes written
                        }
                    }

                    pc += 4;
                } else if (inst == 0x00100073) { // EBREAK
                    finished = true;
                    pc += 4;
                } else {
                    pc += 4;
                }
                break;

            default:
                pc += 4;
                break;
        }

        // Ensure x0 is always 0
        reg[0] = 0;
    }

public:
    RISCVSimulator() {
        memset(reg, 0, sizeof(reg));
        memset(memory, 0, sizeof(memory));
        pc = 0;
        finished = false;
        // Initialize stack pointer to top of memory
        reg[2] = MEMORY_SIZE - 4;  // sp (x2)
        // Initialize global pointer (gp) to middle of memory for data access
        reg[3] = MEMORY_SIZE / 2;  // gp (x3)
    }

    void load_program(const vector<uint8_t>& program, uint32_t start_addr = 0) {
        // Check if this is an ELF file
        if (program.size() >= 4 && program[0] == 0x7F &&
            program[1] == 'E' && program[2] == 'L' && program[3] == 'F') {
            // ELF format - parse and load segments
            load_elf(program);
        } else {
            // Raw binary - load at start_addr
            for (size_t i = 0; i < program.size() && (start_addr + i) < MEMORY_SIZE; i++) {
                memory[start_addr + i] = program[i];
            }
            pc = start_addr;
        }
    }

    void load_elf(const vector<uint8_t>& elf_data) {
        if (elf_data.size() < sizeof(Elf32_Ehdr)) {
            return;
        }

        // Parse ELF header
        Elf32_Ehdr ehdr;
        memcpy(&ehdr, elf_data.data(), sizeof(Elf32_Ehdr));

        // Set entry point
        pc = ehdr.e_entry;

        // Load program headers
        if (ehdr.e_phoff == 0 || ehdr.e_phnum == 0) {
            return;
        }

        for (int i = 0; i < ehdr.e_phnum; i++) {
            uint32_t phoff = ehdr.e_phoff + i * ehdr.e_phentsize;
            if (phoff + sizeof(Elf32_Phdr) > elf_data.size()) {
                break;
            }

            Elf32_Phdr phdr;
            memcpy(&phdr, elf_data.data() + phoff, sizeof(Elf32_Phdr));

            // PT_LOAD = 1
            if (phdr.p_type == 1) {
                // Load segment into memory
                uint32_t vaddr = phdr.p_vaddr;
                uint32_t filesz = phdr.p_filesz;
                uint32_t memsz = phdr.p_memsz;
                uint32_t offset = phdr.p_offset;

                // Copy file data
                for (uint32_t j = 0; j < filesz && (vaddr + j) < MEMORY_SIZE && (offset + j) < elf_data.size(); j++) {
                    memory[vaddr + j] = elf_data[offset + j];
                }

                // Zero-fill remaining memory (BSS)
                for (uint32_t j = filesz; j < memsz && (vaddr + j) < MEMORY_SIZE; j++) {
                    memory[vaddr + j] = 0;
                }
            }
        }
    }

    void run(int max_instructions = 100000000) {
        int count = 0;
        while (!finished && count < max_instructions) {
            if (pc >= MEMORY_SIZE - 3) {
                break;
            }

            uint32_t inst = read_memory_word(pc);
            // Don't stop on null instruction - let it be decoded as NOP or invalid
            execute_instruction(inst);
            count++;
        }
    }

    uint32_t get_register(int idx) {
        if (idx >= 0 && idx < 32) {
            return reg[idx];
        }
        return 0;
    }

    void print_registers() {
        for (int i = 0; i < 32; i++) {
            if (reg[i] != 0) {
                printf("x%d = 0x%08x (%u)\n", i, reg[i], reg[i]);
            }
        }
    }
};

int main() {
    // Read program from stdin
    vector<uint8_t> program;
    uint8_t byte;

    while (cin.read(reinterpret_cast<char*>(&byte), 1)) {
        program.push_back(byte);
    }

    if (program.empty()) {
        return 0;
    }

    RISCVSimulator sim;
    sim.load_program(program);
    sim.run();

    // Output result - the return value is in a0 (x10)
    // Output as signed decimal (C programs typically return signed int)
    int32_t result = (int32_t)sim.get_register(10);
    printf("%d\n", result);

    return 0;
}
