
#pragma once

#include <dual/arm/cpu.hpp>
#include <dual/common/scheduler.hpp>
#include <dual/nds/arm7/memory.hpp>
#include <dual/nds/arm9/cp15.hpp>
#include <dual/nds/arm9/memory.hpp>
#include <dual/nds/ipc.hpp>
#include <dual/nds/irq.hpp>
#include <dual/nds/rom.hpp>
#include <dual/nds/system_memory.hpp>
#include <memory>
#include <span>

namespace dual::nds {

  class NDS {
    public:
      NDS();

      void Reset();
      void Step(int cycles_to_run);
      void LoadBootROM9(std::span<u8, 0x8000> data);
      void LoadBootROM7(std::span<u8, 0x4000> data);
      void LoadROM(std::shared_ptr<ROM> rom);
      void DirectBoot();

      SystemMemory& GetSystemMemory() {
        return m_memory;
      }

    private:
      Scheduler m_scheduler{};

      SystemMemory m_memory{};

      struct ARM9 {
        std::unique_ptr<arm::CPU> cpu{};
        std::unique_ptr<arm9::CP15> cp15{};
        arm9::MemoryBus bus;
        IRQ irq{true};

        ARM9(SystemMemory& memory, IPC& ipc) : bus{memory, {irq, ipc}} {}
      } m_arm9{m_memory, m_ipc};

      struct ARM7 {
        std::unique_ptr<arm::CPU> cpu{};
        arm7::MemoryBus bus;
        IRQ irq{false};

        ARM7(SystemMemory& memory, IPC& ipc) : bus{memory, {irq, ipc}} {}
      } m_arm7{m_memory, m_ipc};

      IPC m_ipc{m_arm9.irq, m_arm7.irq};

      std::shared_ptr<ROM> m_rom;

      u64 m_step_target{};
  };

} // namespace dual::nds
