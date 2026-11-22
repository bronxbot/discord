import React, { useState, useRef, useEffect } from 'react';

/**
 * Custom SVG Area Chart with glowing gradients, interactive tooltips, gridlines, and crosshairs
 */
export function AreaChart({ data = [], height = 240, color = 'indigo' }) {
  const [hoveredIndex, setHoveredIndex] = useState(null);
  const [tooltipPos, setTooltipPos] = useState({ x: 0, y: 0 });
  const svgRef = useRef(null);

  if (!data || data.length === 0) {
    return (
      <div className="flex items-center justify-center border border-white/5 bg-white/2 backdrop-blur-md rounded-2xl" style={{ height }}>
        <p className="text-white/40 text-sm">No activity data available</p>
      </div>
    );
  }

  // Find bounds
  const values = data.map(d => d.value || 0);
  const maxValue = Math.max(...values, 10);
  const minValue = 0;
  const range = maxValue - minValue || 1;

  const padding = { top: 20, right: 20, bottom: 30, left: 50 };
  const width = 800; // Static SVG coordinate space width, scaled responsively

  // Map coordinates
  const points = data.map((d, index) => {
    const x = data.length > 1
      ? padding.left + (index / (data.length - 1)) * (width - padding.left - padding.right)
      : padding.left + (width - padding.left - padding.right) / 2;
    const y = height - padding.bottom - ((d.value - minValue) / range) * (height - padding.top - padding.bottom);
    return { x, y, label: d.label, value: d.value };
  });

  // SVG Path generation
  let linePath = '';
  let areaPath = '';

  if (points.length > 0) {
    linePath = `M ${points[0].x} ${points[0].y} ` + points.slice(1).map(p => `L ${p.x} ${p.y}`).join(' ');
    areaPath = `${linePath} L ${points[points.length - 1].x} ${height - padding.bottom} L ${points[0].x} ${height - padding.bottom} Z`;
  }

  // Dynamic colors based on prop
  const colors = {
    indigo: {
      stroke: '#6366f1',
      glow: 'rgba(99, 102, 241, 0.4)',
      gradientStart: 'rgba(99, 102, 241, 0.3)',
      gradientEnd: 'rgba(99, 102, 241, 0.0)'
    },
    emerald: {
      stroke: '#10b981',
      glow: 'rgba(16, 185, 129, 0.4)',
      gradientStart: 'rgba(16, 185, 129, 0.3)',
      gradientEnd: 'rgba(16, 185, 129, 0.0)'
    },
    purple: {
      stroke: '#a855f7',
      glow: 'rgba(168, 85, 247, 0.4)',
      gradientStart: 'rgba(168, 85, 247, 0.3)',
      gradientEnd: 'rgba(168, 85, 247, 0.0)'
    },
    rose: {
      stroke: '#f43f5e',
      glow: 'rgba(244, 63, 94, 0.4)',
      gradientStart: 'rgba(244, 63, 94, 0.3)',
      gradientEnd: 'rgba(244, 63, 94, 0.0)'
    }
  };

  const activeColor = colors[color] || colors.indigo;

  // Handle SVG Mouse Movement for interactive tooltip
  const handleMouseMove = (e) => {
    if (!svgRef.current) return;
    const rect = svgRef.current.getBoundingClientRect();
    const mouseX = ((e.clientX - rect.left) / rect.width) * width;
    
    // Find closest point based on X coordinate
    let closestIndex = 0;
    let minDiff = Infinity;

    points.forEach((p, idx) => {
      const diff = Math.abs(p.x - mouseX);
      if (diff < minDiff) {
        minDiff = diff;
        closestIndex = idx;
      }
    });

    setHoveredIndex(closestIndex);
    
    // Position the tooltip dynamically
    const tooltipX = (points[closestIndex].x / width) * rect.width;
    const tooltipY = (points[closestIndex].y / height) * rect.height - 50;
    setTooltipPos({ x: tooltipX, y: tooltipY });
  };

  const handleMouseLeave = () => {
    setHoveredIndex(null);
  };

  // Generate gridlines
  const gridLines = [];
  const gridCount = 4;
  for (let i = 0; i <= gridCount; i++) {
    const ratio = i / gridCount;
    const y = padding.top + ratio * (height - padding.top - padding.bottom);
    const value = Math.round(maxValue - ratio * range);
    gridLines.push({ y, value });
  }

  const formatNumber = (num) => {
    if (num >= 1000000) return `${(num / 1000000).toFixed(1).replace(/\.0$/, '')}M`;
    if (num >= 1000) return `${(num / 1000).toFixed(1).replace(/\.0$/, '')}K`;
    return num.toLocaleString();
  };

  return (
    <div className="relative w-full">
      <svg
        ref={svgRef}
        className="w-full overflow-visible select-none"
        style={{ height }}
        onMouseMove={handleMouseMove}
        onMouseLeave={handleMouseLeave}
        viewBox={`0 0 ${width} ${height}`}
        preserveAspectRatio="none"
      >
        <defs>
          <linearGradient id={`area-grad-${color}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor={activeColor.stroke} stopOpacity="0.4" />
            <stop offset="100%" stopColor={activeColor.stroke} stopOpacity="0.0" />
          </linearGradient>
          <filter id={`glow-${color}`} x="-10%" y="-10%" width="120%" height="120%">
            <feGaussianBlur stdDeviation="6" result="blur" />
            <feComposite in="SourceGraphic" in2="blur" operator="over" />
          </filter>
        </defs>

        {/* X and Y Grid Lines */}
        {gridLines.map((line, idx) => (
          <g key={idx} className="opacity-40">
            <line
              x1={padding.left}
              y1={line.y}
              x2={width - padding.right}
              y2={line.y}
              stroke="rgba(255, 255, 255, 0.08)"
              strokeDasharray="4 4"
              strokeWidth="1"
            />
            <text
              x={padding.left - 10}
              y={line.y + 4}
              fill="rgba(255, 255, 255, 0.4)"
              fontSize="10"
              fontFamily="sans-serif"
              textAnchor="end"
            >
              {formatNumber(line.value)}
            </text>
          </g>
        ))}

        {/* X Axis labels */}
        {points.map((p, idx) => {
          // Label thinning for neatness
          const showLabel = idx === 0 || idx === points.length - 1 || idx === Math.floor(points.length / 2) || (points.length < 10 && idx % 2 === 0);
          if (!showLabel) return null;
          return (
            <text
              key={idx}
              x={p.x}
              y={height - 10}
              fill="rgba(255, 255, 255, 0.4)"
              fontSize="10"
              fontFamily="sans-serif"
              textAnchor="middle"
              className="transition-all duration-300"
            >
              {p.label}
            </text>
          );
        })}

        {/* Area Gradient Fill */}
        <path d={areaPath} fill={`url(#area-grad-${color})`} className="transition-all duration-500 ease-out" />

        {/* Glowing Main Path Line */}
        <path
          d={linePath}
          fill="none"
          stroke={activeColor.stroke}
          strokeWidth="2.5"
          filter={`url(#glow-${color})`}
          className="transition-all duration-500 ease-out"
        />

        {/* Hover Crosshair & Selection Highlight */}
        {hoveredIndex !== null && (
          <g>
            <line
              x1={points[hoveredIndex].x}
              y1={padding.top}
              x2={points[hoveredIndex].x}
              y2={height - padding.bottom}
              stroke="rgba(255, 255, 255, 0.15)"
              strokeDasharray="2 2"
              strokeWidth="1.5"
            />
            <circle
              cx={points[hoveredIndex].x}
              cy={points[hoveredIndex].y}
              r="6"
              fill={activeColor.stroke}
              stroke="#ffffff"
              strokeWidth="2"
              className="animate-pulse shadow-lg"
              style={{ filter: `drop-shadow(0 0 8px ${activeColor.stroke})` }}
            />
          </g>
        )}
      </svg>

      {/* Floating HTML Tooltip overlay */}
      {hoveredIndex !== null && points[hoveredIndex] && (
        <div
          className="absolute z-30 pointer-events-none transform -translate-x-1/2 flex flex-col items-center bg-[#15122b]/90 border border-white/10 shadow-2xl rounded-xl px-3 py-2 text-xs text-white backdrop-blur-md transition-all duration-100"
          style={{
            left: `${(points[hoveredIndex].x / width) * 100}%`,
            top: `${tooltipPos.y}px`,
          }}
        >
          <div className="absolute -bottom-1.5 left-1/2 transform -translate-x-1/2 w-3 h-3 rotate-45 bg-[#15122b] border-r border-b border-white/10"></div>
          <span className="font-semibold text-white/50 mb-0.5">{points[hoveredIndex].label}</span>
          <span className="text-sm font-bold text-transparent bg-clip-text bg-gradient-to-r from-purple-400 to-indigo-300">
            {points[hoveredIndex].value.toLocaleString()}
          </span>
        </div>
      )}
    </div>
  );
}

/**
 * Premium Rounded Bar Chart
 */
export function BarChart({ data = [], height = 240, color = 'purple' }) {
  const [hoveredIdx, setHoveredIdx] = useState(null);
  
  if (!data || data.length === 0) {
    return (
      <div className="flex items-center justify-center border border-white/5 bg-white/2 backdrop-blur-md rounded-2xl" style={{ height }}>
        <p className="text-white/40 text-sm">No statistics available</p>
      </div>
    );
  }

  const values = data.map(d => d.value || 0);
  const maxValue = Math.max(...values, 10);
  const padding = { top: 20, right: 10, bottom: 30, left: 45 };
  const width = 600;

  const barWidth = Math.max(((width - padding.left - padding.right) / data.length) * 0.6, 12);
  const spacing = ((width - padding.left - padding.right) - barWidth * data.length) / (data.length - 1 || 1);

  const colors = {
    purple: {
      fill: 'url(#bar-grad-purple)',
      stroke: '#c084fc',
      hover: 'rgba(192, 132, 252, 0.2)'
    },
    emerald: {
      fill: 'url(#bar-grad-emerald)',
      stroke: '#34d399',
      hover: 'rgba(52, 211, 153, 0.2)'
    },
    indigo: {
      fill: 'url(#bar-grad-indigo)',
      stroke: '#818cf8',
      hover: 'rgba(129, 140, 248, 0.2)'
    }
  };

  const activeColor = colors[color] || colors.purple;

  return (
    <div className="relative w-full">
      <svg
        className="w-full overflow-visible select-none"
        style={{ height }}
        viewBox={`0 0 ${width} ${height}`}
        preserveAspectRatio="none"
      >
        <defs>
          <linearGradient id="bar-grad-purple" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#a855f7" />
            <stop offset="100%" stopColor="#6366f1" />
          </linearGradient>
          <linearGradient id="bar-grad-emerald" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#10b981" />
            <stop offset="100%" stopColor="#059669" />
          </linearGradient>
          <linearGradient id="bar-grad-indigo" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="#6366f1" />
            <stop offset="100%" stopColor="#4f46e5" />
          </linearGradient>
        </defs>

        {/* Y Gridlines */}
        {[0, 1, 2, 3].map((g, idx) => {
          const ratio = g / 3;
          const y = padding.top + ratio * (height - padding.top - padding.bottom);
          const value = Math.round(maxValue - ratio * maxValue);
          return (
            <g key={idx} className="opacity-40">
              <line
                x1={padding.left}
                y1={y}
                x2={width - padding.right}
                y2={y}
                stroke="rgba(255, 255, 255, 0.05)"
                strokeWidth="1"
              />
              <text
                x={padding.left - 8}
                y={y + 3}
                fill="rgba(255, 255, 255, 0.4)"
                fontSize="10"
                textAnchor="end"
              >
                {value.toLocaleString()}
              </text>
            </g>
          );
        })}

        {/* Bar Rects */}
        {data.map((item, idx) => {
          const x = padding.left + idx * (barWidth + spacing);
          const barHeight = ((item.value || 0) / maxValue) * (height - padding.top - padding.bottom);
          const y = height - padding.bottom - barHeight;

          return (
            <g
              key={idx}
              className="cursor-pointer"
              onMouseEnter={() => setHoveredIdx(idx)}
              onMouseLeave={() => setHoveredIdx(null)}
            >
              {/* Highlight backdrop behind bar */}
              {hoveredIdx === idx && (
                <rect
                  x={x - spacing / 2}
                  y={padding.top}
                  width={barWidth + spacing}
                  height={height - padding.top - padding.bottom}
                  fill={activeColor.hover}
                  rx="6"
                  className="transition-all duration-300"
                />
              )}

              {/* Core Bar */}
              <rect
                x={x}
                y={y}
                width={barWidth}
                height={Math.max(barHeight, 2)}
                fill={activeColor.fill}
                rx={Math.min(barWidth / 2, 4)}
                className="transition-all duration-500 ease-out"
              />

              {/* Bar Top Glow Edge */}
              {barHeight > 5 && (
                <line
                  x1={x}
                  y1={y}
                  x2={x + barWidth}
                  y2={y}
                  stroke={activeColor.stroke}
                  strokeWidth="2"
                />
              )}

              {/* Label underneath */}
              {idx % Math.ceil(data.length / 8) === 0 && (
                <text
                  x={x + barWidth / 2}
                  y={height - 8}
                  fill="rgba(255, 255, 255, 0.4)"
                  fontSize="9"
                  textAnchor="middle"
                  className="font-mono"
                >
                  {item.label && item.label.length > 8 ? `${item.label.substring(0, 7)}…` : item.label}
                </text>
              )}
            </g>
          );
        })}
      </svg>

      {/* Bar Tooltip overlay */}
      {hoveredIdx !== null && data[hoveredIdx] && (
        <div
          className="absolute z-20 pointer-events-none transform -translate-x-1/2 flex flex-col items-center bg-[#181533]/90 border border-white/10 shadow-xl rounded-xl px-2.5 py-1.5 text-xs text-white backdrop-blur-md transition-all duration-100"
          style={{
            left: `${((padding.left + hoveredIdx * (barWidth + spacing) + barWidth / 2) / width) * 100}%`,
            top: `${height - padding.bottom - ((data[hoveredIdx].value || 0) / maxValue) * (height - padding.top - padding.bottom) - 45}px`,
          }}
        >
          <div className="absolute -bottom-1 left-1/2 transform -translate-x-1/2 w-2 h-2 rotate-45 bg-[#181533] border-r border-b border-white/10"></div>
          <span className="font-semibold text-white/50 mb-0.5">{data[hoveredIdx].label}</span>
          <span className="text-xs font-bold text-indigo-400">
            {data[hoveredIdx].value.toLocaleString()}
          </span>
        </div>
      )}
    </div>
  );
}

/**
 * State-of-the-Art Activity Heatmap Grid
 * Visualizes 24 hours x 7 days grids
 */
export function ActivityHeatmap({ data = [], height = 180 }) {
  const [hoveredCell, setHoveredCell] = useState(null);
  const containerRef = useRef(null);

  const days = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'];
  const hours = Array.from({ length: 24 }, (_, i) => i);

  // Normalize heat density
  const is2DMatrix = Array.isArray(data) && Array.isArray(data[0]);
  const cellCounts = is2DMatrix
    ? data.flatMap(row => row || [])
    : data.map(cell => cell.count || 0);
  const maxCount = Math.max(...cellCounts, 5);

  const getColorIntensity = (count) => {
    if (!count) return 'bg-white/2 hover:bg-white/10 border-white/3';
    const ratio = count / maxCount;
    if (ratio < 0.15) return 'bg-purple-950/40 hover:bg-purple-900/60 border-purple-800/10 shadow-sm';
    if (ratio < 0.4) return 'bg-purple-800/60 hover:bg-purple-700/80 border-purple-600/20';
    if (ratio < 0.7) return 'bg-indigo-600/80 hover:bg-indigo-500/90 border-indigo-400/20 shadow-md';
    return 'bg-pink-500 hover:bg-pink-400 border-pink-400/30 shadow-[0_0_12px_rgba(236,72,153,0.5)]';
  };

  // Safe fetch function for count
  const findCell = (dayIdx, hourIdx) => {
    if (is2DMatrix && Array.isArray(data[dayIdx])) {
      return { count: data[dayIdx][hourIdx] || 0 };
    }
    if (Array.isArray(data)) {
      return data.find(c => c.day === dayIdx && c.hour === hourIdx) || { count: 0 };
    }
    return { count: 0 };
  };

  const handleCellHover = (e, cellData, day, hour) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const parentRect = containerRef.current.getBoundingClientRect();
    setHoveredCell({
      count: cellData.count,
      day,
      hour,
      x: rect.left - parentRect.left + rect.width / 2,
      y: rect.top - parentRect.top - 48
    });
  };

  return (
    <div ref={containerRef} className="relative w-full border border-white/5 bg-white/2 backdrop-blur-xl rounded-2xl pt-14 pb-5 px-6 select-none overflow-x-auto">
      <div className="min-w-[640px] flex flex-col gap-2">
        
        {/* Heatmap header timeline */}
        <div className="flex pl-10 text-[9px] font-mono text-white/30 uppercase tracking-widest justify-between mb-1 pr-2">
          <span>12 am</span>
          <span>4 am</span>
          <span>8 am</span>
          <span>12 pm</span>
          <span>4 pm</span>
          <span>8 pm</span>
          <span>11 pm</span>
        </div>

        {/* Heatmap Grid Row by Row */}
        {days.map((dayName, dayIdx) => (
          <div key={dayIdx} className="flex items-center gap-1">
            <span className="w-8 text-[11px] font-bold text-white/40 font-sans mr-2">{dayName}</span>
            <div className="flex-1 grid grid-cols-24 gap-1">
              {hours.map((hourVal) => {
                const cell = findCell(dayIdx, hourVal);
                return (
                  <div
                      key={hourVal}
                      className={`aspect-square rounded border transition-all duration-300 cursor-pointer ${getColorIntensity(cell.count)}`}
                      onMouseEnter={(e) => handleCellHover(e, cell, dayName, hourVal)}
                      onMouseLeave={() => setHoveredCell(null)}
                  />
                );
              })}
            </div>
          </div>
        ))}
      </div>

      {/* Floating Hover Details overlay */}
      {hoveredCell && (
        <div
          className="absolute z-20 pointer-events-none transform -translate-x-1/2 flex flex-col items-center bg-[#1d193d]/95 border border-white/10 shadow-2xl rounded-xl px-3 py-1.5 text-xs text-white backdrop-blur-md"
          style={{
            left: `${hoveredCell.x}px`,
            top: `${hoveredCell.y}px`
          }}
        >
          <div className="absolute -bottom-1 left-1/2 transform -translate-x-1/2 w-2 h-2 rotate-45 bg-[#1d193d] border-r border-b border-white/10"></div>
          <span className="font-semibold text-white/50 text-[10px]">{hoveredCell.day} &bull; {hoveredCell.hour > 12 ? hoveredCell.hour - 12 : hoveredCell.hour === 0 ? 12 : hoveredCell.hour} {hoveredCell.hour >= 12 ? 'pm' : 'am'}</span>
          <span className="text-[11px] font-bold text-pink-400">
            {hoveredCell.count.toLocaleString()} messages
          </span>
        </div>
      )}
    </div>
  );
}
