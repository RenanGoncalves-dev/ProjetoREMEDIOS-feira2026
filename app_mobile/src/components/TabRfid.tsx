import React from 'react';
import { RfidLogEntry } from '../types/elos';

interface TabRfidProps {
  ringOnRfid: boolean;
  onToggleRingOnRfid: (enabled: boolean) => void;
  lastRfidLog: RfidLogEntry | null;
  rfidHistory: RfidLogEntry[];
  onDispenseEmergency: () => void;
}

export const TabRfid: React.FC<TabRfidProps> = ({
  ringOnRfid,
  onToggleRingOnRfid,
  lastRfidLog,
  rfidHistory,
  onDispenseEmergency
}) => {
  return (
    <div className="tab-view">
      <section className="panel">
        <div className="panel-head">
          <h2>Leitor RFID RC522 — Cuidadores e Emergência</h2>
          <p>
            Permite que cuidadores ou familiares liberem instantaneamente uma dose aproximando o cartão ou tag RFID na caixa física, sem depender do celular.
          </p>
        </div>

        <div className="rfid-monitor-box">
          <span className="status-pill">Módulo RC522 Monitorado</span>
          <strong className="rfid-uid-display">
            {lastRfidLog
              ? `Cartão Lido: UID ${lastRfidLog.uid}`
              : 'Nenhuma leitura registrada nesta sessão'}
          </strong>
          <span className="rfid-sub-display">
            {lastRfidLog
              ? `${lastRfidLog.status} às ${lastRfidLog.time}`
              : 'Aproxime o cartão ou chaveiro RFID na caixa para testar.'}
          </span>
        </div>

        <label className="switch-row" htmlFor="chk_rfid_alarm">
          <div className="switch-info">
            <strong>Tocar alarme no celular ao usar Tag RFID</strong>
            <span>
              Por padrão, o alarme toca apenas nos horários programados. Ative se desejar que o celular também toque e vibre quando uma dose for liberada via cartão RFID.
            </span>
          </div>
          <input
            id="chk_rfid_alarm"
            type="checkbox"
            checked={ringOnRfid}
            onChange={(e) => onToggleRingOnRfid(e.target.checked)}
          />
        </label>
      </section>

      <section className="panel">
        <div className="panel-head">
          <h2>Histórico Recente de Leituras RFID</h2>
          <p>Registros recebidos via Bluetooth durante o uso</p>
        </div>

        {rfidHistory.length === 0 ? (
          <div className="empty-box">Nenhum acionamento por Tag RFID registrado ainda.</div>
        ) : (
          <ul className="schedule-list">
            {rfidHistory.map((entry, idx) => (
              <li key={`${entry.uid}-${idx}`} className="schedule-row">
                <div className="schedule-meta">
                  <strong>UID: {entry.uid}</strong>
                  <span>{entry.status}</span>
                </div>
                <span className="schedule-countdown">{entry.time}</span>
              </li>
            ))}
          </ul>
        )}
      </section>

      <section className="panel">
        <div className="panel-head">
          <h2>Liberação Remota de Emergência</h2>
          <p>Caso esteja sem o cartão físico em mãos, acione a liberação imediata via Bluetooth</p>
        </div>
        <button
          type="button"
          className="btn btn-primary full-width"
          onClick={onDispenseEmergency}
        >
          Liberar Dose de Emergência Agora
        </button>
      </section>
    </div>
  );
};

