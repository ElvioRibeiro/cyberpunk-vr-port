# Coleta de desempenho no PC do jogo

Versão 2 do coletor (17/11/2026): preserva frações de núcleo no cálculo de CPU
e registra a identidade do próprio script no manifesto. A versão anterior
arredondava esse campo ao selecionar a sobrecarga inteira de `Math.Max`.
Os contadores cumulativos e intervalos brutos permitem recalcular as sessões
anteriores; as métricas GPU não foram afetadas por essa correção.

Este coletor funciona separado da correção OSR. Ele somente lê métricas do processo,
consulta a GPU NVIDIA quando `nvidia-smi` está disponível e grava JSONL. Não instala
DLLs, altera configurações, injeta código, inicia o jogo ou ativa o Flight recorder.
Compatível com Windows PowerShell 5.1 e PowerShell 7; sem Python no PC do jogo.

## Executar

Copie `collect.ps1` para o PC do jogo. Com Cyberpunk já aberto, execute no PowerShell:

```powershell
& 'C:\Ferramentas\collect.ps1' -DurationSeconds 2700
```

O caminho do executável em execução identifica a instalação correta. Por padrão,
os resultados ficam em uma subpasta nova, com timestamp/PID, sob:

```text
<jogo>/red4ext/plugins/cyberpunkvrport/log/performance-watch/
```

Se o Windows PowerShell 5.1 bloquear arquivos `.ps1` pela política local, este
comando permite apenas a execução nessa instância, sem mudar a política da máquina:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Ferramentas\collect.ps1' -DurationSeconds 2700
```

É possível escolher outro destino, inclusive uma pasta sincronizada:

```powershell
& 'C:\Ferramentas\collect.ps1' -DurationSeconds 2700 -OutputDirectory 'C:\CPVR-Coleta'
```

Cada execução cria `manifest.json` e `process.jsonl`, sem sobrescrever outra sessão.
O manifest registra PID, início do processo, relógio QPC, caminho da instalação e
hashes da DLL/configuração. `Ctrl+C` interrompe a coleta; o processo também é
acompanhado até sair ou completar a duração. Permissões de leitura devem permitir
acessar o processo selecionado. Se houver mais de um Cyberpunk, use `-TargetPid`.

## Compartilhar e acompanhar

Compartilhe pela rede a pasta `log` do port, que contém tanto os logs nativos quanto
a subpasta `performance-watch`. Informe o caminho UNC acessível neste computador,
por exemplo `\\PC-JOGO\CPVR-Logs` (exemplo, não endereço já configurado). A pasta
pode ser analisada durante a coleta. O coletor continua gravando no PC do jogo;
o compartilhamento não cria uma conexão de controle remoto com ele.

Se usar outro destino para o coletor, disponibilize também os arquivos
`framegen-flight-*.jsonl` e `cyberpunkvrport.log` do mesmo período. No final,
preserve `red4ext/logs/red4ext.log` para complementar o contexto de carregamento.

## Flight recorder do PR #203

Se a DLL usada realmente possuir essa implementação:

1. Abra F10 > FRAMEGEN > **Flight recorder** e confira o status de gravação.
2. Os arquivos ficam em `log/framegen-flight-*.jsonl`.
3. **Mark performance problem** marca a percepção da queda; feche a overlay em
   seguida para medir a sessão normal.

O gravador funciona com FG ligado ou desligado e sem verbose diagnostics global.
Cada arquivo para em 16 MiB; confira o status, pois não há rotação automática.
O coletor registra periodicamente nome, tamanho e idade do último arquivo para
detectar interrupção da telemetria, sem alterar o gravador.

O upstream oficial não tem necessariamente essa opção. O PR fechado e uma versão
de release não identificam a DLL: use seu hash e o menu efetivamente disponível.
No V557 desse PR, mudar o estado de gravação altera a compatibilidade do backend
por ativar/desativar timing. Não pontue o aquecimento/reinicialização inicial como
desempenho sustentado. Se ligar o gravador mudar a queda percebida, registre esse
efeito; o coletor externo não liga/desliga o recurso automaticamente.

## Primeira sessão para distinguir cena de degradação

1. Use a mesma DLL, backend, refresh, resolução, DLSS/RR e configuração durante
   a primeira coleta. Aqueça o jogo por cerca de cinco minutos.
2. Registre 1–2 minutos num local de referência e anote o horário/situação.
3. Jogue normalmente por 15–20 minutos ou até perceber a queda. Marque o problema
   e anote se houve loading, menu, troca de equipamento ou mudança de região.
4. Volte ao local de referência sem recarregar o save, quando possível. Registre
   mais 1–2 minutos com enquadramento e condições semelhantes.
5. Preserve os logs antes de reiniciar. Só depois faça um teste separado de FG
   desligado/ligado, se os dados indicarem custo de geração ou entrega.

Se a telemetria atingir seu limite, abra outro intervalo explicitamente e anote
essa transição. Não mude vários controles ao mesmo tempo para tentar melhorar a
queda antes da primeira captura do problema.

## Interpretação e limites

`process.jsonl` coleta CPU total, memória privada/residente/virtual, handles e
threads a cada segundo. GPU, VRAM global, temperatura, clocks e potência são
consultados a cada cinco segundos por padrão. Valores indisponíveis ficam nulos
ou acompanhados de status de falha; VRAM global não é VRAM exclusiva do jogo.
CPU normalizada cobre o processo inteiro, não a thread principal ou um módulo.

O relógio QPC permite correlacionar com o flight log produzido no mesmo PC. Não
compare QPC de dois computadores; UTC e hostname delimitam a origem. O coletor
não calcula FPS, frametime por frame, percentis ou motion-to-photon.

Memória crescente pode ser cache ou retenção; é sinal para investigar, não prova
de vazamento. Diferenças de uso de GPU não identificam, sozinhas, o culpado. Para
separar tempo do motor, VR port, RED4ext, Lua e driver dentro do mesmo processo,
a próxima etapa é amostragem de stacks/ETW com símbolos correspondentes e janelas
boa/ruim equivalentes. O flight recorder ajuda a localizar espera/captura/geração
antes de ampliar a instrumentação.

Há overhead de consulta e escrita no processo coletor, e timestamps GPU no flight
recorder. Manter a primeira configuração estável e medir esse overhead faz parte
da comparação. `-NoGpu` desativa apenas as consultas externas à GPU.

## Verificação local

Em 17/11/2026, foram executados smoke tests de 2–3 segundos sobre o próprio host
PowerShell, em Windows PowerShell 5.1 e PowerShell 7, com/sem consulta NVIDIA.
No host 5.1 foi usada uma exceção de política somente no processo de teste.
JSONs, QPC crescente, campos de recursos, status GPU, registro de parada e contagem
de amostras foram conferidos. Isso não é teste do Cyberpunk, medição de overhead
em gameplay, nem confirmação de conexão ao outro PC.
