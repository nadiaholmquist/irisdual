
#pragma once

#include <atom/logger/logger.hpp>
#include <atom/integer.hpp>
#include <atom/panic.hpp>
#include <dual/common/fifo.hpp>
#include <dual/common/scheduler.hpp>
#include <dual/nds/video_unit/gpu/registers.hpp>
#include <dual/nds/irq.hpp>

namespace dual::nds::gpu {

  class CommandProcessor {
    public:
      explicit CommandProcessor(
        Scheduler& scheduler,
        IRQ& arm9_irq,
        GXSTAT& gxstat
      )   : m_scheduler{scheduler}
          , m_arm9_irq{arm9_irq}
          , m_gxstat{gxstat} {
      }

      void Reset() {
        m_unpack = {};
        m_cmd_pipe.Reset();
        m_cmd_fifo.Reset();
      }

      void Write_GXFIFO(u32 word) {
        if(m_unpack.params_left > 0) {
          EnqueueFIFO((u8)m_unpack.word, word);

          if(--m_unpack.params_left == 0) {
            m_unpack.word >>= 8;
            m_unpack.cmds_left--;
          }
          return;
        }

        if(m_unpack.cmds_left == 0) {
          m_unpack.cmds_left = 4;
          m_unpack.word = word;
        }

        for(int i = 0; i < 4; i++) {
          const u8 command = (u8)m_unpack.word;

          m_unpack.params_left = k_cmd_num_params[command];

          if(m_unpack.params_left != 0) {
            break;
          }

          EnqueueFIFO(command, 0u);
          m_unpack.word >>= 8;
          m_unpack.cmds_left--;

          if(m_unpack.cmds_left == 0 || m_unpack.word == 0u) {
            m_unpack.cmds_left = 0;
            break;
          }
        }
      }

      void Write_GXCMDPORT(u32 address, u32 param) {
        EnqueueFIFO((address & 0x1FFu) >> 2, param);
      }

    private:
      static constexpr int k_cmd_num_params[256] {
        0, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, // 0x00 - 0x0F (all NOPs)
        1, 0, 1, 1,  1, 0, 16, 12, 16, 12, 9, 3, 3, 0, 0, 0, // 0x10 - 0x1F (Matrix engine)
        1, 1, 1, 2,  1, 1,  1,  1,  1,  1, 1, 1, 0, 0, 0, 0, // 0x20 - 0x2F (Vertex and polygon attributes, mostly)
        1, 1, 1, 1, 32, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, // 0x30 - 0x3F (Material / lighting properties)
        1, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, // 0x40 - 0x4F (Begin/end vertex)
        1, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, // 0x50 - 0x5F (Swap buffers)
        1, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, // 0x60 - 0x6F (Set viewport)
        3, 2, 1                                              // 0x70 - 0x7F (Box, position and vector test)
      };

      void EnqueueFIFO(u8 command, u32 param) {
        const u64 entry = (u64)command << 32 | param;

        if(m_cmd_fifo.IsEmpty() && !m_cmd_pipe.IsFull()) {
          m_cmd_pipe.Write(entry);
        } else {
          if(m_cmd_fifo.IsFull()) {
            ATOM_PANIC("gpu: Attempted to write to full GXFIFO, busy={}", m_gxstat.busy);
          }

          m_cmd_fifo.Write(entry);
          UpdateFifoState();
        }

        if(!m_gxstat.busy) {
          ProcessCommands();
        }
      }

      u64  DequeueFIFO() {
        if(m_cmd_pipe.IsEmpty()) {
          ATOM_PANIC("gpu: bad dequeue from empty GXPIPE");
        }

        const u64 entry = m_cmd_pipe.Read();

        if(m_cmd_pipe.Count() <= 2) {
          for(int i = 0; i < 2; i++) {
            if(m_cmd_fifo.IsEmpty()) break;

            m_cmd_pipe.Write(m_cmd_fifo.Read());
          }

          UpdateFifoState();
        }

        return entry;
      }

      void UpdateFifoState() {
        m_gxstat.cmd_fifo_size = m_cmd_fifo.Count();
        m_gxstat.cmd_fifo_empty = m_cmd_fifo.IsEmpty();
        m_gxstat.cmd_fifo_less_than_half_full = m_cmd_fifo.Count() < 128;

        if(EvaluateFIFOIRQCondition()) {
          // @todo: according to GBATEK the GXFIFO IRQ is level-sensitive.
          m_arm9_irq.Raise(IRQ::Source::GXFIFO);
        }
      }

      [[nodiscard]] bool EvaluateFIFOIRQCondition() const {
        switch((GXSTAT::IRQ)m_gxstat.cmd_fifo_irq) {
          case GXSTAT::IRQ::Empty: return m_cmd_fifo.IsEmpty();
          case GXSTAT::IRQ::LessThanHalfFull: return m_cmd_fifo.Count() < 128;
          default: return false;
        }
      }

      void ProcessCommands() {
        if(m_cmd_pipe.IsEmpty()) {
          m_gxstat.busy = false;
          return;
        }

        const u8 command = (u8)(m_cmd_pipe.Peek() >> 32);
        const size_t number_of_entries = m_cmd_pipe.Count() + m_cmd_fifo.Count();

        if(number_of_entries < k_cmd_num_params[command]) {
          m_gxstat.busy = false;
          return;
        }

        m_gxstat.busy = true;

        m_scheduler.Add(1, [this, command](int _) {
          ExecuteCommand(command);
          ProcessCommands();
        });
      }

      void ExecuteCommand(u8 command) {
        if(k_cmd_num_params[command] == 0) {
          DequeueFIFO();
        }

        for(int i = 0; i < k_cmd_num_params[command]; i++) {
          DequeueFIFO();
        }
      }

      Scheduler& m_scheduler;
      IRQ& m_arm9_irq;
      GXSTAT& m_gxstat;

      struct Unpack {
        u32 word = 0u;
        int cmds_left = 0u;
        int params_left = 0u;
      } m_unpack;

      FIFO<u64, 4>   m_cmd_pipe;
      FIFO<u64, 256> m_cmd_fifo;
  };

} // namespace dual::nds::gpu
