#include "AMD64Assembler.hh"

#include <inttypes.h>

#include <phosg/Strings.hh>
#include <string>

using namespace std;


const char* name_for_register(Register r, OperandSize size) {
  switch (size) {
    case OperandSize::Automatic:
      throw invalid_argument("unresolved operand size");

    case OperandSize::Byte:
      switch (r) {
        case Register::None:
          return "None";
        case Register::AL:
          return "al";
        case Register::CL:
          return "cl";
        case Register::DL:
          return "dl";
        case Register::BL:
          return "bl";
        case Register::AH:
          return "ah";
        case Register::CH:
          return "ch";
        case Register::DH:
          return "dh";
        case Register::BH:
          return "bh";
        case Register::R8B:
          return "r8b";
        case Register::R9B:
          return "r9b";
        case Register::R10B:
          return "r10b";
        case Register::R11B:
          return "r11b";
        case Register::R12B:
          return "r12b";
        case Register::R13B:
          return "r13b";
        case Register::R14B:
          return "r14b";
        case Register::R15B:
          return "r15b";
        case Register::SPL:
          return "spl";
        case Register::BPL:
          return "bpl";
        case Register::SIL:
          return "sil";
        case Register::DIL:
          return "dil";
        default:
          return "UNKNOWN8";
      }
    case OperandSize::Word:
      switch (r) {
        case Register::None:
          return "None";
        case Register::AX:
          return "ax";
        case Register::CX:
          return "cx";
        case Register::DX:
          return "dx";
        case Register::BX:
          return "bx";
        case Register::SP:
          return "sp";
        case Register::BP:
          return "bp";
        case Register::SI:
          return "si";
        case Register::DI:
          return "di";
        case Register::R8W:
          return "r8w";
        case Register::R9W:
          return "r9w";
        case Register::R10W:
          return "r10w";
        case Register::R11W:
          return "r11w";
        case Register::R12W:
          return "r12w";
        case Register::R13W:
          return "r13w";
        case Register::R14W:
          return "r14w";
        case Register::R15W:
          return "r15w";
        default:
          return "UNKNOWN16";
      }
    case OperandSize::DoubleWord:
      switch (r) {
        case Register::None:
          return "None";
        case Register::EAX:
          return "eax";
        case Register::ECX:
          return "ecx";
        case Register::EDX:
          return "edx";
        case Register::EBX:
          return "ebx";
        case Register::ESP:
          return "esp";
        case Register::EBP:
          return "ebp";
        case Register::ESI:
          return "esi";
        case Register::EDI:
          return "edi";
        case Register::R8D:
          return "r8d";
        case Register::R9D:
          return "r9d";
        case Register::R10D:
          return "r10d";
        case Register::R11D:
          return "r11d";
        case Register::R12D:
          return "r12d";
        case Register::R13D:
          return "r13d";
        case Register::R14D:
          return "r14d";
        case Register::R15D:
          return "r15d";
        default:
          return "UNKNOWN32";
      }
    case OperandSize::QuadWord:
      switch (r) {
        case Register::None:
          return "None";
        case Register::RAX:
          return "rax";
        case Register::RCX:
          return "rcx";
        case Register::RDX:
          return "rdx";
        case Register::RBX:
          return "rbx";
        case Register::RSP:
          return "rsp";
        case Register::RBP:
          return "rbp";
        case Register::RSI:
          return "rsi";
        case Register::RDI:
          return "rdi";
        case Register::R8:
          return "r8";
        case Register::R9:
          return "r9";
        case Register::R10:
          return "r10";
        case Register::R11:
          return "r11";
        case Register::R12:
          return "r12";
        case Register::R13:
          return "r13";
        case Register::R14:
          return "r14";
        case Register::R15:
          return "r15";
        default:
          return "UNKNOWN64";
      }
  }
  return "UNKNOWN";
}

