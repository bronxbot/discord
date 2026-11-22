import { useState, useEffect } from 'react'

export default function TranscriptView({ threadId, onBack }) {
  const [data, setData] = useState(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  useEffect(() => {
    setLoading(true)
    fetch(`/api/transcripts/${threadId}`)
      .then(async (res) => {
        if (!res.ok) {
          if (res.status === 401 || res.status === 403) {
            throw new Error('unauthorised')
          }
          throw new Error('failed to load transcript')
        }
        return res.json()
      })
      .then((data) => {
        setData(data)
        setLoading(false)
      })
      .catch((err) => {
        setError(err.message)
        setLoading(false)
      })
  }, [threadId])

  if (loading) {
    return (
      <div style={styles.container}>
        <div style={styles.spinnerContainer}>
          <div className="spinner" />
          <div style={styles.loadingText}>loading transcript...</div>
        </div>
      </div>
    )
  }

  if (error === 'unauthorised') {
    return (
      <div style={styles.container}>
        <div style={styles.card}>
          <h2 style={styles.title}>unauthorised</h2>
          <p style={styles.text}>you must be logged in with a moderator account that has access to this server to view this transcript.</p>
          <a href="/login" style={styles.button}>log in with discord</a>
        </div>
      </div>
    )
  }

  if (error || !data) {
    return (
      <div style={styles.container}>
        <div style={styles.card}>
          <h2 style={styles.title}>transcript not found</h2>
          <p style={styles.text}>{error || 'the requested modmail transcript could not be found or has expired.'}</p>
          <button onClick={onBack} style={styles.button}>go back</button>
        </div>
      </div>
    )
  }

  const closedDate = new Date(data.created_at).toLocaleString('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
    hour12: true
  }).toLowerCase()

  return (
    <div style={styles.wrapper}>
      {/* Header Panel */}
      <div style={styles.header}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
          <button onClick={onBack} style={styles.backButton} aria-label="back">
            <i className="fa-solid fa-arrow-left" />
          </button>
          <div>
            <h1 style={styles.headerTitle}>modmail transcript</h1>
            <div style={styles.metaRow}>
              <span>user: <span style={styles.highlight}>{data.username}</span></span>
              <span style={styles.dot}>•</span>
              <span>thread: <span style={styles.highlight}>{data.thread_id}</span></span>
              <span style={styles.dot}>•</span>
              <span>closed: <span style={styles.highlight}>{closedDate}</span></span>
            </div>
          </div>
        </div>
        <div style={{ fontSize: '0.85rem', color: '#9d8db8' }}>
          closed by: <span style={styles.highlight}><i className="fa-solid fa-user-shield" style={{ marginRight: 6 }} />{data.closed_by}</span>
        </div>
      </div>

      {/* Message List */}
      <div style={styles.chatArea}>
        {data.messages && data.messages.length > 0 ? (
          data.messages.map((msg, i) => {
            const timestampStr = new Date(msg.timestamp).toLocaleString('en-US', {
              hour: 'numeric',
              minute: '2-digit',
              hour12: true
            }).toLowerCase();

            return (
              <div key={msg.id || i} style={styles.messageRow}>
                <img 
                  src={msg.avatar_url || 'https://cdn.discordapp.com/embed/avatars/0.png'} 
                  alt={msg.username} 
                  style={styles.avatar}
                  onError={(e) => { e.target.src = 'https://cdn.discordapp.com/embed/avatars/0.png' }}
                />
                <div style={styles.messageContent}>
                  <div style={styles.messageHeader}>
                    <span style={styles.username}>{msg.username.toLowerCase()}</span>
                    <span style={styles.timestamp}>{timestampStr}</span>
                  </div>
                  {msg.content && (
                    <div style={styles.textBody}>{msg.content}</div>
                  )}

                  {/* Render Embeds */}
                  {msg.embeds && msg.embeds.map((embed, idx) => {
                    const embedColor = embed.color ? `#${embed.color.toString(16).padStart(6, '0')}` : '#7c3aed';
                    return (
                      <div key={idx} style={{ ...styles.embedContainer, borderLeftColor: embedColor }}>
                        {embed.title && <div style={styles.embedTitle}>{embed.title}</div>}
                        {embed.description && <div style={styles.embedDesc}>{embed.description}</div>}
                        {embed.fields && embed.fields.length > 0 && (
                          <div style={styles.embedFieldsGrid}>
                            {embed.fields.map((f, fIdx) => (
                              <div key={fIdx} style={{
                                gridColumn: f.inline ? 'span 1' : '1 / -1',
                                minWidth: '150px'
                              }}>
                                <div style={styles.fieldLabel}>{f.name}</div>
                                <div style={styles.fieldValue}>{f.value}</div>
                              </div>
                            ))}
                          </div>
                        )}
                      </div>
                    )
                  })}

                  {/* Render Attachments */}
                  {msg.attachments && msg.attachments.map((att, idx) => {
                    const isImage = /\.(jpg|jpeg|png|webp|gif)$/i.test(att.filename);
                    if (isImage) {
                      return (
                        <div key={idx} style={styles.imageAttachmentContainer}>
                          <img 
                            src={att.url} 
                            alt={att.filename} 
                            style={styles.imageAttachment}
                          />
                        </div>
                      )
                    }
                    return (
                      <a 
                        key={idx} 
                        href={att.url} 
                        target="_blank" 
                        rel="noreferrer" 
                        style={styles.attachmentCard}
                      >
                        <i className="fa-solid fa-file" style={{ color: '#a78bfa', fontSize: '1.25rem' }} />
                        <div style={{ flex: 1, minWidth: 0 }}>
                          <div style={styles.attachmentName}>{att.filename}</div>
                          <div style={styles.attachmentSize}>{(att.size / 1024).toFixed(1)} kb</div>
                        </div>
                        <i className="fa-solid fa-download" style={{ color: '#9d8db8' }} />
                      </a>
                    )
                  })}
                </div>
              </div>
            )
          })
        ) : (
          <div style={styles.emptyChat}>no messages recorded in this transcript.</div>
        )}
      </div>
      
      <style>{`
        .spinner {
          width: 40px;
          height: 40px;
          border: 3px solid rgba(168, 85, 247, 0.1);
          border-top-color: #a78bfa;
          border-radius: 50%;
          animation: spin 1s infinite linear;
        }
        @keyframes spin {
          0% { transform: rotate(0deg); }
          100% { transform: rotate(360deg); }
        }
      `}</style>
    </div>
  )
}

