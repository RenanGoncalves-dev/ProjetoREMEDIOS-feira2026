package com.elos.caixa;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import com.getcapacitor.BridgeActivity;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

public class MainActivity extends BridgeActivity {

    private static final String CHANNEL_ID = "elos_native_high_channel_v2";
    private static final int REQ_POST_NOTIFICATIONS = 5001;
    private static final int REQ_RINGTONE_PICKER = 5002;

    private MediaPlayer activeMediaPlayer = null;
    private Vibrator vibrator = null;
    private PowerManager.WakeLock wakeLock = null;
    private final Handler alarmHandler = new Handler(Looper.getMainLooper());
    private final Map<Integer, Runnable> scheduledRunnables = new HashMap<>();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        criarCanalDeNotificacao();
        solicitarPermissaoNotificacaoAndroid13();
        ativarWakeLockParcial();

        if (getBridge() != null && getBridge().getWebView() != null) {
            WebView webView = getBridge().getWebView();
            webView.getSettings().setMediaPlaybackRequiresUserGesture(false);
            webView.addJavascriptInterface(new ElosNativeBridge(), "ElosNative");
            webView.reload();
        }
    }

    @Override
    public void onPause() {
        super.onPause();
        // Mantém os timers e a escuta Bluetooth ativos mesmo quando o usuário minimiza o aplicativo
        // ou bloqueia a tela do celular, garantindo que a notificação e o toque disparem fora do app!
        try {
            if (getBridge() != null && getBridge().getWebView() != null) {
                getBridge().getWebView().resumeTimers();
            }
        } catch (Exception ignored) {}
    }

    @Override
    public void onDestroy() {
        pararSomEVibracaoInterno();
        if (wakeLock != null && wakeLock.isHeld()) {
            try { wakeLock.release(); } catch (Exception ignored) {}
        }
        super.onDestroy();
    }

    private void ativarWakeLockParcial() {
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null) {
                wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "ELOS::BluetoothBackgroundLock");
                wakeLock.acquire();
            }
        } catch (Exception ignored) {}
    }

    private void solicitarPermissaoNotificacaoAndroid13() {
        if (Build.VERSION.SDK_INT >= 33) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(
                        this,
                        new String[]{Manifest.permission.POST_NOTIFICATIONS},
                        REQ_POST_NOTIFICATIONS
                );
            }
        }
    }

    private void criarCanalDeNotificacao() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "Alertas de Medicamentos E.L.O.S.",
                    NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("Notificações fora do aplicativo quando um remédio é liberado");
            channel.enableVibration(true);
            channel.setVibrationPattern(new long[]{0, 500, 200, 500, 200, 800});
            channel.setLockscreenVisibility(NotificationCompat.VISIBILITY_PUBLIC);

            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null) {
                nm.createNotificationChannel(channel);
            }
        }
    }

    private void tocarToqueDoCelularInterno(String uriStr, boolean modoChamadaLoop) {
        pararSomEVibracaoInterno();

        try {
            Uri soundUri;
            if (uriStr != null && !uriStr.trim().isEmpty()) {
                soundUri = Uri.parse(uriStr);
            } else {
                soundUri = RingtoneManager.getDefaultUri(
                        modoChamadaLoop ? RingtoneManager.TYPE_RINGTONE : RingtoneManager.TYPE_NOTIFICATION
                );
            }

            activeMediaPlayer = new MediaPlayer();
            activeMediaPlayer.setDataSource(getApplicationContext(), soundUri);

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                AudioAttributes attrs = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build();
                activeMediaPlayer.setAudioAttributes(attrs);
            }

            activeMediaPlayer.setLooping(modoChamadaLoop);
            activeMediaPlayer.prepare();
            activeMediaPlayer.start();
        } catch (Exception e) {
            e.printStackTrace();
        }

        // Aciona vibração física do aparelho
        try {
            if (vibrator != null && vibrator.hasVibrator()) {
                long[] pattern = modoChamadaLoop
                        ? new long[]{0, 600, 300, 600, 300, 1000}
                        : new long[]{0, 400, 150, 400};
                int repeatIdx = modoChamadaLoop ? 0 : -1;

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    vibrator.vibrate(VibrationEffect.createWaveform(pattern, repeatIdx));
                } else {
                    vibrator.vibrate(pattern, repeatIdx);
                }
            }
        } catch (Exception ignored) {}
    }

    private void pararSomEVibracaoInterno() {
        try {
            if (activeMediaPlayer != null) {
                if (activeMediaPlayer.isPlaying()) {
                    activeMediaPlayer.stop();
                }
                activeMediaPlayer.release();
                activeMediaPlayer = null;
            }
        } catch (Exception ignored) {
            activeMediaPlayer = null;
        }

        try {
            if (vibrator != null) {
                vibrator.cancel();
            }
        } catch (Exception ignored) {}
    }

    private void mostrarNotificacaoForaDoAppInterno(String titulo, String mensagem, String uriStr, boolean modoChamadaLoop) {
        criarCanalDeNotificacao();

        Intent intent = new Intent(this, MainActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, flags);

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
                .setContentTitle(titulo)
                .setContentText(mensagem)
                .setStyle(new NotificationCompat.BigTextStyle().bigText(mensagem))
                .setPriority(NotificationCompat.PRIORITY_MAX)
                .setCategory(NotificationCompat.CATEGORY_ALARM)
                .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                .setAutoCancel(true)
                .setVibrate(new long[]{0, 500, 200, 500, 200, 800})
                .setContentIntent(pendingIntent);

        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm != null) {
            nm.notify((int) (System.currentTimeMillis() % 100000), builder.build());
        }

        tocarToqueDoCelularInterno(uriStr, modoChamadaLoop);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_RINGTONE_PICKER && resultCode == Activity.RESULT_OK && data != null) {
            Uri uri = data.getParcelableExtra(RingtoneManager.EXTRA_RINGTONE_PICKED_URI);
            if (uri != null && getBridge() != null && getBridge().getWebView() != null) {
                String title = RingtoneManager.getRingtone(this, uri).getTitle(this);
                String safeTitle = title != null ? title.replace("'", "\\'") : "Toque do Sistema";
                String safeUri = uri.toString().replace("'", "\\'");
                String js = "window.onNativeRingtonePicked && window.onNativeRingtonePicked('" + safeUri + "', '" + safeTitle + "');";
                runOnUiThread(() -> getBridge().getWebView().evaluateJavascript(js, null));
            }
        }
    }

    // ========================================================================
    // PONTE JAVASCRIPT <-> ANDROID NATIVO (Acessível via window.ElosNative)
    // ========================================================================
    public class ElosNativeBridge {

        @JavascriptInterface
        public String listarToquesCelular(String categoria) {
            JSONArray lista = new JSONArray();
            try {
                RingtoneManager manager = new RingtoneManager(MainActivity.this);
                if ("chamada".equalsIgnoreCase(categoria)) {
                    manager.setType(RingtoneManager.TYPE_RINGTONE | RingtoneManager.TYPE_ALARM);
                } else {
                    manager.setType(RingtoneManager.TYPE_NOTIFICATION);
                }

                Cursor cursor = manager.getCursor();
                if (cursor != null && cursor.moveToFirst()) {
                    do {
                        String title = cursor.getString(RingtoneManager.TITLE_COLUMN_INDEX);
                        Uri uri = manager.getRingtoneUri(cursor.getPosition());
                        if (uri != null && title != null) {
                            JSONObject item = new JSONObject();
                            item.put("title", title);
                            item.put("uri", uri.toString());
                            lista.put(item);
                        }
                    } while (cursor.moveToNext());
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
            return lista.toString();
        }

        @JavascriptInterface
        public void abrirSeletorDoSistema(String categoria) {
            try {
                Intent intent = new Intent(RingtoneManager.ACTION_RINGTONE_PICKER);
                int type = "chamada".equalsIgnoreCase(categoria)
                        ? (RingtoneManager.TYPE_RINGTONE | RingtoneManager.TYPE_ALARM)
                        : RingtoneManager.TYPE_NOTIFICATION;
                intent.putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, type);
                intent.putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, "Escolha o toque do celular");
                intent.putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_SILENT, false);
                intent.putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true);
                startActivityForResult(intent, REQ_RINGTONE_PICKER);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }

        @JavascriptInterface
        public void testarToque(String uriStr, boolean modoChamadaLoop) {
            runOnUiThread(() -> tocarToqueDoCelularInterno(uriStr, modoChamadaLoop));
        }

        @JavascriptInterface
        public void pararToque() {
            runOnUiThread(() -> pararSomEVibracaoInterno());
        }

        @JavascriptInterface
        public void dispararAlertaNativo(String titulo, String mensagem, String uriStr, boolean modoChamadaLoop) {
            runOnUiThread(() -> mostrarNotificacaoForaDoAppInterno(titulo, mensagem, uriStr, modoChamadaLoop));
        }

        @JavascriptInterface
        public void limparAgendamentosNativos() {
            runOnUiThread(() -> {
                for (Runnable r : scheduledRunnables.values()) {
                    alarmHandler.removeCallbacks(r);
                }
                scheduledRunnables.clear();
            });
        }

        @JavascriptInterface
        public void agendarNotificacaoEmSegundos(int id, int delaySegundos, String titulo, String mensagem, String uriStr, boolean modoChamadaLoop) {
            runOnUiThread(() -> {
                Runnable anterior = scheduledRunnables.remove(id);
                if (anterior != null) {
                    alarmHandler.removeCallbacks(anterior);
                }
                if (delaySegundos <= 0) return;

                Runnable novo = () -> {
                    scheduledRunnables.remove(id);
                    mostrarNotificacaoForaDoAppInterno(titulo, mensagem, uriStr, modoChamadaLoop);
                };
                scheduledRunnables.put(id, novo);
                alarmHandler.postDelayed(novo, delaySegundos * 1000L);
            });
        }
    }
}
