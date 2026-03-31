#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace std;

const int MEM_SIZE = 1 << 20; // 1MB

uint8_t mem[MEM_SIZE];
uint32_t reg[32];
uint32_t pc;

int32_t sext(uint32_t val, int bits) {
    uint32_t sign = 1U << (bits - 1);
    return (val & ((1U << bits) - 1)) - (val & sign ? sign << 1 : 0);
}

uint32_t load_word(uint32_t addr) {
    return *(uint32_t*)(mem + addr);
}

uint16_t load_half(uint32_t addr) {
    return *(uint16_t*)(mem + addr);
}

uint8_t load_byte(uint32_t addr) {
    return mem[addr];
}

void store_word(uint32_t addr, uint32_t val) {
    *(uint32_t*)(mem + addr) = val;
}

void store_half(uint32_t addr, uint16_t val) {
    *(uint16_t*)(mem + addr) = val;
}

void store_byte(uint32_t addr, uint8_t val) {
    mem[addr] = val;
}

bool execute() {
    uint32_t inst = load_word(pc);
    uint32_t opcode = inst & 0x7F;
    uint32_t rd = (inst >> 7) & 0x1F;
    uint32_t funct3 = (inst >> 12) & 0x7;
    uint32_t rs1 = (inst >> 15) & 0x1F;
    uint32_t rs2 = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25);

    reg[0] = 0;

    switch (opcode) {
        case 0x37: // LUI
            reg[rd] = inst & 0xFFFFF000;
            pc += 4;
            break;

        case 0x17: // AUIPC
            reg[rd] = pc + (inst & 0xFFFFF000);
            pc += 4;
            break;

        case 0x6F: { // JAL
            int32_t imm = sext((((inst >> 31) & 1) << 20) | (((inst >> 12) & 0xFF) << 12) |
                               (((inst >> 20) & 1) << 11) | (((inst >> 21) & 0x3FF) << 1), 21);
            reg[rd] = pc + 4;
            pc += imm;
            break;
        }

        case 0x67: { // JALR
            int32_t imm = sext(inst >> 20, 12);
            uint32_t t = pc + 4;
            pc = (reg[rs1] + imm) & ~1;
            reg[rd] = t;
            break;
        }

        case 0x63: { // Branch
            int32_t imm = sext((((inst >> 31) & 1) << 12) | (((inst >> 7) & 1) << 11) |
                               (((inst >> 25) & 0x3F) << 5) | (((inst >> 8) & 0xF) << 1), 13);
            bool take = false;
            switch (funct3) {
                case 0x0: take = (reg[rs1] == reg[rs2]); break; // BEQ
                case 0x1: take = (reg[rs1] != reg[rs2]); break; // BNE
                case 0x4: take = ((int32_t)reg[rs1] < (int32_t)reg[rs2]); break; // BLT
                case 0x5: take = ((int32_t)reg[rs1] >= (int32_t)reg[rs2]); break; // BGE
                case 0x6: take = (reg[rs1] < reg[rs2]); break; // BLTU
                case 0x7: take = (reg[rs1] >= reg[rs2]); break; // BGEU
            }
            pc += take ? imm : 4;
            break;
        }

        case 0x03: { // Load
            int32_t imm = sext(inst >> 20, 12);
            uint32_t addr = reg[rs1] + imm;
            switch (funct3) {
                case 0x0: reg[rd] = sext(load_byte(addr), 8); break; // LB
                case 0x1: reg[rd] = sext(load_half(addr), 16); break; // LH
                case 0x2: reg[rd] = load_word(addr); break; // LW
                case 0x4: reg[rd] = load_byte(addr); break; // LBU
                case 0x5: reg[rd] = load_half(addr); break; // LHU
            }
            pc += 4;
            break;
        }

        case 0x23: { // Store
            int32_t imm = sext(((inst >> 7) & 0x1F) | ((inst >> 20) & 0xFE0), 12);
            uint32_t addr = reg[rs1] + imm;
            switch (funct3) {
                case 0x0: store_byte(addr, reg[rs2]); break; // SB
                case 0x1: store_half(addr, reg[rs2]); break; // SH
                case 0x2: store_word(addr, reg[rs2]); break; // SW
            }
            pc += 4;
            break;
        }

        case 0x13: { // I-type ALU
            int32_t imm = sext(inst >> 20, 12);
            uint32_t shamt = (inst >> 20) & 0x1F;
            switch (funct3) {
                case 0x0: reg[rd] = reg[rs1] + imm; break; // ADDI
                case 0x2: reg[rd] = ((int32_t)reg[rs1] < imm) ? 1 : 0; break; // SLTI
                case 0x3: reg[rd] = (reg[rs1] < (uint32_t)imm) ? 1 : 0; break; // SLTIU
                case 0x4: reg[rd] = reg[rs1] ^ imm; break; // XORI
                case 0x6: reg[rd] = reg[rs1] | imm; break; // ORI
                case 0x7: reg[rd] = reg[rs1] & imm; break; // ANDI
                case 0x1: reg[rd] = reg[rs1] << shamt; break; // SLLI
                case 0x5:
                    if ((inst >> 30) & 1)
                        reg[rd] = (int32_t)reg[rs1] >> shamt; // SRAI
                    else
                        reg[rd] = reg[rs1] >> shamt; // SRLI
                    break;
            }
            pc += 4;
            break;
        }

        case 0x33: { // R-type ALU
            switch (funct3) {
                case 0x0:
                    if (funct7 == 0x20)
                        reg[rd] = reg[rs1] - reg[rs2]; // SUB
                    else if (funct7 == 0x01)
                        reg[rd] = (int32_t)reg[rs1] * (int32_t)reg[rs2]; // MUL
                    else
                        reg[rd] = reg[rs1] + reg[rs2]; // ADD
                    break;
                case 0x1:
                    if (funct7 == 0x01) { // MULH
                        int64_t p = (int64_t)(int32_t)reg[rs1] * (int64_t)(int32_t)reg[rs2];
                        reg[rd] = p >> 32;
                    } else
                        reg[rd] = reg[rs1] << (reg[rs2] & 0x1F); // SLL
                    break;
                case 0x2:
                    if (funct7 == 0x01) { // MULHSU
                        int64_t p = (int64_t)(int32_t)reg[rs1] * (uint64_t)reg[rs2];
                        reg[rd] = p >> 32;
                    } else
                        reg[rd] = ((int32_t)reg[rs1] < (int32_t)reg[rs2]) ? 1 : 0; // SLT
                    break;
                case 0x3:
                    if (funct7 == 0x01) { // MULHU
                        uint64_t p = (uint64_t)reg[rs1] * (uint64_t)reg[rs2];
                        reg[rd] = p >> 32;
                    } else
                        reg[rd] = (reg[rs1] < reg[rs2]) ? 1 : 0; // SLTU
                    break;
                case 0x4:
                    if (funct7 == 0x01) { // DIV
                        int32_t a = reg[rs1], b = reg[rs2];
                        reg[rd] = b ? a / b : -1;
                    } else
                        reg[rd] = reg[rs1] ^ reg[rs2]; // XOR
                    break;
                case 0x5:
                    if (funct7 == 0x20)
                        reg[rd] = (int32_t)reg[rs1] >> (reg[rs2] & 0x1F); // SRA
                    else if (funct7 == 0x01) { // DIVU
                        reg[rd] = reg[rs2] ? reg[rs1] / reg[rs2] : 0xFFFFFFFF;
                    } else
                        reg[rd] = reg[rs1] >> (reg[rs2] & 0x1F); // SRL
                    break;
                case 0x6:
                    if (funct7 == 0x01) { // REM
                        int32_t a = reg[rs1], b = reg[rs2];
                        reg[rd] = b ? a % b : a;
                    } else
                        reg[rd] = reg[rs1] | reg[rs2]; // OR
                    break;
                case 0x7:
                    if (funct7 == 0x01) { // REMU
                        reg[rd] = reg[rs2] ? reg[rs1] % reg[rs2] : reg[rs1];
                    } else
                        reg[rd] = reg[rs1] & reg[rs2]; // AND
                    break;
            }
            pc += 4;
            break;
        }

        case 0x73: // ECALL/EBREAK
            return false; // Stop execution

        default:
            pc += 4;
            break;
    }

    reg[0] = 0;
    return true;
}