const styles = {
  wrapper: {
    minHeight: '100vh',
    background: '#0d0a1a',
    color: '#e2d9f3',
    display: 'flex',
    flexDirection: 'column',
    fontFamily: 'system-ui, -apple-system, sans-serif'
  },
  container: {
    minHeight: '100vh',
    background: '#0d0a1a',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    padding: '24px'
  },
  spinnerContainer: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: 16
  },
  loadingText: {
    fontSize: '0.9rem',
    color: '#9d8db8',
    letterSpacing: '0.05em'
  },
  card: {
    background: 'rgba(255, 255, 255, 0.02)',
    border: '1px solid rgba(168, 85, 247, 0.15)',
    borderRadius: 16,
    padding: '32px 24px',
    maxWidth: 400,
    width: '100%',
    textAlign: 'center',
    backdropFilter: 'blur(8px)'
  },
  title: {
    fontSize: '1.25rem',
    fontWeight: 700,
    color: '#e2d9f3',
    marginBottom: 12,
    letterSpacing: '-0.02em'
  },
  text: {
    fontSize: '0.9rem',
    color: '#9d8db8',
    lineHeight: 1.6,
    marginBottom: 24
  },
  button: {
    display: 'inline-block',
    padding: '12px 24px',
    background: '#7c3aed',
    color: 'white',
    border: 'none',
    borderRadius: 8,
    fontWeight: 600,
    fontSize: '0.875rem',
    textDecoration: 'none',
    cursor: 'pointer',
    transition: 'background 0.2s'
  },
  header: {
    padding: '24px 32px',
    borderBottom: '1px solid rgba(168, 85, 247, 0.12)',
    background: 'rgba(255, 255, 255, 0.01)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    flexWrap: 'wrap',
    gap: 16
  },
  backButton: {
    background: 'none',
    border: 'none',
    color: '#9d8db8',
    cursor: 'pointer',
    fontSize: '1.125rem',
    padding: 8,
    borderRadius: 8,
    transition: 'all 0.2s',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  },
  headerTitle: {
    fontSize: '1.1rem',
    fontWeight: 700,
    color: 'white',
    margin: 0,
    letterSpacing: '-0.01em',
    textTransform: 'lowercase'
  },
  metaRow: {
    display: 'flex',
    alignItems: 'center',
    gap: 8,
    fontSize: '0.8rem',
    color: '#9d8db8',
    marginTop: 4,
    flexWrap: 'wrap'
  },
  highlight: {
    color: '#c084fc',
    fontWeight: 500
  },
  dot: {
    color: 'rgba(168, 85, 247, 0.3)'
  },
  chatArea: {
    flex: 1,
    padding: '32px 40px',
    maxWidth: 900,
    width: '100%',
    margin: '0 auto',
    display: 'flex',
    flexDirection: 'column',
    gap: 28
  },
  messageRow: {
    display: 'flex',
    gap: 16,
    alignItems: 'flex-start'
  },
  avatar: {
    width: 40,
    height: 40,
    borderRadius: '50%',
    objectFit: 'cover',
    background: '#1a162e',
    border: '1px solid rgba(168, 85, 247, 0.2)'
  },
  messageContent: {
    flex: 1,
    minWidth: 0,
    display: 'flex',
    flexDirection: 'column',
    gap: 6
  },
  messageHeader: {
    display: 'flex',
    alignItems: 'baseline',
    gap: 8
  },
  username: {
    fontSize: '0.9rem',
    fontWeight: 600,
    color: 'white'
  },
  timestamp: {
    fontSize: '0.75rem',
    color: '#6c5d88'
  },
  textBody: {
    fontSize: '0.9rem',
    color: '#e2d9f3',
    lineHeight: 1.6,
    wordBreak: 'break-word',
    whiteSpace: 'pre-wrap'
  },
  embedContainer: {
    background: 'rgba(124, 58, 237, 0.03)',
    borderLeft: '4px solid #7c3aed',
    borderRadius: '0 8px 8px 0',
    padding: '12px 16px',
    marginTop: 6,
    maxWidth: 520,
    display: 'flex',
    flexDirection: 'column',
    gap: 8,
    border: '1px solid rgba(168, 85, 247, 0.08)',
    borderLeftWidth: 4
  },
  embedTitle: {
    fontSize: '0.9rem',
    fontWeight: 600,
    color: 'white'
  },
  embedDesc: {
    fontSize: '0.85rem',
    color: '#9d8db8',
    lineHeight: 1.5,
    whiteSpace: 'pre-wrap'
  },
  embedFieldsGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(auto-fill, minmax(180px, 1fr))',
    gap: 12,
    marginTop: 8
  },
  fieldLabel: {
    fontSize: '0.75rem',
    fontWeight: 600,
    color: '#9d8db8',
    textTransform: 'lowercase',
    marginBottom: 2
  },
  fieldValue: {
    fontSize: '0.8rem',
    color: '#e2d9f3',
    lineHeight: 1.4
  },
  imageAttachmentContainer: {
    marginTop: 6,
    borderRadius: 8,
    overflow: 'hidden',
    maxWidth: 400,
    maxHeight: 300,
    border: '1px solid rgba(168, 85, 247, 0.15)'
  },
  imageAttachment: {
    width: '100%',
    height: '100%',
    objectFit: 'contain'
  },
  attachmentCard: {
    display: 'flex',
    alignItems: 'center',
    gap: 12,
    padding: '12px 16px',
    background: 'rgba(255, 255, 255, 0.02)',
    border: '1px solid rgba(168, 85, 247, 0.15)',
    borderRadius: 8,
    maxWidth: 400,
    marginTop: 6,
    textDecoration: 'none',
    cursor: 'pointer',
    transition: 'all 0.2s'
  },
  attachmentName: {
    fontSize: '0.85rem',
    fontWeight: 600,
    color: 'white',
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    textOverflow: 'ellipsis'
  },
  attachmentSize: {
    fontSize: '0.75rem',
    color: '#9d8db8',
    marginTop: 2
  },
  emptyChat: {
    textAlign: 'center',
    padding: '48px 0',
    color: '#9d8db8',
    fontSize: '0.9rem'
  }
}
