import React, { useState, useEffect, useRef } from 'react';
import { render, Box, Text, useInput } from 'ink';
import chalk from 'chalk';
import gradient from 'gradient-string';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import path from 'path';
import fs from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT_DIR = path.resolve(__dirname, '../../../');

interface PatchNote {
  version: string;
  date: string;
  title: string;
  fixes: string[];
}

const PATCH_NOTES_PATH = path.join(ROOT_DIR, 'tools/deploy-desktop-cli/patch_notes.json');

function checkPatchNotesConflict(): { hasConflict: boolean; message: string | null; notes: PatchNote[] } {
  try {
    if (!fs.existsSync(PATCH_NOTES_PATH)) {
      return { hasConflict: false, message: null, notes: [] };
    }
    const content = fs.readFileSync(PATCH_NOTES_PATH, 'utf-8');
    if (content.includes('<<<<<<<') || content.includes('=======\n') || content.includes('>>>>>>>')) {
      return {
        hasConflict: true,
        message: 'Git merge conflict markers detected in patch_notes.json',
        notes: []
      };
    }
    const parsed = JSON.parse(content);
    if (!Array.isArray(parsed)) {
      return {
        hasConflict: true,
        message: 'patch_notes.json is not a valid JSON array',
        notes: []
      };
    }
    return { hasConflict: false, message: null, notes: parsed };
  } catch (e: any) {
    return {
      hasConflict: true,
      message: `Failed to parse patch_notes.json: ${e.message}`,
      notes: []
    };
  }
}

const CAT_ART = `
    |\\__/,|   (\`\\
  _.|o o  |_   ) )
-(((---(((--------
`;

const STEPS = [
  { id: 'mkdir', label: 'Preparing Remote Directories', command: 'ssh desktop "mkdir -p ~/bpp/src"' },
  {
    id: 'sync_src',
    label: 'Syncing Source Code (Rsync)',
    command: `rsync -avz --exclude='.git' --exclude='build/' --exclude='*.tar.gz' --exclude='*.o' --exclude='*.a' --exclude='.cmake' . desktop:~/bpp/src/`
  },
  {
    id: 'sync_cfg',
    label: 'Syncing Configurations',
    command: 'rsync -avz docker-compose.yml .env desktop:~/bpp/'
  },
  {
    id: 'build_img',
    label: 'Building Podman Image (Desktop)',
    command: 'ssh desktop "cd ~/bpp/src && podman build -t bpp-bot:latest ."'
  },
  {
    id: 'restart_svc',
    label: 'Restarting Containers (podman-compose)',
    command: 'ssh desktop "cd ~/bpp && podman-compose down && podman-compose up -d"'
  }
];

const TABS = [
  { id: 'all', label: 'All Logs' },
  { id: 'error', label: 'Errors/Warn' },
  { id: 'slash', label: 'Commands' },
  { id: 'net', label: 'Network/API' },
  { id: 'sys', label: 'System' },
  { id: 'patches', label: 'Patch Notes' }
] as const;

interface LogEntry {
  id: number;
  raw: string;
  colored: string;
  type: 'all' | 'error' | 'slash' | 'net' | 'sys' | 'info';
}

function Spinner() {
  const [frame, setFrame] = useState(0);
  const frames = ['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'];
  useEffect(() => {
    const timer = setInterval(() => {
      setFrame((prev) => (prev + 1) % frames.length);
    }, 80);
    return () => clearInterval(timer);
  }, []);
  return <Text color="cyan">{frames[frame]} </Text>;
}

