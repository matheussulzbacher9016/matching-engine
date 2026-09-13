# Matching Engine

Matching engine de um ativo em C++17, com estado em memória e construção incremental.
Nesta primeira etapa, o executável e o teste de fumaça validam apenas a infraestrutura.

## Compilar e testar

Requisitos: compilador GCC compatível com C++17, CMake >= 3.20 e Ninja.
No PowerShell, na raiz do repositório, execute uma linha por vez e pare em qualquer falha:

```powershell
cmake -S . -B build-vscode -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build-vscode
ctest --test-dir build-vscode --output-on-failure --no-tests=error
```

Para executar no Windows:

```powershell
.\build-vscode\matching.exe
```