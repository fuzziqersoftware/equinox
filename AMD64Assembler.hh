#pragma once

#include <deque>
#include <unordered_map>
#include <string>

#include "CodeBuffer.hh"


enum Operation {
  ADD_STORE8 = 0x00,
  ADD_STORE  = 0x01,
  ADD_LOAD8  = 0x02,
  ADD_LOAD   = 0x03,
  SUB_STORE8 = 0x28,
  SUB_STORE  = 0x29,
  SUB_LOAD8  = 0x2A,
  SUB_LOAD   = 0x2B,
  CMP_STORE8 = 0x38,
  CMP_STORE  = 0x39,
  CMP_LOAD8  = 0x3A,
  CMP_LOAD   = 0x3B,
  JE8        = 0x74,
  JNE8       = 0x75,
  JGE8       = 0x7D,
  JLE8       = 0x7E,
  MATH8_IMM8 = 0x80,
  MATH_IMM32 = 0x81,
  MATH_IMM8  = 0x83,
  MOV_STORE8 = 0x88,
  MOV_STORE  = 0x89,
  MOV_LOAD8  = 0x8A,
  MOV_LOAD   = 0x8B,
  LEA        = 0x8D,
  RET_IMM    = 0xC2,
  RET        = 0xC3,
  MOV_MEM8_IMM = 0xC6,
  MOV_MEM_IMM = 0xC7,
  CALL32     = 0xE8,
  JMP32      = 0xE9,
  JMP8       = 0xEB,
  INC_DEC8   = 0xFE,
  INC_DEC    = 0xFF,
  CALL_JMP_ABS = 0xFF,

  JE         = 0x0F84,
  JNE        = 0x0F85,
  JGE        = 0x0F8D,
  JLE        = 0x0F8E,
  MOVZX8     = 0x0FB6,
};


enum Register {
  None = -1,

  RAX = 0,
  EAX = 0,
  AX = 0,
  AL = 0,

  RCX = 1,
  ECX = 1,
  CX = 1,
  CL = 1,

  RDX = 2,
  EDX = 2,
  DX = 2,
  DL = 2,

  RBX = 3,
  EBX = 3,
  BX = 3,
  BL = 3,

  RSP = 4,
  ESP = 4,
  SP = 4,
  AH = 4,

  RBP = 5,
  EBP = 5,
  BP = 5,
  CH = 5,

  RSI = 6,
  ESI = 6,
  SI = 6,
  DH = 6,

  RDI = 7,
  EDI = 7,
  DI = 7,
  BH = 7,

  R8 = 8,
  R8D = 8,
  R8W = 8,
  R8B = 8,

  R9 = 9,
  R9D = 9,
  R9W = 9,
  R9B = 9,

  R10 = 10,
  R10D = 10,
  R10W = 10,
  R10B = 10,

  R11 = 11,
  R11D = 11,
  R11W = 11,
  R11B = 11,

  R12 = 12,
  R12D = 12,
  R12W = 12,
  R12B = 12,

  R13 = 13,
  R13D = 13,
  R13W = 13,
  R13B = 13,

  R14 = 14,
  R14D = 14,
  R14W = 14,
  R14B = 14,

  R15 = 15,
  R15D = 15,
  R15W = 15,
  R15B = 15,

  Count = 16,

  RIP = 16,
  EIP = 16,
  IP = 16,

  SPL = 17,
  BPL = 18,
  SIL = 19,
  DIL = 20,
};

enum OperandSize {
  Automatic = -1,
  Byte = 0,
  Word = 1,
  DoubleWord = 2,
  QuadWord = 3,
};

const char* name_for_register(Register r,
    OperandSize size = OperandSize::QuadWord);

Register byte_register_for_register(Register r);

struct MemoryReference {
  Register base_register;
  Register index_register;
  int8_t field_size; // if 0, this is a register reference (not memory)
  int64_t offset;

  MemoryReference();
  MemoryReference(Register base_register, int64_t offset,
      Register index_register = Register::None, uint8_t field_size = 1);
  explicit MemoryReference(Register base_register);

  bool operator==(const MemoryReference& other) const;
  bool operator!=(const MemoryReference& other) const;

  std::string str(OperandSize operand_size) const;
};

// shortcuts for all registers
extern MemoryReference rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15;
extern MemoryReference eax, ecx, edx, ebx, esp, ebp, esi, edi, r8d, r9d, r10d, r11d, r12d, r13d, r14d, r15d;
extern MemoryReference ax, cx, dx, bx, sp, bp, si, di, r8w, r9w, r10w, r11w, r12w, r13w, r14w, r15w;
extern MemoryReference al, cl, dl, bl, ah, ch, dh, bh, r8b, r9b, r10b, r11b, r12b, r13b, r14b, r15b, spl, bpl, sil, dil;