function DeployCLI() {
  const [activeStep, setActiveStep] = useState(0);
  const [statuses, setStatuses] = useState<string[]>(new Array(STEPS.length).fill('pending'));
  const [allLogs, setAllLogs] = useState<LogEntry[]>([]);
  
  const [termCols, setTermCols] = useState(process.stdout.columns || 140);
  const [isPoppedOut, setIsPoppedOut] = useState(false);
  const [activeTab, setActiveTab] = useState(0);
  const [scrollOffset, setScrollOffset] = useState(0);
  const [earlierScrollOffset, setEarlierScrollOffset] = useState(0);
  const [autoScroll, setAutoScroll] = useState(true);

  const [patchNotes, setPatchNotes] = useState<PatchNote[]>([]);
  const [conflictError, setConflictError] = useState<string | null>(null);
  const [patchScrollOffset, setPatchScrollOffset] = useState(0);

  const [unreadErrors, setUnreadErrors] = useState(0);
  const [latestErrorNotification, setLatestErrorNotification] = useState<string | null>(null);
  const [searchQuery, setSearchQuery] = useState('');
  const [isSearchMode, setIsSearchMode] = useState(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [isFinished, setIsFinished] = useState(false);
  const [isStreamingProd, setIsStreamingProd] = useState(false);
  const [isExiting, setIsExiting] = useState(false);
  const [exitFrame, setExitFrame] = useState(-1);
  const [gradientTick, setGradientTick] = useState(0);

  const allLogsRef = useRef<LogEntry[]>([]);
  const autoScrollRef = useRef(true);
  const earlierScrollOffsetRef = useRef(0);
  const unreadErrorsRef = useRef(0);
  const logIdCounterRef = useRef(0);
  const isStreamingProdRef = useRef(false);

  const MAX_VISIBLE_LOGS = isStreamingProd ? 16 : 12;
  const GRADIENT_COLORS = ['#ff007f', '#7f00ff', '#00f0ff', '#ff00ff', '#00ffff'];

  useEffect(() => {
    isStreamingProdRef.current = isStreamingProd;
  }, [isStreamingProd]);

  useEffect(() => {
    const timer = setInterval(() => {
      if (process.stdout.columns && process.stdout.columns !== termCols) {
        setTermCols(process.stdout.columns);
      }
    }, 500);
    return () => clearInterval(timer);
  }, [termCols]);

  useEffect(() => {
    const timer = setInterval(() => {
      setGradientTick((prev) => (prev + 1) % GRADIENT_COLORS.length);
    }, 150);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    if (exitFrame === -1) return;
    if (exitFrame >= 12) {
      process.exit(0);
    }
    const timer = setTimeout(() => {
      setExitFrame((prev) => prev + 1);
    }, 80);
    return () => clearTimeout(timer);
  }, [exitFrame]);

  const changeAutoScroll = (value: boolean) => {
    setAutoScroll(value);
    autoScrollRef.current = value;
  };

  const dissolve = (text: string, frame: number): string => {
    if (frame <= 0) return text;
    const chars = text.split('');
    const threshold = frame / 12;
    const particles = ['.', '*', 'x', ' ', ' '];
    for (let i = 0; i < chars.length; i++) {
      if (chars[i] !== '\n' && chars[i] !== ' ') {
        if (Math.random() < threshold) {
          chars[i] = particles[Math.floor(Math.random() * particles.length)];
        }
      }
    }
    return chars.join('');
  };

  const renderGlitchText = (text: string, color?: string, bold?: boolean, dimColor?: boolean) => {
    const dissolved = exitFrame >= 0 ? dissolve(text, exitFrame) : text;
    return <Text color={color} bold={bold} dimColor={dimColor}>{dissolved}</Text>;
  };

  const colorizeLine = (line: string) => {
    const lower = line.toLowerCase();
    if (line.startsWith('[exec]')) return chalk.magenta(line);
    if (lower.includes('error') || lower.includes('failed') || lower.includes('exit code') || lower.includes('unsuccessful') || lower.includes('exception')) {
      return chalk.red(line);
    }
    if (lower.includes('warning') || lower.includes('warn')) return chalk.yellow(line);
    if (lower.includes('success') || lower.includes('complete') || lower.includes('successful')) return chalk.green(line);
    if (lower.includes('building') || lower.includes('sending') || lower.includes('transferring') || lower.includes('http') || lower.includes('curl')) {
      return chalk.cyan(line);
    }
    if (lower.includes('to-chk') || lower.includes('bytes/sec') || lower.includes('%') || /^\s*\d+/.test(line)) {
      return chalk.gray(line);
    }
    return chalk.white(line);
  };

  const appendLog = (data: string, isStderr: boolean = false) => {
    const lines = data.split('\n').filter(l => l.trim() !== '');
    const newEntries: LogEntry[] = [];

    lines.forEach(line => {
      const lower = line.toLowerCase();
      let type: LogEntry['type'] = 'info';
      if (isStderr || lower.includes('error') || lower.includes('failed') || lower.includes('exception') || lower.includes('crit') || lower.includes('unsuccessful') || lower.includes('exit code') || lower.includes('warn')) {
        type = 'error';
      } else if (lower.includes('slash') || lower.includes('command') || lower.includes('cmd') || lower.includes('interaction') || lower.includes('button') || lower.includes('select')) {
        type = 'slash';
      } else if (lower.includes('http') || lower.includes('curl') || lower.includes('api') || lower.includes('ws') || lower.includes('gateway') || lower.includes('voice') || lower.includes('proxy') || lower.includes('8080')) {
        type = 'net';
      } else if (line.startsWith('[exec]') || lower.includes('podman') || lower.includes('rsync') || lower.includes('build') || lower.includes('container') || lower.includes('ssh')) {
        type = 'sys';
      }

      const colored = type === 'error' && isStderr ? chalk.yellow(line) : colorizeLine(line);

      logIdCounterRef.current += 1;
      newEntries.push({
        id: logIdCounterRef.current,
        raw: line,
        colored,
        type
      });

      if (type === 'error' && isStreamingProdRef.current && activeTab !== 1) {
        unreadErrorsRef.current += 1;
        setUnreadErrors(unreadErrorsRef.current);
        setLatestErrorNotification(line);
        try { process.stdout.write('\x07'); } catch {}
      }
    });

    allLogsRef.current = [...allLogsRef.current, ...newEntries].slice(-1500);
    setAllLogs([...allLogsRef.current]);
  };

  useInput((input, key) => {
    if (conflictError) {
      if (input === 'r') {
        const check = checkPatchNotesConflict();
        if (!check.hasConflict) {
          setConflictError(null);
          setPatchNotes(check.notes);
        }
        return;
      }
      if (input === 'q' || (input === 'c' && key.ctrl)) {
        process.exit(0);
      }
      return;
    }

    if (TABS[activeTab].id === 'patches') {
      const totalLines = getPatchNotesLinesCount();
      const maxScroll = Math.max(0, totalLines - MAX_VISIBLE_LOGS);
      if (input === 'w') {
        setPatchScrollOffset(prev => Math.max(0, prev - 1));
        return;
      }
      if (input === 's') {
        setPatchScrollOffset(prev => Math.min(maxScroll, prev + 1));
        return;
      }
    }

    if (errorMsg || (isFinished && !isStreamingProd)) {
      if (!isExiting) { setIsExiting(true); setExitFrame(0); }
      return;
    }

    if (isSearchMode) {
      if (key.escape || key.return) setIsSearchMode(false);
      else if (key.backspace || key.delete) setSearchQuery(prev => prev.slice(0, -1));
      else if (input && !key.ctrl && !key.meta) setSearchQuery(prev => prev + input);
      return;
    }

    if (key.escape) { if (searchQuery) setSearchQuery(''); return; }
    if (input === 'q' || (input === 'c' && key.ctrl)) { if (!isExiting) { setIsExiting(true); setExitFrame(0); } return; }
    if (input === '/' || input === 'f') { setIsSearchMode(true); return; }
    if (input === 'c') { allLogsRef.current = []; setAllLogs([]); setScrollOffset(0); setEarlierScrollOffset(0); return; }
    if (input === 'a') { changeAutoScroll(!autoScrollRef.current); return; }

    // RIGHT ARROW POPS OUT WINDOW, LEFT ARROW POPS IT BACK
    if (key.rightArrow) {
      if (!isPoppedOut) {
        const tab = TABS[activeTab] || TABS[0];
        const filtered = allLogsRef.current.filter(e => tab.id === 'all' || e.type === tab.id);
        const maxOff = Math.max(0, filtered.length - MAX_VISIBLE_LOGS);
        const curOff = autoScrollRef.current ? maxOff : Math.min(scrollOffset, maxOff);
        const initEarlier = Math.max(0, curOff - MAX_VISIBLE_LOGS);
        setEarlierScrollOffset(initEarlier);
        earlierScrollOffsetRef.current = initEarlier;
      }
      setIsPoppedOut(true);
      return;
    }
    if (key.leftArrow) {
      setIsPoppedOut(false);
      return;
    }

    // Tabs (1-6)
    if (/^[1-6]$/.test(input)) {
      const targetTab = parseInt(input, 10) - 1;
      if (targetTab >= 0 && targetTab < TABS.length) {
        setActiveTab(targetTab);
        changeAutoScroll(true);
        if (targetTab === 1) { unreadErrorsRef.current = 0; setUnreadErrors(0); setLatestErrorNotification(null); }
      }
      return;
    }

    const filtered = getFilteredLogs();
    const maxOff = Math.max(0, filtered.length - MAX_VISIBLE_LOGS);

    // VIM HJKL SCROLLING FOR POPPED-OUT EARLIER TAB
    if (isPoppedOut) {
      if (input === 'k') {
        setEarlierScrollOffset(prev => Math.max(0, prev - 1));
        return;
      }
      if (input === 'j') {
        setEarlierScrollOffset(prev => Math.min(maxOff, prev + 1));
        return;
      }
      if (input === 'h') {
        setEarlierScrollOffset(prev => Math.max(0, prev - 10));
        return;
      }
      if (input === 'l') {
        setEarlierScrollOffset(prev => Math.min(maxOff, prev + 10));
        return;
      }
    }

    // STANDARD SCROLLING FOR RECENT / SINGLE TAB
    if (key.upArrow || (!isPoppedOut && input === 'k')) {
      changeAutoScroll(false);
      setScrollOffset(prev => Math.max(0, prev - 1));
    }
    if (key.downArrow || (!isPoppedOut && input === 'j')) {
      setScrollOffset(prev => {
        const next = Math.min(maxOff, prev + 1);
        if (next >= maxOff) changeAutoScroll(true);
        return next;
      });
    }
    if (key.pageUp || input === 'u') {
      changeAutoScroll(false);
      setScrollOffset(prev => Math.max(0, prev - 10));
    }
    if (key.pageDown || input === 'd') {
      setScrollOffset(prev => {
        const next = Math.min(maxOff, prev + 10);
        if (next >= maxOff) changeAutoScroll(true);
        return next;
      });
    }
  });

  useEffect(() => {
    const check = checkPatchNotesConflict();
    if (check.hasConflict) {
      setConflictError(check.message);
    } else {
      setPatchNotes(check.notes);
    }
  }, []);

  const getPatchNotesLinesCount = (): number => {
    let count = 0;
    patchNotes.forEach((pn) => {
      count += 1; // Header line
      count += pn.fixes.length; // Fix lines
      count += 1; // Spacer line
    });
    return count;
  };

  const renderPatchNotes = () => {
    if (patchNotes.length === 0) {
      return <Text dimColor>No patch notes found.</Text>;
    }

    const lines: React.ReactNode[] = [];
    patchNotes.forEach((pn) => {
      lines.push(
        <Box key={`header-${pn.version}`} flexDirection="row" justifyContent="space-between">
          <Text color="cyan" bold>v{pn.version} - {pn.title}</Text>
          <Text color="gray">{pn.date}</Text>
        </Box>
      );
      pn.fixes.forEach((fix, idx) => {
        lines.push(
          <Text key={`fix-${pn.version}-${idx}`} color="white">
            {"  "}• {fix}
          </Text>
        );
      });
      lines.push(<Text key={`spacer-${pn.version}`}> </Text>);
    });

    const visibleLines = lines.slice(patchScrollOffset, patchScrollOffset + MAX_VISIBLE_LOGS);
    
    return (
      <Box flexDirection="column" minHeight={isStreamingProd ? 18 : 14}>
        <Box marginBottom={1} flexDirection="row" justifyContent="space-between">
          <Text color="yellow" bold>patch notes (scroll with w/s)</Text>
          <Text color="cyan">[{Math.min(lines.length, patchScrollOffset + 1)}-{Math.min(lines.length, patchScrollOffset + MAX_VISIBLE_LOGS)}/{lines.length}]</Text>
        </Box>
        {visibleLines}
        {visibleLines.length === 0 && <Text dimColor>End of patch notes.</Text>}
      </Box>
    );
  };

  useEffect(() => {
    if (conflictError) return;
    if (activeStep >= STEPS.length) {
      setIsFinished(true);
      setIsStreamingProd(true);
      return;
    }

    setStatuses(prev => { const next = [...prev]; next[activeStep] = 'running'; return next; });

    const step = STEPS[activeStep];
    appendLog(`[exec] ${step.command}`);

    const child = spawn(step.command, { shell: true, cwd: ROOT_DIR });

    child.stdout.on('data', (data) => appendLog(data.toString()));
    child.stderr.on('data', (data) => appendLog(data.toString(), true));

    child.on('close', (code) => {
      if (code === 0) {
        setStatuses(prev => { const next = [...prev]; next[activeStep] = 'success'; return next; });
        setActiveStep(prev => prev + 1);
      } else {
        setStatuses(prev => { const next = [...prev]; next[activeStep] = 'error'; return next; });
        setErrorMsg(`Step "${step.label}" failed with exit code ${code}`);
      }
    });

    return () => { child.kill(); };
  }, [activeStep]);

  useEffect(() => {
    if (!isStreamingProd) return;

    appendLog('--------------------------------------------------');
    appendLog('[info] Stack deployed successfully. Streaming live prod logs...');
    appendLog('--------------------------------------------------');

    const child = spawn('ssh desktop "podman logs -f --tail 100 bpp-bot"', { shell: true, cwd: ROOT_DIR });

    child.stdout.on('data', (data) => appendLog(data.toString()));
    child.stderr.on('data', (data) => appendLog(data.toString(), true));

    child.on('close', (code) => appendLog(`[info] Log stream closed (exit code ${code}).`));

    return () => { child.kill(); };
  }, [isStreamingProd]);

  const getFilteredLogs = () => {
    const tab = TABS[activeTab] || TABS[0];
    return allLogs.filter(entry => {
      if (!entry || !tab) return false;
      if (tab.id !== 'all' && entry.type !== tab.id) return false;
      if (searchQuery && entry.raw) return entry.raw.toLowerCase().includes(searchQuery.toLowerCase());
      return true;
    });
  };

  const filteredLogs = getFilteredLogs();
  const maxOffset = Math.max(0, filteredLogs.length - MAX_VISIBLE_LOGS);
  const currentOffset = autoScroll ? maxOffset : Math.min(scrollOffset, maxOffset);
  const curEarlier = Math.min(earlierScrollOffset, maxOffset);

  const endLine = Math.min(filteredLogs.length, currentOffset + MAX_VISIBLE_LOGS);
  const startLine = filteredLogs.length > 0 ? (currentOffset + 1) : 0;
  const isAtLatest = autoScroll && endLine === filteredLogs.length;
  
  const rangeStr = isAtLatest ? `${startLine}-${endLine}` : `${startLine}-${endLine}/${filteredLogs.length}`;
  const scrollIndicator = filteredLogs.length > 0 ? `[${rangeStr}] [${autoScroll ? chalk.green('AUTO') : chalk.yellow('MANUAL')}]` : '';

  const endEarlier = Math.min(filteredLogs.length, curEarlier + MAX_VISIBLE_LOGS);
  const startEarlier = filteredLogs.length > 0 ? (curEarlier + 1) : 0;
  const earlierIndicator = filteredLogs.length > 0 ? `[${startEarlier}-${endEarlier}/${filteredLogs.length}] [MANUAL]` : '';

  const outerWidth = Math.max(termCols - 2, 80);
  const singleBoxWidth = outerWidth;
  const splitBoxWidth = Math.floor((outerWidth - 3) / 2);

  const visibleMain = isPoppedOut
    ? filteredLogs.slice(curEarlier, curEarlier + MAX_VISIBLE_LOGS)
    : filteredLogs.slice(currentOffset, currentOffset + MAX_VISIBLE_LOGS);

  const visiblePopped = isPoppedOut 
    ? filteredLogs.slice(currentOffset, currentOffset + MAX_VISIBLE_LOGS) 
    : [];

  const shiftedColors = [...GRADIENT_COLORS.slice(gradientTick), ...GRADIENT_COLORS.slice(0, gradientTick)];
  const animatedGradient = gradient(shiftedColors);

  if (conflictError) {
    return (
      <Box flexDirection="column" padding={1} borderStyle="double" borderColor="red" width={outerWidth}>
        <Box flexDirection="row" marginBottom={1}>
          <Text>{animatedGradient(exitFrame >= 0 ? dissolve(CAT_ART, exitFrame) : CAT_ART)}</Text>
          <Box flexDirection="column" justifyContent="center" marginLeft={2}>
            {renderGlitchText('bpp desktop', 'magenta', true)}
            {renderGlitchText('meow', undefined, false, true)}
          </Box>
        </Box>
        <Text color="red" bold>⚠️ PATCH NOTES CONFLICT DETECTED</Text>
        <Box marginY={1}>
          <Text color="white">{conflictError}</Text>
        </Box>
        <Text color="yellow">
          Please resolve the conflicts in tools/deploy-desktop-cli/patch_notes.json, then press 'r' to reload or 'q' to quit.
        </Text>
        <Box marginTop={1}>
          <Text dimColor>[r] Retry/Reload  [q] Quit</Text>
        </Box>
      </Box>
    );
  }

  return (
    <Box flexDirection="column" padding={1} width={outerWidth}>
      {/* Header */}
      <Box flexDirection="row" marginBottom={1}>
        <Text>{animatedGradient(exitFrame >= 0 ? dissolve(CAT_ART, exitFrame) : CAT_ART)}</Text>
        <Box flexDirection="column" justifyContent="center" marginLeft={2}>
          {renderGlitchText('bpp desktop', 'magenta', true)}
          {renderGlitchText('meow', undefined, false, true)}
        </Box>
      </Box>

      {/* Progress Steps / Status */}
      {!isStreamingProd ? (
        <Box flexDirection="column" marginBottom={1}>
          {STEPS.map((step, idx) => {
            let icon = '○'; let textColor = 'gray';
            if (statuses[idx] === 'running') {
              return (
                <Box key={step.id} flexDirection="row">
                  {exitFrame >= 0 ? renderGlitchText('⠋ ', 'cyan') : <Spinner />}
                  {renderGlitchText(step.label, 'cyan', true)}
                </Box>
              );
            } else if (statuses[idx] === 'success') { icon = '✔'; textColor = 'green'; }
            else if (statuses[idx] === 'error') { icon = '✖'; textColor = 'red'; }
            return (
              <Box key={step.id} flexDirection="row">
                {renderGlitchText(icon + ' ', textColor)}
                {renderGlitchText(step.label, textColor)}
              </Box>
            );
          })}
        </Box>
      ) : (
        <Box flexDirection="row" marginBottom={1} justifyContent="space-between" width={outerWidth}>
          <Box flexDirection="row">
            <Text color="green" bold>✔ Stack Deployed </Text>
            <Text dimColor>({STEPS.length} tasks completed)</Text>
          </Box>
          <Text color="green">● LIVE TELEMETRY</Text>
        </Box>
      )}

      {/* Error Notification Banner */}
      {latestErrorNotification && isStreamingProd && (
        <Box borderStyle="single" borderColor="red" paddingX={1} marginBottom={1} width={outerWidth} justifyContent="space-between">
          <Text color="yellow" bold>⚠️ NEW ERROR: {latestErrorNotification.slice(0, 60)}...</Text>
          <Text color="red" bold>[Press 2 to view]</Text>
        </Box>
      )}

      {/* Tab Bar */}
      <Box flexDirection="row" marginBottom={1} borderStyle="single" borderColor="cyan" paddingX={1} width={outerWidth} justifyContent="space-between">
        <Box flexDirection="row">
          <Text bold color="yellow">FILTER: </Text>
          {TABS.map((tab, idx) => {
            const isSelected = activeTab === idx;
            const badge = (tab.id === 'error' && unreadErrors > 0) ? ` (${unreadErrors})` : '';
            const color = isSelected ? 'cyan' : (tab.id === 'error' && unreadErrors > 0 ? 'red' : 'gray');
            return (
              <Box key={tab.id} marginRight={2}>
                <Text color={color} bold={isSelected} underline={isSelected}>
                  [{idx + 1}:{tab.label}{badge}]
                </Text>
              </Box>
            );
          })}
        </Box>
        <Text dimColor>{isPoppedOut ? '[← Collapse Pop-out]' : '[→ Pop Out Window]'}</Text>
      </Box>

      {/* Search Bar */}
      {isSearchMode ? (
        <Box borderStyle="single" borderColor="yellow" paddingX={1} marginBottom={1} width={outerWidth}>
          <Text color="yellow" bold>🔎 Filter: </Text>
          <Text color="white" bold>{searchQuery}</Text>
          <Text color="cyan">█</Text>
          <Text dimColor> (Enter/Esc to exit)</Text>
        </Box>
      ) : searchQuery ? (
        <Box flexDirection="row" marginBottom={1} paddingX={1} width={outerWidth} justifyContent="space-between">
          <Text color="yellow">Active Filter: "{searchQuery}"</Text>
          <Text dimColor>[Press / to change, Esc to clear]</Text>
        </Box>
      ) : null}

      {/* Log Windows Window */}
      {TABS[activeTab].id === 'patches' ? (
        <Box borderStyle="single" borderColor="gray" flexDirection="column" padding={1} minHeight={isStreamingProd ? 18 : 14} width={singleBoxWidth}>
          {renderPatchNotes()}
        </Box>
      ) : (
        <Box flexDirection="row" justifyContent="space-between" width={outerWidth}>
          {/* Main Box */}
          <Box borderStyle="single" borderColor="gray" flexDirection="column" padding={1} minHeight={isStreamingProd ? 18 : 14} width={isPoppedOut ? splitBoxWidth : singleBoxWidth}>
            <Box marginBottom={1} flexDirection="row" justifyContent="space-between">
              <Text color="yellow" bold>{isPoppedOut ? 'logs (earlier)' : 'logs'}</Text>
              <Text color="cyan">{isPoppedOut ? earlierIndicator : scrollIndicator}</Text>
            </Box>
            {visibleMain.map(log => (
              <Text key={log.id} wrap="truncate">
                {exitFrame >= 0 ? dissolve(log.colored, exitFrame) : log.colored}
              </Text>
            ))}
            {visibleMain.length === 0 && <Text dimColor>Buffer ready...</Text>}
          </Box>

          {/* Popped Out Box */}
          {isPoppedOut && (
            <Box borderStyle="single" borderColor="cyan" flexDirection="column" padding={1} minHeight={isStreamingProd ? 18 : 14} width={splitBoxWidth}>
              <Box marginBottom={1} flexDirection="row" justifyContent="space-between">
                <Text color="cyan" bold>popped out logs (recent)</Text>
                <Text color="cyan">{scrollIndicator}</Text>
              </Box>
              {visiblePopped.map(log => (
                <Text key={log.id} wrap="truncate">
                  {exitFrame >= 0 ? dissolve(log.colored, exitFrame) : log.colored}
                </Text>
              ))}
              {visiblePopped.length === 0 && <Text dimColor>No recent logs...</Text>}
            </Box>
          )}
        </Box>
      )}

      {/* Footer Controls */}
      <Box marginTop={1} paddingX={1} flexDirection="row" justifyContent="space-between" width={outerWidth}>
        <Box flexDirection="row">
          <Text dimColor>
            {TABS[activeTab].id === 'patches'
              ? '[1-6] Tabs  [w/s] Scroll Patches  [q] Quit'
              : (isPoppedOut
                  ? '[→/←] Pop out/in  [1-6] Tabs  [↑/↓/u/d] Scroll Recent  [hjkl] Scroll Earlier (j/k: 1 line, h/l: 10 lines)  [/] Filter  [c] Clear  [q] Quit'
                  : '[→] Pop out  [1-6] Tabs  [↑/↓/j/k/u/d] Scroll  [a] Auto-scroll  [/] Filter  [c] Clear  [q] Quit')}
          </Text>
        </Box>
        {renderGlitchText('v2.0.3', 'gray')}
      </Box>

      {/* Alerts */}
      {errorMsg && (
        <Box borderStyle="double" borderColor="red" paddingX={2} paddingY={1} marginTop={1} flexDirection="column" width={outerWidth}>
          {renderGlitchText('✖ CRITICAL DEPLOYMENT FAILURE', 'red', true)}
          {renderGlitchText(errorMsg, 'red')}
          <Box marginTop={1}><Text bold color="yellow">Press any key to exit...</Text></Box>
        </Box>
      )}

      {isFinished && !isStreamingProd && (
        <Box borderStyle="double" borderColor="green" paddingX={2} paddingY={1} marginTop={1} flexDirection="column" width={outerWidth}>
          {renderGlitchText('🎉 DEPLOYMENT COMPLETED SUCCESSFULLY', 'green', true)}
          {renderGlitchText('Container started.', 'green')}
          <Box marginTop={1}><Text bold color="yellow">Press any key to exit...</Text></Box>
        </Box>
      )}
    </Box>
  );
}

render(<DeployCLI />);
