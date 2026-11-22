import { useState, useEffect, useRef } from 'react'
import { io } from 'socket.io-client'
import { INVITE_URL, SUPPORT_URL, DASHBOARD_URL, STATUS_URL, FEATURES, COMMANDS, STATS, TAGLINES } from './data'
import TranscriptView from './components/TranscriptView'

const DOCS_URL = 'https://docs.bronx.siqnole.dev'

function Navbar() {
  const [open, setOpen] = useState(false)
  const [scrolled, setScrolled] = useState(false)
  useEffect(() => {
    const h = () => setScrolled(window.scrollY > 20)
    window.addEventListener('scroll', h)
    return () => window.removeEventListener('scroll', h)
  }, [])
  return (
    <nav style={{ position: 'fixed', top: 0, left: 0, right: 0, zIndex: 50, transition: 'background 0.3s', background: scrolled ? 'rgba(13,10,26,0.92)' : 'transparent', backdropFilter: scrolled ? 'blur(12px)' : 'none', borderBottom: scrolled ? '1px solid rgba(168,85,247,0.15)' : 'none' }}>
      <div className="container" style={{ height: 64, display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <a href="/" style={{ fontWeight: 700, fontSize: '1.25rem', color: '#a78bfa', textDecoration: 'none', letterSpacing: '-0.02em' }}>bronx</a>
        <div style={{ display: 'flex', alignItems: 'center', gap: 32 }} className="desktop-nav">
          {[{l:'features',h:'#features'},{l:'commands',h:'#commands'},{l:'status',h:STATUS_URL,x:true},{l:'dashboard',h:DASHBOARD_URL,x:true}].map(i=>(
            <a key={i.l} href={i.h} target={i.x?'_blank':undefined} rel={i.x?'noreferrer':undefined}
              style={{fontSize:'0.875rem',color:'#9d8db8',textDecoration:'none',transition:'color 0.2s'}}
              onMouseEnter={e=>e.target.style.color='#c084fc'} onMouseLeave={e=>e.target.style.color='#9d8db8'}>
              {i.l}
            </a>
          ))}
          <a href={INVITE_URL} target="_blank" rel="noreferrer"
            style={{padding:'8px 20px',borderRadius:8,background:'#7c3aed',color:'white',fontWeight:600,fontSize:'0.875rem',textDecoration:'none',transition:'background 0.2s'}}
            onMouseEnter={e=>e.target.style.background='#9333ea'} onMouseLeave={e=>e.target.style.background='#7c3aed'}>
            invite bronx
          </a>
        </div>
        <button onClick={()=>setOpen(!open)} style={{background:'none',border:'none',color:'#9d8db8',cursor:'pointer',fontSize:'1.25rem',display:'none'}} className="mobile-btn" aria-label="menu">
          <i className={open?'fa-solid fa-xmark':'fa-solid fa-bars'} />
        </button>
      </div>
      {open && (
        <div style={{background:'rgba(19,13,46,0.97)',borderBottom:'1px solid rgba(168,85,247,0.15)',padding:'0 1.5rem 1rem'}}>
          {[{l:'features',h:'#features'},{l:'commands',h:'#commands'},{l:'status',h:STATUS_URL,x:true},{l:'dashboard',h:DASHBOARD_URL,x:true}].map(i=>(
            <a key={i.l} href={i.h} target={i.x?'_blank':undefined} rel={i.x?'noreferrer':undefined} onClick={()=>setOpen(false)}
              style={{display:'block',padding:'12px 0',color:'#9d8db8',textDecoration:'none',fontSize:'0.875rem',borderBottom:'1px solid rgba(168,85,247,0.1)'}}>
              {i.l}
            </a>
          ))}
          <a href={INVITE_URL} target="_blank" rel="noreferrer" onClick={()=>setOpen(false)}
            style={{display:'block',marginTop:12,padding:'10px 20px',borderRadius:8,background:'#7c3aed',color:'white',fontWeight:600,fontSize:'0.875rem',textDecoration:'none',textAlign:'center'}}>
            invite bronx
          </a>
        </div>
      )}
      <style>{`.desktop-nav{display:flex!important}@media(max-width:768px){.desktop-nav{display:none!important}.mobile-btn{display:block!important}}`}</style>
    </nav>
  )
}

function Hero() {
  const [tagline, setTagline] = useState(TAGLINES[0])
  const [fade, setFade] = useState(true)
  const idx = useRef(0)
  useEffect(() => {
    const t = setInterval(() => {
      setFade(false)
      setTimeout(() => { idx.current = (idx.current+1)%TAGLINES.length; setTagline(TAGLINES[idx.current]); setFade(true) }, 300)
    }, 3000)
    return () => clearInterval(t)
  }, [])
  return (
    <section style={{minHeight:'100vh',display:'flex',flexDirection:'column',alignItems:'center',justifyContent:'center',textAlign:'center',padding:'0 1.5rem',position:'relative',overflow:'hidden'}}>
      <div style={{position:'absolute',inset:0,pointerEvents:'none'}}>
        <div style={{position:'absolute',top:'35%',left:'50%',transform:'translate(-50%,-50%)',width:600,height:600,background:'rgba(124,58,237,0.12)',borderRadius:'50%',filter:'blur(120px)'}}/>
        <div style={{position:'absolute',bottom:0,right:0,width:300,height:300,background:'rgba(109,40,217,0.08)',borderRadius:'50%',filter:'blur(80px)'}}/>
      </div>
      <div style={{position:'relative',zIndex:1,maxWidth:640,width:'100%'}}>
        <h1 style={{fontSize:'clamp(4rem,10vw,7rem)',fontWeight:800,color:'white',letterSpacing:'-0.04em',lineHeight:1,marginBottom:16}}>bronx</h1>
        <p style={{color:'#9d8db8',fontSize:'1.125rem',marginBottom:40,transition:'opacity 0.3s',opacity:fade?1:0,minHeight:28}}>{tagline}</p>
        <div style={{display:'flex',gap:16,justifyContent:'center',flexWrap:'wrap'}}>
          <a href={INVITE_URL} target="_blank" rel="noreferrer"
            style={{padding:'14px 32px',borderRadius:12,background:'#7c3aed',color:'white',fontWeight:600,fontSize:'0.95rem',textDecoration:'none',transition:'all 0.2s',boxShadow:'0 8px 32px rgba(124,58,237,0.35)'}}
            onMouseEnter={e=>{e.currentTarget.style.background='#9333ea';e.currentTarget.style.transform='scale(1.04)'}}
            onMouseLeave={e=>{e.currentTarget.style.background='#7c3aed';e.currentTarget.style.transform='scale(1)'}}>
            <i className="fa-solid fa-robot" style={{marginRight:8}}/>invite bronx
          </a>
          <a href={DASHBOARD_URL} target="_blank" rel="noreferrer"
            style={{padding:'14px 32px',borderRadius:12,border:'1px solid rgba(124,58,237,0.4)',color:'#c084fc',fontWeight:600,fontSize:'0.95rem',textDecoration:'none',transition:'all 0.2s'}}
            onMouseEnter={e=>{e.currentTarget.style.background='rgba(124,58,237,0.15)';e.currentTarget.style.transform='scale(1.04)'}}
            onMouseLeave={e=>{e.currentTarget.style.background='transparent';e.currentTarget.style.transform='scale(1)'}}>
            <i className="fa-solid fa-gauge" style={{marginRight:8}}/>dashboard
          </a>
        </div>
      </div>
      <a href="#features" style={{position:'absolute',bottom:32,left:'50%',transform:'translateX(-50%)',color:'#7c3aed',fontSize:'1.25rem',textDecoration:'none',animation:'bounce 2s infinite'}}>
        <i className="fa-solid fa-chevron-down"/>
      </a>
      <style>{`@keyframes bounce{0%,100%{transform:translateX(-50%) translateY(0)}50%{transform:translateX(-50%) translateY(8px)}}`}</style>
    </section>
  )
}

function Features() {
  return (
    <section id="features" style={{padding:'96px 0'}}>
      <div className="container">
        <h2 style={{fontSize:'2rem',fontWeight:700,color:'white',textAlign:'center',marginBottom:12}}>features</h2>
        <p style={{color:'#9d8db8',textAlign:'center',marginBottom:48,fontSize:'0.95rem'}}>everything your server needs in one bot</p>
        <div style={{display:'grid',gridTemplateColumns:'repeat(auto-fill,minmax(220px,1fr))',gap:16}}>
          {FEATURES.map(f=>(
            <div key={f.name} style={{padding:'20px',borderRadius:12,background:'rgba(255,255,255,0.03)',border:'1px solid rgba(124,58,237,0.15)',transition:'all 0.2s',cursor:'default'}}
              onMouseEnter={e=>{e.currentTarget.style.border='1px solid rgba(124,58,237,0.4)';e.currentTarget.style.background='rgba(255,255,255,0.05)'}}
              onMouseLeave={e=>{e.currentTarget.style.border='1px solid rgba(124,58,237,0.15)';e.currentTarget.style.background='rgba(255,255,255,0.03)'}}>
              <i className={f.icon} style={{color:'#a78bfa',fontSize:'1.25rem',marginBottom:12,display:'block'}}/>
              <div style={{color:'white',fontWeight:600,fontSize:'0.9rem',marginBottom:6}}>{f.name}</div>
              <div style={{color:'#9d8db8',fontSize:'0.8rem',lineHeight:1.6}}>{f.desc}</div>
            </div>
          ))}
        </div>
      </div>
    </section>
  )
}

function Stats({ liveStats }) {
  const displayStats = [
    { value: liveStats?.commands?.total ? `${liveStats.commands.total.toLocaleString()}` : '150,000+', label: 'commands' },
    { value: liveStats?.users ? `${liveStats.users.toLocaleString()}` : '15,000+', label: 'active users' },
    { value: 'C++17', label: 'built with' },
    { value: liveStats?.fishing?.total ? `${liveStats.fishing.total.toLocaleString()}` : '50,000+', label: 'fish caught' },
  ]

  return (
    <section style={{padding:'64px 0',background:'rgba(124,58,237,0.04)',borderTop:'1px solid rgba(124,58,237,0.1)',borderBottom:'1px solid rgba(124,58,237,0.1)'}}>
      <div className="container">
        <div style={{display:'grid',gridTemplateColumns:'repeat(auto-fit,minmax(140px,1fr))',gap:24,textAlign:'center'}}>
          {displayStats.map(s=>(
            <div key={s.label}>
              <div style={{fontSize:'2.5rem',fontWeight:800,color:'#a78bfa',letterSpacing:'-0.02em',marginBottom:4}}>{s.value}</div>
              <div style={{fontSize:'0.75rem',color:'#9d8db8',textTransform:'uppercase',letterSpacing:'0.1em'}}>{s.label}</div>
            </div>
          ))}
        </div>
      </div>
    </section>
  )
}

function Commands() {
  const [active, setActive] = useState('economy')
  const [search, setSearch] = useState('')
  const category = COMMANDS.find(c=>c.id===active)
  const filtered = search.trim()
    ? COMMANDS.flatMap(c=>c.commands.map(cmd=>({...cmd,catName:c.name,catIcon:c.icon})))
        .filter(cmd=>cmd.name.toLowerCase().includes(search.toLowerCase())||cmd.desc.toLowerCase().includes(search.toLowerCase())||(cmd.aliases&&cmd.aliases.toLowerCase().includes(search.toLowerCase())))
    : null

  return (
    <section id="commands" style={{padding:'96px 0',background:'rgba(13,10,26,0.6)'}}>
      <div className="container">
        <h2 style={{fontSize:'2rem',fontWeight:700,color:'white',textAlign:'center',marginBottom:8}}>commands</h2>
        <p style={{color:'#9d8db8',textAlign:'center',marginBottom:40,fontSize:'0.95rem'}}>150+ commands across 15 categories</p>

        <div style={{position:'relative',maxWidth:480,margin:'0 auto 40px'}}>
          <i className="fa-solid fa-magnifying-glass" style={{position:'absolute',left:14,top:'50%',transform:'translateY(-50%)',color:'#9d8db8',fontSize:'0.875rem'}}/>
          <input type="text" placeholder="search commands..." value={search} onChange={e=>setSearch(e.target.value)}
            style={{width:'100%',paddingLeft:40,paddingRight:16,paddingTop:11,paddingBottom:11,borderRadius:10,background:'rgba(255,255,255,0.04)',border:'1px solid rgba(124,58,237,0.25)',color:'white',fontSize:'0.875rem',outline:'none',fontFamily:'inherit'}}
            onFocus={e=>e.target.style.border='1px solid rgba(124,58,237,0.55)'}
            onBlur={e=>e.target.style.border='1px solid rgba(124,58,237,0.25)'}/>
        </div>

        {filtered ? (
          <div style={{maxWidth:720,margin:'0 auto',display:'grid',gap:8}}>
            {filtered.length===0
              ? <p style={{textAlign:'center',color:'#9d8db8',padding:'32px 0'}}>no commands found</p>
              : filtered.map((cmd,i)=>(
                <div key={i} style={{display:'flex',gap:12,padding:'14px 16px',borderRadius:10,background:'rgba(255,255,255,0.03)',border:'1px solid rgba(124,58,237,0.12)'}}>
                  <i className={cmd.catIcon} style={{color:'#a78bfa',marginTop:2,flexShrink:0,width:16,textAlign:'center'}}/>
                  <div>
                    <div style={{display:'flex',gap:8,flexWrap:'wrap',alignItems:'center',marginBottom:4}}>
                      <span style={{fontFamily:'monospace',color:'white',fontSize:'0.875rem',fontWeight:600}}>{cmd.name}</span>
                      {cmd.aliases&&<span style={{color:'rgba(167,139,250,0.6)',fontSize:'0.75rem'}}>{cmd.aliases}</span>}
                      <span style={{marginLeft:'auto',color:'rgba(167,139,250,0.4)',fontSize:'0.75rem'}}>{cmd.catName}</span>
                    </div>
                    <p style={{color:'#9d8db8',fontSize:'0.8rem',lineHeight:1.6}}>{cmd.desc}</p>
                  </div>
                </div>
              ))
            }
          </div>
        ) : (
          <div style={{display:'flex',gap:24}}>
            <div style={{width:180,flexShrink:0,display:'flex',flexDirection:'column',gap:2}} className="cmd-sidebar">
              {COMMANDS.map(c=>(
                <button key={c.id} onClick={()=>setActive(c.id)}
                  style={{display:'flex',alignItems:'center',gap:10,padding:'9px 12px',borderRadius:8,border:active===c.id?'1px solid rgba(124,58,237,0.35)':'1px solid transparent',background:active===c.id?'rgba(124,58,237,0.18)':'transparent',color:active===c.id?'white':'#9d8db8',fontSize:'0.8rem',fontFamily:'inherit',cursor:'pointer',textAlign:'left',transition:'all 0.15s',width:'100%'}}>
                  <i className={c.icon} style={{width:14,textAlign:'center',fontSize:'0.85rem',color:active===c.id?'#a78bfa':'#9d8db8'}}/>
                  {c.name}
                </button>
              ))}
            </div>

            {category&&(
              <div style={{flex:1,display:'grid',gap:6,alignContent:'start'}}>
                {category.commands.map((cmd,i)=>(
                  <div key={i} style={{padding:'12px 16px',borderRadius:10,background:'rgba(255,255,255,0.02)',border:'1px solid rgba(124,58,237,0.1)',transition:'all 0.15s'}}
                    onMouseEnter={e=>{e.currentTarget.style.border='1px solid rgba(124,58,237,0.3)';e.currentTarget.style.background='rgba(255,255,255,0.04)'}}
                    onMouseLeave={e=>{e.currentTarget.style.border='1px solid rgba(124,58,237,0.1)';e.currentTarget.style.background='rgba(255,255,255,0.02)'}}>
                    <div style={{display:'flex',gap:8,flexWrap:'wrap',alignItems:'baseline',marginBottom:4}}>
                      <span style={{fontFamily:'monospace',color:'white',fontSize:'0.875rem',fontWeight:600}}>{cmd.name}</span>
                      {cmd.aliases&&<span style={{color:'rgba(167,139,250,0.55)',fontSize:'0.75rem'}}>{cmd.aliases}</span>}
                    </div>
                    <p style={{color:'#9d8db8',fontSize:'0.8rem',lineHeight:1.65}}>{cmd.desc}</p>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>
      <style>{`@media(max-width:640px){.cmd-sidebar{display:none}}`}</style>
    </section>
  )
}

function CTA() {
  return (
    <section style={{padding:'96px 0'}}>
      <div className="container">
        <div style={{textAlign:'center',padding:'64px 32px',borderRadius:20,background:'rgba(124,58,237,0.07)',border:'1px solid rgba(124,58,237,0.2)',position:'relative',overflow:'hidden'}}>
          <div style={{position:'absolute',top:'50%',left:'50%',transform:'translate(-50%,-50%)',width:400,height:400,background:'rgba(124,58,237,0.08)',borderRadius:'50%',filter:'blur(80px)',pointerEvents:'none'}}/>
          <h2 style={{fontSize:'2rem',fontWeight:700,color:'white',marginBottom:12,position:'relative'}}>ready to play?</h2>
          <p style={{color:'#9d8db8',marginBottom:36,position:'relative'}}>add bronx to your server and get started in seconds.</p>
          <div style={{display:'flex',gap:16,justifyContent:'center',flexWrap:'wrap',position:'relative'}}>
            <a href={INVITE_URL} target="_blank" rel="noreferrer"
              style={{padding:'14px 32px',borderRadius:12,background:'#7c3aed',color:'white',fontWeight:600,textDecoration:'none',transition:'all 0.2s',boxShadow:'0 8px 32px rgba(124,58,237,0.3)'}}
              onMouseEnter={e=>{e.currentTarget.style.background='#9333ea';e.currentTarget.style.transform='scale(1.04)'}}
              onMouseLeave={e=>{e.currentTarget.style.background='#7c3aed';e.currentTarget.style.transform='scale(1)'}}>
              <i className="fa-solid fa-robot" style={{marginRight:8}}/>invite bronx
            </a>
            <a href={SUPPORT_URL} target="_blank" rel="noreferrer"
              style={{padding:'14px 32px',borderRadius:12,border:'1px solid rgba(124,58,237,0.35)',color:'#c084fc',fontWeight:600,textDecoration:'none',transition:'all 0.2s'}}
              onMouseEnter={e=>{e.currentTarget.style.background='rgba(124,58,237,0.15)';e.currentTarget.style.transform='scale(1.04)'}}
              onMouseLeave={e=>{e.currentTarget.style.background='transparent';e.currentTarget.style.transform='scale(1)'}}>
              <i className="fa-brands fa-discord" style={{marginRight:8}}/>join support server
            </a>
          </div>
        </div>
      </div>
    </section>
  )
}

function Footer() {
  const col = (links) => (
    <div style={{display:'flex',flexDirection:'column',gap:10}}>
      {links.map(l=>(
        <a key={l.label} href={l.href} target={l.external?'_blank':undefined} rel={l.external?'noreferrer':undefined}
          style={{color:'#9d8db8',fontSize:'0.875rem',textDecoration:'none',transition:'color 0.2s'}}
          onMouseEnter={e=>e.target.style.color='#c084fc'} onMouseLeave={e=>e.target.style.color='#9d8db8'}>
          {l.label}
        </a>
      ))}
    </div>
  )
  return (
    <footer style={{borderTop:'1px solid rgba(124,58,237,0.15)',padding:'64px 0 32px'}}>
      <div className="container">
        <div style={{display:'grid',gridTemplateColumns:'2fr 1fr 1fr',gap:40,marginBottom:48}} className="footer-grid">
          <div>
            <div style={{fontSize:'1.5rem',fontWeight:800,color:'#a78bfa',letterSpacing:'-0.02em',marginBottom:12}}>bronx</div>
            <p style={{color:'#9d8db8',fontSize:'0.875rem',lineHeight:1.7,maxWidth:280}}>elevate your discord server with advanced economy, immersive gambling, and modular utilities. built with c++ for maximum performance.</p>
          </div>
          <div>
            <div style={{color:'white',fontWeight:600,fontSize:'0.875rem',marginBottom:16}}>product</div>
            {col([{label:'home',href:'/'},{label:'guide',href:DOCS_URL+'/guide',external:true},{label:'api docs',href:DOCS_URL,external:true},{label:'status',href:STATUS_URL,external:true}])}
          </div>
          <div>
            <div style={{color:'white',fontWeight:600,fontSize:'0.875rem',marginBottom:16}}>legal & social</div>
            {col([{label:'invite bronx',href:INVITE_URL,external:true},{label:'support server',href:SUPPORT_URL,external:true},{label:'dashboard',href:DASHBOARD_URL,external:true},{label:'privacy policy',href:DOCS_URL+'/privacy',external:true},{label:'terms of service',href:DOCS_URL+'/tos',external:true}])}
          </div>
        </div>
        <div style={{borderTop:'1px solid rgba(124,58,237,0.1)',paddingTop:24,display:'flex',justifyContent:'space-between',flexWrap:'wrap',gap:8,color:'#9d8db8',fontSize:'0.75rem'}}>
          <span>© 2026 bronx. all rights reserved.</span>
          <span>built by <a href="https://github.com/williamcommu" target="_blank" rel="noreferrer" style={{color:'inherit',textDecoration:'none'}} onMouseEnter={e=>e.target.style.color='#c084fc'} onMouseLeave={e=>e.target.style.color='#9d8db8'}>williamcommu</a></span>
        </div>
      </div>
      <style>{`@media(max-width:640px){.footer-grid{grid-template-columns:1fr!important}}`}</style>
    </footer>
  )
}

import ServersPage from './components/ServersPage'
import DashboardPage from './components/DashboardPage'

export default function App() {
  const [liveStats, setLiveStats] = useState(null)
  const [page, setPage] = useState('landing')
  const [selectedGuild, setSelectedGuild] = useState(null)
  const [transcriptId, setTranscriptId] = useState(null)

  // Socket connection for realtime landing stats
  useEffect(() => {
    const socketUrl = import.meta.env.DEV ? 'http://localhost:3000' : window.location.origin;
    const socket = io(socketUrl, {
      transports: ['websocket'],
      autoConnect: true
    })

    socket.on('initial-stats', (data) => {
      if (data?.realtime) {
        setLiveStats(data.realtime)
      }
    })

    socket.on('stats-update', (data) => {
      setLiveStats(data)
    })

    return () => {
      socket.disconnect()
    }
  }, [])

  // PushState Pathname-based Client Router
  useEffect(() => {
    const handleLocationChange = () => {
      const path = window.location.pathname;
      const params = new URLSearchParams(window.location.search);
      const isDashboardSubdomain = false;

      if (path.startsWith('/transcripts/')) {
        const parts = path.split('/');
        const tId = parts[2];
        if (tId) {
          setTranscriptId(tId);
          setPage('transcript');
        } else {
          window.history.replaceState({}, '', '/');
          setPage('landing');
        }
      } else if (path === '/servers') {
        setPage('servers');
      } else if (path === '/dashboard') {
        const serverId = params.get('server');
        if (serverId) {
          setSelectedGuild(serverId);
          setPage('dashboard');
        } else {
          window.history.replaceState({}, '', '/servers');
          setPage('servers');
        }
      } else if (isDashboardSubdomain) {
        // On the dashboard subdomain, always route to server selection / login
        window.history.replaceState({}, '', '/servers');
        setPage('servers');
      } else {
        setPage('landing');
      }
    };

    handleLocationChange();
    window.addEventListener('popstate', handleLocationChange);
    return () => window.removeEventListener('popstate', handleLocationChange);
  }, []);

  const navigateTo = (targetPage, guildId = null) => {
    if (targetPage === 'servers') {
      window.history.pushState({}, '', '/servers');
      setPage('servers');
    } else if (targetPage === 'dashboard') {
      if (guildId) {
        window.history.pushState({}, '', `/dashboard?server=${guildId}`);
        setSelectedGuild(guildId);
        setPage('dashboard');
      } else {
        window.history.pushState({}, '', '/servers');
        setPage('servers');
      }
    } else if (targetPage === 'transcript') {
      if (guildId) {
        window.history.pushState({}, '', `/transcripts/${guildId}`);
        setTranscriptId(guildId);
        setPage('transcript');
      } else {
        window.history.pushState({}, '', '/servers');
        setPage('servers');
      }
    } else {
      window.history.pushState({}, '', '/');
      setPage('landing');
    }
  };

  // Render correct view based on routing state
  if (page === 'servers') {
    return <ServersPage onSelectServer={(gid) => navigateTo('dashboard', gid)} />;
  }

  if (page === 'dashboard' && selectedGuild) {
    return <DashboardPage guildId={selectedGuild} setPage={navigateTo} />;
  }

  if (page === 'transcript' && transcriptId) {
    return <TranscriptView threadId={transcriptId} onBack={() => navigateTo('servers')} />;
  }

  return (
    <div style={{minHeight:'100vh',background:'#0d0a1a',color:'#e2d9f3'}}>
      <Navbar />
      <Hero />
      <Features />
      <Stats liveStats={liveStats} />
      <Commands />
      <CTA />
      <Footer />
    </div>
  )
}

