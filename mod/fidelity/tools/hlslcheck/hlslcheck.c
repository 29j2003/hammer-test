/* Compile an HLSL file with D3DX9 (d3dx9_33.dll, same compiler the Source SDK uses for SM3).
   usage: hlslcheck.exe file.fxc profile [NAME=VALUE ...]  */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { const char *Name; const char *Definition; } MACRO;
typedef struct ID3DXBuffer ID3DXBuffer;
typedef struct { void *QI; ULONG (__stdcall *AddRef)(void*); ULONG (__stdcall *Release)(void*);
  void* (__stdcall *GetBufferPointer)(void*); DWORD (__stdcall *GetBufferSize)(void*); } BufVtbl;
typedef HRESULT (WINAPI *CompileFn)(const char*, const MACRO*, void*, const char*, const char*, DWORD, void**, void**, void**);
int main(int argc, char **argv) {
  if (argc < 3) { printf("usage\n"); return 2; }
  HMODULE m = LoadLibraryA("d3dx9_33.dll");
  if (!m) { printf("cannot load d3dx9_33.dll\n"); return 2; }
  CompileFn f = (CompileFn)GetProcAddress(m, "D3DXCompileShaderFromFileA");
  MACRO macros[64]; int n = 0; static char buf[64][256];
  for (int i = 3; i < argc && n < 62; i++) {
    strncpy(buf[n], argv[i], 255); char *eq = strchr(buf[n], '=');
    if (eq) { *eq = 0; macros[n].Definition = eq + 1; } else macros[n].Definition = "1";
    macros[n].Name = buf[n]; n++; }
  macros[n].Name = NULL; macros[n].Definition = NULL;
  void *code = NULL, *err = NULL;
  HRESULT hr = f(argv[1], macros, NULL, "main", argv[2], 0, &code, &err, NULL);
  if (err) { BufVtbl *v = *(BufVtbl**)err; printf("%s\n", (char*)v->GetBufferPointer(err)); }
  if (FAILED(hr)) { printf("FAILED hr=0x%08lx\n", hr); return 1; }
  BufVtbl *v = *(BufVtbl**)code; printf("OK %s, %lu bytes\n", argv[2], v->GetBufferSize(code));
  { const char *o = getenv("HLSL_OUT"); if (o) { FILE *fp = fopen(o, "wb"); fwrite(v->GetBufferPointer(code), 1, v->GetBufferSize(code), fp); fclose(fp); } }
  return 0;
}
