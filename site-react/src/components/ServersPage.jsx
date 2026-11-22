import React, { useState, useEffect } from 'react';

export default function ServersPage({ setPage, setGuildId, onSelectServer }) {
  const [loading, setLoading] = useState(true);
  const [authData, setAuthData] = useState({ authenticated: false, user: null, guilds: [], isBotOwner: false });
  const [searchQuery, setSearchQuery] = useState('');
  const [showOnlyInstalled, setShowOnlyInstalled] = useState(false);

  const BOT_CLIENT_ID = '828380019406929962';
  const BOT_INVITE_URL = `https://discord.com/oauth2/authorize?client_id=${BOT_CLIENT_ID}&permissions=8&scope=bot%20applications.commands`;

  useEffect(() => {
    async function fetchAuth() {
      try {
        const res = await fetch('/api/auth/user');
        const data = await res.json();
        
        if (!data.authenticated) {
          setAuthData({ authenticated: false, user: null, guilds: [], isBotOwner: false });
          setLoading(false);
          return;
        }

        let guilds = [];
        if (data.guilds) {
          if (Array.isArray(data.guilds)) {
            guilds = data.guilds.slice();
          } else if (typeof data.guilds === 'object') {
            guilds = Object.values(data.guilds);
          }
        }

        // Fetch public guilds for discovery
        try {
          const pubRes = await fetch('/api/guilds/public');
          const publicGuilds = await pubRes.json();
          
          let publicGuildsArray = [];
          if (publicGuilds) {
            if (Array.isArray(publicGuilds)) {
              publicGuildsArray = publicGuilds;
            } else if (typeof publicGuilds === 'object') {
              publicGuildsArray = Object.values(publicGuilds);
            }
          }
          
          publicGuildsArray.forEach(pub => {
            if (!pub || !pub.id) return;
            const existing = guilds.find(g => g && g.id === pub.id);
            if (!existing) {
              guilds.push({
                ...pub,
                botPresent: true,
                isGlobalDiscovery: true
              });
            } else {
              existing.isGlobalDiscovery = true;
              existing.rating = pub.rating;
              existing.trend = pub.trend;
              existing.trendIcon = pub.trendIcon;
              existing.trendPct = pub.trendPct;
            }
          });
        } catch (e) {
          console.warn('Failed to fetch public guilds:', e);
        }

        // Sort: Bot present first, then rating/activity descending, then name alphabetically
        guilds.sort((a, b) => {
          const aBp = a.botPresent !== false ? 0 : 1;
          const bBp = b.botPresent !== false ? 0 : 1;
          if (aBp !== bBp) return aBp - bBp;
          
          if (a.botPresent !== false && b.botPresent !== false) {
            if ((b.rating || 0) !== (a.rating || 0)) {
              return (b.rating || 0) - (a.rating || 0);
            }
          }
          
          return (a.name || '').localeCompare(b.name || '');
        });

        // Filter invalid guilds
        guilds = guilds.filter(g => g && g.id && g.id !== 'undefined' && g.id !== 'null');

        setAuthData({
          authenticated: true,
          user: data.user,
          guilds,
          isBotOwner: data.isBotOwner
        });
      } catch (err) {
        console.error('Error fetching authentication state:', err);
      } finally {
        setLoading(false);
      }
    }
    fetchAuth();
  }, []);

  const handleLogin = () => {
    window.location.href = '/login';
  };

  const handleLogout = () => {
    window.location.href = '/logout';
  };

  const getInitials = (name = '') => {
    return name
      .split(' ')
      .map(w => w[0])
      .join('')
      .slice(0, 3)
      .toUpperCase();
  };

  const getRoleBadge = (perms = {}) => {
    if (perms.isOwner) {
      return {
        label: 'Owner',
        styles: 'bg-amber-500/10 text-amber-300 border border-amber-500/30 shadow-[0_0_8px_rgba(245,158,11,0.1)]'
      };
    }
    if (perms.canAdmin) {
      return {
        label: 'Admin',
        styles: 'bg-purple-500/10 text-purple-300 border border-purple-500/30 shadow-[0_0_8px_rgba(168,85,247,0.1)]'
      };
    }
    if (perms.canManage) {
      return {
        label: 'Manager',
        styles: 'bg-blue-500/10 text-blue-300 border border-blue-500/30'
      };
    }
    return {
      label: 'Moderator',
      styles: 'bg-slate-500/10 text-slate-300 border border-slate-500/20'
    };
  };

  const formatMetric = (val = 0) => {
    if (val >= 1000000) return `${(val / 1000000).toFixed(1)}M`;
    if (val >= 1000) return `${(val / 1000).toFixed(1)}k`;
    return val;
  };

  const filteredGuilds = authData.guilds.filter(guild => {
    const matchesSearch = guild.name.toLowerCase().includes(searchQuery.toLowerCase());
    const matchesInstalled = !showOnlyInstalled || guild.botPresent !== false;
    return matchesSearch && matchesInstalled;
  });

  if (loading) {
    return (
      <div className="min-h-screen bg-[#070514] text-white flex flex-col items-center justify-center p-6 relative overflow-hidden">
        {/* Decorative backdrop glows */}
        <div className="absolute top-[-20%] left-[-20%] w-[60%] h-[60%] rounded-full bg-purple-900/15 blur-[130px] pointer-events-none" />
        <div className="absolute bottom-[-20%] right-[-20%] w-[60%] h-[60%] rounded-full bg-indigo-900/15 blur-[130px] pointer-events-none" />
        
        <div className="flex flex-col items-center gap-5 relative z-10">
          <div className="w-14 h-14 rounded-full border-t-2 border-r-2 border-indigo-500 border-solid animate-spin shadow-[0_0_15px_rgba(99,102,241,0.2)]" />
          <h1 className="text-2xl font-black tracking-widest text-indigo-200">bronx</h1>
          <p className="text-sm text-white/50 tracking-wider">Verifying credentials and loading guilds...</p>
        </div>
      </div>
    );
  }

  // Beautifully designed premium Login/Sign-In Gate
  if (!authData.authenticated) {
    return (
      <div className="min-h-screen bg-[#070514] text-white flex flex-col items-center justify-center p-6 relative overflow-hidden">
        {/* Animated backdrop grids and premium blurred circles */}
        <div className="absolute top-[-10%] left-[-10%] w-[50%] h-[50%] rounded-full bg-indigo-600/10 blur-[150px] pointer-events-none animate-pulse" />
        <div className="absolute bottom-[-10%] right-[-10%] w-[50%] h-[50%] rounded-full bg-purple-600/10 blur-[150px] pointer-events-none animate-pulse" />
        <div className="absolute inset-0 bg-[linear-gradient(to_right,rgba(255,255,255,0.015)_1px,transparent_1px),linear-gradient(to_bottom,rgba(255,255,255,0.015)_1px,transparent_1px)] bg-[size:4rem_4rem] pointer-events-none" />

        <div className="max-w-lg w-full border border-white/10 bg-white/[0.02] backdrop-blur-3xl p-10 rounded-[32px] text-center shadow-[0_25px_50px_-12px_rgba(0,0,0,0.6)] relative z-10 flex flex-col items-center group transition-all duration-500 hover:border-indigo-500/20">
          
          {/* Neon Gradient Bot Icon */}
          <div className="w-20 h-20 rounded-3xl bg-gradient-to-tr from-indigo-500 via-purple-500 to-pink-500 flex items-center justify-center shadow-lg shadow-indigo-500/20 mb-8 transform group-hover:scale-105 transition-all duration-500">
            <i className="fa-solid fa-robot text-4xl text-white drop-shadow-[0_2px_8px_rgba(0,0,0,0.2)] animate-bounce" />
          </div>
          
          <h1 className="text-4xl font-black tracking-tight mb-3">
            Welcome to <span className="text-transparent bg-clip-text bg-gradient-to-r from-indigo-400 via-purple-400 to-pink-400">bronx</span>
          </h1>
          <p className="text-base text-white/60 mb-8 max-w-sm leading-relaxed">
            Take full control of your Discord community's moderation, dynamic leveling, games, and real-time statistics.
          </p>

          {/* Premium styled feature highlights */}
          <div className="grid grid-cols-2 gap-4 w-full mb-8 text-left">
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 hover:border-white/10 transition-colors">
              <span className="text-xl mb-2 block">🛡️</span>
              <h3 className="text-xs font-bold text-white/95">Security Suite</h3>
              <p className="text-[10px] text-white/45 mt-0.5">Anti-nuke, anti-raid, active spam filters.</p>
            </div>
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 hover:border-white/10 transition-colors">
              <span className="text-xl mb-2 block">🎣</span>
              <h3 className="text-xs font-bold text-white/95">Active Economy</h3>
              <p className="text-[10px] text-white/45 mt-0.5">Fishing, mining, bazaar trade and games.</p>
            </div>
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 hover:border-white/10 transition-colors">
              <span className="text-xl mb-2 block">📈</span>
              <h3 className="text-xs font-bold text-white/95">Visual Analytics</h3>
              <p className="text-[10px] text-white/45 mt-0.5">Real-time statistics & demographic charts.</p>
            </div>
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 hover:border-white/10 transition-colors">
              <span className="text-xl mb-2 block">⚙️</span>
              <h3 className="text-xs font-bold text-white/95">Modular Control</h3>
              <p className="text-[10px] text-white/45 mt-0.5">Enable and customize modules dynamically.</p>
            </div>
          </div>

          {/* Glowing Discord Login Button */}
          <button
            onClick={handleLogin}
            className="px-10 py-4 w-full sm:w-auto rounded-2xl bg-gradient-to-r from-indigo-500 to-[#5865F2] hover:from-indigo-600 hover:to-[#4752c4] active:scale-[0.98] transition-all duration-300 font-extrabold text-white text-sm shadow-[0_8px_24px_rgba(88,101,242,0.35)] hover:shadow-[0_12px_32px_rgba(88,101,242,0.45)] flex items-center justify-center gap-3 cursor-pointer group"
          >
            <i className="fa-brands fa-discord text-xl group-hover:scale-110 transition-transform duration-300" />
            Continue with Discord
          </button>
          
          <span className="text-[10px] text-white/30 mt-8 tracking-widest uppercase font-mono">
            secure connection &bull; oauth2 payload verified
          </span>
        </div>
      </div>
    );
  }

  const { user } = authData;

  return (
    <div className="min-h-screen bg-[#070514] text-white p-6 md:p-12 xl:p-16 relative overflow-hidden flex flex-col">
      {/* Dynamic Background Gradients */}
      <div className="absolute top-[-25%] left-[-25%] w-[80%] h-[80%] rounded-full bg-purple-950/15 blur-[160px] pointer-events-none" />
      <div className="absolute bottom-[-25%] right-[-25%] w-[80%] h-[80%] rounded-full bg-indigo-950/15 blur-[160px] pointer-events-none" />
      <div className="absolute inset-0 bg-[linear-gradient(to_right,rgba(255,255,255,0.012)_1px,transparent_1px),linear-gradient(to_bottom,rgba(255,255,255,0.012)_1px,transparent_1px)] bg-[size:4rem_4rem] pointer-events-none" />

      {/* Styled Modern Header */}
      <header className="relative z-10 flex flex-col md:flex-row items-center justify-between gap-6 mb-16 border-b border-white/5 pb-8 max-w-7xl w-full mx-auto px-4 md:px-8">
        <div className="flex items-center gap-5">
          <div className="w-14 h-14 rounded-2xl bg-gradient-to-tr from-indigo-500 to-purple-500 flex items-center justify-center shadow-lg shadow-indigo-500/10">
            <i className="fa-solid fa-robot text-2xl text-white" />
          </div>
          <div>
            <h1 className="text-4xl font-black tracking-tight bg-clip-text text-transparent bg-gradient-to-r from-white via-white to-indigo-300">bronx</h1>
            <span className="text-xs text-indigo-400 font-mono font-bold tracking-widest uppercase block mt-1">server selection</span>
          </div>
        </div>

        {/* Premium User Card Badge */}
        <div className="flex items-center gap-4 bg-white/[0.03] border border-white/5 rounded-2xl p-3.5 pr-5 backdrop-blur-xl shadow-lg">
          <img
            src={user.avatar ? `https://cdn.discordapp.com/avatars/${user.id}/${user.avatar}.png?size=64` : `https://cdn.discordapp.com/embed/avatars/${(BigInt(user.id) >> 22n) % 6n}.png`}
            alt="user avatar"
            className="w-11 h-11 rounded-xl object-cover shadow border border-white/10"
          />
          <div className="text-left">
            <p className="text-sm font-black leading-tight text-white/90">{user.global_name || user.username}</p>
            <span className="text-[10px] text-white/40 font-mono block mt-0.5">@{user.username}</span>
          </div>
          <div className="w-px h-8 bg-white/10 mx-2" />
          <button
            onClick={handleLogout}
            className="w-10 h-10 rounded-xl bg-white/5 hover:bg-red-500/20 text-white/60 hover:text-red-400 flex items-center justify-center transition-all duration-300 cursor-pointer"
            title="Log out"
          >
            <i className="fa-solid fa-right-from-bracket text-sm" />
          </button>
        </div>
      </header>

      {/* Main Content Area */}
      <main className="relative z-10 flex-1 max-w-7xl w-full mx-auto px-4 md:px-8">
        
        {/* Elegant Controls Panel */}
        <div className="flex flex-col md:flex-row gap-6 justify-between items-center mb-12 bg-white/[0.02] border border-white/5 rounded-3xl p-6 md:py-6 md:px-8 backdrop-blur-2xl shadow-xl">
          {/* Search box with icons */}
          <div className="w-full md:max-w-md relative">
            <i className="fa-solid fa-magnifying-glass absolute left-4 top-1/2 transform -translate-y-1/2 text-white/40 text-base" />
            <input
              type="text"
              placeholder="Search your Discord servers..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full bg-[#110d2c]/60 border border-white/10 rounded-2xl py-3.5 pl-12 pr-4 text-base text-white placeholder-white/30 focus:outline-none focus:border-indigo-500/50 focus:bg-[#110d2c]/85 transition-all duration-300"
            />
          </div>

          {/* Premium Apple-Style Interactive Toggle Switch */}
          <label className="flex items-center gap-4 cursor-pointer self-start md:self-auto group relative select-none">
            <input
              type="checkbox"
              checked={showOnlyInstalled}
              onChange={(e) => setShowOnlyInstalled(e.target.checked)}
              className="sr-only peer"
            />
            {/* Track */}
            <div className="w-12 h-7 bg-white/10 rounded-full peer peer-focus:ring-2 peer-focus:ring-indigo-500/30 peer-checked:bg-gradient-to-r peer-checked:from-indigo-500 peer-checked:to-purple-500 transition-all duration-300 relative after:content-[''] after:absolute after:top-[4px] after:left-[4px] after:bg-white after:rounded-full after:h-[20px] after:w-[20px] after:transition-all after:duration-300 peer-checked:after:translate-x-5 shadow-inner" />
            <span className="text-sm font-bold text-white/60 group-hover:text-white/95 transition-colors">Only show servers with bronx bot</span>
          </label>
        </div>

        {/* Global Admin Portal Shortcut with Glowing Accent */}
        {authData.isBotOwner && (
          <div className="mb-12 border border-purple-500/20 bg-gradient-to-r from-purple-950/20 via-indigo-950/20 to-purple-950/20 backdrop-blur-xl p-8 md:p-10 rounded-3xl flex flex-col md:flex-row justify-between items-start md:items-center gap-6 relative overflow-hidden group min-h-[140px]">
            <div className="absolute inset-0 bg-gradient-to-tr from-purple-500/5 to-indigo-500/5 opacity-0 group-hover:opacity-100 transition-opacity duration-500 pointer-events-none" />
            <div className="relative z-10 flex-1">
              <h2 className="text-xl font-black text-purple-300 flex items-center gap-2.5">
                <i className="fa-solid fa-crown text-purple-400 animate-pulse" />
                Root Owner Portal
              </h2>
              <p className="text-sm text-white/50 mt-2 max-w-2xl leading-relaxed">You are recognized as the bot administrator. Gain full access to global variables, real-time node statistics, live system events, and developer utilities.</p>
            </div>
            <button
              onClick={() => {
                if (onSelectServer) {
                  onSelectServer('global');
                } else {
                  setGuildId('global');
                  setPage('dashboard');
                }
              }}
              className="relative z-10 px-8 py-3.5 bg-gradient-to-r from-purple-500 to-indigo-500 hover:from-purple-600 hover:to-indigo-600 active:scale-[0.98] transition-all rounded-2xl text-xs font-black shadow-lg shadow-purple-500/20 hover:shadow-purple-500/30 cursor-pointer shrink-0"
            >
              Enter Administration
            </button>
          </div>
        )}

        {/* Beautiful Servers Listing Grid with defined card sizes */}
        {filteredGuilds.length === 0 ? (
          <div className="flex flex-col items-center justify-center py-32 border border-dashed border-white/5 rounded-3xl bg-white/[0.01] backdrop-blur-md">
            <div className="w-20 h-20 rounded-full bg-white/5 flex items-center justify-center mb-6 text-white/20">
              <i className="fa-solid fa-folder-open text-3xl" />
            </div>
            <p className="text-base font-bold text-white/40">No matching servers found</p>
            <p className="text-sm text-white/20 mt-2">Make sure you have management permissions in the target guild.</p>
          </div>
        ) : (
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-8 mb-16">
            {filteredGuilds.map((guild) => {
              const botInstalled = guild.botPresent !== false;
              const roleBadgeInfo = getRoleBadge(guild.permissions);
              
              const handleClick = (e) => {
                if (botInstalled) {
                  e.preventDefault();
                  if (onSelectServer) {
                     onSelectServer(guild.id);
                  } else {
                    setGuildId(guild.id);
                    setPage('dashboard');
                  }
                }
              };

              return (
                <a
                  key={guild.id}
                  href={botInstalled ? `/dashboard?server=${guild.id}` : `${BOT_INVITE_URL}&guild_id=${guild.id}&disable_guild_select=true`}
                  target={botInstalled ? '_self' : '_blank'}
                  rel="noreferrer"
                  onClick={handleClick}
                  className={`group relative flex flex-col justify-between border rounded-[26px] p-6 md:p-7 backdrop-blur-xl transition-all duration-300 hover:-translate-y-1.5 cursor-pointer shadow-lg min-h-[240px] ${
                    botInstalled
                      ? 'bg-[#120f2b]/40 border-white/10 hover:bg-[#120f2b]/60 hover:border-indigo-500/40 hover:shadow-[0_15px_30px_rgba(99,102,241,0.08)]'
                      : 'bg-[#0b0a1a]/60 border-white/5 opacity-80 hover:opacity-100 hover:border-white/15 hover:shadow-[0_15px_30px_rgba(0,0,0,0.4)]'
                  }`}
                >
                  {/* Neon glow hover gradient inside card */}
                  {botInstalled && (
                    <div className="absolute inset-0 rounded-[26px] bg-gradient-to-tr from-indigo-500/5 via-transparent to-purple-500/5 opacity-0 group-hover:opacity-100 transition-opacity duration-500 pointer-events-none" />
                  )}

                  <div>
                    {/* Top row: Server Icon and Title */}
                    <div className="flex gap-5 items-center mb-6">
                      {/* Server Icon with Hover Zoom */}
                      <div className="w-16 h-16 rounded-2xl overflow-hidden flex items-center justify-center bg-[#191535]/90 border border-white/15 shrink-0 shadow-lg relative">
                        {guild.icon ? (
                          <img
                            src={`https://cdn.discordapp.com/icons/${guild.id}/${guild.icon}.png?size=128`}
                            alt={guild.name}
                            className="w-full h-full object-cover group-hover:scale-110 transition-transform duration-500"
                          />
                        ) : (
                          <span className="text-base font-black text-indigo-300 font-mono tracking-widest">
                            {getInitials(guild.name)}
                          </span>
                        )}
                      </div>

                      {/* Server Name & Badge */}
                      <div className="flex-1 min-w-0 text-left">
                        <div className="flex items-center gap-2 mb-2">
                          <h3 className="font-extrabold text-base leading-snug text-white group-hover:text-indigo-200 transition-colors truncate" title={guild.name}>
                            {guild.name}
                          </h3>
                          {botInstalled && guild.trendIcon && (
                            <i
                              className={`fa-solid ${guild.trendIcon} shrink-0`}
                              style={{
                                color: guild.trend === 'hot' ? '#ff6b6b' : (guild.trend === 'growing' ? '#10b981' : '#9ca3af'),
                                fontSize: '11px'
                              }}
                              title={`${guild.trendPct}% activity change yesterday`}
                            />
                          )}
                        </div>

                        <span className={`text-[10px] font-black uppercase tracking-widest px-2.5 py-1 rounded-lg inline-block ${roleBadgeInfo.styles}`}>
                          {roleBadgeInfo.label}
                        </span>
                      </div>
                    </div>

                    {/* Middle Row: Server Metrics */}
                    {botInstalled && guild.rating > 0 && (
                      <div className="mb-6 bg-white/[0.02] border border-white/5 rounded-xl p-3.5 flex justify-between items-center text-xs">
                        <span className="text-white/40 flex items-center gap-2">
                          <i className="fa-solid fa-comment-dots text-indigo-400 text-sm" />
                          Activity Score
                        </span>
                        <span className="font-mono font-black text-white/90">
                          {formatMetric(guild.rating)} msgs/day
                        </span>
                      </div>
                    )}
                  </div>

                  {/* Bottom Row: Action / Status Panel */}
                  <div className="flex items-center justify-between mt-auto pt-5 border-t border-white/10">
                    {botInstalled ? (
                      <>
                        <span className="text-xs text-emerald-400 font-extrabold flex items-center gap-2 relative">
                          {/* Pulsing breathing dot */}
                          <span className="w-2.5 h-2.5 rounded-full bg-emerald-400 animate-ping absolute opacity-70" />
                          <span className="w-2.5 h-2.5 rounded-full bg-emerald-400" />
                          Active
                        </span>
                        <div className="text-xs text-white/80 font-black group-hover:text-indigo-300 flex items-center gap-1.5 transition-colors">
                          Configure
                          <i className="fa-solid fa-chevron-right text-[10px] group-hover:translate-x-1 transition-transform" />
                        </div>
                      </>
                    ) : (
                      <>
                        <span className="text-xs text-white/30 flex items-center gap-2">
                          <span className="w-2.5 h-2.5 rounded-full bg-white/20" />
                          Not Installed
                        </span>
                        <div className="text-xs text-indigo-400 font-extrabold group-hover:text-indigo-300 flex items-center gap-1.5 transition-colors">
                          Invite Bot
                          <i className="fa-solid fa-arrow-up-right-from-square text-[10px] translate-y-[0.5px]" />
                        </div>
                      </>
                    )}
                  </div>
                </a>
              );
            })}
          </div>
        )}
      </main>
    </div>
  );
}
