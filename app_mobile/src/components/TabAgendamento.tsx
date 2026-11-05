import React, { useState } from 'react';
import { ElosDeviceState, ScheduleMode } from '../types/elos';
import { IconTrash } from './Icons';

interface TabAgendamentoProps {
  deviceState: ElosDeviceState;
  elapsedSeconds: number;
  onSaveSchedule: (delay: number, repeat: number, desc: string) => Promise<boolean>;
  onRemoveSchedule: (id: number) => void;
  onClearSchedules: () => void;
  onUpdateStock: (total: number, ocupados: number) => Promise<boolean>;
  onDispenseNow: () => void;
  showToast: (msg: string) => void;
}

export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  const hrs = Math.floor(s / 3600);
  const mins = Math.floor((s % 3600) / 60);
  const secs = s % 60;
  return [hrs, mins, secs].map((n) => String(n).padStart(2, '0')).join(':');
}

export const TabAgendamento: React.FC<TabAgendamentoProps> = ({
  deviceState,
  elapsedSeconds,
  onSaveSchedule,
  onRemoveSchedule,
  onClearSchedules,
  onUpdateStock,
  onDispenseNow,
  showToast
}) => {
  const [mode, setMode] = useState<ScheduleMode>('horario');
  const [medName, setMedName] = useState('');
  const [clockHour, setClockHour] = useState(8);
  const [clockMin, setClockMin] = useState(0);
  const [repeatDaily, setRepeatDaily] = useState(true);
  const [intervalHours, setIntervalHours] = useState(8);
  const [timerHours, setTimerHours] = useState(0);
  const [timerMins, setTimerMins] = useState(15);

  const [inpTotal, setInpTotal] = useState(String(deviceState.total || 7));
  const [inpOcupados, setInpOcupados] = useState('');

  // Calcula próxima dose e lista com tempo restante atualizado
  const schedulesWithRemaining = (deviceState.agendamentos || []).map((item) => ({
    ...item,
    remaining: Math.max(0, item.restante - elapsedSeconds)
  }));

  const nearest = schedulesWithRemaining.reduce<{
    id: number;
    remaining: number;
    desc: string;
  } | null>((acc, cur) => {
    if (!acc || cur.remaining < acc.remaining) {
      return { id: cur.id, remaining: cur.remaining, desc: cur.desc };
    }
    return acc;
  }, null);

  const stockPct =
    deviceState.total > 0
      ? Math.min(100, Math.round((deviceState.ocupados / deviceState.total) * 100))
      : 0;

  const stockColor =
    stockPct <= 15 ? 'var(--danger)' : stockPct <= 40 ? 'var(--warn)' : 'var(--ok)';

  const handleStepClock = (dH: number, dM: number) => {
    setClockHour((h) => (h + dH + 24) % 24);
    setClockMin((m) => (m + dM + 60) % 60);
  };

  const handleStepTimer = (dH: number, dM: number) => {
    setTimerHours((h) => Math.max(0, Math.min(99, h + dH)));
    setTimerMins((m) => {
      const next = (m + dM + 60) % 60;
      return timerHours === 0 && next === 0 ? 1 : next;
    });
  };

  const handleSave = async () => {
    let delay = 0;
    let repeat = 0;
    let timeText = '';

    if (mode === 'horario') {
      const hh = String(clockHour).padStart(2, '0');
      const mm = String(clockMin).padStart(2, '0');
      const now = new Date();
      const target = new Date(
        now.getFullYear(),
        now.getMonth(),
        now.getDate(),
        clockHour,
        clockMin,
        0
      );
      if (target <= now) target.setDate(target.getDate() + 1);
      delay = Math.max(1, Math.floor((target.getTime() - now.getTime()) / 1000));
      if (repeatDaily) {
        repeat = 86400;
        timeText = `Diário às ${hh}:${mm}`;
      } else {
        timeText = `Às ${hh}:${mm}`;
      }
    } else if (mode === 'intervalo') {
      delay = intervalHours * 3600;
      repeat = intervalHours * 3600;
      timeText = `A cada ${intervalHours}h`;
    } else {
      delay = timerHours * 3600 + timerMins * 60;
      if (delay <= 0) {
        showToast('Selecione um tempo maior que zero.');
        return;
      }
      timeText =
        timerHours > 0 ? `Em ${timerHours}h ${timerMins}m` : `Em ${timerMins} min`;
    }

    const cleanName = medName.trim();
    const finalDesc = cleanName ? `${cleanName} (${timeText})` : timeText;
    const ok = await onSaveSchedule(delay, repeat, finalDesc);
    if (ok) {
      setMedName('');
    }
  };

  const handleStockSubmit = async () => {
    const t = parseInt(inpTotal, 10);
    const o = parseInt(inpOcupados, 10);
    if (isNaN(t) || t < 1 || isNaN(o) || o < 0) {
      showToast('Preencha a capacidade total e as doses abastecidas.');
      return;
    }
    const ok = await onUpdateStock(t, o);
    if (ok) {
      setInpOcupados('');
    }
  };

  return (
    <div className="tab-view">
      {/* PAINEL DE RESUMO RÁPIDO */}
      <div className="summary-grid">
        <div className="summary-card">
          <span className="summary-label">Próxima Dose</span>
          <strong className="summary-value mono">
            {nearest ? formatDuration(nearest.remaining) : '--:--:--'}
          </strong>
          <span className="summary-sub">
            {nearest ? nearest.desc : 'Sem horários ativos'}
          </span>
        </div>

        <div className="summary-card">
          <span className="summary-label">Estoque da Caixa</span>
          <strong className="summary-value">
            {deviceState.ocupados} <span className="muted-slash">/ {deviceState.total}</span>
          </strong>
          <div className="progress-track">
            <div
              className="progress-fill"
              style={{ width: `${stockPct}%`, backgroundColor: stockColor }}
            />
          </div>
          <span className="summary-sub">Doses disponíveis</span>
        </div>
      </div>

      {/* CONFIGURADOR DE HORÁRIO */}
      <section className="panel">
        <div className="panel-head">
          <h2>Novo Agendamento</h2>
          <p>Defina o medicamento e o modo de liberação automática</p>
        </div>

        <div className="form-group">
          <label className="form-label" htmlFor="inp_med_name">
            Identificação do medicamento (opcional)
          </label>
          <input
            id="inp_med_name"
            type="text"
            className="text-input"
            maxLength={22}
            placeholder="Ex: Dipirona, Pressão, Vitamina..."
            value={medName}
            onChange={(e) => setMedName(e.target.value)}
          />
        </div>

        <div className="form-group">
          <label className="form-label">Modo de programação</label>
          <div className="segmented">
            <button
              type="button"
              className={`seg-item ${mode === 'horario' ? 'active' : ''}`}
              onClick={() => setMode('horario')}
            >
              Hora Fixa
            </button>
            <button
              type="button"
              className={`seg-item ${mode === 'intervalo' ? 'active' : ''}`}
              onClick={() => setMode('intervalo')}
            >
              Intervalo
            </button>
            <button
              type="button"
              className={`seg-item ${mode === 'cronometro' ? 'active' : ''}`}
              onClick={() => setMode('cronometro')}
            >
              Cronômetro
            </button>
          </div>
        </div>

        {mode === 'horario' && (
          <div className="mode-body">
            <div className="preset-grid">
              {[
                { label: 'Manhã', h: 8, m: 0, time: '08:00' },
                { label: 'Almoço', h: 12, m: 0, time: '12:00' },
                { label: 'Tarde', h: 16, m: 0, time: '16:00' },
                { label: 'Noite', h: 20, m: 0, time: '20:00' }
              ].map((p) => (
                <button
                  key={p.label}
                  type="button"
                  className={`preset-chip ${clockHour === p.h && clockMin === p.m ? 'selected' : ''}`}
                  onClick={() => {
                    setClockHour(p.h);
                    setClockMin(p.m);
                  }}
                >
                  <span>{p.label}</span>
                  <small>{p.time}</small>
                </button>
              ))}
            </div>

            <div className="picker-surface">
              <div className="picker-col">
                <span className="picker-caption">Hora</span>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepClock(1, 0)}
                >
                  + 1h
                </button>
                <div className="picker-digits">
                  {String(clockHour).padStart(2, '0')}
                </div>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepClock(-1, 0)}
                >
                  - 1h
                </button>
              </div>

              <div className="picker-colon">:</div>

              <div className="picker-col">
                <span className="picker-caption">Minuto</span>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepClock(0, 5)}
                >
                  + 5m
                </button>
                <div className="picker-digits">
                  {String(clockMin).padStart(2, '0')}
                </div>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepClock(0, -5)}
                >
                  - 5m
                </button>
              </div>
            </div>

            <label className="switch-row" htmlFor="chk_daily">
              <div className="switch-info">
                <strong>Repetir todos os dias</strong>
                <span>Mantém o horário programado continuamente</span>
              </div>
              <input
                id="chk_daily"
                type="checkbox"
                checked={repeatDaily}
                onChange={(e) => setRepeatDaily(e.target.checked)}
              />
            </label>
          </div>
        )}

        {mode === 'intervalo' && (
          <div className="mode-body">
            <div className="preset-grid">
              {[
                { h: 4, sub: '6 doses/dia' },
                { h: 6, sub: '4 doses/dia' },
                { h: 8, sub: '3 doses/dia' },
                { h: 12, sub: '2 doses/dia' }
              ].map((p) => (
                <button
                  key={p.h}
                  type="button"
                  className={`preset-chip ${intervalHours === p.h ? 'selected' : ''}`}
                  onClick={() => setIntervalHours(p.h)}
                >
                  <span>{p.h} em {p.h}h</span>
                  <small>{p.sub}</small>
                </button>
              ))}
            </div>

            <div className="picker-surface">
              <button
                type="button"
                className="step-btn wide"
                onClick={() => setIntervalHours((h) => Math.max(1, h - 1))}
              >
                - 1h
              </button>
              <div className="picker-center">
                <span className="picker-caption">Repetir a cada</span>
                <div className="picker-digits">{intervalHours}h</div>
              </div>
              <button
                type="button"
                className="step-btn wide"
                onClick={() => setIntervalHours((h) => Math.min(48, h + 1))}
              >
                + 1h
              </button>
            </div>
          </div>
        )}

        {mode === 'cronometro' && (
          <div className="mode-body">
            <div className="preset-grid">
              {[
                { label: '1 min', sub: 'Teste rápido', h: 0, m: 1 },
                { label: '15 min', sub: 'Curto', h: 0, m: 15 },
                { label: '30 min', sub: 'Médio', h: 0, m: 30 },
                { label: '1 hora', sub: 'Longo', h: 1, m: 0 }
              ].map((p) => (
                <button
                  key={p.label}
                  type="button"
                  className={`preset-chip ${timerHours === p.h && timerMins === p.m ? 'selected' : ''}`}
                  onClick={() => {
                    setTimerHours(p.h);
                    setTimerMins(p.m);
                  }}
                >
                  <span>{p.label}</span>
                  <small>{p.sub}</small>
                </button>
              ))}
            </div>

            <div className="picker-surface">
              <div className="picker-col">
                <span className="picker-caption">Horas</span>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepTimer(1, 0)}
                >
                  + 1h
                </button>
                <div className="picker-digits">
                  {String(timerHours).padStart(2, '0')}
                </div>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepTimer(-1, 0)}
                >
                  - 1h
                </button>
              </div>

              <div className="picker-colon">:</div>

              <div className="picker-col">
                <span className="picker-caption">Minutos</span>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepTimer(0, 1)}
                >
                  + 1m
                </button>
                <div className="picker-digits">
                  {String(timerMins).padStart(2, '0')}
                </div>
                <button
                  type="button"
                  className="step-btn"
                  onClick={() => handleStepTimer(0, -1)}
                >
                  - 1m
                </button>
              </div>
            </div>
          </div>
        )}

        <button type="button" className="btn btn-primary full-width" onClick={handleSave}>
          Confirmar e Salvar Agendamento
        </button>
      </section>

      {/* LISTA DE HORÁRIOS PROGRAMADOS */}
      <section className="panel">
        <div className="panel-head row-between">
          <div>
            <h2>Horários Ativos</h2>
            <p>Sincronizados na memória interna da caixa</p>
          </div>
          {schedulesWithRemaining.length > 0 && (
            <button
              type="button"
              className="btn-text-danger"
              onClick={onClearSchedules}
            >
              Limpar todos
            </button>
          )}
        </div>

        {schedulesWithRemaining.length === 0 ? (
          <div className="empty-box">Nenhum agendamento ativo no momento.</div>
        ) : (
          <ul className="schedule-list">
            {schedulesWithRemaining.map((item) => (
              <li key={item.id} className="schedule-row">
                <div className="schedule-meta">
                  <strong>{item.desc}</strong>
                  <span>
                    {item.repete > 0
                      ? `Repete a cada ${formatDuration(item.repete)}`
                      : 'Dose única'}
                  </span>
                </div>
                <div className="schedule-actions">
                  <span className="schedule-countdown">
                    {formatDuration(item.remaining)}
                  </span>
                  <button
                    type="button"
                    className="icon-del-btn"
                    title="Remover horário"
                    onClick={() => onRemoveSchedule(item.id)}
                  >
                    <IconTrash size={16} />
                  </button>
                </div>
              </li>
            ))}
          </ul>
        )}
      </section>

      {/* ABASTECIMENTO E DISPENSAÇÃO MANUAL */}
      <section className="panel">
        <div className="panel-head">
          <h2>Controle de Estoque e Dispensação</h2>
          <p>Atualize a quantidade abastecida ou libere uma dose avulsa</p>
        </div>

        <div className="grid-2">
          <div>
            <label className="form-label" htmlFor="inp_total_stock">
              Capacidade Total
            </label>
            <input
              id="inp_total_stock"
              type="number"
              min={1}
              inputMode="numeric"
              className="text-input"
              value={inpTotal}
              onChange={(e) => setInpTotal(e.target.value)}
            />
          </div>
          <div>
            <label className="form-label" htmlFor="inp_ocup_stock">
              Doses Abastecidas
            </label>
            <input
              id="inp_ocup_stock"
              type="number"
              min={0}
              inputMode="numeric"
              placeholder="Ex: 7"
              className="text-input"
              value={inpOcupados}
              onChange={(e) => setInpOcupados(e.target.value)}
            />
          </div>
        </div>

        <div className="grid-2" style={{ marginTop: 12 }}>
          <button type="button" className="btn btn-secondary" onClick={handleStockSubmit}>
            Atualizar Estoque
          </button>
          <button type="button" className="btn btn-primary" onClick={onDispenseNow}>
            Dispensar 1 Dose
          </button>
        </div>
      </section>
    </div>
  );
};

