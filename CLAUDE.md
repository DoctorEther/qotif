# CLAUDE.md — Qotif

Qotif é um editor de mapas de Quake (.map) **exclusivo para Linux**, em C99, com
Motif/OpenMotif e OpenGL 1.x de função fixa (modo imediato, sem shaders). A
interface imita o Hammer/WorldCraft e a configuração imita o TrenchBroom: jogos,
entidades, atalhos, cores e perfis de compilação ficam em arquivos de texto.

O usuário fala português. Responda e escreva documentação em português;
identificadores e comentários no código ficam em inglês, como já estão.

## Onde está cada coisa

| Pasta | O que é |
|-------|---------|
| `C:\Users\Doutor\Desktop\qotif` | árvore de trabalho onde o Claude edita |
| `G:\Projetos\C\qotif` | cópia do usuário no disco do Linux, onde ele compila e testa no Artix. Nem sempre está montada aqui |
| `F:` | a instalação do Linux (Artix) do usuário, quando montada |

Não é repositório git. Depois de alterar arquivos, lembre o usuário de copiá-los
para `G:` (ou copie, se o drive estiver montado e ele pedir).

## Regras do projeto (pedidas pelo usuário)

- **100% Linux.** Não há versão para Windows nem código `#ifdef _WIN32`; não
  adicione portabilidade para outros sistemas. (Uma versão WinAPI chegou a ser
  feita em `C:\Users\Doutor\Desktop\qotif-win32`, mas o usuário a descartou:
  ignore essa pasta.)
- C99 puro (`-std=c99`), **sem CMake**: só `make`.
- Interface em Motif/Xt, OpenGL via GLX, somente fixed-function pipeline.
- **100% portátil, sem instalação:** não há alvo `install`. O programa acha
  `data/` ao lado do executável e grava `config/` ao lado dele.

## Arquitetura

```
src/common, mathlib, lexer, ini     utilitários (strings, caminhos, vetores, tokenizador, .ini)
src/map, brush                      dados do mapa, leitura/gravação .map, geometria, CSG
src/textures, vfs, eclass, game     WAD2/WAD3, PAK, FGD/DEF, jogos, preferências, caminhos
src/editor, undo, view, actions     núcleo do editor (ferramentas, seleção, vistas, atalhos)
src/render                          todo o OpenGL (vistas 2D/3D, navegador de texturas)
src/ui.h                            contrato: serviços que a interface oferece ao núcleo
src/ui_main.c                       janela principal, vistas, GLX, teclado/mouse, menus
src/ui_dialogs.c                    diálogos (entidade, face, texturas, preferências, compilar...)
src/ui_icons.c                      ícones da paleta
src/ui_internal.h                   estado compartilhado da interface Motif
src/main.c                          main() -> ui_main()
```

- **O núcleo não conhece o Motif.** Tudo que precisa da interface passa por
  `ui.h` (`ui_redraw_all`, `ui_status`, `ui_prompt`, `ui_file_dialog`,
  `ui_grab_pointer`...). A interface chama `view_mouse_*`, `view_wheel`,
  `action_for_key`/`action_run`, `fly_step` e `render_view`.
- **Atalhos:** `actions.c` tem a tabela de ações (nome, rótulo, atalho padrão,
  com nomes de keysym do X). Os menus são montados dessa tabela. O usuário
  reconfigura em `config/prefs.cfg`, seção `[keys]`.
- **Desfazer** (`undo.c`): uma cópia completa do mapa por passo (`undo_push(desc)`
  antes de cada edição). Simples e robusto; o custo de memória é aceitável para
  mapas de Quake.
- **Brushes** (`brush.c`): planos definidos por 3 pontos; normal =
  (p0−p1)×(p2−p1), apontando para fora. As faces (windings) saem do recorte de um
  winding base enorme pelos outros planos. CSG: split, subtract (Carve), hollow,
  merge por fecho convexo.
- **Edição de vértices** (`editor.c`, `TOOL_VERTEX`, Shift+V): alças de vértice,
  aresta e face (`vhandle_t`). Ao mover, o brush é refeito como fecho convexo dos
  novos pontos; se ficar côncavo ou degenerado o movimento é recusado. As texturas
  passam para as novas faces por `transfer_textures` (procura a face antiga de
  normal mais parecida).
- **Textura por face:** Shift+clique esquerdo na vista 3D seleciona uma face em
  qualquer ferramenta; a ferramenta de textura aplica por face.
- **Texturas:** projeção por eixo dominante do Quake no formato padrão e eixos
  explícitos no Valve 220. A trava de textura só é exata em rotações no Valve 220.
- **Entidades:** FGD (com herança, e flags/choices mesclados das classes base)
  ou `.def`. Chave `_tb_def` do worldspawn, compatível com TrenchBroom.
- **Caminhos** (`game.c`): `paths_init(argv0)` acha a pasta do executável por
  `/proc/self/exe` (ou `realpath(argv0)`). Se `config/` ao lado do executável não
  puder ser gravada, usa `$XDG_CONFIG_HOME/qotif` ou `~/.config/qotif`.
