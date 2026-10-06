# CMake before CMP0207 normalization can return C:\Windows\system32/foo.dll.
# Match both separators at every boundary. Only the OS directory is excluded;
# SDK/application DLLs and unresolved DLL names must still be validated.
set(AGENTCAD_WINDOWS_SYSTEM_DLL_REGEX
  [=[^[A-Za-z]:[/\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\]]=])
