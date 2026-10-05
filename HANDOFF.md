# HANDOFF — estado do trabalho (2026-10-05)

Leia o `CLAUDE.md` primeiro (arquitetura, regras, armadilhas, como testar).

## Resumo

O editor está completo para Linux em `C:\Users\Doutor\Desktop\qotif`. Aqui só é
possível checar a sintaxe (stubs em `tools/xstub`); o usuário compila e testa no
Artix, a partir de `G:\Projetos\C\qotif`.

Nada está pela metade no código. O projeto é **somente Linux**: a versão Windows
feita antes foi descartada pelo usuário, e o código `#ifdef _WIN32` que ela tinha
colocado em `common.c`, `game.c` e `render.c` foi removido (os três arquivos
voltaram ao código Linux original).

## Histórico das tarefas (em ordem)

1. Editor criado (Motif + OpenGL 1.x, C99, Makefile simples).
2. Erros de build corrigidos (`XM_ALTERNATIVE` com `-std=c99`).
3. `libXm.so.5` não encontrada: era o Motif compilado da fonte em `/usr/local`
   sombreando o pacote. O usuário foi orientado a removê-lo; o Makefile deixou
   de usar `/usr/local` e ganhou `XMSTRINGDEFINES`.
4. Interface quebrada (rótulos com nomes de widgets, sobreposição) e sem
   redimensionar: corrigido com o grid em `XmForm`. Programa tornado 100% portátil.
5. Botão direito só funcionava com um menu aberto: corrigido (pai do popup).
6. View > Show Console, Z para capturar o mouse no 3D, Shift+Espaço maximiza a vista.
7. Paleta de ferramentas não trocava a ferramenta: corrigido. Adicionados
   edição de vértices/arestas/faces e textura por face.
8. Divisor visível entre as vistas.
9. VSync e backface culling ligados por padrão (View > Vertical Sync / Backface Culling).
10. (Descartado) versão Windows; o projeto passou a ser declarado 100% Linux.

## O que foi verificado

- Todo o código da interface passa na checagem de sintaxe com os stubs.
- O usuário testou no Artix até o item 9; as mensagens dele seguiram para novos
  pedidos sem relatar regressão, mas não houve confirmação explícita de cada
  correção. Os itens 6 a 9 nunca foram vistos rodando pelo Claude.
- A lógica do núcleo (edição de vértices, CSG, enrolamento das faces para o
  culling) foi exercitada com programas de teste no Windows enquanto existia a
  versão Windows; o núcleo não mudou desde então, exceto a remoção do `_WIN32`.

## Não testado

- Texturas reais (WAD2/WAD3, PAK, paleta): não há Quake nem WADs aqui.
- Compilação de mapas (F9).
- Abrir/salvar `.map` reais de outros editores.

## Pendências e problemas conhecidos

- **Copiar para `G:\Projetos\C\qotif`:** a cópia do usuário pode estar atrás da
  árvore daqui. O mais seguro é copiar `src/`, `Makefile`, `README.md` e
  `data/` inteiros. O drive G: não estava montado na última verificação.
- **Confirmar no Artix** que `common.c`, `game.c` e `render.c` compilam limpos
  depois da remoção do `_WIN32` (aqui não dá para compilá-los).
- **`README.md` desatualizado:** em "Limitações conhecidas" ainda diz
  "Não há edição de vértices", mas a ferramenta de vértices existe (Shift+V).
  Corrigir essa linha (a tabela de mouse do mesmo README já está certa).
- `data/games/halflife.cfg` espera um `halflife.fgd` que o usuário precisa fornecer.
- Trava de textura em rotações só é exata no formato Valve 220.
- Sem suporte a patches/brushDef do Quake 3 (fora do escopo, Quake 1).

## Possíveis próximos passos (não pedidos ainda)

- Pedir ao usuário para testar no Artix os itens 6–9 e a ferramenta de vértices.
- Testar com um Quake real (pak0.pak + um WAD) se o usuário fornecer os arquivos.

## Arquivos auxiliares

- `tools/xstub/`: cabeçalhos falsos de X11/Xt/Motif/GLX para checar a sintaxe da
  interface Motif nesta máquina Windows (ver `CLAUDE.md`).
