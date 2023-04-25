
#include <atom/logger/logger.hpp>
#include <fstream>

#include "application.hpp"

Application::Application() {
  SDL_Init(SDL_INIT_VIDEO);

  m_nds = std::make_unique<dual::nds::NDS>();
}

Application::~Application() {
  for(auto& texture : m_textures) SDL_DestroyTexture(texture);

  SDL_DestroyRenderer(m_renderer);
  SDL_DestroyWindow(m_window);
  SDL_Quit();
}

int Application::Run(int argc, char **argv) {
  CreateWindow();
  if(argc < 2) {
    LoadROM("pmdblue.nds");
  } else {
    LoadROM(argv[1]);
  }
  LoadBootROM("boot9.bin", true);
  LoadBootROM("boot7.bin", false);
  MainLoop();
  return 0;
}

void Application::CreateWindow() {
  m_window = SDL_CreateWindow(
    "ndsemu",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    512,
    768,
    SDL_WINDOW_ALLOW_HIGHDPI
  );

  m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  for(auto& texture : m_textures) {
    texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
  }
}

void Application::LoadROM(const char* path) {
  u8* data;
  size_t size;
  std::ifstream file{path, std::ios::binary};

  if(!file.good()) {
    ATOM_PANIC("Failed to open NDS file: '{}'", path);
  }

  file.seekg(0, std::ios::end);
  size = file.tellg();
  file.seekg(0);

  data = new u8[size];
  file.read((char*)data, static_cast<std::streamsize>(size));

  if(!file.good()) {
    ATOM_PANIC("Failed to read NDS file: '{}'", path);
  }

  m_nds->LoadROM(std::make_shared<dual::nds::MemoryROM>(data, size));
  m_nds->DirectBoot();
}

void Application::LoadBootROM(const char* path, bool arm9) {
  const size_t maximum_size = arm9 ? 0x8000 : 0x4000;

  std::ifstream file{path, std::ios::binary};

  if(!file.good()) {
    ATOM_PANIC("Failed to open boot ROM: '{}'", path);
  }

  size_t size;

  file.seekg(0, std::ios::end);
  size = file.tellg();
  file.seekg(0);

  if(size > maximum_size) {
    ATOM_PANIC("Boot ROM is too big, expected {} bytes but got {} bytes", maximum_size, size);
  }

  std::array<u8, 0x8000> boot_rom{};

  file.read((char*)boot_rom.data(), static_cast<std::streamsize>(size));

  if(!file.good()) {
    ATOM_PANIC("Failed to read Boot ROM: '{}'", path);
  }

  if(arm9) {
    m_nds->LoadBootROM9(boot_rom);
  } else {
    m_nds->LoadBootROM7(std::span<u8, 0x4000>{boot_rom.data(), 0x4000});
  }
}

void Application::MainLoop() {
  static const SDL_Rect rects[2] {
    {0,   0, 512, 384},
    {0, 384, 512, 384}
  };

  SDL_Event event;

  dual::nds::PPU& ppu_a = m_nds->GetVideoUnit().GetPPU(0);
  dual::nds::PPU& ppu_b = m_nds->GetVideoUnit().GetPPU(1);

  while(true) {
    while(SDL_PollEvent(&event)) {
      if(event.type == SDL_QUIT) {
        return;
      }
    }

    m_nds->Step(559241);

    SDL_UpdateTexture(m_textures[0], nullptr, ppu_a.GetOutput(), 256 * sizeof(u32));
    SDL_UpdateTexture(m_textures[1], nullptr, ppu_b.GetOutput(), 256 * sizeof(u32));

    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_textures[0], nullptr, &rects[0]);
    SDL_RenderCopy(m_renderer, m_textures[1], nullptr, &rects[1]);
    SDL_RenderPresent(m_renderer);
  }
}