Register byte_register_for_register(Register r) {
  switch (r) {
    case Register::None:
      return Register::None;
    case Register::RAX:
      return Register::AL;
    case Register::RCX:
      return Register::CL;
    case Register::RDX:
      return Register::DL;
    case Register::RBX:
      return Register::BL;
    case Register::RSP:
      return Register::SPL;
    case Register::RBP:
      return Register::BPL;
    case Register::RSI:
      return Register::SIL;
    case Register::RDI:
      return Register::DIL;
    case Register::R8:
      return Register::R8B;
    case Register::R9:
      return Register::R9B;
    case Register::R10:
      return Register::R10B;
    case Register::R11:
      return Register::R11B;
    case Register::R12:
      return Register::R12B;
    case Register::R13:
      return Register::R13B;
    case Register::R14:
      return Register::R14B;
    case Register::R15:
      return Register::R15B;
    default:
      return Register::None;
  }
}

static string data_for_opcode(uint32_t opcode) {
  string ret;
  if (opcode > 0xFFFFFF) {
    ret += (opcode >> 24) & 0xFF;
  }
  if (opcode > 0xFFFF) {
    ret += (opcode >> 16) & 0xFF;
  }
  if (opcode > 0xFF) {
    ret += (opcode >> 8) & 0xFF;
  }
  ret += opcode & 0xFF;
  return ret;
}



MemoryReference::MemoryReference() : base_register(Register::None),
    index_register(Register::None), field_size(0), offset(0) { }
MemoryReference::MemoryReference(Register base_register, int64_t offset,
    Register index_register, uint8_t field_size) : base_register(base_register),
    index_register(index_register), field_size(field_size), offset(offset) { }
MemoryReference::MemoryReference(Register base_register) :
    base_register(base_register), index_register(Register::None), field_size(0),
    offset(0) { }

bool MemoryReference::operator==(const MemoryReference& other) const {
  return (this->base_register == other.base_register) &&
         (this->index_register == other.index_register) &&
         (this->field_size == other.field_size) &&
         (this->offset == other.offset);
}

bool MemoryReference::operator!=(const MemoryReference& other) const {
  return !this->operator==(other);
}

string MemoryReference::str(OperandSize operand_size) const {
  if (!this->field_size) {
    return name_for_register(this->base_register, operand_size);
  }

  string ret = "[";
  if (this->base_register != Register::None) {
    ret += name_for_register(this->base_register, OperandSize::QuadWord);
  }
  if (this->index_register != Register::None) {
    if (ret.size() > 1) {
      ret += " + ";
    }
    if (this->field_size > 1) {
      ret += string_printf("%hhd * ", this->field_size);
    }
    ret += name_for_register(this->index_register, OperandSize::QuadWord);
  }
  if (this->offset) {
    if (ret.size() > 1) {
      if (offset < 0) {
        ret += " - ";
      } else {
        ret += " + ";
      }
    }
    ret += string_printf("0x%" PRIX64,
        (this->offset < 0) ? -this->offset : this->offset);
  }
  return ret + "]";
}

