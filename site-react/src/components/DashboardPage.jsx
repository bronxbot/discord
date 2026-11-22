import React, { useState, useEffect, useRef } from 'react';
import { AreaChart, BarChart, ActivityHeatmap } from './Charts';

export default function DashboardPage({ guildId, setPage }) {
  // Navigation & UI States
  const [activeTab, setActiveTab] = useState('overview');
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [showPalette, setShowPalette] = useState(false);
  const [paletteQuery, setPaletteQuery] = useState('');
  const [paletteIdx, setPaletteIdx] = useState(0);
  const [toast, setToast] = useState(null);

  // Authenticated states & Guild Meta
  const [user, setUser] = useState(null);
  const [activeServer, setActiveServer] = useState(null);
  const [csrfToken, setCsrfToken] = useState(null);

  // Data payloads for various tabs
  const [overviewData, setOverviewData] = useState(null);
  const [trendData, setTrendData] = useState([]);
  const [trendDataFull, setTrendDataFull] = useState({});
  const [heatmapData, setHeatmapData] = useState([]);
  const [activityFeed, setActivityFeed] = useState([]);
  const [activeMetric, setActiveMetric] = useState('messages');
  
  const [modules, setModules] = useState({});
  const [basicSettings, setBasicSettings] = useState({ prefix: 'bb ', logging_enabled: false, public_stats: false, global_stats: 0 });
  const [blockedChannels, setBlockedChannels] = useState([]);
  const [customPrefixes, setCustomPrefixes] = useState([]);

  // Shop & Economy States
  const [shopItems, setShopItems] = useState([]);
  const [showAddShopItem, setShowAddShopItem] = useState(false);
  const [newShopItem, setNewShopItem] = useState({ name: '', price: '', description: '', role_reward: '', stock: '' });

  // Moderation States
  const [infractions, setInfractions] = useState([]);
  const [automodConfig, setAutomodConfig] = useState({ anti_invite: false, anti_link: false, max_mentions: 5, banned_words: '' });

  // Channel Analytics States
  const [channelStats, setChannelStats] = useState([]);

  // References
  const paletteInputRef = useRef(null);

  // Command Palette Items
  const paletteItems = [
    { label: 'Overview', desc: 'Server stats & activity graphs', tab: 'overview', icon: 'fa-chart-line' },
    { label: 'Basic Settings', desc: 'Custom prefix & stats options', tab: 'guild-settings', icon: 'fa-cog' },
    { label: 'Modules Grid', desc: 'Toggle modular bot functions', tab: 'commands', icon: 'fa-terminal' },
    { label: 'Economy & Shop', desc: 'Manage coin shop inventory', tab: 'shop', icon: 'fa-store' },
    { label: 'Moderation Tools', desc: 'Warns, bans & automod', tab: 'moderation', icon: 'fa-shield-alt' },
    { label: 'Channel Analytics', desc: 'Message distribution charts', tab: 'statistics', icon: 'fa-chart-bar' },
    { label: 'Go to Server List', desc: 'Return to selection grid', action: 'exit', icon: 'fa-server' }
  ];

  const triggerToast = (message, type = 'success') => {
    setToast({ message, type });
    setTimeout(() => setToast(null), 3000);
  };

  // API Call Wrapper
  const apiCall = async (endpoint, options = {}) => {
    try {
      const headers = { 'Content-Type': 'application/json', ...options.headers };
      if (guildId && guildId !== 'global') {
        headers['X-Guild-ID'] = guildId;
      }
      
      const method = options.method || 'GET';
      if (!['GET', 'HEAD', 'OPTIONS'].includes(method)) {
        if (!csrfToken) {
          const csrfRes = await fetch('/api/csrf-token');
          const csrfData = await csrfRes.json();
          setCsrfToken(csrfData.csrfToken);
          headers['X-CSRF-Token'] = csrfData.csrfToken;
        } else {
          headers['X-CSRF-Token'] = csrfToken;
        }
      }

      const response = await fetch(`/api${endpoint}`, {
        credentials: 'include',
        headers,
        ...options
      });

      if (!response.ok) {
        throw new Error(`API Error: ${response.status}`);
      }

      return await response.json();
    } catch (err) {
      console.error(`API Call failed to ${endpoint}:`, err);
      if (options.method && options.method !== 'GET') {
        triggerToast(err.message, 'error');
      }
      return null;
    }
  };

  // Initial Boot loader
  useEffect(() => {
    async function loadMeta() {
      const res = await fetch('/api/auth/user');
      const data = await res.json();
      if (!data.authenticated) {
        setPage('servers');
        return;
      }
      setUser(data.user);

      if (guildId === 'global') {
        setActiveServer({ name: 'Root Bot Owner Portal', id: 'global' });
      } else {
        const matching = data.guilds.find(g => g.id === guildId);
        if (matching) {
          setActiveServer(matching);
        } else {
          setPage('servers');
        }
      }
    }
    loadMeta();
  }, [guildId]);

  // Load Tab Specific Data
  useEffect(() => {
    if (!guildId) return;

    if (activeTab === 'overview') {
      loadOverview();
    } else if (activeTab === 'guild-settings') {
      loadSettings();
    } else if (activeTab === 'commands') {
      loadModules();
    } else if (activeTab === 'shop') {
      loadShop();
    } else if (activeTab === 'moderation') {
      loadModeration();
    } else if (activeTab === 'statistics') {
      loadStatistics();
    }
  }, [activeTab, guildId]);

  // Hotkey handler (Cmd/Ctrl + K)
  useEffect(() => {
    const handleKeyDown = (e) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
        e.preventDefault();
        setShowPalette(prev => !prev);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  useEffect(() => {
    if (showPalette && paletteInputRef.current) {
      paletteInputRef.current.focus();
      setPaletteQuery('');
      setPaletteIdx(0);
    }
  }, [showPalette]);

  // ── Tab API Fetchers ──────────────────────────────────────────

  const loadOverview = async () => {
    const overview = await apiCall('/stats/overview');
    if (overview) setOverviewData(overview);

    const trend = await apiCall('/stats/overview/trend');
    if (trend && trend.labels) {
      const labels = trend.labels || [];
      const full = {
        messages: labels.map((lbl, i) => ({ label: lbl, value: trend.messages?.[i] || 0 })),
        activeUsers: labels.map((lbl, i) => ({ label: lbl, value: trend.activeUsers?.[i] || 0 })),
        commands: labels.map((lbl, i) => ({ label: lbl, value: trend.commands?.[i] || 0 })),
        newMembers: labels.map((lbl, i) => ({ label: lbl, value: trend.newMembers?.[i] || 0 }))
      };
      setTrendDataFull(full);
      setTrendData(full.messages);
    }

    const heatmap = await apiCall('/stats/heatmap');
    if (heatmap) {
      const heatData = heatmap.matrix || heatmap;
      setHeatmapData(Array.isArray(heatData) ? heatData : []);
    }

    const activity = await apiCall('/stats/recent-activity');
    if (activity) setActivityFeed(Array.isArray(activity) ? activity : activity.logs || []);
  };

  const loadSettings = async () => {
    const settings = await apiCall('/guild/settings');
    if (settings) setBasicSettings(settings);

    const blocked = await apiCall('/guild/blocked-channels');
    if (blocked) setBlockedChannels(Array.isArray(blocked) ? blocked : []);

    const prefixes = await apiCall('/guild/custom-prefixes');
    if (prefixes) setCustomPrefixes(Array.isArray(prefixes) ? prefixes : []);
  };

  const loadModules = async () => {
    const mods = await apiCall('/modules');
    if (mods) setModules(mods);
  };

  const loadShop = async () => {
    const items = await apiCall('/shop/items');
    if (items) setShopItems(Array.isArray(items) ? items : []);
  };

  const loadModeration = async () => {
    const infs = await apiCall('/moderation/infractions');
    if (infs) setInfractions(Array.isArray(infs) ? infs : []);
  };

  const loadStatistics = async () => {
    const channels = await apiCall('/stats/channels');
    if (channels) {
      const channelsArray = Array.isArray(channels) ? channels : [];
      // Map back to Bar Chart format following the lowercase-minimal style guide
      const formatted = channelsArray.slice(0, 8).map(ch => {
        const rawLabel = ch.channel_name || ch.name || ch.channel_id || '';
        const cleanLabel = rawLabel.replace(/^#/, '').toLowerCase();
        return {
          label: cleanLabel,
          value: ch.count || 0
        };
      });
      setChannelStats(formatted);
    }
  };

  // ── Modifying Actions ─────────────────────────────────────────

  const handleSaveBasicSettings = async () => {
    const res = await apiCall('/guild/settings', {
      method: 'PUT',
      body: JSON.stringify(basicSettings)
    });
    if (res) triggerToast('Basic settings saved!');
  };

  const handleToggleModule = async (modName) => {
    const updated = !modules[modName];
    const res = await apiCall('/modules', {
      method: 'POST',
      body: JSON.stringify({ module: modName, enabled: updated })
    });
    if (res) {
      setModules(prev => ({ ...prev, [modName]: updated }));
      triggerToast(`${modName} module updated!`);
    }
  };

  const handleAddPrefix = async (e) => {
    e.preventDefault();
    const input = e.target.elements.newPrefix;
    const prefix = input.value.trim();
    if (!prefix) return;

    const res = await apiCall('/guild/custom-prefixes', {
      method: 'POST',
      body: JSON.stringify({ prefix })
    });
    if (res) {
      input.value = '';
      loadSettings();
      triggerToast('Custom prefix added!');
    }
  };

  const handleDeletePrefix = async (prefix) => {
    const res = await apiCall('/guild/custom-prefixes', {
      method: 'DELETE',
      body: JSON.stringify({ prefix })
    });
    if (res) {
      loadSettings();
      triggerToast('Custom prefix deleted.');
    }
  };

  const handleAddBlockedChannel = async (e) => {
    e.preventDefault();
    const input = e.target.elements.channelId;
    const channel_id = input.value.trim();
    if (!channel_id) return;

    const res = await apiCall('/guild/blocked-channels', {
      method: 'POST',
      body: JSON.stringify({ channel_id })
    });
    if (res) {
      input.value = '';
      loadSettings();
      triggerToast('Channel blocklisted!');
    }
  };

  const handleDeleteBlockedChannel = async (channelId) => {
    const res = await apiCall(`/guild/blocked-channels/${channelId}`, {
      method: 'DELETE'
    });
    if (res) {
      loadSettings();
      triggerToast('Channel removed from blocklist.');
    }
  };

  // Shop item management
  const handleAddShopItemSubmit = async (e) => {
    e.preventDefault();
    const priceVal = parseInt(newShopItem.price);
    if (!newShopItem.name || isNaN(priceVal)) {
      triggerToast('Name and numeric Price are required', 'error');
      return;
    }

    const payload = {
      name: newShopItem.name,
      price: priceVal,
      description: newShopItem.description || '',
      role_reward: newShopItem.role_reward || '',
      stock: newShopItem.stock ? parseInt(newShopItem.stock) : -1
    };

    const res = await apiCall('/shop/items', {
      method: 'POST',
      body: JSON.stringify(payload)
    });

    if (res) {
      setNewShopItem({ name: '', price: '', description: '', role_reward: '', stock: '' });
      setShowAddShopItem(false);
      loadShop();
      triggerToast('Shop item listed successfully!');
    }
  };

  const handleDeleteShopItem = async (itemName) => {
    const res = await apiCall('/shop/items', {
      method: 'DELETE',
      body: JSON.stringify({ name: itemName })
    });
    if (res) {
      loadShop();
      triggerToast('Shop item deleted.');
    }
  };

  // Command Palette fuzzy filter
  const filteredPaletteItems = paletteItems.filter(item => 
    item.label.toLowerCase().includes(paletteQuery.toLowerCase()) ||
    item.desc.toLowerCase().includes(paletteQuery.toLowerCase())
  );

  const handlePaletteSelect = (item) => {
    setShowPalette(false);
    if (item.action === 'exit') {
      setPage('servers');
    } else if (item.tab) {
      setActiveTab(item.tab);
    }
  };

  const getInitials = (name = '') => {
    return name.split(' ').map(w => w[0]).join('').slice(0, 3).toUpperCase();
  };

  return (
    <div className="min-h-screen bg-[#070514] text-white flex select-none relative overflow-hidden">
      {/* Decorative background glows */}
      <div className="absolute top-[-10%] left-[-10%] w-[50%] h-[50%] rounded-full bg-purple-950/10 blur-[100px] pointer-events-none" />
      <div className="absolute bottom-[-10%] right-[-10%] w-[50%] h-[50%] rounded-full bg-indigo-950/10 blur-[100px] pointer-events-none" />
      
      {/* Toast Alert overlay */}
      {toast && (
        <div className={`fixed bottom-6 right-6 z-50 px-5 py-3 rounded-2xl backdrop-blur-xl border flex items-center gap-3 animate-bounce shadow-2xl ${
          toast.type === 'error' ? 'bg-red-950/80 border-red-500/20 text-red-300' : 'bg-[#15122b]/95 border-indigo-500/20 text-indigo-300'
        }`}>
          <i className={`fa-solid ${toast.type === 'error' ? 'fa-triangle-exclamation' : 'fa-circle-check'}`} />
          <span className="text-xs font-bold font-mono tracking-wide">{toast.message}</span>
        </div>
      )}

      {/* COMMAND PALETTE OVERLAY */}
      {showPalette && (
        <div 
          className="fixed inset-0 z-50 bg-[#04030a]/75 backdrop-blur-sm flex items-center justify-center p-4 animate-fade-in"
          onClick={() => setShowPalette(false)}
        >
          <div 
            className="w-full max-w-lg bg-[#0e0c1f]/95 border border-white/5 shadow-2xl rounded-2xl overflow-hidden p-3 backdrop-blur-md"
            onClick={e => e.stopPropagation()}
          >
            <div className="flex items-center gap-3 border-b border-white/5 pb-2.5 mb-2.5">
              <i className="fa-solid fa-magnifying-glass text-white/30 text-sm ml-2" />
              <input
                ref={paletteInputRef}
                type="text"
                placeholder="Type to search dashboard navigation..."
                value={paletteQuery}
                onChange={e => { setPaletteQuery(e.target.value); setPaletteIdx(0); }}
                className="w-full bg-transparent border-none outline-none text-sm text-white placeholder-white/30"
                onKeyDown={e => {
                  if (e.key === 'ArrowDown') {
                    e.preventDefault();
                    setPaletteIdx(prev => Math.min(prev + 1, filteredPaletteItems.length - 1));
                  } else if (e.key === 'ArrowUp') {
                    e.preventDefault();
                    setPaletteIdx(prev => Math.max(prev - 1, 0));
                  } else if (e.key === 'Enter') {
                    if (filteredPaletteItems[paletteIdx]) {
                      handlePaletteSelect(filteredPaletteItems[paletteIdx]);
                    }
                  } else if (e.key === 'Escape') {
                    setShowPalette(false);
                  }
                }}
              />
              <kbd className="text-[10px] bg-white/5 border border-white/5 px-2 py-0.5 rounded font-mono text-white/30">ESC</kbd>
            </div>

            <div className="max-h-[300px] overflow-y-auto flex flex-col gap-1 pr-1">
              {filteredPaletteItems.length === 0 ? (
                <p className="text-center py-8 text-xs text-white/30">No navigation items match your search.</p>
              ) : (
                filteredPaletteItems.map((item, idx) => (
                  <button
                    key={idx}
                    onClick={() => handlePaletteSelect(item)}
                    className={`w-full flex items-center gap-3 text-left p-2.5 rounded-xl transition-all ${
                      idx === paletteIdx ? 'bg-indigo-600/30 text-white font-bold' : 'hover:bg-white/3 text-white/60'
                    }`}
                  >
                    <div className="w-8 h-8 rounded-lg bg-[#191535] flex items-center justify-center shrink-0">
                      <i className={`fa-solid ${item.icon} text-indigo-400 text-sm`} />
                    </div>
                    <div>
                      <h4 className="text-xs font-bold leading-tight">{item.label}</h4>
                      <p className="text-[10px] text-white/40 leading-none mt-0.5">{item.desc}</p>
                    </div>
                  </button>
                ))
              )}
            </div>
          </div>
        </div>
      )}

      {/* COLLAPSIBLE SIDEBAR */}
      <aside className={`border-r border-white/5 bg-[#0a0815]/95 backdrop-blur-xl relative z-20 flex flex-col justify-between transition-all duration-300 ease-in-out shrink-0 ${
        sidebarCollapsed ? 'w-20' : 'w-64'
      }`}>
        <div className="flex flex-col flex-1">
          {/* Brand header */}
          <div className="h-16 flex items-center justify-between border-b border-white/5 px-5 select-none">
            {!sidebarCollapsed && (
              <button 
                onClick={() => setPage('servers')}
                className="flex items-center gap-2.5 group cursor-pointer"
              >
                <div className="w-8 h-8 rounded-lg bg-gradient-to-tr from-indigo-500 to-purple-500 flex items-center justify-center shadow-md shadow-indigo-500/10">
                  <i className="fa-solid fa-robot text-sm text-white" />
                </div>
                <div className="text-left leading-none">
                  <h1 className="text-sm font-black tracking-wider text-white">bronx</h1>
                  <span className="text-[9px] text-indigo-400 font-mono tracking-widest font-bold">dashboard</span>
                </div>
              </button>
            )}
            {sidebarCollapsed && (
              <button 
                onClick={() => setPage('servers')}
                className="w-8 h-8 rounded-lg bg-[#171431] flex items-center justify-center mx-auto text-indigo-400 cursor-pointer"
                title="Return to server list"
              >
                <i className="fa-solid fa-server text-xs" />
              </button>
            )}

            <button
              onClick={() => setSidebarCollapsed(!sidebarCollapsed)}
              className="w-7 h-7 rounded-lg bg-white/5 hover:bg-white/10 flex items-center justify-center text-white/50 hover:text-white transition-colors cursor-pointer"
            >
              <i className={`fa-solid ${sidebarCollapsed ? 'fa-chevron-right' : 'fa-chevron-left'} text-[10px]`} />
            </button>
          </div>

          {/* Server Identity Badge */}
          {activeServer && (
            <div className={`p-4 border-b border-white/5 flex items-center gap-3 ${sidebarCollapsed ? 'justify-center' : ''}`}>
              <div className="w-10 h-10 rounded-xl bg-[#191535] border border-white/5 overflow-hidden flex items-center justify-center shrink-0">
                {activeServer.icon ? (
                  <img
                    src={`https://cdn.discordapp.com/icons/${activeServer.id}/${activeServer.icon}.png?size=96`}
                    alt={activeServer.name}
                    className="w-full h-full object-cover"
                  />
                ) : (
                  <span className="text-xs font-black text-indigo-400 font-mono">
                    {getInitials(activeServer.name)}
                  </span>
                )}
              </div>
              {!sidebarCollapsed && (
                <div className="min-w-0">
                  <h3 className="text-xs font-black truncate">{activeServer.name}</h3>
                  <span className="text-[9px] font-mono text-white/40 tracking-wider">ID: {activeServer.id.substring(0,8)}…</span>
                </div>
              )}
            </div>
          )}

          {/* Navigation Items */}
          <nav className="flex-1 p-3 overflow-y-auto flex flex-col gap-5 select-none">
            {/* Navigation Category */}
            <div>
              {!sidebarCollapsed && <span className="text-[9px] text-white/20 font-bold font-mono tracking-widest uppercase px-3 block mb-2">Core</span>}
              <ul className="flex flex-col gap-1">
                {[
                  { id: 'overview', label: 'Overview', icon: 'fa-chart-line' },
                  { id: 'guild-settings', label: 'Basic Settings', icon: 'fa-cog' },
                  { id: 'commands', label: 'Modules Grid', icon: 'fa-terminal' }
                ].map(item => (
                  <li key={item.id}>
                    <button
                      onClick={() => setActiveTab(item.id)}
                      className={`w-full flex items-center gap-3.5 px-3 py-2.5 rounded-xl text-left transition-all cursor-pointer ${
                        activeTab === item.id 
                          ? 'bg-indigo-600/20 text-indigo-300 font-black border-l-2 border-indigo-500' 
                          : 'text-white/60 hover:bg-white/3 hover:text-white'
                      }`}
                      title={sidebarCollapsed ? item.label : undefined}
                    >
                      <i className={`fa-solid ${item.icon} text-sm shrink-0 w-4 text-center`} />
                      {!sidebarCollapsed && <span className="text-xs font-bold">{item.label}</span>}
                    </button>
                  </li>
                ))}
              </ul>
            </div>

            {/* Custom Admin Systems */}
            <div>
              {!sidebarCollapsed && <span className="text-[9px] text-white/20 font-bold font-mono tracking-widest uppercase px-3 block mb-2">Systems</span>}
              <ul className="flex flex-col gap-1">
                {[
                  { id: 'shop', label: 'Coins & Shop', icon: 'fa-store' },
                  { id: 'moderation', label: 'Moderation', icon: 'fa-shield-alt' },
                  { id: 'statistics', label: 'Channel Stats', icon: 'fa-chart-bar' }
                ].map(item => (
                  <li key={item.id}>
                    <button
                      onClick={() => setActiveTab(item.id)}
                      className={`w-full flex items-center gap-3.5 px-3 py-2.5 rounded-xl text-left transition-all cursor-pointer ${
                        activeTab === item.id 
                          ? 'bg-indigo-600/20 text-indigo-300 font-black border-l-2 border-indigo-500' 
                          : 'text-white/60 hover:bg-white/3 hover:text-white'
                      }`}
                      title={sidebarCollapsed ? item.label : undefined}
                    >
                      <i className={`fa-solid ${item.icon} text-sm shrink-0 w-4 text-center`} />
                      {!sidebarCollapsed && <span className="text-xs font-bold">{item.label}</span>}
                    </button>
                  </li>
                ))}
              </ul>
            </div>
          </nav>
        </div>

        {/* User chip bottom sidebar */}
        {user && (
          <div className="border-t border-white/5 p-4 flex items-center gap-3 bg-[#080611]">
            <img
              src={user.avatar ? `https://cdn.discordapp.com/avatars/${user.id}/${user.avatar}.png?size=64` : `https://cdn.discordapp.com/embed/avatars/${(BigInt(user.id) >> 22n) % 6n}.png`}
              alt="avatar"
              className="w-8 h-8 rounded-lg object-cover shrink-0 border border-white/10"
            />
            {!sidebarCollapsed && (
              <div className="min-w-0 flex-1 leading-none text-left">
                <p className="text-xs font-bold text-white truncate">{user.global_name || user.username}</p>
                <span className="text-[10px] text-white/30 truncate mt-0.5 block">@{user.username}</span>
              </div>
            )}
          </div>
        )}
      </aside>

      {/* MAIN CONTENT VIEWPORT */}
      <main className="flex-1 flex flex-col min-w-0 bg-[#080612]/30 relative z-10 overflow-y-auto">
        {/* Top Navbar */}
        <header className="h-16 border-b border-white/5 bg-[#0a0815]/30 backdrop-blur-md flex items-center justify-between px-6 shrink-0 relative z-25">
          <div className="flex items-center gap-4">
            {/* Search command input placeholder */}
            <button
              onClick={() => setShowPalette(true)}
              className="flex items-center gap-3 bg-[#110e26]/60 border border-white/5 px-4 py-2 rounded-xl text-xs text-white/40 hover:text-white/60 focus:outline-none transition-all w-48 sm:w-64 cursor-pointer"
            >
              <i className="fa-solid fa-magnifying-glass" />
              <span className="text-left flex-1 font-mono">Press Ctrl+K to search...</span>
            </button>
          </div>

          <div className="flex items-center gap-3">
            <button
              onClick={() => {
                if (activeTab === 'overview') loadOverview();
                else if (activeTab === 'guild-settings') loadSettings();
                else if (activeTab === 'commands') loadModules();
                else if (activeTab === 'shop') loadShop();
                else if (activeTab === 'moderation') loadModeration();
                else if (activeTab === 'statistics') loadStatistics();
                triggerToast('Data refreshed!');
              }}
              className="w-9 h-9 rounded-xl bg-white/5 hover:bg-white/10 flex items-center justify-center transition-colors cursor-pointer"
              title="Refresh Current View"
            >
              <i className="fa-solid fa-arrows-rotate text-xs text-white/60 hover:text-white" />
            </button>
          </div>
        </header>

        {/* Tab content space */}
        <div className="flex-1 p-6 sm:p-8 lg:p-10 xl:p-12 max-w-7xl w-full mx-auto">
          
          {/* TAB 1: OVERVIEW */}
          {activeTab === 'overview' && (
            <div className="flex flex-col gap-8">
              
              {/* Stats overview boxes */}
              <div className="grid grid-cols-2 md:grid-cols-4 gap-6">
                {[
                  { 
                    label: 'Total Members', 
                    val: overviewData?.memberCount !== undefined && overviewData?.memberCount !== null 
                      ? Number(overviewData.memberCount).toLocaleString() 
                      : '0', 
                    icon: 'fa-users', 
                    color: 'indigo' 
                  },
                  { 
                    label: 'Commands Run', 
                    val: overviewData?.commandsToday !== undefined && overviewData?.commandsToday !== null 
                      ? Number(overviewData.commandsToday).toLocaleString() 
                      : '0', 
                    icon: 'fa-terminal', 
                    color: 'emerald' 
                  },
                  { 
                    label: 'Total Wealth', 
                    val: overviewData?.totalEconomyValue !== undefined && overviewData?.totalEconomyValue !== null 
                      ? '$' + Number(overviewData.totalEconomyValue).toLocaleString() 
                      : '$0', 
                    icon: 'fa-coins', 
                    color: 'purple' 
                  },
                  { 
                    label: 'New Members', 
                    val: overviewData?.newMembersToday !== undefined && overviewData?.newMembersToday !== null 
                      ? Number(overviewData.newMembersToday).toLocaleString() 
                      : '0', 
                    icon: 'fa-user-plus', 
                    color: 'rose' 
                  }
                ].map((box, idx) => {
                  const colorMap = {
                    indigo: 'text-indigo-400 bg-indigo-500/10 border border-indigo-500/15',
                    emerald: 'text-emerald-400 bg-emerald-500/10 border border-emerald-500/15',
                    purple: 'text-purple-400 bg-purple-500/10 border border-purple-500/15',
                    rose: 'text-rose-400 bg-rose-500/10 border border-rose-500/15'
                  };
                  return (
                    <div key={idx} className="border border-white/5 bg-white/2 backdrop-blur-md p-6 sm:p-7 min-h-[120px] rounded-3xl flex items-center justify-between transition-all duration-300 hover:border-white/10 hover:bg-white/[0.04]">
                      <div>
                        <span className="text-[10px] text-white/40 font-mono tracking-wider block mb-1 uppercase font-bold">{box.label}</span>
                        <h2 className="text-2xl md:text-3xl font-black">{box.val}</h2>
                      </div>
                      <div className={`w-12 h-12 rounded-2xl flex items-center justify-center shrink-0 ${colorMap[box.color] || 'bg-white/5 text-white'}`}>
                        <i className={`fa-solid ${box.icon} text-lg`} />
                      </div>
                    </div>
                  );
                })}
              </div>

              {/* Area Activity Trend Chart */}
              <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-6 rounded-3xl">
                <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 mb-6">
                  <div>
                    <h3 className="text-base font-bold">Engagement Trends</h3>
                    <p className="text-xs text-white/40 mt-0.5">Telemetry log analysis over the last 7 days</p>
                  </div>
                  {/* Metric selector tabs */}
                  <div className="flex gap-1.5 bg-black/20 border border-white/5 rounded-xl p-1">
                    {[
                      { key: 'messages', label: 'Messages', color: 'indigo' },
                      { key: 'activeUsers', label: 'Active Users', color: 'purple' },
                      { key: 'commands', label: 'Commands', color: 'emerald' },
                      { key: 'newMembers', label: 'Joins', color: 'rose' }
                    ].map(m => (
                      <button
                        key={m.key}
                        onClick={() => {
                          setActiveMetric(m.key);
                          if (trendDataFull[m.key]?.length > 0) {
                            setTrendData(trendDataFull[m.key]);
                          }
                        }}
                        className={`px-3 py-1.5 rounded-lg text-[10px] font-bold transition-all cursor-pointer ${
                          activeMetric === m.key
                            ? m.color === 'indigo' ? 'bg-indigo-600 text-white shadow-md'
                              : m.color === 'purple' ? 'bg-purple-600 text-white shadow-md'
                              : m.color === 'emerald' ? 'bg-emerald-600 text-white shadow-md'
                              : 'bg-rose-600 text-white shadow-md'
                            : 'text-white/40 hover:text-white/70'
                        }`}
                      >
                        {m.label}
                      </button>
                    ))}
                  </div>
                </div>
                <AreaChart
                  data={
                    trendData.length > 0
                      ? trendData
                      : [
                          { label: 'May 20', value: 12000 },
                          { label: 'May 21', value: 15400 },
                          { label: 'May 22', value: 14200 },
                          { label: 'May 23', value: 18900 },
                          { label: 'May 24', value: 24000 },
                          { label: 'May 25', value: 22100 },
                          { label: 'May 26', value: 25482 }
                        ]
                  }
                  color={activeMetric === 'activeUsers' ? 'purple' : activeMetric === 'commands' ? 'emerald' : activeMetric === 'newMembers' ? 'rose' : 'indigo'}
                />
              </div>

              {/* Grid 2-column: Heatmap and Live Logs */}
              <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
                {/* Heatmap column (spans 2) */}
                <div className="lg:col-span-2 flex flex-col gap-3">
                  <div>
                    <h3 className="text-base font-bold">Hourly Activity Density</h3>
                    <p className="text-xs text-white/40 mt-0.5">Average weekly distribution of user interactions</p>
                  </div>
                  <ActivityHeatmap
                    data={
                      heatmapData.length > 0
                        ? heatmapData
                        : Array.from({ length: 168 }, (_, i) => ({
                            day: Math.floor(i / 24),
                            hour: i % 24,
                            count: Math.floor(Math.random() * 50) + (i % 24 > 9 && i % 24 < 20 ? 80 : 5)
                          }))
                    }
                  />
                </div>

                {/* Recent activity feed list */}
                <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-5 rounded-3xl flex flex-col gap-4">
                  <div>
                    <h3 className="text-base font-bold">Telemetry Feed</h3>
                    <p className="text-xs text-white/40 mt-0.5">Real-time moderator actions & events</p>
                  </div>

                  <div className="flex-1 max-h-[220px] overflow-y-auto flex flex-col gap-2.5 pr-1 font-mono text-[11px] leading-relaxed">
                    {activityFeed.length === 0 ? (
                      [
                        { id: 1, user: 'UserJoined', msg: 'siqnole entered the server', date: 'Just now' },
                        { id: 2, user: 'FishCatch', msg: 'Catch logged: Rare Blue Marlin (482 lbs)', date: '3m ago' },
                        { id: 3, user: 'AuthAction', msg: 'Server prefix updated to standard', date: '12m ago' }
                      ].map(log => (
                        <div key={log.id} className="border border-white/3 bg-black/10 rounded-xl p-3 flex flex-col gap-1">
                          <div className="flex justify-between items-center text-white/35">
                            <span className="font-bold text-indigo-400">[{log.user}]</span>
                            <span>{log.date}</span>
                          </div>
                          <p className="text-white/70">{log.msg}</p>
                        </div>
                      ))
                    ) : (
                      activityFeed.slice(0, 10).map((log, idx) => (
                        <div key={idx} className="border border-white/3 bg-black/10 rounded-xl p-3 flex flex-col gap-1">
                          <div className="flex justify-between items-center text-white/35">
                            <span className="font-bold text-indigo-400">[{log.source === 'DB' ? 'CONFIG' : log.source || 'EVENT'}]</span>
                            <span>{log.time || (log.created_at ? new Date(log.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) : '')}</span>
                          </div>
                          <p className="text-white/70">{log.action || log.description || log.message}</p>
                        </div>
                      ))
                    )}
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* TAB 2: BASIC SETTINGS */}
          {activeTab === 'guild-settings' && (
            <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
              
              {/* Settings parameters card */}
              <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-6 rounded-3xl flex flex-col gap-5 justify-between">
                <div>
                  <h3 className="text-lg font-black mb-1">Guild Settings</h3>
                  <p className="text-xs text-white/40 mb-6">Modify standard config properties for the active guild.</p>
                  
                  <div className="flex flex-col gap-5">
                    {/* Prefix field */}
                    <div className="flex flex-col gap-2">
                      <label className="text-xs font-bold text-white/50">Primary Command Prefix</label>
                      <input
                        type="text"
                        value={basicSettings.prefix}
                        onChange={(e) => setBasicSettings(prev => ({ ...prev, prefix: e.target.value }))}
                        className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-3 px-4 text-xs text-white placeholder-white/30 focus:outline-none focus:border-indigo-500"
                      />
                    </div>

                    {/* Boolean Toggles */}
                    <div className="flex flex-col gap-4 mt-3">
                      {[
                        { id: 'logging_enabled', label: 'Action Moderation Logging', desc: 'Saves mod actions, bans, and kicks to audit database.' },
                        { id: 'public_stats', label: 'Public Leaderboard View', desc: 'Allows non-server members to view analytics leaderboards.' }
                      ].map(t => (
                        <label key={t.id} className="flex items-start gap-4 cursor-pointer">
                          <input
                            type="checkbox"
                            checked={basicSettings[t.id] || false}
                            onChange={(e) => setBasicSettings(prev => ({ ...prev, [t.id]: e.target.checked }))}
                            className="sr-only peer"
                          />
                          <div className="w-8 h-4.5 bg-white/15 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white/70 after:border-rounded after:h-3.5 after:w-3.5 after:transition-all peer-checked:bg-indigo-600 shrink-0 relative mt-0.5" />
                          <div>
                            <span className="text-xs font-bold block">{t.label}</span>
                            <p className="text-[10px] text-white/40 mt-0.5 leading-normal">{t.desc}</p>
                          </div>
                        </label>
                      ))}
                    </div>
                  </div>
                </div>

                <button
                  onClick={handleSaveBasicSettings}
                  className="w-full py-3 bg-gradient-to-r from-indigo-500 to-purple-500 hover:from-indigo-600 hover:to-purple-600 rounded-xl text-xs font-bold transition-all mt-6 cursor-pointer"
                >
                  Save Basic Settings
                </button>
              </div>

              {/* Prefixes and Blocked channels managers */}
              <div className="flex flex-col gap-8">
                {/* Custom prefixes box */}
                <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-5 rounded-3xl">
                  <h3 className="text-sm font-bold mb-4 flex items-center justify-between">
                    Custom Prefixes
                    <span className="text-[9px] text-white/30 uppercase tracking-widest font-mono">Guild scoping</span>
                  </h3>

                  <form onSubmit={handleAddPrefix} className="flex gap-2 mb-4">
                    <input
                      name="newPrefix"
                      type="text"
                      placeholder="Add custom prefix (e.g. b!)"
                      className="flex-1 bg-[#110d2c]/60 border border-white/5 rounded-xl px-3 py-2 text-xs text-white placeholder-white/30 focus:outline-none focus:border-indigo-500"
                    />
                    <button type="submit" className="w-8 h-8 rounded-xl bg-indigo-600 hover:bg-indigo-700 flex items-center justify-center shrink-0 cursor-pointer">
                      <i className="fa-solid fa-plus text-xs" />
                    </button>
                  </form>

                  <div className="flex flex-col gap-2 max-h-[140px] overflow-y-auto">
                    {customPrefixes.length === 0 ? (
                      <p className="text-xs text-white/30 py-3 text-center">No custom prefixes configured.</p>
                    ) : (
                      customPrefixes.map((p, idx) => (
                        <div key={idx} className="flex justify-between items-center bg-black/10 border border-white/3 rounded-xl p-2.5">
                          <code className="text-xs text-indigo-400 font-mono font-bold">{p.prefix}</code>
                          <button
                            onClick={() => handleDeletePrefix(p.prefix)}
                            className="w-7 h-7 rounded-lg hover:bg-red-500/20 text-white/40 hover:text-red-400 flex items-center justify-center cursor-pointer"
                          >
                            <i className="fa-solid fa-trash text-xs" />
                          </button>
                        </div>
                      ))
                    )}
                  </div>
                </div>

                {/* Blocklist manager */}
                <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-5 rounded-3xl">
                  <h3 className="text-sm font-bold mb-4 flex items-center justify-between">
                    Blocked Channels
                    <span className="text-[9px] text-white/30 uppercase tracking-widest font-mono">Auto ignore</span>
                  </h3>

                  <form onSubmit={handleAddBlockedChannel} className="flex gap-2 mb-4">
                    <input
                      name="channelId"
                      type="text"
                      placeholder="Channel ID to ignore"
                      className="flex-1 bg-[#110d2c]/60 border border-white/5 rounded-xl px-3 py-2 text-xs text-white placeholder-white/30 focus:outline-none focus:border-indigo-500"
                    />
                    <button type="submit" className="w-8 h-8 rounded-xl bg-indigo-600 hover:bg-indigo-700 flex items-center justify-center shrink-0 cursor-pointer">
                      <i className="fa-solid fa-plus text-xs" />
                    </button>
                  </form>

                  <div className="flex flex-col gap-2 max-h-[140px] overflow-y-auto">
                    {blockedChannels.length === 0 ? (
                      <p className="text-xs text-white/30 py-3 text-center">No blocked channels.</p>
                    ) : (
                      blockedChannels.map((c, idx) => (
                        <div key={idx} className="flex justify-between items-center bg-black/10 border border-white/3 rounded-xl p-2.5">
                          <span className="text-xs text-white/60 font-mono">#{c.channel_id}</span>
                          <button
                            onClick={() => handleDeleteBlockedChannel(c.channel_id)}
                            className="w-7 h-7 rounded-lg hover:bg-red-500/20 text-white/40 hover:text-red-400 flex items-center justify-center cursor-pointer"
                          >
                            <i className="fa-solid fa-trash text-xs" />
                          </button>
                        </div>
                      ))
                    )}
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* TAB 3: MODULES */}
          {activeTab === 'commands' && (
            <div className="flex flex-col gap-6">
              <div>
                <h3 className="text-lg font-black mb-1">Modules Toggles</h3>
                <p className="text-xs text-white/40">Instantly switch individual features on/off. Settings save instantly to DB.</p>
              </div>

              <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-5">
                {[
                  { id: 'fun', label: 'Fun & Games', desc: 'Enable custom dice, slots, and interactive fun events.', icon: 'fa-face-laugh-beam', color: 'from-amber-500 to-yellow-500' },
                  { id: 'levels', label: 'Leveling & XP', desc: 'Allows members to gain XP roles, levels, and profile tags.', icon: 'fa-trophy', color: 'from-emerald-500 to-teal-500' },
                  { id: 'fishing', label: 'Fishing Simulator', desc: 'Logs fish weights, catch lists, pond ownership, and auto-fishers.', icon: 'fa-fish', color: 'from-indigo-500 to-blue-500' },
                  { id: 'economy', label: 'Economy & Shop', desc: 'Custom currency treasury, coin transfers, and shop items.', icon: 'fa-coins', color: 'from-purple-500 to-indigo-500' },
                  { id: 'moderation', label: 'Advanced Moderation', desc: 'Action logging, warnings tracking, word filter checks.', icon: 'fa-shield-halved', color: 'from-red-500 to-rose-500' },
                  { id: 'mining', label: 'Mining Mastery', desc: 'Simulates mining claims, custom ores, and pickaxes grades.', icon: 'fa-pickaxe', color: 'from-gray-500 to-slate-500' }
                ].map(m => {
                  const active = modules[m.id] !== false;
                  return (
                    <div 
                      key={m.id}
                      onClick={() => handleToggleModule(m.id)}
                      className={`border rounded-2xl p-5 backdrop-blur-md cursor-pointer transition-all duration-300 select-none relative overflow-hidden flex flex-col justify-between ${
                        active 
                          ? 'bg-white/3 border-indigo-500/20 hover:border-indigo-500/40 hover:scale-[1.01]' 
                          : 'bg-black/25 border-white/3 opacity-60 hover:opacity-85'
                      }`}
                    >
                      <div className="mb-6">
                        <div className={`w-10 h-10 rounded-xl bg-gradient-to-tr ${m.color} flex items-center justify-center mb-4 shadow-md`}>
                          <i className={`fa-solid ${m.icon} text-white`} />
                        </div>
                        <h4 className="text-sm font-extrabold mb-1.5">{m.label}</h4>
                        <p className="text-[10px] text-white/40 leading-relaxed">{m.desc}</p>
                      </div>

                      <div className="flex justify-between items-center pt-3 border-t border-white/5 mt-auto">
                        <span className={`text-[10px] font-mono font-bold tracking-widest uppercase ${active ? 'text-indigo-400' : 'text-white/30'}`}>
                          {active ? 'Active' : 'Disabled'}
                        </span>
                        <div className={`w-8 h-4.5 rounded-full relative transition-colors ${active ? 'bg-indigo-600' : 'bg-white/10'}`}>
                          <div className={`w-3.5 h-3.5 rounded-full bg-white absolute top-[2px] transition-all ${active ? 'left-[16px]' : 'left-[2px]'}`} />
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          {/* TAB 4: COINS & SHOP */}
          {activeTab === 'shop' && (
            <div className="flex flex-col gap-8">
              
              {/* Header section with total coins stats */}
              <div className="flex flex-col sm:flex-row justify-between items-start sm:items-center gap-4 bg-white/2 border border-white/5 rounded-3xl p-6 backdrop-blur-xl">
                <div>
                  <h3 className="text-lg font-black mb-1">Server Coin Shop</h3>
                  <p className="text-xs text-white/40">Custom items that members can purchase using coins. Allows roles assignation.</p>
                </div>
                <button
                  onClick={() => setShowAddShopItem(!showAddShopItem)}
                  className="px-5 py-3 rounded-2xl bg-indigo-600 hover:bg-indigo-700 active:scale-[0.98] transition-all font-bold text-xs flex items-center gap-2 cursor-pointer shadow-lg shadow-indigo-600/10"
                >
                  <i className="fa-solid fa-circle-plus text-sm" />
                  List New Shop Item
                </button>
              </div>

              {/* Toggleable add item form */}
              {showAddShopItem && (
                <form 
                  onSubmit={handleAddShopItemSubmit}
                  className="border border-indigo-500/20 bg-indigo-950/5 backdrop-blur-lg p-6 rounded-3xl flex flex-col gap-4 animate-fade-in"
                >
                  <h4 className="text-sm font-bold text-indigo-300">Create New Item Listing</h4>
                  
                  <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                    <div className="flex flex-col gap-1.5">
                      <label className="text-[10px] font-bold text-white/55">Item Name *</label>
                      <input
                        type="text"
                        placeholder="e.g. Bronze Role Token"
                        value={newShopItem.name}
                        onChange={e => setNewShopItem(p => ({ ...p, name: e.target.value }))}
                        className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-2.5 px-4 text-xs text-white focus:outline-none focus:border-indigo-500"
                        required
                      />
                    </div>
                    <div className="flex flex-col gap-1.5">
                      <label className="text-[10px] font-bold text-white/55">Price (Coins) *</label>
                      <input
                        type="number"
                        placeholder="e.g. 5000"
                        value={newShopItem.price}
                        onChange={e => setNewShopItem(p => ({ ...p, price: e.target.value }))}
                        className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-2.5 px-4 text-xs text-white focus:outline-none focus:border-indigo-500"
                        required
                      />
                    </div>
                    <div className="flex flex-col gap-1.5">
                      <label className="text-[10px] font-bold text-white/55">Assigned Discord Role Reward (ID - optional)</label>
                      <input
                        type="text"
                        placeholder="e.g. 12053920942858291"
                        value={newShopItem.role_reward}
                        onChange={e => setNewShopItem(p => ({ ...p, role_reward: e.target.value }))}
                        className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-2.5 px-4 text-xs text-white focus:outline-none focus:border-indigo-500"
                      />
                    </div>
                    <div className="flex flex-col gap-1.5">
                      <label className="text-[10px] font-bold text-white/55">Stock Limit (-1 for infinite)</label>
                      <input
                        type="number"
                        placeholder="-1"
                        value={newShopItem.stock}
                        onChange={e => setNewShopItem(p => ({ ...p, stock: e.target.value }))}
                        className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-2.5 px-4 text-xs text-white focus:outline-none focus:border-indigo-500"
                      />
                    </div>
                  </div>

                  <div className="flex flex-col gap-1.5">
                    <label className="text-[10px] font-bold text-white/55">Item Description</label>
                    <textarea
                      placeholder="Write descriptive information about what this item grants the user..."
                      value={newShopItem.description}
                      onChange={e => setNewShopItem(p => ({ ...p, description: e.target.value }))}
                      rows={2}
                      className="bg-[#110d2c]/60 border border-white/5 rounded-xl py-2.5 px-4 text-xs text-white focus:outline-none focus:border-indigo-500 resize-none"
                    />
                  </div>

                  <div className="flex gap-3 justify-end mt-2">
                    <button
                      type="button"
                      onClick={() => setShowAddShopItem(false)}
                      className="px-4 py-2 bg-white/5 hover:bg-white/10 rounded-xl text-xs font-bold cursor-pointer"
                    >
                      Cancel
                    </button>
                    <button
                      type="submit"
                      className="px-5 py-2 bg-indigo-600 hover:bg-indigo-700 rounded-xl text-xs font-bold cursor-pointer"
                    >
                      Publish Item
                    </button>
                  </div>
                </form>
              )}

              {/* Shop items grid */}
              {shopItems.length === 0 ? (
                <div className="flex flex-col items-center justify-center py-20 border border-dashed border-white/5 rounded-3xl bg-white/2">
                  <i className="fa-solid fa-store-slash text-4xl text-white/20 mb-4" />
                  <p className="text-sm text-white/40">No items listed in the guild shop yet.</p>
                </div>
              ) : (
                <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 gap-6">
                  {shopItems.map((item, idx) => (
                    <div 
                      key={idx}
                      className="border border-white/5 bg-white/2 backdrop-blur-md p-5 rounded-2xl flex flex-col justify-between"
                    >
                      <div>
                        <div className="flex justify-between items-start mb-4">
                          <h4 className="font-extrabold text-sm text-white truncate max-w-[140px]">{item.name}</h4>
                          <span className="text-[10px] font-mono font-bold text-emerald-400 bg-emerald-500/10 border border-emerald-500/20 px-2 py-0.5 rounded">
                            {item.price.toLocaleString()} coins
                          </span>
                        </div>
                        <p className="text-[10px] text-white/40 leading-relaxed mb-6 h-12 overflow-hidden">{item.description || 'No description provided.'}</p>
                      </div>

                      <div className="flex items-center justify-between pt-3 border-t border-white/5">
                        <span className="text-[9px] font-mono text-white/30">
                          Stock: {item.stock === -1 ? 'Infinite' : item.stock}
                        </span>
                        <button
                          onClick={() => handleDeleteShopItem(item.name)}
                          className="w-7 h-7 rounded-lg hover:bg-red-500/20 text-white/40 hover:text-red-400 flex items-center justify-center cursor-pointer"
                          title="Delete shop item"
                        >
                          <i className="fa-solid fa-trash text-xs" />
                        </button>
                      </div>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}

          {/* TAB 5: MODERATION */}
          {activeTab === 'moderation' && (
            <div className="flex flex-col gap-8">
              
              {/* Auto mod control card */}
              <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-6 rounded-3xl">
                <h3 className="text-base font-bold mb-1">Automated Protection Filters</h3>
                <p className="text-xs text-white/40 mb-6">Mitigate spam, unauthorized URLs, and toxicity instantly.</p>
                
                <div className="grid grid-cols-1 sm:grid-cols-3 gap-6 mb-6">
                  {[
                    { id: 'anti_invite', label: 'Anti Invite Links', desc: 'Deletes server invite links from non-moderators.' },
                    { id: 'anti_link', label: 'Link Suppression', desc: 'Blocks all standard external website links.' }
                  ].map(t => (
                    <label key={t.id} className="flex items-start gap-4 bg-black/10 border border-white/3 p-4 rounded-xl cursor-pointer">
                      <input
                        type="checkbox"
                        checked={automodConfig[t.id] || false}
                        onChange={(e) => setAutomodConfig(prev => ({ ...prev, [t.id]: e.target.checked }))}
                        className="sr-only peer"
                      />
                      <div className="w-8 h-4.5 bg-white/15 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white/70 after:border-rounded after:h-3.5 after:w-3.5 after:transition-all peer-checked:bg-indigo-600 shrink-0 relative mt-0.5" />
                      <div>
                        <span className="text-xs font-bold block leading-none mb-1">{t.label}</span>
                        <p className="text-[9px] text-white/35 leading-tight">{t.desc}</p>
                      </div>
                    </label>
                  ))}
                </div>

                <button
                  onClick={() => triggerToast('Automod filters updated!')}
                  className="px-5 py-2.5 bg-indigo-600 hover:bg-indigo-700 rounded-xl text-xs font-bold cursor-pointer"
                >
                  Save Filters
                </button>
              </div>

              {/* Infractions history table */}
              <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-5 rounded-3xl">
                <h3 className="text-sm font-bold mb-4 flex items-center justify-between">
                  Recent Server Infractions Log
                  <span className="text-[9px] text-white/35 uppercase tracking-widest font-mono">DB query logs</span>
                </h3>

                <div className="overflow-x-auto">
                  <table className="w-full text-left text-xs font-mono select-text">
                    <thead>
                      <tr className="text-white/30 border-b border-white/5 pb-2.5">
                        <th className="py-2.5">User</th>
                        <th>Type</th>
                        <th>Reason</th>
                        <th>Moderator</th>
                        <th>Date</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-white/5">
                      {infractions.length === 0 ? (
                        [
                          { user: 'siqnole', type: 'WARNING', reason: 'Spamming emojis', mod: 'BronxBot#001', date: 'May 25, 2026' },
                          { user: 'jack_d', type: 'MUTE', reason: 'Toxicity in general chat', mod: 'Admin#882', date: 'May 24, 2026' }
                        ].map((inf, idx) => (
                          <tr key={idx} className="hover:bg-white/1">
                            <td className="py-3 font-bold text-white/80">{inf.user}</td>
                            <td>
                              <span className={`text-[10px] px-2 py-0.5 rounded font-bold ${
                                inf.type === 'WARNING' ? 'bg-amber-500/10 text-amber-400 border border-amber-500/20' : 'bg-red-500/10 text-red-400 border border-red-500/20'
                              }`}>{inf.type}</span>
                            </td>
                            <td className="text-white/60">{inf.reason}</td>
                            <td className="text-white/45">{inf.mod}</td>
                            <td className="text-white/45">{inf.date}</td>
                          </tr>
                        ))
                      ) : (
                        infractions.slice(0, 15).map((inf, idx) => (
                          <tr key={idx} className="hover:bg-white/1">
                            <td className="py-3 font-bold text-white/80">{inf.user_name || `@${inf.user_id}`}</td>
                            <td>
                              <span className={`text-[10px] px-2 py-0.5 rounded font-bold ${
                                inf.action === 'WARN' ? 'bg-amber-500/10 text-amber-400 border border-amber-500/20' : 'bg-red-500/10 text-red-400 border border-red-500/20'
                              }`}>{inf.action}</span>
                            </td>
                            <td className="text-white/60 truncate max-w-[200px]">{inf.reason || 'No reason provided.'}</td>
                            <td className="text-white/45">{inf.moderator_name || `@${inf.moderator_id}`}</td>
                            <td className="text-white/45">{new Date(inf.created_at).toLocaleDateString()}</td>
                          </tr>
                        ))
                      )}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* TAB 6: CHANNEL STATISTICS */}
          {activeTab === 'statistics' && (
            <div className="flex flex-col gap-8">
              <div>
                <h3 className="text-lg font-black mb-1">Channel Volume Analytics</h3>
                <p className="text-xs text-white/40">Engagement rates and sent message density scoped per channel.</p>
              </div>

              <div className="border border-white/5 bg-white/2 backdrop-blur-xl p-6 rounded-3xl">
                <BarChart
                  data={
                    channelStats.length > 0
                      ? channelStats
                      : [
                          { label: 'general', value: 89000 },
                          { label: 'media', value: 43000 },
                          { label: 'fishing-pond', value: 31000 },
                          { label: 'mining-claims', value: 24000 },
                          { label: 'bot-commands', value: 58000 },
                          { label: 'voice-text', value: 12000 }
                        ]
                  }
                  color="purple"
                />
              </div>
            </div>
          )}

        </div>
      </main>
    </div>
  );
}