int main() {
    memset(mem, 0, sizeof(mem));
    memset(reg, 0, sizeof(reg));

    // Read input
    vector<uint8_t> prog;
    int ch;
    while ((ch = getchar()) != EOF) {
        prog.push_back(ch);
    }

    // Check for ELF
    if (prog.size() >= 52 && prog[0] == 0x7F && prog[1] == 'E' && prog[2] == 'L' && prog[3] == 'F') {
        // Parse ELF
        uint32_t entry = *(uint32_t*)&prog[24];
        uint32_t phoff = *(uint32_t*)&prog[28];
        uint16_t phnum = *(uint16_t*)&prog[44];

        pc = entry;
        reg[2] = MEM_SIZE - 4; // sp

        for (int i = 0; i < phnum; i++) {
            uint32_t *ph = (uint32_t*)&prog[phoff + i * 32];
            if (ph[0] == 1) { // PT_LOAD
                uint32_t off = ph[1], vaddr = ph[2], filesz = ph[4], memsz = ph[5];
                if (vaddr < MEM_SIZE) {
                    memcpy(mem + vaddr, &prog[off], min(filesz, (uint32_t)prog.size() - off));
                    if (memsz > filesz)
                        memset(mem + vaddr + filesz, 0, memsz - filesz);
                }
            }
        }
    } else {
        // Raw binary
        memcpy(mem, prog.data(), min((size_t)MEM_SIZE, prog.size()));
        pc = 0;
        reg[2] = MEM_SIZE - 4;
    }

    // Execute
    for (int i = 0; i < 100000000 && execute(); i++);

    // Output
    printf("%d\n", (int32_t)reg[10]);

    return 0;
}
