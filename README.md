# Qotif

Editor de mapas de Quake para Linux/X11, escrito em C99 com **Motif/OpenMotif**
e a **fixed-function pipeline do OpenGL 1.x** (modo imediato, sem shaders).
A interface segue o Hammer/WorldCraft; a configuração segue o TrenchBroom:
jogos, definições de entidade, atalhos, cores e perfis de compilação ficam em
arquivos de texto.

## Compilar

Dependências: compilador C99, `make`, Motif (libXm), Xt, X11 e OpenGL.

    # Debian/Ubuntu
    sudo apt install build-essential libmotif-dev libgl-dev
    # Fedora
    sudo dnf install gcc make motif-devel mesa-libGL-devel
    # Arch/Artix
    sudo pacman -S base-devel openmotif mesa

    make            # gera ./qotif
    ./qotif         # roda no lugar, sem instalar
    make dist       # pasta qotif-portable/ com executável, data/ e a libXm

Não há instalação: o Qotif é portátil. Ele procura `data/` ao lado do
executável e grava as preferências em `config/`, também ao lado dele. A pasta
inteira pode ser copiada para qualquer lugar (ou um pendrive). O `make dist`
copia junto a `libXm` usada no build para `lib/`, e o executável a encontra lá
(rpath `$ORIGIN/lib`), então a máquina de destino não precisa ter o Motif.

Sem CMake: só o `Makefile`. Variáveis úteis: `CC`, `CFLAGS`,
`INCDIRS` e `LIBDIRS` (para Motif/X11 fora do caminho padrão, como nos BSDs
ou um Motif compilado em `/usr/local`; inclua `-Wl,-rpath,<dir>` em
`LIBDIRS` para o executável achar a biblioteca ao rodar).

## Primeiro uso

1. `Edit > Preferences > Games`: escolha o jogo e informe o diretório do jogo
   (o que contém `id1/`). A paleta (`gfx/palette.lmp`) é lida de lá, inclusive
   de dentro de `pak0.pak`.
2. `Map > Texture WADs...`: liste os WADs (chave `wad` do worldspawn, separados
   por `;`). Caminhos relativos são procurados no diretório do jogo, no
   diretório do mod e no diretório do mapa.
3. Em `Preferences > Games`, defina as variáveis das ferramentas
   (`QBSP = /caminho/qbsp`, `LIGHT`, `VIS`, `ENGINE`...) usadas por
   `File > Compile / Run` (F9).

## Interface

- **Quatro vistas** (3D, topo, frente, lado) que dividem o espaço proporcionalmente.
  Arraste o vão entre elas para mover a divisão (no cruzamento, os dois eixos);
  a posição fica salva nas preferências.
  Clique no rótulo de uma vista para trocar o tipo ou o modo 3D (texturizado,
  sombreado, wireframe). `Shift+Espaço` maximiza a vista ativa e `View > Show
  Console` mostra ou esconde o console.
- **Paleta de ferramentas** (esquerda): seleção, câmera, entidade, bloco,
  aplicação de textura, recorte e vértices.
- **Barra de objetos** (direita): textura atual, classes de entidade, tipo de
  primitiva (bloco, cunha, cilindro, pirâmide) e informações da seleção.
- **Console** com mensagens e avisos, abaixo das vistas.

Mouse:

| Onde | Ação |
|------|------|
| 2D, botão esquerdo | ferramenta atual; arrastar move; alças escalam; clicar de novo na seleção alterna escala/rotação; Shift+arrastar clona; Ctrl+clique soma/remove; arrastar no vazio seleciona por retângulo |
| 2D, botão do meio/direito arrastando | rolar a vista |
| 2D, botão direito (clique) | menu de contexto |
| 2D, roda | zoom no cursor |
| 3D, botão direito arrastando | olhar; WASD/QE voam enquanto segura; Shift acelera |
| 3D, tecla Z | captura o mouse: olhar sem segurar botão, WASD/QE voam; Z ou Esc solta |
| 3D, botão do meio | deslocar a câmera |
| 3D, ferramenta de textura | esquerdo seleciona face (Ctrl soma, Shift = brush inteiro), direito aplica a textura, Alt+esquerdo copia a textura |
| 3D, Shift+esquerdo (qualquer ferramenta) | seleciona uma face; Ctrl+Shift soma ou remove |
| Ferramenta de vértices (Shift+V) | arraste as alças dos brushes selecionados: brancas = vértices, azuis = arestas, laranja = faces; Ctrl+clique soma alças; em 2D alças sobrepostas se movem juntas; em 3D o arraste é horizontal e Alt move na vertical; movimentos que deixariam o brush côncavo são recusados |