- **Interface Motif:**
  - Um único contexto GLX compartilhado por todos os `XmDrawingArea` (vistas,
    preview, navegador). Todas as áreas usam o **mesmo visual**, de preferência
    o visual padrão da tela.
  - As quatro vistas ficam num `XmForm` com `fractionBase 1000`; os anexos são
    atualizados pelo divisor próprio (`splitter_event`, `draw_splitters`,
    `SPLIT_GAP 4`). A posição é salva em `prefs.split_x/split_y`.
  - O console fica num `XmPanedWindow` com `XmNskipAdjust` nas vistas.
  - **VSync:** só a última vista redesenhada em cada passada usa intervalo 1
    (`swap_next` em `ui_main.c`); as outras usam 0, senão as quatro dividiriam a
    taxa de quadros. Usa GLX_EXT, MESA ou SGI swap control, o que existir.
  - Captura do mouse (Z) com `XGrabPointer` e cursor invisível.
- **Compilação de mapas:** perfis em `data/games/*.cfg`, rodados com `/bin/sh`
  no diretório do mapa, saída no console.

## Decisões e por quê

| Decisão | Motivo |
|---------|--------|
| Grid de vistas em `XmForm` com divisor próprio | `XmPanedWindow` aninhados não redimensionavam e sobrepunham as vistas |
| Menu de contexto com pai na barra de status | um popup filho da área de trabalho instala um grab passivo no botão 3 e o botão direito só funcionava com um menu aberto |
| Exclusividade manual na paleta de ferramentas | `radioBehavior` do Motif tinha corrida e a ferramenta não trocava |
| `-DXMSTRINGDEFINES -DXTSTRINGDEFINES` | nomes de recursos viram strings; um descompasso entre cabeçalho e biblioteca do Motif não embaralha mais os recursos (rótulos mostrando nomes de widgets) |
| rpath `$ORIGIN/lib` + `make dist` copiando a `libXm` | roda em máquina sem Motif instalado |
| Divisor cinza com bordas em relevo | o vão preto entre as vistas não era visível |
| Backface culling com `glFrontFace(GL_CW)` | os brushes são enrolados em sentido horário vistos de fora (conferido numericamente); culling desligado nas vistas 2D e no ortho de pixels |
| Undo por snapshot completo | simples, sem bugs de undo parcial |

## Armadilhas encontradas

- **Motif compilado da fonte em `/usr/local`:** o gcc procura `/usr/local/include`
  antes de `/usr/include`, então os cabeçalhos errados eram usados e o programa
  pedia `libXm.so.5`. Solução: remover o Motif de `/usr/local` e usar o pacote
  `openmotif` do Arch. O Makefile não tem `/usr/local` fixo; use `INCDIRS`/`LIBDIRS`.
- **`XM_ALTERNATIVE` indefinido com `-std=c99`:** há macros substitutas em `ui_internal.h`.
- **Popups Motif e grabs**, `radioBehavior` e `XmPanedWindow` aninhados: veja a tabela acima.
- **Heredocs no shell estragam `'\\'`:** para escrever código C com barras
  invertidas use a ferramenta Write/Edit, não heredoc.

## Como compilar e rodar (no Linux do usuário)

```bash
make            # gera ./qotif
./qotif
make dist       # pasta qotif-portable/ com a libXm em lib/
```

Dependências no Artix: `pacman -S base-devel openmotif mesa`.

## Como testar

A máquina onde o Claude roda é Windows, sem X11 nem Motif: **não dá para compilar
nem rodar o Qotif aqui.** O teste real é sempre do usuário, no Artix.

- **Checagem de sintaxe da interface Motif:** `tools/xstub/` tem cabeçalhos
  falsos de X11/Xt/Motif/GLX só para isso:

  ```bash
  cd /c/Users/Doutor/Desktop/qotif && gcc -std=c99 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DXMSTRINGDEFINES -DXTSTRINGDEFINES -Itools/xstub src/ui_main.c src/ui_dialogs.c src/ui_icons.c
  ```

  Deve dar zero erros. Os avisos `cast ... of different size` vêm do `long` de
  32 bits do Windows e não existem no Linux. Funções novas do Motif usadas pelo
  código precisam ser declaradas nos stubs.
- **Núcleo:** arquivos que não usam APIs POSIX (`brush.c`, `map.c`, `editor.c`,
  `mathlib.c`...) podem ser checados com `gcc -fsyntax-only` direto, ou testados
  com pequenos programas de teste no scratchpad. `common.c`, `game.c` e
  `render.c` usam `mkdir`/`readlink`/`realpath` POSIX e `GL/gl.h`, que não
  compilam no MinGW: esses erros aqui são esperados.
- Não há jogo, WADs nem compiladores (qbsp/light/vis) nesta máquina.

O estado atual, o que foi testado e as pendências estão em `HANDOFF.md`.
