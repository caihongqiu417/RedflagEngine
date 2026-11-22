#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <stdexcept>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using Microsoft::WRL::ComPtr;
static ComPtr<ID3D11Device> s_Device;
static ComPtr<ID3D11DeviceContext> s_Context;

static void CheckHR(HRESULT status)
{
    if (FAILED(status))
        throw std::runtime_error("D3D11 panel capture failed");
}

void BeginPanelCapture()
{
    CheckHR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &s_Device,
                              nullptr, &s_Context));
    ImGui_ImplDX11_Init(s_Device.Get(), s_Context.Get());
    ImGui_ImplDX11_NewFrame();
}

void SavePanelCapture(const std::filesystem::path& path)
{
    ImVec2 size = ImGui::GetIO().DisplaySize;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = UINT(size.x);
    description.Height = UINT(size.y);
    description.ArraySize = description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color, copy;
    ComPtr<ID3D11RenderTargetView> view;
    CheckHR(s_Device->CreateTexture2D(&description, nullptr, &color));
    CheckHR(s_Device->CreateRenderTargetView(color.Get(), nullptr, &view));
    const float clear[4] = {0.12f, 0.12f, 0.12f, 1};
    s_Context->ClearRenderTargetView(view.Get(), clear);
    s_Context->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CheckHR(s_Device->CreateTexture2D(&description, nullptr, &copy));
    s_Context->CopyResource(copy.Get(), color.Get());
    D3D11_MAPPED_SUBRESOURCE bytes{};
    CheckHR(s_Context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &bytes));
    int ok =
        stbi_write_png(path.string().c_str(), description.Width, description.Height, 4, bytes.pData, bytes.RowPitch);
    s_Context->Unmap(copy.Get(), 0);
    ImGui_ImplDX11_Shutdown();
    if (!ok)
        throw std::runtime_error("Cannot save panel capture");
}