Operações (menu Map): Tie to Entity, Move to World, Carve, Hollow, Convex
Merge, espelhar, girar 90°, alinhar ao grid, ir para brush, checar problemas,
converter para Valve 220, carregar point file de vazamento (`.pts`/`.lin`).

`Help > Keyboard Shortcuts` (F1) lista todos os atalhos.

## Configuração

Arquivos do usuário em `config/` ao lado do executável. Se essa pasta não
puder ser gravada, o Qotif usa `~/.config/qotif/` (ou `$XDG_CONFIG_HOME/qotif`).
O console mostra qual está em uso ao iniciar.

- `prefs.cfg`: preferências, cores (`[colors]`), atalhos (`[keys]`, por
  exemplo `carve = Ctrl+Shift+C`, até dois separados por vírgula, com nomes de
  keysym do X), e uma seção `[game <nome>]` por jogo com `path`, `mod` e as
  variáveis das ferramentas.
- `games/*.cfg`: configurações de jogo. Um arquivo aqui substitui o do sistema
  que tenha o mesmo `name`.

Diretórios de dados, em ordem de busca: `config/`, `$QOTIF_DATA`, `data/` ao
lado do executável e `./data`.

Exemplo de configuração de jogo (`data/games/quake.cfg`):

    [game]
    name = Quake
    basedir = id1
    palette = gfx/palette.lmp
    mapformat = standard        ; ou valve220
    entities = quake.fgd        ; .fgd (Hammer) ou .def (QuakeEd/Radiant)
    wads =

    [variables]
    QBSP = qbsp

    [profile Full]
    cmd = ${QBSP} "${MAP_FILE}"
    cmd = ${LIGHT} "${MAP_BASE}.bsp"

Variáveis prontas: `MAP_FILE`, `MAP_DIR`, `MAP_BASE`, `MAP_NAME`, `GAME_DIR`,
`BASE_DIR`, `BASE_NAME`, `MOD`, `CONFIG_DIR`, e também as do ambiente. Os
comandos rodam com `/bin/sh` no diretório do mapa.

Um mapa pode usar seu próprio arquivo de entidades pela chave do worldspawn
`_tb_def` (`external:/caminho/x.fgd` ou `builtin:quake.fgd`), compatível com o
TrenchBroom (`Map > Entity Definitions...`).

O visual do Motif pode ser ajustado por recursos X (`~/.Xresources`), por
exemplo `Qotif*background: #c0c0c0`.

## Formatos

- Mapas: Quake padrão e Valve 220 (lidos e gravados). Tokens extras no fim das
  faces (flags do Quake 2) são preservados.
- Texturas: WAD2 (Quake) e WAD3 (Half-Life, com paleta própria).
- Arquivos: diretórios soltos e `pak0.pak`..`pak9.pak`.
- Entidades: FGD (classes base, `color`, `size`, choices e flags mesclados por
  herança, `@include`) e `.def`.

## Limitações conhecidas

- Não há edição de vértices nem patches/brushDef do Quake 3.
- A trava de textura (Texture Lock) é exata para movimentos em todos os
  formatos e para rotações apenas no formato Valve 220.
- `data/games/halflife.cfg` espera um `halflife.fgd` fornecido pelo usuário.

## Código

    src/common, mathlib, lexer, ini   utilitários
    src/map, brush                    dados, .map, geometria e CSG
    src/textures, vfs, eclass, game   WAD/PAK, FGD/DEF, jogos e preferências
    src/editor, undo, view, actions   núcleo do editor (sem dependência do toolkit)
    src/render                        OpenGL 1.x
    src/ui_main, ui_dialogs, ui_icons Motif/Xt/GLX