MemoryReference rax(Register::RAX);
MemoryReference rcx(Register::RCX);
MemoryReference rdx(Register::RDX);
MemoryReference rbx(Register::RBX);
MemoryReference rsp(Register::RSP);
MemoryReference rbp(Register::RBP);
MemoryReference rsi(Register::RSI);
MemoryReference rdi(Register::RDI);
MemoryReference r8(Register::R8);
MemoryReference r9(Register::R9);
MemoryReference r10(Register::R10);
MemoryReference r11(Register::R11);
MemoryReference r12(Register::R12);
MemoryReference r13(Register::R13);
MemoryReference r14(Register::R14);
MemoryReference r15(Register::R15);
MemoryReference eax(Register::EAX);
MemoryReference ecx(Register::ECX);
MemoryReference edx(Register::EDX);
MemoryReference ebx(Register::EBX);
MemoryReference esp(Register::ESP);
MemoryReference ebp(Register::EBP);
MemoryReference esi(Register::ESI);
MemoryReference edi(Register::EDI);
MemoryReference r8d(Register::R8D);
MemoryReference r9d(Register::R9D);
MemoryReference r10d(Register::R10D);
MemoryReference r11d(Register::R11D);
MemoryReference r12d(Register::R12D);
MemoryReference r13d(Register::R13D);
MemoryReference r14d(Register::R14D);
MemoryReference r15d(Register::R15D);
MemoryReference ax(Register::AX);
MemoryReference cx(Register::CX);
MemoryReference dx(Register::DX);
MemoryReference bx(Register::BX);
MemoryReference sp(Register::SP);
MemoryReference bp(Register::BP);
MemoryReference si(Register::SI);
MemoryReference di(Register::DI);
MemoryReference r8w(Register::R8W);
MemoryReference r9w(Register::R9W);
MemoryReference r10w(Register::R10W);
MemoryReference r11w(Register::R11W);
MemoryReference r12w(Register::R12W);
MemoryReference r13w(Register::R13W);
MemoryReference r14w(Register::R14W);
MemoryReference r15w(Register::R15W);
MemoryReference al(Register::AL);
MemoryReference cl(Register::CL);
MemoryReference dl(Register::DL);
MemoryReference bl(Register::BL);
MemoryReference ah(Register::AH);
MemoryReference ch(Register::CH);
MemoryReference dh(Register::DH);
MemoryReference bh(Register::BH);
MemoryReference r8b(Register::R8B);
MemoryReference r9b(Register::R9B);
MemoryReference r10b(Register::R10B);
MemoryReference r11b(Register::R11B);
MemoryReference r12b(Register::R12B);
MemoryReference r13b(Register::R13B);
MemoryReference r14b(Register::R14B);
MemoryReference r15b(Register::R15B);
MemoryReference spl(Register::SPL);
MemoryReference bpl(Register::BPL);
MemoryReference sil(Register::SIL);
MemoryReference dil(Register::DIL);


static inline bool is_extension_register(Register r) {
  return (static_cast<int8_t>(r) >= 8) && (static_cast<int8_t>(r) < 16);
}

static inline bool is_nonextension_byte_register(Register r) {
  return static_cast<int8_t>(r) >= 17;
}



void AMD64Assembler::write_label(const std::string& name) {
  this->labels.emplace_back(name, this->stream.size());
  if (!this->name_to_label.emplace(name, &this->labels.back()).second) {
    throw invalid_argument("duplicate label name: " + name);
  }
}



