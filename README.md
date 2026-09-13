# Matching Engine

Matching engine de um único ativo em C++17. Implementa `limit`, `market`, `peg bid`, `peg offer`, cancelamento, alteração e visualização individual das ordens. Estado em memória, execução sequencial e somente biblioteca padrão do C++.

## Compilar, testar e executar

Requisitos: compilador GCC compatível com C++17, CMake >= 3.20 e Ninja. No Windows, o toolchain usado pelo roteiro é MinGW-w64 UCRT64. As ferramentas e DLLs de execução devem estar acessíveis pelo `PATH`.

No PowerShell, na raiz do repositório, execute uma linha por vez; pare se qualquer comando falhar:

```powershell
cmake -S . -B build-vscode -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build-vscode
ctest --test-dir build-vscode --output-on-failure --no-tests=error
.\build-vscode\matching.exe
```

O programa aguarda comandos, sem imprimir prompt. Digite `help` para ajuda e `exit` para sair. EOF também encerra a sessão. Não digite os símbolos `>>>` do enunciado.

Em Linux com GCC e Ninja, os três comandos de build/teste são os mesmos; execute `./build-vscode/matching`. A configuração fornecida usa flags de GCC/Clang, não é uma configuração para todos os compiladores.

Para uma compilação Release separada:

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build-release
ctest --test-dir build-release --output-on-failure --no-tests=error
```

Os testes usam `CHECK`, que lança exceções, e continuam ativos em Release.

## Comandos

| Comando | Significado |
|---|---|
| `limit buy 10 100` | Compra limite de 100 unidades a 10. |
| `limit sell 10.5 100` | Venda limite de 100 unidades a 10,50. |
| `market buy 150` | Compra imediatamente até 150 unidades. |
| `market sell 150` | Vende imediatamente até 150 unidades. |
| `peg bid buy 150` | Compra que acompanha a melhor limit de compra. |
| `peg offer sell 150` | Venda que acompanha a melhor limit de venda. |
| `peg offer buy 150` | Compra que acompanha a melhor limit de venda; pode executar imediatamente. |
| `peg bid sell 150` | Venda que acompanha a melhor limit de compra; pode executar imediatamente. |
| `cancel order 4` | Cancela o saldo da ordem 4. |
| `amend order 4 9.98 -` | Altera somente o preço de uma limit. |
| `amend order 4 - 50` | Define o novo saldo restante como 50. |
| `amend order 4 9.98 50` | Altera preço e saldo restante. |
| `print book` | Exibe as ordens ativas por prioridade e lista as pegs suspensas. |
| `help` / `exit` | Ajuda / encerramento. |

Use os IDs realmente retornados. Eles começam em 1 a cada execução; markets também consomem IDs, apesar de nunca permanecerem no livro. A alteração preserva o ID. Uma ordem totalmente executada ou cancelada deixa de existir e não pode ser alterada/cancelada novamente.

Preço usa ponto decimal, no máximo duas casas, sem notação científica. Quantidade é um inteiro positivo. `-` em `amend` significa manter aquele campo, não um número negativo. `amend` com ambos os campos `-` é rejeitado.

`Order created` confirma aceitação, não permanência: uma limit agressiva pode ser totalmente executada no mesmo comando. `Order cancelled` confirma cancelamento. Erros de domínio são enviados a stderr sem interromper a sessão. Encerramento normal retorna 0; falha de leitura, 1; exceção fatal, 2. Uma sessão pode retornar 0 mesmo tendo rejeitado comandos individuais, pois são erros recuperáveis.

## Políticas do domínio

Estas são escolhas explícitas para completar as ambiguidades do exercício, não regras universais de exchanges.

| Situação | Política |
|---|---|
| Ativo | Um único ativo implícito. |
| Preço e quantidade | Inteiros de 64 bits, preço em centavos e unidades indivisíveis. |
| Prioridade | Melhor preço; no mesmo preço, menor sequência de prioridade. |
| Limit agressiva | Executa; sobra permanece no preço limite. |
| Market sem liquidez suficiente | Executa o disponível e descarta a sobra. |
| Preço do trade | Preço da cotação ativa mais antiga entre as duas ordens selecionadas. |
| Alteração de preço | Perde prioridade e pode cruzar o livro. |
| Aumento do saldo | Perde prioridade, mesmo sem mudar o preço. |
| Redução isolada do saldo | Mantém prioridade. |
| Alteração para os mesmos valores | Mantém prioridade. |
| Quantidade de `amend` | Novo saldo restante, não total original nem incremento. |
| Referência de peg | Melhor preço entre limits vivas do lado bid/offer. Pegs não são âncoras. |
| Referência ausente | Peg suspensa, mantendo ID, saldo e sequência; fora da fila executável. |
| Referência reaparece | Reativação automática. |
| Reprecificação automática | Conserva a sequência FIFO, conforme a prioridade ilustrada no enunciado. |
| Alteração manual de peg | Aceita quantidade; preço explícito é rejeitado porque é automático. |
| Atualização das referências | Antes de resolver cruzamentos e após cada fill, cancelamento e alteração. |

**Por que preencher limits agressivas:** o enunciado permite essa escolha. Ela aproveita o mesmo mecanismo de matching e evita deixar o livro cruzado ao final do comando.

**Por que preço inteiro:** converter diretamente o texto para centavos permite comparações exatas, sem arredondamento binário. O parser verifica limites antes de multiplicar/somar. Não calculamos preço × quantidade.

**Por que excluir pegs da referência:** uma peg sozinha não deve sustentar seu próprio preço indefinidamente; duas pegs também não devem formar uma referência circular. O enunciado não define esse caso e esta implementação escolhe limits como âncoras independentes.

**Uma consequência importante:** se uma sell limit de 1 unidade é a única âncora de uma peg offer sell de 3, e a limit tem prioridade, uma market buy de 4 executa apenas a limit. A âncora desaparece, a peg suspende antes do próximo fill e as 3 unidades restantes da market são descartadas. Isso é uma política deliberada, coberta por teste.

## Dois contadores lógicos de tempo

`sequence` decide a fila. `working_sequence` registra quando a cotação/prioridade atual foi ativada e decide qual das ordens selecionadas fornece o preço do trade. Ambos vêm de um contador monotônico; não usamos relógio de parede.

Uma peg buy antiga pode subir de 10 para 20 enquanto já existe sell a 15. Ela conserva FIFO, mas seu novo preço acabou de ser ativado. O trade ocorre a 15, não a 20. Uma mudança manual que perde prioridade renova os dois campos; uma reprecificação automática renova somente `working_sequence`. Reprecificações no mesmo ciclo são processadas por ID crescente, de forma determinística.

## Exemplos reproduzíveis

No PowerShell:

```powershell
Get-Content .\examples\basic.txt | .\build-vscode\matching.exe
Get-Content .\examples\pegged.txt | .\build-vscode\matching.exe
```

O exemplo básico produz, além das confirmações de criação:

```text
Trade, price: 20, qty: 150
Trade, price: 20, qty: 150
Trade, price: 10, qty: 100
```

O primeiro trade exibido corresponde a dois fills internos, de 100 e 50, contra ordens distintas nessa ordem FIFO. A agregação ocorre somente para fills consecutivos do mesmo preço dentro do mesmo comando. Se a soma não couber em `Qty`, são impressas linhas separadas. Os fills originais e seus IDs continuam disponíveis no `Result`.

No exemplo pegged, após entrar a compra de 300 a 10,1, a ordem das compras é: peg de 150 a 10,1; limit de 300 a 10,1; limit de 200 a 10; limit de 100 a 9,99.

## Arquitetura e estruturas

| Arquivo | Responsabilidade |
|---|---|
| `include/matching/types.hpp` | Tipos, validação, parsing e formatação exata. |
| `include/matching/engine.hpp` | Livro, índices, matching, cancel, amend, peg e snapshots. |
| `include/matching/cli.hpp` | Gramática, apresentação e sessão baseada em streams. |
| `src/main.cpp` | Conecta stdin/stdout/stderr à sessão e trata falhas fatais. |
| `tests/check.hpp` | Asserções de testes independentes de `NDEBUG`. |
| `tests/engine_tests.cpp` | Cenários determinísticos do domínio. |
| `tests/cli_tests.cpp` | Gramática, sessão, erros e formato de saída. |
| `tests/state_tests.cpp` | Oráculo independente e invariantes de ciclo de vida. |

| Estrutura | Função |
|---|---|
| `std::map<Id, Order>` | Fonte de verdade das ordens vivas, inclusive suspensas. |
| Dois `std::set<Entry, Priority>` | Índices executáveis: compra decrescente e venda crescente, depois sequência e ID. |
| Dois `std::multiset<Price>` | Uma entrada por limit viva para achar as âncoras bid/offer. |
| `std::set<Id>` | Identifica pegs e ordena sua atualização deterministicamente. |
| `std::vector<Trade>` | Fills produzidos por um comando. |

Os índices guardam valores, sem ponteiros ou iteradores entre contêineres. Para mudar preço/prioridade, removemos a chave antiga antes de reinseri-la. Um fill parcial altera apenas o saldo na fonte de verdade. No multiset, `erase(find(price))` remove uma ocorrência; `erase(price)` apagaria todas as limits daquele preço.

Uma alternativa seria `map<price, list<order>>` com índice por ID. Ela é natural para FIFO com inserção no fim. Aqui, uma peg entra em outro preço mantendo prioridade antiga; encontrar a posição na lista exigiria trabalho adicional. O set permite a reinserção ordenada em `O(log(N+1))`. O map por ID privilegia custo logarítmico garantido e simplicidade; não alegamos cancelamento `O(1)`.

C++17 fornece os recursos necessários, incluindo `std::optional` e structured bindings. Um compilador recente pode continuar usando esse padrão. Os métodos nos headers são inline, permitindo reutilizar a mesma implementação nos testes sem biblioteca externa. Em um projeto maior, separar implementação em `.cpp` poderia reduzir recompilações. Não há herança, threads, estado global mutável ou gerenciamento manual de memória.

## Correção e complexidade

Ao final de um comando, ordens vivas possuem saldo positivo, ordens ativas estão no índice de seu lado e o livro não está cruzado. O comparador determina uma ordem estrita. Cada fill usa o mínimo dos dois saldos, respeita ambos os limites e zera pelo menos uma ordem. Reprecificar não cria ordens nem quantidade; por isso os laços de matching terminam.

Considere `N` ordens vivas, `P` pegs e `K` fills produzidos pelo comando:

| Operação | Custo |
|---|---|
| Acessar a melhor entrada/referência | `O(1)` no extremo do índice. |
| Buscar por ID / inserir / remover dos índices | `O(log(N+1))`. |
| Cancelar sem pegs | `O(log(N+1))`. |
| Limit, market ou amend sem pegs | `O((K+1) log(N+1))`. |
| Reavaliar pegs | `O(P log(N+1))`, pois há buscas por ID. |
| Comando com pegs | `O((K+1)(P+1) log(N+1))`. |
| Snapshot completo | `O(N log(N+1))`, com cópias e buscas no map. |
| Estado em memória | `O(N)`; resultado temporário, `O(K)`. |

O principal limite de desempenho é reavaliar todas as pegs após cada fill. Uma evolução seria separar pegs por referência e só reavaliar o grupo afetado. Se muitas ordens realmente mudam de preço, ainda será necessário trabalho proporcional a elas. Não há benchmark de throughput neste projeto.

## Estratégia de testes

O CTest executa três suítes:

- `engine_tests`: validação numérica e overflow, 100.000 round-trips de preço, FIFO nos dois lados, níveis de preço, fills parciais, sobra de market, limit agressiva, cancelamento, alteração, quatro combinações de peg/side, suspensão, reativação, âncoras duplicadas e preço de execução;
- `cli_tests`: gramática estrita, saída, IDs, comandos inválidos, sessão que continua após erros, EOF, `exit`, falha de leitura, preservação de formatação e overflow da agregação;
- `state_tests`: cinco sementes fixas, totalizando 10.000 operações limit/market comparadas com vetor/busca linear independente e 15.000 operações envolvendo também pegs/cancel/amend, verificando conservação de saldo, unicidade, ordenação, referências e ausência de cruzamento residual.

O oráculo verifica IDs, preço e quantidade de cada fill e o livro após cada operação. Os testes aleatórios são reproduzíveis. Invariantes não substituem o oráculo; não há segunda implementação completa de pegged orders.

## Limitações

Tick de 0,01, shares inteiras, um ativo e execução sequencial. Sem persistência, histórico acumulado de trades, offsets/caps de peg, autenticação, risco, taxas, prevenção de self-trade ou recuperação de processo. Sem rollback transacional em falta de memória/esgotamento de contadores: essas falhas são fatais, não erros de comando recuperáveis. IDs e sequências não dão wrap-around. Testes não cobrem toda falha de recursos nem constituem prova de adequação a produção.
