# =============================================================================
# Script Automatizado para Compilar o ELOS_Caixa.apk pelo Terminal do VS Code
# Uso: .\compilar_apk.ps1
# =============================================================================

$ErrorActionPreference = "Continue"

Write-Host "`n[1/4] Sincronizando arquivos web (HTML/CSS/JS) com o projeto Android (Capacitor)..." -ForegroundColor Cyan
Push-Location app_mobile
npx cap sync android
Pop-Location

# Se o Android SDK estiver instalado localmente no PC, compila direto via Gradle local!
$sdkLocal = "$env:LOCALAPPDATA\Android\Sdk"
if ((Test-Path $sdkLocal) -or $env:ANDROID_HOME) {
    Write-Host "`n[2/4] Android SDK local detectado! Compilando APK localmente com Gradle..." -ForegroundColor Green
    Push-Location app_mobile\android
    .\gradlew.bat assembleDebug
    Pop-Location
    Copy-Item -Force "app_mobile\android\app\build\outputs\apk\debug\app-debug.apk" "app_mobile\ELOS_Caixa.apk"
    Write-Host "`n✅ APK compilado com sucesso em: app_mobile\ELOS_Caixa.apk" -ForegroundColor Green
    exit 0
}

Write-Host "`n[2/4] Salvando alteracoes locais e sincronizando com o GitHub..." -ForegroundColor Cyan
# 1. Salva TODAS as alteracoes locais primeiro (evita o erro 'You have unstaged changes')
git add -A
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git commit -m "build: Atualiza codigo do aplicativo para gerar novo APK"
}

# 2. Agora com os arquivos salvos, sincroniza com o GitHub sem conflito
git pull --rebase -X ours origin main
if ($LASTEXITCODE -ne 0) {
    git rebase --abort 2>$null
    git pull --no-edit -X ours origin main
}

# Guarda o hash atual antes do push para saber quando o GitHub Actions terminar de gerar o APK novo
$hashAntes = (git rev-parse HEAD).Trim()

git push origin main
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n❌ Erro ao enviar para o GitHub. Verifique sua conexao." -ForegroundColor Red
    exit 1
}

Write-Host "`n[3/4] Aguardando o GitHub Actions compilar o novo ELOS_Caixa.apk..." -ForegroundColor Yellow
$tempoDecorrido = 0
$compilou = $false

while ($tempoDecorrido -lt 150) {
    Start-Sleep -Seconds 10
    $tempoDecorrido += 10
    git fetch origin main 2>$null
    $hashRemoto = (git rev-parse origin/main).Trim()
    if ($hashRemoto -ne $hashAntes) {
        $compilou = $true
        break
    }
    Write-Host "   Compilando na nuvem... ($tempoDecorrido s)" -ForegroundColor DarkGray
}

Write-Host "`n[4/4] Baixando o novo ELOS_Caixa.apk pronto para a sua pasta..." -ForegroundColor Cyan
git pull --rebase origin main

if ($compilou) {
    Write-Host "`n✅ Pronto! O seu novo instalador ja esta atualizado em: app_mobile\ELOS_Caixa.apk" -ForegroundColor Green
} else {
    Write-Host "`n⚠️ A compilacao ainda esta finalizando no GitHub. Rode 'git pull origin main' em alguns segundos." -ForegroundColor Yellow
}
