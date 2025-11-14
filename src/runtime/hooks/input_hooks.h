#pragma once

#include <cstdint>

namespace wolf::runtime::hooks
{

bool setupInputHooks();
void cleanupInputHooks();

// Called from gui.cpp to query/update mouse release state
bool isMouseReleased();
void setMouseReleased(bool released);

// Called from dinput8.cpp when DirectInput8Create succeeds
void hookDirectInput8InterfaceFromProxy(void *pDI);

} // namespace wolf::runtime::hooks
