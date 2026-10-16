/* Stand-in for the game's main.dll. Its import of DirectInput8Create is what
 * makes the loader map dinput8.dll while this module is being loaded. */
#include <windows.h>

typedef HRESULT(WINAPI *CreateFn)(HINSTANCE instance, DWORD version, const IID *iid, void **out, IUnknown *outer);

__declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, const IID *iid, void **out, IUnknown *outer);

/* The address the loader bound the dinput8 import to. */
__declspec(dllexport) CreateFn FakeMain_DirectInput8Create(void)
{
    return &DirectInput8Create;
}
