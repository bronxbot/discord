import React, { useState, useEffect, useRef } from 'react';
import { render, Box, Text } from 'ink';
import chalk from 'chalk';
import gradient from 'gradient-string';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import path from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT_DIR = path.resolve(__dirname, '../../');

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
  const [logs, setLogs] = useState<string[]>([]);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [isFinished, setIsFinished] = useState(false);
  const logBoxRef = useRef<string[]>([]);

  // Function to append logs safely
  const appendLog = (data: string) => {
    const lines = data.split('\n').filter(line => line.trim() !== '');
    logBoxRef.current = [...logBoxRef.current, ...lines].slice(-8); // Keep last 8 lines
    setLogs([...logBoxRef.current]);
  };

  useEffect(() => {
    if (activeStep >= STEPS.length) {
      setIsFinished(true);
      return;
    }

    // Mark current step as running
    setStatuses(prev => {
      const next = [...prev];
      next[activeStep] = 'running';
      return next;
    });

    const step = STEPS[activeStep];
    appendLog(`[exec] ${step.command}`);

    // Spawn the shell command relative to the project root directory
    const child = spawn(step.command, { shell: true, cwd: ROOT_DIR });

    child.stdout.on('data', (data) => {
      appendLog(data.toString());
    });

    child.stderr.on('data', (data) => {
      appendLog(chalk.yellow(data.toString()));
    });

    child.on('close', (code) => {
      if (code === 0) {
        setStatuses(prev => {
          const next = [...prev];
          next[activeStep] = 'success';
          return next;
        });
        setActiveStep(prev => prev + 1);
      } else {
        setStatuses(prev => {
          const next = [...prev];
          next[activeStep] = 'error';
          return next;
        });
        setErrorMsg(`Step "${step.label}" failed with exit code ${code}`);
      }
    });

    return () => {
      child.kill();
    };
  }, [activeStep]);

  // Color gradient for the header
  const neonTheme = gradient(['#ff007f', '#7f00ff', '#00f0ff']);

  return (
    <Box flexDirection="column" padding={1}>
      {/* Header */}
      <Box flexDirection="row" marginBottom={1}>
        <Text>{neonTheme(CAT_ART)}</Text>
        <Box flexDirection="column" justifyContent="center" marginLeft={2}>
          <Text bold color="magenta">BPP DESKTOP DEPLOYER v2.0</Text>
          <Text dimColor>overengineered interactive podman deployment</Text>
        </Box>
      </Box>

      {/* Legend */}
      <Box borderStyle="round" borderColor="cyan" paddingX={2} marginBottom={1} flexDirection="row">
        <Text bold color="cyan">Legend: </Text>
        <Text color="gray">  ○ Pending  </Text>
        <Text color="cyan">  ⠋ Running  </Text>
        <Text color="green">  ✔ Success  </Text>
        <Text color="red">  ✖ Error    </Text>
      </Box>

      {/* Progress Steps */}
      <Box flexDirection="column" marginBottom={1}>
        {STEPS.map((step, idx) => {
          let icon = '○';
          let textColor = 'gray';

          if (statuses[idx] === 'running') {
            return (
              <Box key={step.id} flexDirection="row">
                <Spinner />
                <Text color="cyan" bold>{step.label}</Text>
              </Box>
            );
          } else if (statuses[idx] === 'success') {
            icon = '✔';
            textColor = 'green';
          } else if (statuses[idx] === 'error') {
            icon = '✖';
            textColor = 'red';
          }

          return (
            <Box key={step.id} flexDirection="row">
              <Text color={textColor}>{icon} </Text>
              <Text color={textColor}>{step.label}</Text>
            </Box>
          );
        })}
      </Box>

      {/* Terminal logs window */}
      <Box borderStyle="single" borderColor="gray" flexDirection="column" padding={1} minHeight={10} width={80}>
        <Box marginBottom={1}>
          <Text bold color="yellow">Log Stream:</Text>
        </Box>
        {logs.map((log, index) => (
          <Text key={index} wrap="truncate">
            {log}
          </Text>
        ))}
        {logs.length === 0 && <Text color="gray">Initializing log buffer...</Text>}
      </Box>

      {/* Footer / Alerts */}
      {errorMsg && (
        <Box borderStyle="double" borderColor="red" paddingX={2} paddingY={1} marginTop={1} flexDirection="column">
          <Text bold color="red">✖ CRITICAL DEPLOYMENT FAILURE</Text>
          <Text color="red">{errorMsg}</Text>
        </Box>
      )}

      {isFinished && (
        <Box borderStyle="double" borderColor="green" paddingX={2} paddingY={1} marginTop={1} flexDirection="column" width={80}>
          <Text bold color="green">🎉 DEPLOYMENT COMPLETED SUCCESSFULLY</Text>
          <Text color="green">Container refreshed and started on Desktop.</Text>
          <Text color="gray">Monitor logs with: ssh desktop 'podman logs -f bpp-bot'</Text>
        </Box>
      )}
    </Box>
  );
}

render(<DeployCLI />);
