import React, { useMemo, useState } from 'react';
import { PhoneTone, SoundCategory } from '../types/elos';
import { IconPlay } from './Icons';

interface TabNotificacoesProps {
  soundCategory: SoundCategory;
  selectedToneUri: string;
  selectedToneTitle: string;
  tones: PhoneTone[];
  onChangeCategory: (cat: SoundCategory) => void;
  onSelectTone: (uri: string, title: string, preview: boolean) => void;
  onOpenSystemPicker: () => void;
  onTestAlarm: () => void;
}

export const TabNotificacoes: React.FC<TabNotificacoesProps> = ({
  soundCategory,
  selectedToneUri,
  selectedToneTitle,
  tones,
  onChangeCategory,
  onSelectTone,
  onOpenSystemPicker,
  onTestAlarm
}) => {
  const [searchQuery, setSearchQuery] = useState('');

  const filteredTones = useMemo(() => {
    const q = searchQuery.trim().toLowerCase();
    if (!q) return tones;
    return tones.filter((t) => t.title.toLowerCase().includes(q));
  }, [tones, searchQuery]);

  return (
    <div className="tab-view">
      <section className="panel">
        <div className="panel-head">
          <h2>Notificações e Toques do Aparelho</h2>
          <p>
            Configure o alerta que aparecerá fora do aplicativo (com som e vibração) quando o remédio for dispensado.
          </p>
        </div>

        <div className="active-tone-banner">
          <span className="banner-label">Toque selecionado atualmente</span>
          <strong className="banner-value">
            {selectedToneTitle}{' '}
            <span className="banner-tag">
              {soundCategory === 'chamada' ? 'Toque de Chamada' : 'Som de Notificação'}
            </span>
          </strong>
        </div>

        <div className="form-group">
          <label className="form-label">Estilo do alerta sonoro</label>
          <div className="segmented">
            <button
              type="button"
              className={`seg-item ${soundCategory === 'notificacao' ? 'active' : ''}`}
              onClick={() => onChangeCategory('notificacao')}
            >
              Som de Notificação (Mensagem)
            </button>
            <button
              type="button"
              className={`seg-item ${soundCategory === 'chamada' ? 'active' : ''}`}
              onClick={() => onChangeCategory('chamada')}
            >
              Toque Inteiro (Chamada)
            </button>
          </div>
        </div>

        <div className="form-group">
          <label className="form-label" htmlFor="inp_tone_search">
            Pesquisar nos toques pré-instalados do celular ({filteredTones.length})
          </label>
          <input
            id="inp_tone_search"
            type="text"
            className="text-input"
            placeholder="Digite o nome do toque para filtrar..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
          />
        </div>

        <div className="tone-scroll-box">
          {filteredTones.length === 0 ? (
            <div className="empty-box">Nenhum toque encontrado com esse filtro.</div>
          ) : (
            <ul className="tone-list">
              {filteredTones.map((tone, idx) => {
                const isSelected = tone.uri === selectedToneUri;
                return (
                  <li
                    key={`${tone.uri || 'default'}-${idx}`}
                    className={`tone-row ${isSelected ? 'selected' : ''}`}
                  >
                    <button
                      type="button"
                      className="tone-main-btn"
                      onClick={() => onSelectTone(tone.uri, tone.title, true)}
                    >
                      <span className={`radio-dot ${isSelected ? 'checked' : ''}`} />
                      <span className="tone-name">{tone.title}</span>
                    </button>
                    <button
                      type="button"
                      className="tone-preview-btn"
                      onClick={() => onSelectTone(tone.uri, tone.title, true)}
                    >
                      <IconPlay size={12} />
                      <span>Ouvir</span>
                    </button>
                  </li>
                );
              })}
            </ul>
          )}
        </div>

        <div className="grid-2" style={{ marginTop: 14 }}>
          <button
            type="button"
            className="btn btn-secondary"
            onClick={onOpenSystemPicker}
          >
            Seletor do Android
          </button>
          <button
            type="button"
            className="btn btn-primary"
            onClick={onTestAlarm}
          >
            Testar Notificação e Som
          </button>
        </div>
      </section>
    </div>
  );
};