string AMD64Assembler::generate_rm(Operation op, const MemoryReference& mem,
    Register reg, OperandSize size, uint32_t extra_prefixes,
    bool skip_64bit_prefix) {
  uint32_t opcode = static_cast<uint32_t>(op);

  string ret;
  if (!mem.field_size) { // behavior = 3 (register reference)
    Register mem_base = mem.base_register;
    bool mem_ext = is_extension_register(mem_base);
    bool mem_nonext_byte = is_nonextension_byte_register(mem_base);
    bool reg_ext = is_extension_register(reg);
    bool reg_nonext_byte = is_nonextension_byte_register(reg);

    // using spl, bpl, sil, dil require a prefix of 0x40
    if (reg_nonext_byte) {
      reg = static_cast<Register>(reg - 13); // convert from enum value to register number
    }
    if (mem_nonext_byte) {
      mem_base = static_cast<Register>(mem_base - 13); // convert from enum value to register number
    }

    if (extra_prefixes) {
      ret += data_for_opcode(extra_prefixes);
    }

    uint8_t prefix_byte = 0x40 | (mem_ext ? 0x01 : 0) | (reg_ext ? 0x04 : 0);
    if (!skip_64bit_prefix && (size == OperandSize::QuadWord)) {
      prefix_byte |= 0x08;
    }
    if (size == OperandSize::Word) {
      ret += 0x66;
    }

    if (mem_nonext_byte || reg_nonext_byte || (prefix_byte != 0x40)) {
      ret += prefix_byte;
    }
    ret += data_for_opcode(opcode);
    ret += static_cast<char>(0xC0 | ((reg & 7) << 3) | (mem_base & 7));
    return ret;
  }

  // TODO: implement these cases
  if (mem.base_register == Register::None) {
    throw invalid_argument("memory references without base not supported");
  }

  bool reg_ext = is_extension_register(reg);
  bool mem_index_ext = is_extension_register(mem.index_register);
  bool mem_base_ext = is_extension_register(mem.base_register);

  uint8_t rm_byte = ((reg & 7) << 3);
  uint8_t sib_byte = 0;
  if (mem.index_register != Register::None) {
    rm_byte |= 0x04;

    if (mem.field_size == 8) {
      sib_byte = 0xC0;
    } else if (mem.field_size == 4) {
      sib_byte = 0x80;
    } else if (mem.field_size == 2) {
      sib_byte = 0x40;
    } else if (mem.field_size != 1) {
      throw invalid_argument("field size must be 1, 2, 4, or 8");
    }

    if (mem.base_register == Register::RBP) {
      throw invalid_argument("RBP cannot be used as a base register in index addressing");
    }
    if (mem.index_register == Register::RSP) {
      throw invalid_argument("RSP cannot be used as a base register in index addressing");
    }

    sib_byte |= mem.base_register & 7;
    if (mem.index_register == Register::None) {
      sib_byte |= ((Register::RSP & 7) << 3);
    } else {
      sib_byte |= ((mem.index_register & 7) << 3);
    }

    if (mem.base_register == Register::RIP) {
      throw invalid_argument("RIP cannot be used with scaled index addressing");
    }

  } else if ((mem.base_register == Register::RSP) ||
             (mem.base_register == Register::R12)) {
    rm_byte |= (mem.base_register & 7);
    sib_byte = 0x24;

  } else {
    if (mem.base_register == Register::RIP) {
      rm_byte |= 0x05;
    } else {
      rm_byte |= (mem.base_register & 7);
    }
  }

  // if an offset was given, update the behavior appropriately
  if (mem.offset == 0) {
    // behavior is 0; nothing to do
  } else if ((mem.offset <= 0x7F) && (mem.offset >= -0x80)) {
    rm_byte |= 0x40;
  } else if ((mem.offset <= 0x7FFFFFFFLL) && (mem.offset >= -0x80000000LL)) {
    rm_byte |= 0x80;
  } else {
    throw invalid_argument("offset must fit in 32 bits");
  }

  // if no offset was given and the sib byte was not used and the base reg is
  // RBP or R13, then add a fake offset of 0 to the opcode (this is because this
  // case is shadowed by the RIP special case above)
  if ((mem.offset == 0) && ((rm_byte & 7) != 0x04) &&
      ((mem.base_register == Register::RSP) || (mem.base_register == Register::R13))) {
    rm_byte |= 0x40;
  }

  // fill in the ret string
  if (extra_prefixes) {
    ret += data_for_opcode(extra_prefixes);
  }

  uint8_t prefix_byte = 0x40 | (reg_ext ? 0x04 : 0) | (mem_index_ext ? 0x02 : 0) |
      (mem_base_ext ? 0x01 : 0);
  if (size == OperandSize::QuadWord) {
    prefix_byte |= 0x08;
  }
  if (size == OperandSize::Word) {
    ret += 0x66;
  }

  if (prefix_byte != 0x40) {
    ret += prefix_byte;
  }
  ret += data_for_opcode(opcode);
  ret += rm_byte;
  if ((rm_byte & 0x07) == 0x04) {
    ret += sib_byte;
  }
  if (rm_byte & 0x40) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 1);
  } else if ((rm_byte & 0x80) || (mem.base_register == Register::RIP)) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 4);
  }
  return ret;
}

string AMD64Assembler::generate_rm(Operation op, const MemoryReference& mem,
    uint8_t z, OperandSize size, uint32_t extra_prefixes,
    bool skip_64bit_prefix) {
  return AMD64Assembler::generate_rm(op, mem, static_cast<Register>(z), size,
      extra_prefixes, skip_64bit_prefix);
}

void AMD64Assembler::write_rm(Operation op, const MemoryReference& mem,
    Register reg, OperandSize size, uint32_t extra_prefixes,
    bool skip_64bit_prefix) {
  this->write(this->generate_rm(op, mem, reg, size, extra_prefixes,
      skip_64bit_prefix));
}

