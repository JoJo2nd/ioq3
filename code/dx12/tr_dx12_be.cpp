extern "C" {
#include "tr_local.h"
#include "../sdl/sdl_icon.h"

#include "SDL.h"
#include "SDL_syswm.h"
}

#include <SDKDDKVer.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const size_t dx12FrameBufferCount = 2;

struct dx12_context {
  SDL_Window* sdlWindow;
  SDL_SysWMinfo sdlSysWMinfo;
  uint32_t vidWidth, vidHeight;
  uint32_t DXGIFactoryFlags;
  ID3D12Debug* debugController;
  IDXGIFactory5* IDXGIFactory5;
  IDXGIAdapter1* IAdapter1;
  ID3D12Device4* ID3D12Device4;
  ID3D12CommandQueue* ICMDQueue;
  IDXGISwapChain1* ISwapChain1;
  IDXGISwapChain3* ISwapChain3;
  ID3D12DescriptorHeap* IRTVHeap;
  ID3D12Resource* IARenderTargets[dx12FrameBufferCount];
  ID3D12RootSignature* IRootSignature;
  ID3D12PipelineState* IPipelineState;
  ID3D12CommandAllocator* ICMDAlloc;
  ID3D12GraphicsCommandList* ICMDList;
  ID3D12Resource* IVertexBuffer;
  ID3D12Fence* IFence;
};

static dx12_context_t dx12ctx;

static char const* QDX12_GetError(HRESULT hr) {
  switch(hr) {
  case D3D12_ERROR_ADAPTER_NOT_FOUND: return "The specified cached PSO was created on a different adapterand cannot be reused on the current adapter.";
  case D3D12_ERROR_DRIVER_VERSION_MISMATCH:	return "The specified cached PSO was created on a different driver versionand cannot be reused on the current adapter.";
  case DXGI_ERROR_INVALID_CALL:	return "The method call is invalid.For example, a method's parameter may not be a valid pointer.";
  case DXGI_ERROR_WAS_STILL_DRAWING: return "The previous blit operation that is transferring information to or from this surface is incomplete.";
  case E_FAIL: return "Attempted to create a device with the debug layer enabled and the layer is not installed.";
  case E_INVALIDARG: return "An invalid parameter was passed to the returning function.";
  case E_OUTOFMEMORY: return "Direct3D could not allocate sufficient memory to complete the call.";
  case E_NOTIMPL: return "The method call isn't implemented with the passed parameter combination.";
  case S_FALSE: return "Alternate success value, indicating a successful but nonstandard completion(the precise meaning depends on context).";
  case S_OK: return "OK";
  default: return "Unknown error";
  }
}

extern "C" dx12_context_t* QDX12_CreateWindow() {
  SDL_Surface* icon = NULL;
  uint32_t sdl_flags = SDL_WINDOW_SHOWN;
  char const* driver_name = NULL;
  HRESULT hr;

  if (!SDL_WasInit(SDL_INIT_VIDEO)) {
    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
      ri.Printf(PRINT_ALL, "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
      return NULL;
    }
  }

  driver_name = SDL_GetCurrentVideoDriver();
  ri.Printf(PRINT_ALL, "SDL using driver \"%s\"\n", driver_name);

  icon = SDL_CreateRGBSurfaceFrom(
    (void*)CLIENT_WINDOW_ICON.pixel_data,
    CLIENT_WINDOW_ICON.width,
    CLIENT_WINDOW_ICON.height,
    CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
    CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
    0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
    0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
  );

  dx12ctx.vidWidth = 1920;
  dx12ctx.vidHeight = 1080;

  if ((dx12ctx.sdlWindow = SDL_CreateWindow(CLIENT_WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    dx12ctx.vidWidth, dx12ctx.vidHeight, sdl_flags)) == NULL)
  {
    ri.Printf(PRINT_DEVELOPER, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return NULL;
  }

  SDL_SetWindowIcon(dx12ctx.sdlWindow, icon);

  if (SDL_GetWindowWMInfo(dx12ctx.sdlWindow, &dx12ctx.sdlSysWMinfo) == SDL_FALSE) {
    ri.Printf(PRINT_ALL, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
    return NULL;
  }

  if (FAILED(hr = D3D12GetDebugInterface(IID_PPV_ARGS(&dx12ctx.debugController)))) {
    ri.Printf(PRINT_ALL, "D3D12GetDebugInterface failed: %s\n", QDX12_GetError(hr));
    return NULL;
  }
  dx12ctx.debugController->EnableDebugLayer();
  dx12ctx.DXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;

  if (FAILED(hr = CreateDXGIFactory2(dx12ctx.DXGIFactoryFlags, IID_PPV_ARGS(&dx12ctx.IDXGIFactory5)))) {
    ri.Printf(PRINT_ALL, "CreateDXGIFactory2 failed: %s\n", QDX12_GetError(hr));
    return NULL;
  }
  dx12ctx.IDXGIFactory5->MakeWindowAssociation(dx12ctx.sdlSysWMinfo.info.win.window, DXGI_MWA_NO_ALT_ENTER);

  DXGI_ADAPTER_DESC1 adapterDesc = {};
  for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != dx12ctx.IDXGIFactory5->EnumAdapters1(adapterIndex, &dx12ctx.IAdapter1); ++adapterIndex)
  {
    dx12ctx.IAdapter1->GetDesc1(&adapterDesc);

    if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;

    // Passing NULL tests that device creation would work.
    if (SUCCEEDED(D3D12CreateDevice(dx12ctx.IAdapter1, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), NULL)))
      break;
  }
  if (FAILED(hr = D3D12CreateDevice(dx12ctx.IAdapter1, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&dx12ctx.ID3D12Device4)))) {
    ri.Printf(PRINT_ALL, "D3D12CreateDevice failed: %s\n", QDX12_GetError(hr));
    return NULL;
  }

  return &dx12ctx;
}
