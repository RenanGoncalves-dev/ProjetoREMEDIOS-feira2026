import React from 'react';

interface AlarmModalProps {
  visible: boolean;
  title: string;
  description: string;
  timeText: string;
  onConfirmStop: () => void;
}

export const AlarmModal: React.FC<AlarmModalProps> = ({
  visible,
  title,
  description,
  timeText,
  onConfirmStop
}) => {
  if (!visible) return null;

  return (
    <div className="modal-backdrop" role="alertdialog" aria-modal="true">
      <div className="modal-card">
        <span className="modal-badge">Alerta de Medicamento</span>
        <h2 className="modal-title">{title}</h2>
        <div className="modal-clock">{timeText}</div>
        <p className="modal-sub">O seu medicamento foi liberado na Caixa E.L.O.S.:</p>
        <div className="modal-desc">{description}</div>
        <button
          type="button"
          className="btn btn-success full-width modal-btn"
          onClick={onConfirmStop}
        >
          Confirmar Retirada e Parar Toque
        </button>
      </div>
    </div>
  );
};

