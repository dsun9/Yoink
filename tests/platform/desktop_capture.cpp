#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<IDXGIOutputDuplication> duplication;
static RECT screen;
static ComPtr<ID3D11Texture2D> staging;

/* Diagnostic only. Desktop Duplication sees transparent GL composition;
 * GDI screen copies can return the underlying window instead. */
extern "C" bool pp_test_desktop_capture_start(RECT rectangle) {
  ComPtr<IDXGIFactory1> factory;
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
  HMONITOR monitor=MonitorFromRect(&rectangle,MONITOR_DEFAULTTONEAREST);
  for(UINT a=0;;a++) {
    ComPtr<IDXGIAdapter1> adapter;
    HRESULT enumeration=factory->EnumAdapters1(a,&adapter);
    if(enumeration==DXGI_ERROR_NOT_FOUND)break;
    if(FAILED(enumeration))return false;
    for(UINT o=0;;o++) {
  ComPtr<IDXGIOutput> output;
  enumeration=adapter->EnumOutputs(o,&output);
  if(enumeration==DXGI_ERROR_NOT_FOUND)break;
  if(FAILED(enumeration))return false;
  DXGI_OUTPUT_DESC desc{};if(FAILED(output->GetDesc(&desc))||desc.Monitor!=monitor)continue;
  if(desc.Rotation!=DXGI_MODE_ROTATION_IDENTITY)return false;
  screen=desc.DesktopCoordinates;
  if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return false;
  ComPtr<IDXGIOutput5> output5;DXGI_FORMAT format=DXGI_FORMAT_B8G8R8A8_UNORM;
  return SUCCEEDED(output.As(&output5))&&SUCCEEDED(output5->DuplicateOutput1(device.Get(),0,1,&format,&duplication));
    }
  }
  return false;
}
extern "C" unsigned char *pp_test_desktop_capture(RECT rectangle,bool require_new) {
  if(!duplication||rectangle.left<screen.left||rectangle.top<screen.top||rectangle.right>screen.right||rectangle.bottom>screen.bottom){std::fprintf(stderr,"Capture out of bounds rect=%ld,%ld,%ld,%ld screen=%ld,%ld,%ld,%ld\n",rectangle.left,rectangle.top,rectangle.right,rectangle.bottom,screen.left,screen.top,screen.right,screen.bottom);return nullptr;}
  ComPtr<IDXGIResource> resource;DXGI_OUTDUPL_FRAME_INFO info{};
  HRESULT result=duplication->AcquireNextFrame(250,&info,&resource);
  bool acquired=SUCCEEDED(result);
  if(result==DXGI_ERROR_WAIT_TIMEOUT&&(require_new||!staging)){std::fprintf(stderr,"Capture timed out waiting for a fresh desktop frame\n");return nullptr;}
  if(!acquired&&result!=DXGI_ERROR_WAIT_TIMEOUT){std::fprintf(stderr,"Desktop Duplication acquire failed: %08lx\n",result);return nullptr;}
  bool ok=acquired||staging!=nullptr;
  if(acquired) {
    ComPtr<ID3D11Texture2D> texture;ok=SUCCEEDED(resource.As(&texture));D3D11_TEXTURE2D_DESC td{};
    if(ok&&!staging) {
      texture->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;
      HRESULT allocation=device->CreateTexture2D(&td,nullptr,&staging);
      ok=td.Format==DXGI_FORMAT_B8G8R8A8_UNORM&&SUCCEEDED(allocation);
      if(!ok)std::fprintf(stderr,"Capture texture format=%u size=%ux%u allocation=%08lx\n",td.Format,td.Width,td.Height,allocation);
    }
    if(ok)context->CopyResource(staging.Get(),texture.Get());
  }
  unsigned char *pixels=nullptr;
  if(ok) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) {
      size_t stride=(rectangle.right-rectangle.left)*4;
      pixels=static_cast<unsigned char *>(std::malloc(stride*(rectangle.bottom-rectangle.top)));
      if(pixels)for(LONG y=rectangle.top;y<rectangle.bottom;y++)
        std::memcpy(pixels+(y-rectangle.top)*stride,static_cast<unsigned char *>(mapped.pData)+(y-screen.top)*mapped.RowPitch+(rectangle.left-screen.left)*4,stride);
      context->Unmap(staging.Get(),0);
    }
  }
  if(acquired)duplication->ReleaseFrame();return pixels;
}
