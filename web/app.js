'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const words = {
    pt: {
      closeHelper:'Encerrar auxiliar', helperClosed:'Auxiliar encerrado', helperClosedTitle:'O auxiliar local foi encerrado.', helperClosedMessage:'Para gerar ou conferir novamente, abra ptbr/Gerar-jogo.command (macOS), ptbr/Gerar-jogo.bat (Windows) ou generate-game.sh (Linux).', skip:'Ir para o gerador', closedBeta:'BETA FECHADO', localOnly:'EXECUÇÃO LOCAL', language:'Idioma', toolLabel:'FERRAMENTA DE GERAÇÃO / 01', title:'Seu disco.\nUma nova largada.', intro:'Transforme sua cópia de Underground 2 na pasta do jogo para PSP. A conversão acontece aqui, no seu computador.', private:'ISO permanece no computador', movies:'28 filmes incluídos', retryConnection:'Reconectar', setupTitle:'Preparar a largada', setupSubtitle:'Selecione a origem e o destino da conversão.', isoLabel:'Sua ISO do jogo', browse:'Escolher', isoHelp:'Xbox ou PS2 · versão americana NTSC-U · cópia do seu próprio disco.', outputLabel:'Onde salvar o jogo', outputHelp:'A saída será uma pasta PSP/GAME/NFSU2VIEW. Escolha um local com 12 GB livres. Ao escolher uma pasta, o gerador cria NFSU2-PSP dentro dela.', sourceOptions:'Detecção da fonte', sourceLabel:'Console de origem', autoSource:'Automática (recomendado)', sourceHelp:'A origem será conferida antes da conversão. A fonte PS2 tem diferenças documentadas no manual.', prepare:'Conferir ferramentas incluídas', prepareHelp:'O pacote nativo inclui Python e todos os conversores. Não precisa instalar WSL, Homebrew ou compilador. Se faltar algum arquivo, extraia novamente o pacote completo.', generate:'Gerar jogo para PSP', resume:'Retomar geração', check:'Conferir requisitos', resumeNote:'Parou no meio? Use a mesma ISO e pasta para retomar as etapas compatíveis.', progressLabel:'ACOMPANHAMENTO', idle:'Aguardando', readyTitle:'Tudo começa com a sua ISO.', readyMessage:'Escolha os caminhos ao lado. Você poderá acompanhar cada etapa aqui.', stagePreparation:'Preparação', stageIso:'Conferência da ISO', stageConversion:'Conversão dos arquivos', stageVerification:'Verificação da saída', stageComplete:'Pronto para copiar', cancel:'Cancelar processo', planLabel:'ANTES DE COMEÇAR', diskSpace:'livres no computador', duration:'Xbox / PS2 na primeira geração*', durationNote:'*Tempos do Mac de referência. Variam com o computador e a fila de processamento.', resultLabel:'SAÍDA CONFERIDA', resultTitle:'Sua pasta está pronta.', resultDescription:'A geração foi conferida contra a referência desta fonte e deste executável. Consulte o manual para copiar a pasta para o cartão.', openOutput:'Abrir pasta gerada ↗', readManual:'Abrir manual', identity:'Identidade e evidências da geração', errorTitle:'A geração precisa de atenção.', errorHelp:'Os registros e arquivos parciais foram preservados. Corrija o problema e gere novamente com os mesmos caminhos para retomar quando possível.', logs:'Registros do processo', logsHelp:'Use estes registros se precisar relatar um problema.', downloadLog:'Baixar registros ↓', importantLabel:'SOBRE ESTA BETA', limitsTitle:'Geração verificada. Beta em evolução.', limitsDescription:'A conferência da pasta confirma os arquivos gerados. As limitações de jogabilidade e de hardware continuam válidas.', bugGuide:'Manual e como reportar bugs', outputNoteLabel:'O QUE VOCÊ RECEBE', outputNote:'Uma pasta de jogo para o Memory Stick. Este gerador não cria uma ISO de PSP nem copia arquivos para o aparelho.', fanProject:'PROJETO DE FÃ · BETA FECHADO', preflightReady:'Requisitos disponíveis', preflightMissing:'Há requisitos a resolver', running:'Em andamento', checking:'Conferindo', preparing:'Preparando', cancelling:'Cancelando', cancelled:'Interrompido', failed:'Atenção', diagnostic:'Diagnóstico', complete:'Concluído', titleChecking:'Conferindo o ponto de partida.', titlePreparing:'Conferindo os conversores incluídos.', titleRunning:'Sua conversão está em andamento.', titleCancelling:'Interrompendo com segurança.', titleCancelled:'Você pode retomar daqui.', titleFailed:'Vamos resolver a interrupção.', titleDiagnostic:'A saída precisa de conferência.', titleComplete:'Verificação concluída.', cancelledMessage:'A geração foi interrompida. Mantenha a mesma ISO e pasta para reutilizar as etapas compatíveis.', disconnected:'A conexão com o auxiliar local foi interrompida. Uma conversão já iniciada pode continuar; reconecte para conferir.', noToken:'Abra ptbr/Gerar-jogo.command (macOS), ptbr/Gerar-jogo.bat (Windows) ou generate-game.sh (Linux) para iniciar o auxiliar e abrir esta página com acesso local.', fileMode:'Esta página precisa do auxiliar local. Abra ptbr/Gerar-jogo.command (macOS), ptbr/Gerar-jogo.bat (Windows) ou generate-game.sh (Linux).', picking:'Escolha na janela do sistema…', pickerFallback:'Não foi possível abrir o seletor. Digite ou cole o caminho completo no campo.', estimated:'Progresso estimado da etapa', steps:'Progresso da etapa', processing:'Processando · sem estimativa', verified:'Geração e saída conferidas', elapsed:'decorridos', logUnavailable:'Não foi possível baixar os registros.', opened:'Solicitação enviada para abrir a pasta.', requestFailed:'Não foi possível concluir a solicitação.', localHelper:'Aguardando o auxiliar local…', missingPaths:'Preencha o caminho da ISO e da pasta de saída.', build:'Build', report:'Relatório', source:'Fonte', iso:'ISO', output:'Saída', hash:'SHA-256 do executável', verification:'Verificação', details:'Detalhes', unavailable:'Indisponível', logLabel:'Registros do processo', progressAria:'Progresso da geração', manualTitle:'NFSU2 PSP · Gerador local', checkedMessage:'Requisitos conferidos. A ISO e os arquivos serão validados durante a geração.', preparingHint:'O auxiliar confere as ferramentas incluídas; nada é instalado no sistema.'
    },
    en: {
      closeHelper:'Close helper', helperClosed:'Helper closed', helperClosedTitle:'The local helper is closed.', helperClosedMessage:'To generate or check again, open Generate-game.command (macOS), Generate-game.bat (Windows), or generate-game.sh (Linux).', skip:'Skip to generator', closedBeta:'CLOSED BETA', localOnly:'RUNS LOCALLY', language:'Language', toolLabel:'BUILD TOOL / 01', title:'Your disc.\nA new starting line.', intro:'Turn your copy of Underground 2 into a PSP game folder. Conversion happens right here, on your computer.', private:'Your ISO stays on your computer', movies:'All 28 movies included', retryConnection:'Reconnect', setupTitle:'Set up your next run', setupSubtitle:'Choose the source and destination for conversion.', isoLabel:'Your game ISO', browse:'Browse', isoHelp:'Xbox or PS2 · US NTSC-U version · dumped from your own disc.', outputLabel:'Where to save the game', outputHelp:'The output contains PSP/GAME/NFSU2VIEW. Choose a drive with 12 GB free. The folder picker creates NFSU2-PSP inside your selected folder.', sourceOptions:'Source detection', sourceLabel:'Source console', autoSource:'Automatic (recommended)', sourceHelp:'The source is checked before conversion. PS2 source differences are documented in the manual.', prepare:'Verify included tools', prepareHelp:'The native download includes Python and all conversion tools. No WSL, Homebrew or compiler installation is needed. If files are missing, extract the complete download again.', generate:'Generate PSP game', resume:'Resume generation', check:'Check requirements', resumeNote:'Interrupted? Use the same ISO and output folder to resume compatible steps.', progressLabel:'LIVE PROGRESS', idle:'Waiting', readyTitle:'It starts with your ISO.', readyMessage:'Choose the paths on the left. Follow every step here once the process starts.', stagePreparation:'Preparation', stageIso:'ISO validation', stageConversion:'File conversion', stageVerification:'Output verification', stageComplete:'Ready to copy', cancel:'Cancel process', planLabel:'BEFORE YOU START', diskSpace:'free on your computer', duration:'Xbox / PS2 for the first conversion*', durationNote:'*Times measured on the reference Mac. Actual time depends on your computer and the processing queue.', resultLabel:'OUTPUT VERIFIED', resultTitle:'Your folder is ready.', resultDescription:'The generated files were checked against the reference for this source and executable. Read the manual to copy the folder to your memory card.', openOutput:'Open generated folder ↗', readManual:'Open manual', identity:'Build identity and generation evidence', errorTitle:'The generation needs attention.', errorHelp:'Logs and partial files have been preserved. Resolve the problem and generate again with the same paths to resume when possible.', logs:'Process logs', logsHelp:'Include these logs when reporting a problem.', downloadLog:'Download logs ↓', importantLabel:'ABOUT THIS BETA', limitsTitle:'Verified files. An evolving beta.', limitsDescription:'Output verification confirms the generated files. Gameplay and hardware limitations still apply.', bugGuide:'Manual and bug reporting guide', outputNoteLabel:'WHAT YOU GET', outputNote:'A game folder for your Memory Stick. This tool does not create a PSP ISO or copy files onto your device.', fanProject:'FAN PROJECT · CLOSED BETA', preflightReady:'Requirements available', preflightMissing:'Some requirements need attention', running:'Running', checking:'Checking', preparing:'Preparing', cancelling:'Cancelling', cancelled:'Interrupted', failed:'Attention', diagnostic:'Diagnostic', complete:'Complete', titleChecking:'Checking the starting point.', titlePreparing:'Checking the included converters.', titleRunning:'Your conversion is running.', titleCancelling:'Stopping the process safely.', titleCancelled:'You can resume from here.', titleFailed:'Let’s resolve the interruption.', titleDiagnostic:'The output needs attention.', titleComplete:'Verification complete.', cancelledMessage:'Generation was interrupted. Keep the same ISO and output folder to reuse compatible steps.', disconnected:'The local helper connection was interrupted. A conversion already started may still be running; reconnect to check.', noToken:'Open Generate-game.command (macOS), Generate-game.bat (Windows), or generate-game.sh (Linux) to start the helper and open this page with local access.', fileMode:'This page needs the local helper. Open Generate-game.command (macOS), Generate-game.bat (Windows), or generate-game.sh (Linux).', picking:'Choose in the system dialog…', pickerFallback:'The file picker could not be opened. Type or paste the full path into the field.', estimated:'Estimated stage progress', steps:'Stage progress', processing:'Processing · no estimate', verified:'Generation and output verified', elapsed:'elapsed', logUnavailable:'Could not download the logs.', opened:'Sent a request to open the folder.', requestFailed:'The request could not be completed.', localHelper:'Waiting for the local helper…', missingPaths:'Enter both the ISO path and the output folder.', build:'Build', report:'Report', source:'Source', iso:'ISO', output:'Output', hash:'Executable SHA-256', verification:'Verification', details:'Details', unavailable:'Unavailable', logLabel:'Process logs', progressAria:'Generation progress', manualTitle:'NFSU2 PSP · Local build tool', checkedMessage:'Requirements checked. The ISO and its files will be validated during generation.', preparingHint:'The helper checks the tools included in your download; nothing is installed on your system.'
    }
  };
  let language = 'en';
  try { language = localStorage.getItem('nfsu2-language') === 'pt' ? 'pt' : 'en'; } catch (_) {}
  const token = new URLSearchParams(location.hash.slice(1)).get('token') || '';
  let helperClosed = false;
  let state = null, connected = false, initialized = false, pending = false, pickerBusy = false, polling = false, timer = null, toastTimer = null;
  let lastLogText = '', lastResultSignature = '', lastError = '', lastAnnouncement = '';
  const t = key => words[language][key] || key;
  const localPage = location.protocol !== 'file:' && !!token;
  const isBusy = () => pending || pickerBusy || !connected || Boolean(state?.busy);
  const payload = () => ({iso:$('iso').value.trim(), output:$('output').value.trim(), source:$('source').value});
  function toast(message) { $('toast').textContent = message; $('toast').hidden = false; clearTimeout(toastTimer); toastTimer = setTimeout(() => {$('toast').hidden = true;}, 6500); }
  function announce(message) { if (lastAnnouncement !== message) { $('status-message').textContent = message; lastAnnouncement = message; } }
  function setConnection(ok) {
    connected = ok && !helperClosed;
    $('connection-banner').hidden = connected;
    $('connection-message').textContent = helperClosed ? t('helperClosedMessage') : localPage ? t('disconnected') : t(location.protocol === 'file:' ? 'fileMode' : 'noToken');
    $('reconnect').hidden = !localPage || helperClosed;
    controls();
  }
  function controls() {
    const busy = isBusy();
    $('shutdown').disabled = busy || helperClosed;
    ['iso','output','source','pick-iso','pick-output','start','check','prepare'].forEach(id => { $(id).disabled = busy; });
    $('cancel').hidden = helperClosed || !state?.busy || !['preparing','running','checking','cancelling'].includes(state?.status);
    $('cancel').disabled = pending || !connected || state?.status === 'cancelling';
    $('open-output').disabled = pending || !connected || state?.status !== 'complete';
    $('download-log').disabled = !connected || !lastLogText;
    $('start').querySelector('[data-i18n]').textContent = t(state?.can_resume ? 'resume' : 'generate');
  }
  async function request(path, data, options = {}) {
    const init = {method:data === undefined ? 'GET' : 'POST',headers:{'X-NFSU2-Token':token},cache:'no-store'};
    if (data !== undefined) {init.headers['Content-Type'] = 'application/json'; init.body = JSON.stringify(data);}
    if (options.signal) init.signal = options.signal;
    let response;
    try { response = await fetch(path, init); } catch (error) { error.transport = true; setConnection(false); throw error; }
    if (!response.ok) {
      let message = `${t('requestFailed')} (${response.status})`;
      try { const body = await response.json(); message = body.error || body.message || message; } catch (_) {}
      const error = new Error(message); error.httpStatus = response.status; throw error;
    }
    return options.raw ? response : response.json();
  }
  function stageIndex(stage, status) {
    if (status === 'complete') return 4;
    if (status === 'preparing' || status === 'checking') return 0;
    const name = String(stage || '').toLowerCase();
    if (/verif|validat.*output|integrity/.test(name)) return 3;
    if (/conver|extract|package|asset|build/.test(name)) return 2;
    if (/iso|source|hash/.test(name)) return 1;
    if (/prepar|preflight|setup/.test(name)) return 0;
    return -1;
  }
  function formatElapsed(seconds) {
    const n = Math.max(0,Math.floor(Number(seconds) || 0));
    if (!n) return '';
    const h = Math.floor(n / 3600), m = Math.floor((n % 3600) / 60), s = n % 60;
    return `${h ? `${h}h ` : ''}${m ? `${m}m ` : ''}${s}s ${t('elapsed')}`;
  }
  function renderPreflight(preflight) {
    $('preflight').hidden = !preflight;
    if (!preflight) return;
    $('preflight').classList.toggle('needs-attention', !preflight.ready);
    $('preflight-icon').textContent = preflight.ready ? '✓' : '!';
    $('preflight-title').textContent = t(preflight.ready ? 'preflightReady' : 'preflightMissing');
    const list = $('preflight-problems'); list.replaceChildren();
    (preflight.problems || []).forEach(problem => { const li = document.createElement('li'); li.textContent = typeof problem === 'string' ? problem : String(problem.message || JSON.stringify(problem)); list.append(li); });
    $('preflight-platform').textContent = [preflight.platform, preflight.python].filter(Boolean).join(' · ');
    $('prepare').hidden = Boolean(preflight.ready);
    $('prepare-help').hidden = Boolean(preflight.ready);
  }
  function appendIdentity(label, value) {
    if (value === undefined || value === null || value === '') return;
    const dt = document.createElement('dt'), dd = document.createElement('dd');
    dt.textContent = label;
    dd.textContent = typeof value === 'object' ? JSON.stringify(value, null, 2) : String(value);
    $('identity-list').append(dt,dd);
  }
  function renderResult() {
    const complete = state?.status === 'complete';
    $('result').hidden = !complete;
    if (!complete) return;
    const report = state.result || {};
    $('result-path').textContent = report.game_path || report.game_folder || report.output_path || report.output || state.job?.output || '';
    const signature = JSON.stringify([language,report,state.build,state.job]);
    if (signature === lastResultSignature) return;
    lastResultSignature = signature; $('identity-list').replaceChildren();
    appendIdentity(t('build'), report.candidate || state.build?.candidate);
    appendIdentity(t('source'), report.source || report.source_kind || state.job?.source);
    appendIdentity(t('iso'), state.job?.iso);
    appendIdentity(t('output'), state.job?.output);
    appendIdentity(t('hash'), report.eboot_sha256 || state.build?.eboot_sha256);
    appendIdentity(t('verification'), report.verification || (report.reference_verified === true ? t('verified') : report.status));
    if (Object.keys(report).length) appendIdentity(t('report'), report);
  }
  function renderKnownIssues() {
    const raw = state?.build?.known_issues;
    const root = $('known-issues'); root.replaceChildren();
    if (!raw) return;
    const descriptions = {
      'new-executable-hardware-and-memory-stick-listening-validation-pending': {
        en:'This version still needs testing and listening checks on a PSP and Memory Stick.',
        pt:'Esta versão ainda precisa de testes e conferência do áudio no PSP e Memory Stick.'
      },
      'original-drift-domain-qualified-not-global-parity': {
        en:'Drift has had limited local tests; handling across all cars and tracks still needs validation.',
        pt:'O Drift passou por testes locais limitados; a dirigibilidade de todos os carros e pistas ainda precisa de validação.'
      },
      'custom-dyno-and-special-preset-drift-use-previous-model': {
        en:'Custom dyno settings and special preset cars use the previous handling for the whole event.',
        pt:'O dyno personalizado e carros de presets especiais usam a dirigibilidade anterior durante o evento inteiro.'
      },
      '2d-collision-and-unqualified-wild-hydraulics-branches': {
        en:'Collisions use a simplified model; extreme handling and hydraulics still need testing.',
        pt:'As colisões usam um modelo simplificado; comportamentos extremos e hidráulica ainda precisam de testes.'
      },
      'drag-4401-rival-recovery-loop-deferred': {
        en:'In Tunnel Construction, opponents may get stuck and repeatedly respawn.',
        pt:'Em Tunnel Construction, os adversários podem ficar presos e reaparecer repetidamente.'
      },
      'tone-visibility-uv-cull-doors-review-incomplete': {
        en:'Colors, materials, textures, scenery visibility and doors still need review.',
        pt:'Cores, materiais, texturas, visibilidade do cenário e portas ainda precisam de revisão.'
      },
      'psp1000-other-models-and-global30fps-unapproved': {
        en:'PSP-1000, PSP-2000, Go and consistent 30 fps still need validation.',
        pt:'PSP-1000, PSP-2000, Go e os 30 fps constantes ainda precisam de validação.'
      },
      'ps2-original-usa-disc-image-unverified': {
        en:'The original US PS2 disc image has not been verified.',
        pt:'A imagem do disco americano original de PS2 ainda não foi verificada.'
      },
      'historical-psplink-stop-manual-recovery': {
        en:'Stopping an earlier PSPLink test required manual recovery.',
        pt:'O encerramento de um teste anterior pelo PSPLink exigiu recuperação manual.'
      },
      'drag-4401-rival-recovery-loop': {
        en:'In Tunnel Construction, opponents may get stuck and repeatedly respawn.',
        pt:'Em Tunnel Construction, os adversários podem ficar presos e reaparecer repetidamente.'
      },
      'tone-and-visibility-review-incomplete': {
        en:'Visual tone, materials and visibility are still under review.',
        pt:'A tonalidade, os materiais e a visibilidade ainda estão em revisão.'
      },
      'psp1000-and-global30fps-unapproved': {
        en:'Support for PSP-1000, PSP-2000 and Go, and consistent 30 fps, still need validation.',
        pt:'O suporte ao PSP-1000, PSP-2000 e Go, e os 30 fps constantes, ainda precisam de validação.'
      },
      'ps2-original-disc-image-unverified': {
        en:'The original US PS2 disc image still needs validation.',
        pt:'A imagem do disco americano original de PS2 ainda precisa de validação.'
      },
      'card-installation-and-physical-native-save-not-tested': {
        en:'Full Memory Stick play and native saves still need validation.',
        pt:'Jogar integralmente pelo Memory Stick e os saves nativos ainda precisam de validação.'
      },
      'psplink-stop-required-manual-recovery': {
        en:'Stopping the PSPLink hardware test required manual recovery.',
        pt:'O encerramento do teste de hardware pelo PSPLink exigiu recuperação manual.'
      }
    };
    const items = Array.isArray(raw) ? raw : typeof raw === 'object' ? Object.values(raw) : [raw];
    const ul = document.createElement('ul');
    items.forEach(item => {
      const code = typeof item === 'string' ? item : String(item.code || item.description || item.message || JSON.stringify(item));
      const li = document.createElement('li');
      li.textContent = descriptions[code]?.[language] || `${language === 'pt' ? 'Limitação conhecida' : 'Known limitation'}: ${code}`;
      ul.append(li);
    });
    root.append(ul);
    const link = document.createElement('a');
    link.className = 'inline-link';
    link.href = language === 'pt' ? '/ptbr/LIMITACOES.md' : '/KNOWN_ISSUES.md';
    link.target = '_blank'; link.rel = 'noopener';
    link.textContent = language === 'pt' ? 'Consultar todas as limitações ↗' : 'Read all known limitations ↗';
    root.append(link);
  }
  function render() {
    if (!state) {controls(); return;}
    const status = state.status || 'idle';
    const active = ['checking','preparing','running','cancelling'].includes(status);
    const interrupted = ['failed','diagnostic'].includes(status);
    const pill = $('status-pill'); pill.textContent = t(status);
    pill.className = `status-pill${active ? ' active' : status === 'complete' ? ' success' : interrupted || status === 'cancelled' ? ' attention' : ''}`;
    const titleKey = ({checking:'titleChecking',preparing:'titlePreparing',running:'titleRunning',cancelling:'titleCancelling',cancelled:'titleCancelled',failed:'titleFailed',diagnostic:'titleDiagnostic',complete:'titleComplete'})[status] || 'readyTitle';
    $('progress-title').textContent = t(titleKey);
    announce(state.message || t(status === 'cancelled' ? 'cancelledMessage' : status === 'preparing' ? 'preparingHint' : status === 'complete' ? 'verified' : 'readyMessage'));
    const index = stageIndex(state.stage,status);
    document.querySelectorAll('#stages li').forEach((li,i) => {
      const done = status === 'complete' || i < index;
      li.classList.toggle('done',done); li.classList.toggle('active',status !== 'complete' && i === index && (active || interrupted || status === 'cancelled'));
      li.querySelector('.stage-check').textContent = done ? '✓' : '';
      if (li.classList.contains('active')) li.setAttribute('aria-current','step'); else li.removeAttribute('aria-current');
    });
    const progressPresent = typeof state.progress === 'number' && Number.isFinite(state.progress);
    const showProgress = active || status === 'complete' || (progressPresent && status !== 'idle');
    $('progress-container').hidden = !showProgress;
    const progress = status === 'complete' ? 100 : progressPresent ? Math.max(0,Math.min(99,state.progress)) : null;
    const indeterminate = progress === null;
    $('progress-bar').classList.toggle('indeterminate',indeterminate && active);
    $('progress-fill').style.width = progress === null ? '0%' : `${progress}%`;
    if (progress === null) $('progress-bar').removeAttribute('aria-valuenow'); else $('progress-bar').setAttribute('aria-valuenow',String(progress));
    $('progress-value').textContent = progress === null ? '—' : `${Math.floor(progress)}%`;
    $('progress-description').textContent = t(status === 'complete' ? 'verified' : progress === null ? 'processing' : state.progress_kind === 'estimated' ? 'estimated' : 'steps');
    $('elapsed').textContent = formatElapsed(state.elapsed);
    renderPreflight(state.preflight);
    const logText = (Array.isArray(state.logs) ? state.logs : []).join('\n');
    if (logText !== lastLogText) {
      const pre = $('logs'), atEnd = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 55;
      pre.textContent = logText; lastLogText = logText;
      $('log-count').textContent = String(state.logs?.length || 0);
      if (atEnd) pre.scrollTop = pre.scrollHeight;
    }
    $('error-panel').hidden = !interrupted;
    if (interrupted) $('error-message').textContent = state.message || lastError || t('requestFailed');
    $('build-candidate').textContent = state.build?.candidate ? `${t('build')} ${state.build.candidate}` : '';
    renderKnownIssues(); renderResult(); controls();
  }
  async function poll() {
    clearTimeout(timer);
    if (!localPage || polling || document.hidden || helperClosed) return;
    polling = true;
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 12000);
    try {
      const nextState = await request('/api/state', undefined, {signal:controller.signal});
      if (helperClosed) return;
      state = nextState; setConnection(true);
      if (!initialized) {
        if (state.job?.iso) $('iso').value = state.job.iso;
        if (!$('output').value) $('output').value = state.job?.output || state.default_output || '';
        if (['auto','xbox','ps2'].includes(state.job?.source)) $('source').value = state.job.source;
        initialized = true;
      }
      render();
    } catch (error) { setConnection(false); }
    finally { clearTimeout(timeout); polling = false; if (!document.hidden && !helperClosed) timer = setTimeout(poll,connected ? 1000 : 3000); }
  }
  async function action(path, data) {
    if (pending) return;
    pending = true; lastError = ''; controls();
    try { await request(path,data); await poll(); }
    catch (error) { if (!error.transport) { lastError = error.message; toast(error.message); } }
    finally {pending = false; controls();}
  }
  function validPaths() { if (!$('build-form').reportValidity()) return false; const p = payload(); if (!p.iso || !p.output) {toast(t('missingPaths')); return false;} return true; }
  async function pick(kind) {
    pickerBusy = true; controls(); toast(t('picking'));
    try { const result = await request('/api/pick',{kind}); if (result.path && !result.cancelled) $(kind === 'iso' ? 'iso' : 'output').value = result.path; $('toast').hidden = true; }
    catch (error) { if (!error.transport) toast(`${error.message} ${t('pickerFallback')}`); }
    finally { pickerBusy = false; controls(); $(kind === 'iso' ? 'iso' : 'output').focus(); }
  }
  function translate() {
    document.documentElement.lang = language === 'pt' ? 'pt-BR' : 'en'; document.title = t('manualTitle');
    document.querySelectorAll('[data-i18n]').forEach(el => {const key = el.dataset.i18n; if (key === 'title') { el.replaceChildren(); t(key).split('\n').forEach((line,i) => {if(i) el.append(document.createElement('br')); el.append(document.createTextNode(line));}); } else el.textContent = t(key);});
    document.querySelectorAll('.manual-link').forEach(link => {link.href = language === 'pt' ? '/ptbr/manual-reportar-bugs.pdf' : '/docs/bug-reporting-guide-EN.pdf';});
    $('language').value = language;
    $('iso').placeholder = language === 'pt' ? '/caminho/para/Underground2.iso' : '/path/to/Underground2.iso';
    $('logs').setAttribute('aria-label',t('logLabel')); $('progress-bar').setAttribute('aria-label',t('progressAria'));
    if (!connected) setConnection(false);
    if (helperClosed) renderClosed(); else render();
  }
  function renderClosed() {
    setConnection(false);
    $('status-pill').textContent = t('helperClosed');
    $('status-pill').className = 'status-pill';
    $('progress-title').textContent = t('helperClosedTitle');
    announce(t('helperClosedMessage'));
    $('progress-container').hidden = true;
    $('elapsed').textContent = '';
    $('error-panel').hidden = true;
    controls();
  }
  $('shutdown').addEventListener('click',async () => {
    if (isBusy() || helperClosed) return;
    pending = true; controls();
    try {
      await request('/api/shutdown',{});
      helperClosed = true;
      clearTimeout(timer);
      renderClosed();
    } catch (error) { if (!error.transport) toast(error.message); }
    finally { pending = false; controls(); }
  });
  $('language').addEventListener('change',event => {language = event.target.value; try {localStorage.setItem('nfsu2-language',language);} catch (_) {} translate();});
  $('pick-iso').addEventListener('click',() => pick('iso'));
  $('pick-output').addEventListener('click',() => pick('output'));
  $('build-form').addEventListener('submit',event => {event.preventDefault(); if(validPaths()) action('/api/start',payload());});
  $('check').addEventListener('click',() => {if(validPaths()) action('/api/check',payload());});
  $('prepare').addEventListener('click',() => action('/api/prepare',{}));
  $('cancel').addEventListener('click',() => action('/api/cancel',{}));
  $('reconnect').addEventListener('click',poll);
  $('open-output').addEventListener('click',() => action('/api/open-output',{}));
  $('download-log').addEventListener('click',async () => {
    try {const response = await request('/api/log',undefined,{raw:true});const blob = await response.blob(), url = URL.createObjectURL(blob), a = document.createElement('a'); a.href = url; a.download = 'nfsu2-generation.log'; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000);}
    catch(error) {if(!error.transport) toast(error.message || t('logUnavailable'));}
  });
  document.addEventListener('visibilitychange',() => {if(document.hidden) clearTimeout(timer); else poll();});
  translate();
  if (localPage) poll();
})();