class AMD64Assembler {
public:
  AMD64Assembler() = default;
  AMD64Assembler(const AMD64Assembler&) = delete;
  AMD64Assembler(AMD64Assembler&&) = delete;
  AMD64Assembler& operator=(const AMD64Assembler&) = delete;
  AMD64Assembler& operator=(AMD64Assembler&&) = delete;
  ~AMD64Assembler() = default;

  // skip_missing_labels should only be used when debugging callers; it may
  // cause assemble() to return incorrect offsets for jmp/call opcodes
  std::string assemble(std::unordered_set<size_t>& patch_offsets,
      std::multimap<size_t, std::string>* label_offsets = NULL,
      bool skip_missing_labels = false);

  // label support
  void write_label(const std::string& name);

  // stack opcodes
  void write_push(Register r);
  void write_pop(Register r);

  // move opcodes
  void write_lea(Register r, const MemoryReference& mem);
  void write_mov(const MemoryReference& to, const MemoryReference& from,
      OperandSize size = OperandSize::QuadWord);
  void write_mov(Register reg, int64_t value,
      OperandSize size = OperandSize::QuadWord);
  void write_mov(const MemoryReference& mem, int64_t value,
      OperandSize size = OperandSize::QuadWord);
  void write_movzx8(Register to, const MemoryReference& from,
      OperandSize size = OperandSize::QuadWord);

  // control flow opcodes
  void write_jmp(const std::string& label_name);
  void write_call(const MemoryReference& mem);
  void write_ret(uint16_t stack_bytes = 0);
  void write_je(const std::string& label_name);
  void write_jne(const std::string& label_name);
  void write_jge(const std::string& label_name);
  void write_jle(const std::string& label_name);

  // math opcodes
  void write_add(const MemoryReference& to, const MemoryReference& from,
      OperandSize size = OperandSize::QuadWord);
  void write_add(const MemoryReference& to, int64_t value,
      OperandSize size = OperandSize::QuadWord);
  void write_sub(const MemoryReference& to, const MemoryReference& from,
      OperandSize size = OperandSize::QuadWord);
  void write_sub(const MemoryReference& to, int64_t value,
      OperandSize size = OperandSize::QuadWord);

  void write_inc(const MemoryReference& target,
      OperandSize size = OperandSize::QuadWord);
  void write_dec(const MemoryReference& target,
      OperandSize size = OperandSize::QuadWord);

  // comparison opcodes
  void write_cmp(const MemoryReference& to, const MemoryReference& from,
      OperandSize size = OperandSize::QuadWord);
  void write_cmp(const MemoryReference& to, int64_t value,
      OperandSize size = OperandSize::QuadWord);

private:
  static std::string generate_jmp(Operation op8, Operation op32,
    int64_t opcode_address, int64_t target_address, OperandSize* offset_size = NULL);
  static std::string generate_rm(Operation op, const MemoryReference& mem,
      Register reg, OperandSize size, uint32_t extra_prefixes = 0,
      bool skip_64bit_prefix = false);
  static std::string generate_rm(Operation op, const MemoryReference& mem,
      uint8_t z, OperandSize size, uint32_t extra_prefixes = 0,
      bool skip_64bit_prefix = false);
  void write_rm(Operation op, const MemoryReference& mem, Register reg,
      OperandSize size, uint32_t extra_prefixes = 0,
      bool skip_64bit_prefix = false);
  void write_rm(Operation op, const MemoryReference& mem, uint8_t z,
      OperandSize size, uint32_t extra_prefixes = 0,
      bool skip_64bit_prefix = false);
  static Operation load_store_oper_for_args(Operation op,
      const MemoryReference& to, const MemoryReference& from, OperandSize size);
  void write_load_store(Operation base_op, const MemoryReference& to,
      const MemoryReference& from, OperandSize size);
  void write_jcc(Operation op8, Operation op, const std::string& label_name);
  void write_imm_math(Operation math_op, const MemoryReference& to,
      int64_t value, OperandSize size);

  void write(const std::string& opcode);

  struct Patch {
    size_t where;
    uint8_t size; // 1, 4, or 8
    bool absolute;
    Patch(size_t where, uint8_t size, bool absolute);
  };

  struct StreamItem {
    std::string data;
    Operation relative_jump_opcode8; // 0 if not a relative jump
    Operation relative_jump_opcode32; // 0 if not a relative jump

    std::string patch_label_name; // blank for no patch
    Patch patch; // relative to start of data string

    StreamItem(const std::string& data);
    StreamItem(const std::string& data, Operation opcode8, Operation opcode32);
    StreamItem(const std::string& data, const std::string& patch_label_name,
        size_t where, uint8_t size, bool absolute);
  };
  std::deque<StreamItem> stream;

  struct Label {
    std::string name;
    size_t stream_location;
    size_t byte_location;
    std::deque<Patch> patches;

    Label(const std::string& name, size_t stream_location);
    Label(const Label&) = delete;
    Label(Label&&) = default;
  };
  std::deque<Label> labels;
  std::unordered_map<std::string, Label*> name_to_label;
};
