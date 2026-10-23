/* A plain DLL with no wolf_mod_init. Built as deps_helper.dll, which the deps mod imports from its own directory, and as the mod noexport, which WOLF
 * refuses. */
__declspec( dllexport ) int helper_value( void )
{
    return 42;
}
