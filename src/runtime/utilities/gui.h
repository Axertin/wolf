#pragma once

// Forward declarations
struct ID3D11Device;
struct ID3D11DeviceContext;

void guiInitHooks();
void guiCleanup();

// D3D11 device access for mod backend initialization
ID3D11Device *guiGetD3D11Device();
ID3D11DeviceContext *guiGetD3D11DeviceContext();

// Render draw data using Wolf's ImGui backend
void guiRenderDrawData(void *drawData);
