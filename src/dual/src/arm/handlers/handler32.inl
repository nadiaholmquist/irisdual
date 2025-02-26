
enum class ARMDataOp {
  AND = 0,
  EOR = 1,
  SUB = 2,
  RSB = 3,
  ADD = 4,
  ADC = 5,
  SBC = 6,
  RSC = 7,
  TST = 8,
  TEQ = 9,
  CMP = 10,
  CMN = 11,
  ORR = 12,
  MOV = 13,
  BIC = 14,
  MVN = 15
};

template <bool immediate, ARMDataOp opcode, bool set_flags, int field4>
void ARM_DataProcessing(u32 instruction) {
  int reg_dst = (instruction >> 12) & 0xF;
  int reg_op1 = (instruction >> 16) & 0xF;
  int reg_op2 = (instruction >>  0) & 0xF;

  u32 op2 = 0;
  u32 op1 = m_state.reg[reg_op1];

  int carry = m_state.cpsr.c;

  if constexpr(immediate) {
    int value = instruction & 0xFF;
    int shift = ((instruction >> 8) & 0xF) * 2;

    if(shift != 0) {
      carry = (value >> (shift - 1)) & 1;
      op2   = (value >> shift) | (value << (32 - shift));
    } else {
      op2 = value;
    }
  } else {
    constexpr int  shift_type = ( field4 >> 1) & 3;
    constexpr bool shift_imm  = (~field4 >> 0) & 1;

    u32 shift;

    op2 = m_state.reg[reg_op2];

    if constexpr(shift_imm) {
      shift = (instruction >> 7) & 0x1F;
    } else {
      shift = m_state.reg[(instruction >> 8) & 0xF];

      if(reg_op1 == 15) op1 += 4;
      if(reg_op2 == 15) op2 += 4;
    }

    DoShift(shift_type, op2, shift, carry, shift_imm);
  }

  auto& cpsr = m_state.cpsr;
  auto& result = m_state.reg[reg_dst];

  switch(opcode) {
    case ARMDataOp::AND:
      result = op1 & op2;
      if constexpr(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
    case ARMDataOp::EOR:
      result = op1 ^ op2;
      if constexpr(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
    case ARMDataOp::SUB:
      result = SUB(op1, op2, set_flags);
      break;
    case ARMDataOp::RSB:
      result = SUB(op2, op1, set_flags);
      break;
    case ARMDataOp::ADD:
      result = ADD(op1, op2, set_flags);
      break;
    case ARMDataOp::ADC:
      result = ADC(op1, op2, set_flags);
      break;
    case ARMDataOp::SBC:
      result = SBC(op1, op2, set_flags);
      break;
    case ARMDataOp::RSC:
      result = SBC(op2, op1, set_flags);
      break;
    case ARMDataOp::TST:
      SetZeroAndSignFlag(op1 & op2);
      cpsr.c = carry;
      break;
    case ARMDataOp::TEQ:
      SetZeroAndSignFlag(op1 ^ op2);
      cpsr.c = carry;
      break;
    case ARMDataOp::CMP:
      SUB(op1, op2, true);
      break;
    case ARMDataOp::CMN:
      ADD(op1, op2, true);
      break;
    case ARMDataOp::ORR:
      result = op1 | op2;
      if(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
    case ARMDataOp::MOV:
      result = op2;
      if constexpr(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
    case ARMDataOp::BIC:
      result = op1 & ~op2;
      if constexpr(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
    case ARMDataOp::MVN:
      result = ~op2;
      if constexpr(set_flags) {
        SetZeroAndSignFlag(result);
        cpsr.c = carry;
      }
      break;
  }

  if(reg_dst == 15) {
    if constexpr(set_flags) {
      auto spsr = *m_spsr;

      SwitchMode((Mode)spsr.mode);
      m_state.cpsr = spsr;
    }

    if constexpr(opcode != ARMDataOp::TST &&
                 opcode != ARMDataOp::TEQ &&
                 opcode != ARMDataOp::CMP &&
                 opcode != ARMDataOp::CMN) {
      if(m_state.cpsr.thumb) {
        ReloadPipeline16();
      } else {
        ReloadPipeline32();
      }
    }
  } else {
    m_state.r15 += 4;
  }
}

template <bool immediate, bool use_spsr, bool to_status>
void ARM_StatusTransfer(u32 instruction) {
  if constexpr(to_status) {
    u32 op;
    u32 mask = 0;
    u8  fsxc = (instruction >> 16) & 0xF;

    if constexpr(immediate) {
      if(fsxc == 0) {
        // Hint instructions (such as WFI) are encoded as immediate MSR with fsxc==0.
        return ARM_Hint(instruction);
      }

      int value = instruction & 0xFF;
      int shift = ((instruction >> 8) & 0xF) * 2;

      op = (value >> shift) | (value << (32 - shift));
    } else {
      op = m_state.reg[instruction & 0xF];
    }

    if(fsxc & 1) mask |= 0x000000FF;
    if(fsxc & 2) mask |= 0x0000FF00;
    if(fsxc & 4) mask |= 0x00FF0000;
    if(fsxc & 8) mask |= 0xFF000000;

    u32 value = op & mask;

    if constexpr(!use_spsr) {
      if(mask & 0xFF) {
        SwitchMode(static_cast<Mode>(value & 0x1F));
      }
      m_state.cpsr.word = (m_state.cpsr.word & ~mask) | value;
    } else {
      m_spsr->word = (m_spsr->word & ~mask) | value;
    }
  } else {
    int dst = (instruction >> 12) & 0xF;

    if constexpr(use_spsr) {
      m_state.reg[dst] = m_spsr->word;
    } else {
      m_state.reg[dst] = m_state.cpsr.word;
    }
  }

  m_state.r15 += 4;
}

template <bool accumulate, bool set_flags>
void ARM_Multiply(u32 instruction) {
  u32 result;

  int op1 = (instruction >>  0) & 0xF;
  int op2 = (instruction >>  8) & 0xF;
  int op3 = (instruction >> 12) & 0xF;
  int dst = (instruction >> 16) & 0xF;

  result = m_state.reg[op1] * m_state.reg[op2];

  if constexpr(accumulate) {
    result += m_state.reg[op3];
  }

  if constexpr(set_flags) {
    SetZeroAndSignFlag(result);
  }

  m_state.reg[dst] = result;
  m_state.r15 += 4;
}

template <bool sign_extend, bool accumulate, bool set_flags>
void ARM_MultiplyLong(u32 instruction) {
  int op1 = (instruction >> 0) & 0xF;
  int op2 = (instruction >> 8) & 0xF;

  int dst_lo = (instruction >> 12) & 0xF;
  int dst_hi = (instruction >> 16) & 0xF;

  s64 result;

  if constexpr(sign_extend) {
    s64 a = m_state.reg[op1];
    s64 b = m_state.reg[op2];

    // Sign-extend operands
    if(a & 0x80000000) a |= 0xFFFFFFFF00000000;
    if(b & 0x80000000) b |= 0xFFFFFFFF00000000;

    result = a * b;
  } else {
    u64 uresult = (u64)m_state.reg[op1] * (u64)m_state.reg[op2];

    result = (s64)uresult;
  }

  if constexpr(accumulate) {
    s64 value = m_state.reg[dst_hi];

    // @todo: in theory we should be able to shift by 32 because value in 64-bit.
    value <<= 16;
    value <<= 16;
    value  |= m_state.reg[dst_lo];

    result += value;
  }

  u32 result_hi = result >> 32;

  m_state.reg[dst_lo] = result & 0xFFFFFFFF;
  m_state.reg[dst_hi] = result_hi;

  if constexpr(set_flags) {
    m_state.cpsr.n = result_hi >> 31;
    m_state.cpsr.z = result == 0;
  }

  m_state.r15 += 4;
}

template <bool accumulate, bool x, bool y>
void ARM_SignedHalfwordMultiply(u32 instruction) {
  if(m_model == Model::ARM7) {
    ARM_Undefined(instruction);
    return;
  }

  int op1 = (instruction >>  0) & 0xF;
  int op2 = (instruction >>  8) & 0xF;
  int op3 = (instruction >> 12) & 0xF;
  int dst = (instruction >> 16) & 0xF;

  s16 value1;
  s16 value2;

  if constexpr(x) {
    value1 = s16(m_state.reg[op1] >> 16);
  } else {
    value1 = s16(m_state.reg[op1] & 0xFFFF);
  }

  if constexpr(y) {
    value2 = s16(m_state.reg[op2] >> 16);
  } else {
    value2 = s16(m_state.reg[op2] & 0xFFFF);
  }

  u32 result = u32(value1 * value2);

  if constexpr(accumulate) {
    // Update sticky-flag without saturating the result.
    // @todo: create a helper to detect overflow and use it instead of QADD?
    m_state.reg[dst] = QADD(result, m_state.reg[op3], false);
  } else {
    m_state.reg[dst] = result;
  }

  m_state.r15 += 4;
}

template <bool accumulate, bool y>
void ARM_SignedWordHalfwordMultiply(u32 instruction) {
  if(m_model == Model::ARM7) {
    // @todo: unclear how this instruction behaves on the ARM7.
    ARM_Undefined(instruction);
    return;
  }

  int op1 = (instruction >>  0) & 0xF;
  int op2 = (instruction >>  8) & 0xF;
  int op3 = (instruction >> 12) & 0xF;
  int dst = (instruction >> 16) & 0xF;

  s32 value1 = s32(m_state.reg[op1]);
  s16 value2;

  if constexpr(y) {
    value2 = s16(m_state.reg[op2] >> 16);
  } else {
    value2 = s16(m_state.reg[op2] & 0xFFFF);
  }

  u32 result = u32((value1 * value2) >> 16);

  if constexpr(accumulate) {
    // Update sticky-flag without saturating the result.
    // @todo: create a helper to detect overflow and use it instead of QADD?
    m_state.reg[dst] = QADD(result, m_state.reg[op3], false);
  } else {
    m_state.reg[dst] = result;
  }

  m_state.r15 += 4;
}

template <bool x, bool y>
void ARM_SignedHalfwordMultiplyLongAccumulate(u32 instruction) {
  if(m_model == Model::ARM7) {
    // @todo: unclear how this instruction behaves on the ARM7.
    ARM_Undefined(instruction);
    return;
  }

  int op1 = (instruction >> 0) & 0xF;
  int op2 = (instruction >> 8) & 0xF;
  int dst_lo = (instruction >> 12) & 0xF;
  int dst_hi = (instruction >> 16) & 0xF;

  s16 value1;
  s16 value2;

  if constexpr(x) {
    value1 = s16(m_state.reg[op1] >> 16);
  } else {
    value1 = s16(m_state.reg[op1] & 0xFFFF);
  }

  if constexpr(y) {
    value2 = s16(m_state.reg[op2] >> 16);
  } else {
    value2 = s16(m_state.reg[op2] & 0xFFFF);
  }

  u64 result = value1 * value2;

  result += m_state.reg[dst_lo];
  result += u64(m_state.reg[dst_hi]) << 32;

  m_state.reg[dst_lo] = result & 0xFFFFFFFF;
  m_state.reg[dst_hi] = result >> 32;

  m_state.r15 += 4;
}

template <bool byte>
void ARM_SingleDataSwap(u32 instruction) {
  int src  = (instruction >>  0) & 0xF;
  int dst  = (instruction >> 12) & 0xF;
  int base = (instruction >> 16) & 0xF;

  u32 tmp;

  if constexpr(byte) {
    tmp = ReadByte(m_state.reg[base]);
    WriteByte(m_state.reg[base], (u8)m_state.reg[src]);
  } else {
    tmp = ReadWordRotate(m_state.reg[base]);
    WriteWord(m_state.reg[base], m_state.reg[src]);
  }

  m_state.reg[dst] = tmp;
  m_state.r15 += 4;
}

template <bool link>
void ARM_BranchAndExchangeMaybeLink(u32 instruction) {
  u32 address = m_state.reg[instruction & 0xF];

  if constexpr(link) {
    if(m_model == Model::ARM7) {
      ARM_Undefined(instruction);
      return;
    }
    m_state.r14 = m_state.r15 - 4;
  }

  if(address & 1) {
    m_state.r15 = address & ~1;
    m_state.cpsr.thumb = 1;
    ReloadPipeline16();
  } else {
    m_state.r15 = address & ~3;
    ReloadPipeline32();
  }
}

template <bool pre, bool add, bool immediate, bool writeback, bool load, int opcode>
void ARM_HalfDoubleAndSignedTransfer(u32 instruction) {
  int dst  = (instruction >> 12) & 0xF;
  int base = (instruction >> 16) & 0xF;

  u32 offset;
  u32 address = m_state.reg[base];
  bool allow_writeback = !load || base != dst;

  if constexpr(immediate) {
    offset = (instruction & 0xF) | ((instruction >> 4) & 0xF0);
  } else {
    offset = m_state.reg[instruction & 0xF];
  }

  m_state.r15 += 4;

  if constexpr(pre) {
    address += add ? offset : -offset;
  }

  switch(opcode) {
    case 1: {
      if constexpr(load) {
        m_state.reg[dst] = ReadHalfMaybeRotate(address);
      } else {
        WriteHalf(address, m_state.reg[dst]);
      }
      break;
    }
    case 2: {
      if constexpr(load) {
        m_state.reg[dst] = ReadByteSigned(address);
      } else if(m_model != Model::ARM7) {
        // LDRD: using an odd numbered destination register is undefined.
        if((dst & 1) == 1) {
          m_state.r15 -= 4;
          ARM_Undefined(instruction);
          return;
        }

        /* LDRD writeback edge-case deviates from the regular LDR behavior.
         * Instead it behaves more like a LDM instruction, in that the
         * base register writeback happens between the first and second load.
         */
        allow_writeback = base != (dst + 1);

        m_state.reg[dst + 0] = ReadWord(address + 0);
        m_state.reg[dst + 1] = ReadWord(address + 4);

        if(dst == 14) {
          ReloadPipeline32();
        }
      }
      break;
    }
    case 3: {
      if constexpr(load) {
        m_state.reg[dst] = ReadHalfSigned(address);
      } else if(m_model != Model::ARM7) {
        // STRD: using an odd numbered destination register is undefined.
        if((dst & 1) == 1) {
          m_state.r15 -= 4;
          ARM_Undefined(instruction);
          return;
        }

        WriteWord(address + 0, m_state.reg[dst + 0]);
        WriteWord(address + 4, m_state.reg[dst + 1]);
      }
      break;
    }
    default: {
      ATOM_PANIC("this code should have not been reached");
    }
  }

  if(allow_writeback) {
    if constexpr(!pre) {
      m_state.reg[base] += add ? offset : -offset;
    } else if(writeback) {
      m_state.reg[base] = address;
    }
  }

  if(load && dst == 15) {
    ReloadPipeline32();
  }
}

template <bool link>
void ARM_BranchAndLink(u32 instruction) {
  u32 offset = instruction & 0xFFFFFF;

  if(offset & 0x800000) {
    offset |= 0xFF000000;
  }

  if constexpr(link) {
    m_state.r14 = m_state.r15 - 4;
  }

  m_state.r15 += offset * 4;
  ReloadPipeline32();
}

void ARM_BranchLinkExchangeImm(u32 instruction) {
  u32 offset = instruction & 0xFFFFFF;

  if(offset & 0x800000) {
    offset |= 0xFF000000;
  }

  offset = (offset << 2) | ((instruction >> 23) & 2);

  m_state.r14  = m_state.r15 - 4;
  m_state.r15 += offset;
  m_state.cpsr.thumb = 1;
  ReloadPipeline16();
}

template <bool immediate, bool pre, bool add, bool byte, bool writeback, bool load>
void ARM_SingleDataTransfer(u32 instruction) {
  u32 offset;

  int dst  = (instruction >> 12) & 0xF;
  int base = (instruction >> 16) & 0xF;
  u32 address = m_state.reg[base];

  constexpr bool translation = !pre && writeback;

  // We do not support LDRT/STRT at the moment.
  if(translation) {
    ARM_Unimplemented(instruction);
    return;
  }

  // Calculate offset relative to base register.
  if constexpr(immediate) {
    offset = instruction & 0xFFF;
  } else {
    int carry  = m_state.cpsr.c;
    int opcode = (instruction >> 5) & 3;
    int amount = (instruction >> 7) & 0x1F;

    offset = m_state.reg[instruction & 0xF];
    DoShift(opcode, offset, amount, carry, true);
  }

  m_state.r15 += 4;

  if constexpr(pre) {
    address += add ? offset : -offset;
  }

  if constexpr(load) {
    if constexpr(byte) {
      m_state.reg[dst] = ReadByte(address);
    } else {
      m_state.reg[dst] = ReadWordRotate(address);
    }
  } else {
    if constexpr(byte) {
      WriteByte(address, (u8)m_state.reg[dst]);
    } else {
      WriteWord(address, m_state.reg[dst]);
    }
  }

  // Writeback final address to the base register.
  if(!load || base != dst) {
    if constexpr(!pre) {
      m_state.reg[base] += add ? offset : -offset;
    } else if(writeback) {
      m_state.reg[base] = address;
    }
  }

  if constexpr(load) {
    if(dst == 15) {
      if((m_state.r15 & 1) && m_model != Model::ARM7) {
        if(byte || translation) {
          ATOM_PANIC("unpredictable LDRB or LDRT to PC (PC=0x{:08X})", m_state.r15);
        }
        m_state.cpsr.thumb = 1;
        m_state.r15 &= ~1;
        ReloadPipeline16();
      } else {
        ReloadPipeline32();
      }
    }
  }
}

template <bool pre, bool add, bool user_mode, bool writeback, bool load>
void ARM_BlockDataTransfer(u32 instruction) {
  int list = instruction & 0xFFFF;
  int base = (instruction >> 16) & 0xF;

  Mode mode;
  bool transfer_pc = list & (1 << 15);

  int bytes;
  u32 base_new;
  u32 address = m_state.reg[base];
  bool base_is_first = false;
  bool base_is_last = false;

  // Fail if we detect any unknown ARM11 edge-cases
  if(m_model == Model::ARM11) {
    if(list == 0) {
      ATOM_PANIC("unknown ARM11 LDM/STM with empty register set: 0x{:08X}", instruction);
    }

    if(writeback && (list & (1 << base)) != 0) {
      ATOM_PANIC("unknown ARM11 LDM/STM with writeback and to/from base register: 0x{:08X}", instruction);
    }
  }

  if(list != 0) {
    #if defined(__has_builtin) && __has_builtin(__builtin_popcount)
      bytes = __builtin_popcount(list) * sizeof(u32);
    #else
      bytes = 0;
      for(int i = 0; i <= 15; i++) {
        if(list & (1 << i))
          bytes += sizeof(u32);
      }
    #endif

    #if defined(__has_builtin) && __has_builtin(__builtin_ctz)
      base_is_first = __builtin_ctz(list) == base;
    #else
      base_is_first = (list & ((1 << base) - 1)) == 0;
    #endif

    #if defined(__has_builtin) && __has_builtin(__builtin_clz)
      base_is_last = (31 - __builtin_clz(list)) == base;
    #else
      base_is_last = (list >> base) == 1;
    #endif
  } else {
    bytes = 16 * sizeof(u32);
    if(m_model == Model::ARM7) {
      list = 1 << 15;
      transfer_pc = true;
    }
  }

  if constexpr(!add) {
    address -= bytes;
    base_new = address;
  } else {
    base_new = address + bytes;
  }

  m_state.r15 += 4;

  // STM ARMv4: store new base if base is not the first register and old base otherwise.
  // STM ARMv5: always store old base.
  if constexpr(writeback && !load) {
    if(m_model == Model::ARM7 && !base_is_first) {
      m_state.reg[base] = base_new;
    }
  }

  if constexpr(user_mode) {
    if(!load || !transfer_pc) {
      mode = (Mode)m_state.cpsr.mode;
      SwitchMode(Mode::User);
    }
  }

  int i = 0;
  u32 remaining = list;

  while(remaining != 0) {
    #if defined(__has_builtin) && __has_builtin(__builtin_ctz)
      i = __builtin_ctz(remaining);
    #else
      while((remaining & (1 << i)) == 0) i++;
    #endif

    if constexpr(pre == add) {
      address += 4;
    }

    if constexpr(load) {
      m_state.reg[i] = ReadWord(address);
    } else {
      WriteWord(address, m_state.reg[i]);
    }

    if constexpr(pre != add) {
      address += 4;
    }

    remaining &= ~(1 << i);
  }

  if constexpr(user_mode) {
    if(load && transfer_pc) {
      auto& spsr = *m_spsr;

      SwitchMode((Mode)spsr.mode);
      m_state.cpsr = spsr;
    } else {
      SwitchMode(mode);
    }
  }

  if constexpr(writeback) {
    if constexpr(load) {
      switch(m_model) {
        case Model::ARM9:
        case Model::ARM11: // @todo: research ARM11MPCore behaviour
          // LDM ARMv5: writeback if base is the only register or not the last register.
          if(!base_is_last || list == (1 << base))
            m_state.reg[base] = base_new;
          break;
        case Model::ARM7:
          // LDM ARMv4: writeback if base in not in the register list.
          if(!(list & (1 << base)))
            m_state.reg[base] = base_new;
          break;
      }
    } else {
      m_state.reg[base] = base_new;
    }
  }

  if constexpr(load) {
    if(transfer_pc) {
      if((m_state.r15 & 1) && !user_mode && m_model != Model::ARM7) {
        m_state.cpsr.thumb = 1;
        m_state.r15 &= ~1;
      }

      if(m_state.cpsr.thumb) {
        ReloadPipeline16();
      } else {
        ReloadPipeline32();
      }
    }
  }
}

void ARM_SWI(u32 instruction) {
  // Save current program status register.
  m_state.spsr[(int)Bank::Supervisor] = m_state.cpsr;

  // Enter SVC mode and disable IRQs.
  SwitchMode(Mode::Supervisor);
  m_state.cpsr.mask_irq = 1;

  // Save current program counter and jump to SVC exception vector.
  m_state.r14 = m_state.r15 - 4;
  m_state.r15 = m_exception_base + 0x08;
  ReloadPipeline32();
}

void ARM_CountLeadingZeros(u32 instruction) {
  if(m_model == Model::ARM7) {
    ARM_Undefined(instruction);
    return;
  }

  int dst = (instruction >> 12) & 0xF;
  int src =  instruction & 0xF;

  u32 value = m_state.reg[src];

  if(value == 0) {
    m_state.reg[dst] = 32;
    m_state.r15 += 4;
    return;
  }

  #if defined(__has_builtin) && __has_builtin(__builtin_clz)
    m_state.reg[dst] = __builtin_clz(value);
  #else
    u32 result = 0;

    const u32 mask[] = {
      0xFFFF0000,
      0xFF000000,
      0xF0000000,
      0xC0000000,
      0x80000000 };
    const int shift[] = { 16, 8, 4, 2, 1 };

    for(int i = 0; i < 5; i++) {
      if((value & mask[i]) == 0) {
        result |= shift[i];
        value <<= shift[i];
      }
    }

    m_state.reg[dst] = result;
  #endif

  m_state.r15 += 4;
}

template <int opcode>
void ARM_SaturatingAddSubtract(u32 instruction) {
  if(m_model == Model::ARM7) {
    ARM_Undefined(instruction);
    return;
  }

  int src1 =  instruction & 0xF;
  int src2 = (instruction >> 16) & 0xF;
  int dst  = (instruction >> 12) & 0xF;
  u32 op2  = m_state.reg[src2];

  if constexpr((opcode & 0b1001) != 0) {
    ARM_Undefined(instruction);
    return;
  }

  bool subtract = opcode & 2;
  bool double_op2 = opcode & 4;

  if(double_op2) {
    u32 result = op2 + op2;

    if((op2 ^ result) >> 31) {
      m_state.cpsr.q = 1;
      result = 0x80000000 - (result >> 31);
    }

    op2 = result;
  }

  if(subtract) {
    m_state.reg[dst] = QSUB(m_state.reg[src1], op2);
  } else {
    m_state.reg[dst] = QADD(m_state.reg[src1], op2);
  }

  m_state.r15 += 4;
}

void ARM_CoprocessorRegisterTransfer(u32 instruction) {
  int dst = (instruction >> 12) & 0xF;
  int cp_rm = instruction & 0xF;
  int cp_rn = (instruction >> 16) & 0xF;
  int opcode1 = (instruction >> 21) & 7;
  int opcode2 = (instruction >>  5) & 7;
  int cp_num  = (instruction >>  8) & 0xF;

  auto coprocessor = m_coprocessors[cp_num];

  if(coprocessor == nullptr) {
    ARM_Undefined(instruction);
    return;
  }

  if(instruction & (1 << 20)) {
    m_state.reg[dst] = coprocessor->MRC(opcode1, cp_rn, cp_rm, opcode2);
  } else {
    coprocessor->MCR(opcode1, cp_rn, cp_rm, opcode2, m_state.reg[dst]);
  }

  m_state.r15 += 4;
}

void ARM_Hint(u32 instruction) {
  const u8 type = instruction & 0xFFu;

  switch(type) {
    case 0u: {
      // NOP
      break;
    }
    case 3u: {
      // WFI
      SetWaitingForIRQ(true);
      break;
    }
    default: {
      ATOM_PANIC("unhandled ARM11 hint instruction: 0x{:08X} (PC = 0x{:08X})", instruction, m_state.r15);
    }
  }

  m_state.r15 += 4;
}

void ARM_Undefined(u32 instruction) {
  ATOM_PANIC("undefined ARM instruction: 0x{:08X} (PC = 0x{:08X})", instruction, m_state.r15);

  /*// Save current program status register.
  m_state.spsr[(int)Bank::Undefined] = m_state.cpsr;

  // Enter UND mode and disable IRQs.
  SwitchMode(Mode::Undefined);
  m_state.cpsr.mask_irq = 1;

  // Save current program counter and jump to UND exception vector.
  m_state.r14 = m_state.r15 - 4;
  m_state.r15 = m_exception_base + 0x04;
  ReloadPipeline32();*/
}

void ARM_Unimplemented(u32 instruction) {
  ATOM_PANIC("unimplemented ARM instruction: 0x{:08X} (PC = 0x{:08X})", instruction, m_state.r15);
}