void AMD64Assembler::write_rm(Operation op, const MemoryReference& mem,
    uint8_t z, OperandSize size, uint32_t extra_prefixes,
    bool skip_64bit_prefix) {
  this->write(this->generate_rm(op, mem, z, size, extra_prefixes,
      skip_64bit_prefix));
}




Operation AMD64Assembler::load_store_oper_for_args(Operation op,
    const MemoryReference& to, const MemoryReference& from, OperandSize size) {
  return static_cast<Operation>(op | ((size != OperandSize::Byte) ? 1 : 0) |
      (from.field_size ? 2 : 0));
}

void AMD64Assembler::write_load_store(Operation base_op, const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  if (to.field_size && from.field_size) {
    throw invalid_argument("load/store opcodes can have at most one memory reference");
  }

  Operation op = load_store_oper_for_args(base_op, to, from, size);
  if (!from.field_size) {
    this->write_rm(op, to, from.base_register, size);
  } else {
    this->write_rm(op, from, to.base_register, size);
  }
}



void AMD64Assembler::write_push(Register r) {
  string data;
  if (is_extension_register(r)) {
    data += 0x41;
    data += (static_cast<uint8_t>(r) - 8) | 0x50;
  } else {
    data += r | 0x50;
  }
  this->write(data);
}

void AMD64Assembler::write_pop(Register r) {
  string data;
  if (is_extension_register(r)) {
    data += 0x41;
    data += (static_cast<uint8_t>(r) - 8) | 0x58;
  } else {
    data += r | 0x58;
  }
  this->write(data);
}



void AMD64Assembler::write_lea(Register r, const MemoryReference& mem) {
  this->write_rm(Operation::LEA, mem, r, OperandSize::QuadWord);
}

void AMD64Assembler::write_mov(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  this->write_load_store(Operation::MOV_STORE8, to, from, size);
}

void AMD64Assembler::write_mov(Register reg, int64_t value, OperandSize size) {
  string data;
  if (size == OperandSize::QuadWord) {
    // if the value can fit in a standard mov, use that instead
    if (((value & 0xFFFFFFFF80000000) == 0) || ((value & 0xFFFFFFFF80000000) == 0xFFFFFFFF80000000)) {
      this->write_mov(MemoryReference(reg), value, size);
      return;
    }
    data += 0x48 | (is_extension_register(reg) ? 0x01 : 0);
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 8);

  } else if (size == OperandSize::DoubleWord) {
    string data;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 4);

  } else if (size == OperandSize::Word) {
    string data;
    data += 0x66;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 2);

  } else if (size == OperandSize::Byte) {
    string data;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB0 | (reg & 7);
    data += static_cast<int8_t>(value);

  } else {
    throw invalid_argument("unknown operand size");
  }
  this->write(data);
}

void AMD64Assembler::write_mov(const MemoryReference& mem, int64_t value,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::MOV_MEM8_IMM : Operation::MOV_MEM_IMM;
  string data = this->generate_rm(op, mem, 0, size);

  if (((value & 0xFFFFFFFF80000000) != 0) && ((value & 0xFFFFFFFF80000000) != 0xFFFFFFFF80000000)) {
    throw invalid_argument("value out of range for r/m mov");
  }

  // this opcode has a 32-bit imm for both 32-bit and 64-bit operand sizes
  if ((size == OperandSize::QuadWord) || (size == OperandSize::DoubleWord)) {
    data.append(reinterpret_cast<const char*>(&value), 4);
  } else if (size == OperandSize::Word) {
    data.append(reinterpret_cast<const char*>(&value), 2);
  } else if (size == OperandSize::Byte) {
    data += static_cast<int8_t>(value);
  } else {
    throw invalid_argument("unknown operand size");
  }

  this->write(data);
}

void AMD64Assembler::write_movzx8(Register reg, const MemoryReference& mem,
    OperandSize size) {
  this->write_rm(Operation::MOVZX8, mem, reg, size);
}



void AMD64Assembler::write_jmp(const std::string& label_name) {
  this->stream.emplace_back(label_name, Operation::JMP8, Operation::JMP32);
}

std::string AMD64Assembler::generate_jmp(Operation op8, Operation op32,
    int64_t opcode_address, int64_t target_address, OperandSize* offset_size) {
  int64_t offset = target_address - opcode_address;

  if (op8) { // may be omitted for call opcodes
    int64_t offset8 = offset - 2 - (static_cast<int64_t>(op8) > 0xFF);
    if ((offset8 >= -0x80) && (offset8 <= 0x7F)) {
      string data;
      if (op8 > 0xFF) {
        data += (op8 >> 8) & 0xFF;
      }
      data += op8 & 0xFF;
      data.append(reinterpret_cast<const char*>(&offset8), 1);
      if (offset_size) {
        *offset_size = OperandSize::Byte;
      }
      return data;
    }
  }

  int64_t offset32 = offset - 5 - (static_cast<int64_t>(op32) > 0xFF);
  if ((offset32 >= -0x80000000LL) && (offset32 <= 0x7FFFFFFFLL)) {
    string data;
    if (op32 > 0xFF) {
      data += (op32 >> 8) & 0xFF;
    }
    data += op32 & 0xFF;
    data.append(reinterpret_cast<const char*>(&offset32), 4);
    if (offset_size) {
      *offset_size = OperandSize::QuadWord;
    }
    return data;
  }

  // the nasty case: we have to use a 64-bit offset. here we do this by putting
  // the address on the stack, and "returning" to it
  // TODO: support conditional jumps and 64-bit calls here
  if (op32 != Operation::JMP32) {
    throw runtime_error("64-bit calls and conditional jumps not yet implemented");
  }
  string data;
  // push <low 4 bytes of address>
  data += 0x68;
  data.append(reinterpret_cast<const char*>(&target_address), 4);
  // mov [rsp+4], <high 4 bytes of address>
  data += 0xC7;
  data += 0x44;
  data += 0x24;
  data += 0x04;
  data.append(reinterpret_cast<const char*>(&target_address) + 4, 4);
  // ret
  data += 0xC3;
  if (offset_size) {
    *offset_size = OperandSize::QuadWord;
  }
  return data;
}

void AMD64Assembler::write_call(const MemoryReference& mem) {
  this->write_rm(Operation::CALL_JMP_ABS, mem, 2, OperandSize::DoubleWord);
}

void AMD64Assembler::write_ret(uint16_t stack_bytes) {
  if (stack_bytes) {
    string data("\xC2", 1);
    data.append(reinterpret_cast<const char*>(&stack_bytes), 2);
    this->write(data);
  } else {
    this->write("\xC3");
  }
}



void AMD64Assembler::write_jcc(Operation op8, Operation op,
    const std::string& label_name) {
  this->stream.emplace_back(label_name, op8, op);
}

void AMD64Assembler::write_je(const string& label_name) {
  this->write_jcc(Operation::JE8, Operation::JE, label_name);
}

void AMD64Assembler::write_jne(const string& label_name) {
  this->write_jcc(Operation::JNE8, Operation::JNE, label_name);
}

void AMD64Assembler::write_jge(const string& label_name) {
  this->write_jcc(Operation::JGE8, Operation::JGE, label_name);
}

void AMD64Assembler::write_jle(const string& label_name) {
  this->write_jcc(Operation::JLE8, Operation::JLE, label_name);
}



void AMD64Assembler::write_imm_math(Operation math_op,
    const MemoryReference& to, int64_t value, OperandSize size) {
  if (math_op & 0xC7) {
    throw invalid_argument("immediate math opcodes must use basic Operation types");
  }

  Operation op;
  if (size == OperandSize::Byte) {
    op = Operation::MATH8_IMM8;
  } else if ((value < -0x80000000LL) || (value > 0x7FFFFFFFLL)) {
    throw invalid_argument("immediate value out of range");
  } else if ((value > 0x7F) || (value < -0x80)) {
    op = Operation::MATH_IMM32;
  } else {
    op = Operation::MATH_IMM8;
  }

  uint8_t z = (math_op >> 3) & 7;
  string data = this->generate_rm(op, to, z, size);
  if ((size == OperandSize::Byte) || (op == Operation::MATH_IMM8)) {
    data += static_cast<uint8_t>(value);
  } else if (size == OperandSize::Word) {
    data.append(reinterpret_cast<const char*>(&value), 2);
  } else {
    data.append(reinterpret_cast<const char*>(&value), 4);
  }
  this->write(data);
}



void AMD64Assembler::write_add(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::ADD_STORE8, to, from, size);
}

void AMD64Assembler::write_add(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::ADD_STORE8, to, value, size);
}

void AMD64Assembler::write_sub(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::SUB_STORE8, to, from, size);
}

void AMD64Assembler::write_sub(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::SUB_STORE8, to, value, size);
}

void AMD64Assembler::write_cmp(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::CMP_STORE8, to, from, size);
}

void AMD64Assembler::write_cmp(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::CMP_STORE8, to, value, size);
}



void AMD64Assembler::write_inc(const MemoryReference& target,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::INC_DEC8 : Operation::INC_DEC;
  this->write_rm(op, target, 0, size);
}

void AMD64Assembler::write_dec(const MemoryReference& target,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::INC_DEC8 : Operation::INC_DEC;
  this->write_rm(op, target, 1, size);
}



void AMD64Assembler::write(const string& data) {
  this->stream.emplace_back(data);
}

string AMD64Assembler::assemble(unordered_set<size_t>& patch_offsets,
    multimap<size_t, string>* label_offsets, bool skip_missing_labels) {
  string code;

  // general strategy: assemble everything in order. for backward jumps, we know
  // exactly what the offset will be, so just use the right opcode. for forward
  // jumps, compute the offset as if all intervening jumps are 32-bit jumps, and
  // then backpatch the offset appropriately. this will waste space in some edge
  // cases but I'm lazy

  auto apply_patch = [&code, &patch_offsets](const Label& l, const Patch& p) {
    int64_t value = static_cast<int64_t>(l.byte_location);
    if (!p.absolute) {
      value -= (static_cast<int64_t>(p.where) + p.size);
    }

    // 8-bit patch
    if (p.size == 1) {
      if (p.absolute) {
        throw runtime_error("8-bit patches must be relative");
      }
      if ((value < -0x80) || (value > 0x7F)) {
        throw runtime_error("8-bit patch location too far away");
      }
      *reinterpret_cast<int8_t*>(&code[p.where]) = static_cast<int8_t>(value);

    // 32-bit patch
    } else if (p.size == 4) {
      if (p.absolute) {
        throw runtime_error("32-bit patches must be relative");
      }
      if ((value < -0x80000000LL) || (value > 0x7FFFFFFFLL)) {
        throw runtime_error("32-bit patch location too far away");
      }
      *reinterpret_cast<int32_t*>(&code[p.where]) = static_cast<int32_t>(value);

    // 64-bit patch
    } else if (p.size == 8) {
      if (p.absolute) {
        patch_offsets.emplace(p.where);
      }
      *reinterpret_cast<int64_t*>(&code[p.where]) = value;

    } else {
      throw invalid_argument("patch size is not 1, 4, or 8 bytes");
    }
  };

  size_t stream_location = 0;
  auto label_it = this->labels.begin();
  for (auto stream_it = this->stream.begin(); stream_it != this->stream.end(); stream_it++) {
    const auto& item = *stream_it;

    // if there's a label at this location, set its memory location
    while ((label_it != this->labels.end()) &&
        (label_it->stream_location == stream_location)) {
      label_it->byte_location = code.size();
      if (label_offsets) {
        label_offsets->emplace(label_it->byte_location, label_it->name);
      }

      // if there are patches waiting, perform them now that the label's
      // location is determined
      for (const auto& patch : label_it->patches) {
        apply_patch(*label_it, patch);
      }
      label_it->patches.clear();
      label_it++;
    }

    // if this stream item is a jump opcode, find the relevant label
    if (item.relative_jump_opcode8 || item.relative_jump_opcode32) {
      Label* label = NULL;
      try {
        label = this->name_to_label.at(item.data);
      } catch (const out_of_range& e) {
        if (!skip_missing_labels) {
          throw runtime_error("nonexistent label: " + item.data);
        }
      }

      if (label) {
        // if the label's address is known, we can easily write a jump opcode
        if (label->byte_location <= code.size()) {
          code += this->generate_jmp(item.relative_jump_opcode8,
              item.relative_jump_opcode32, code.size(),
              label->byte_location);

        // else, we have to estimate how far away the label will be
        } else {

          // find the target label
          size_t target_stream_location = label->stream_location;

          // find the maximum number of bytes between here and there
          int64_t max_displacement = 0;
          size_t where_stream_location = stream_location;
          for (auto where_it = stream_it + 1;
               (where_it != this->stream.end()) && (where_stream_location < target_stream_location);
               where_it++, where_stream_location++) {
            if (where_it->relative_jump_opcode8 || where_it->relative_jump_opcode32) {
              // assume it's a 32-bit jump
              max_displacement += 5 + (where_it->relative_jump_opcode32 > 0xFF);
            } else {
              max_displacement += where_it->data.size();
            }
          }

          // generate a bogus forward jmp opcode, and the appropriate patches
          OperandSize offset_size;
          code += this->generate_jmp(item.relative_jump_opcode8,
              item.relative_jump_opcode32, code.size(),
              code.size() + max_displacement, &offset_size);
          if (offset_size == OperandSize::Byte) {
            label->patches.emplace_back(code.size() - 1, 1, false);
          } else if (offset_size == OperandSize::QuadWord) {
            label->patches.emplace_back(code.size() - 4, 4, false);
          } else {
            throw runtime_error("64-bit jump cannot be backpatched");
          }
        }
      }

    // this item is not a jump opcode; stick it in the buffer
    } else {
      code += item.data;
    }

    // if this stream item has a patch, apply it from the appropriate label
    if (item.patch.size) {
      // get the label that the patch refers to
      Label* label = NULL;
      try {
        label = this->name_to_label.at(item.patch_label_name);
      } catch (const out_of_range& e) {
        if (!skip_missing_labels) {
          throw runtime_error("nonexistent label: " + item.patch_label_name);
        }
      }

      if (label) {
        // if we know the label's location already, apply the patch now
        if (label->byte_location <= code.size()) {
          apply_patch(*label, item.patch);

        // if we don't know the label's location, make the patch pending
        } else {
          label->patches.emplace_back(
              code.size() - item.data.size() + item.patch.where,
              item.patch.size, item.patch.absolute);
        }
      }
    }

    stream_location++;
  }

  // bugcheck: make sure there are no patches waiting
  for (const auto& label : this->labels) {
    if (!label.patches.empty()) {
      throw logic_error("some patches were not applied");
    }
  }

  this->name_to_label.clear();
  this->labels.clear();
  this->stream.clear();

  return code;
}

AMD64Assembler::StreamItem::StreamItem(const std::string& data) : data(data),
    relative_jump_opcode8(Operation::ADD_STORE8),
    relative_jump_opcode32(Operation::ADD_STORE8), patch(0, 0, false) { }

AMD64Assembler::StreamItem::StreamItem(const std::string& data,
    Operation opcode8, Operation opcode32) : data(data),
    relative_jump_opcode8(opcode8), relative_jump_opcode32(opcode32),
    patch(0, 0, false) { }

AMD64Assembler::StreamItem::StreamItem(const std::string& data,
    const string& patch_label_name, size_t where, uint8_t size, bool absolute) :
    data(data), relative_jump_opcode8(Operation::ADD_STORE8),
    relative_jump_opcode32(Operation::ADD_STORE8),
    patch_label_name(patch_label_name), patch(where, size, absolute) { }

AMD64Assembler::Patch::Patch(size_t where, uint8_t size, bool absolute) :
    where(where), size(size), absolute(absolute) { }

AMD64Assembler::Label::Label(const std::string& name, size_t stream_location) :
    name(name), stream_location(stream_location),
    byte_location(0xFFFFFFFFFFFFFFFF) { }
