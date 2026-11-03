# =============================================================================
# Script Automatizado para Compilar o ELOS_Caixa.apk pelo Terminal do VS Code
# Uso: .\compilar_apk.ps1
# =============================================================================

$ErrorActionPreference = "Continue"

# Garante que estamos sempre na pasta raiz do projeto (mesmo se chamado de outra subpasta)
Set-Location $PSScriptRoot

Write-Host "`n[1/4] Preparando versao e sincronizando arquivos (HTML/CSS/JS) com o Android..." -ForegroundColor Cyan
$agora = Get-Date -Format "dd/MM/yyyy HH:mm:ss"
"{ `"compiladoEm`": `"$agora`" }" | Set-Content -Path "app_mobile\www\build-info.json" -Encoding UTF8

Push-Location app_mobile
npx cap sync android
Pop-Location

Write-Host "`n[2/4] Salvando alteracoes locais e enviando para o compilador..." -ForegroundColor Cyan
git add app_mobile/www app_mobile/android app_mobile/package.json app_mobile/capacitor.config.json .github compilar_apk.ps1 README.md
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git commit -m "build: Gera nova versao do APK ($agora)"
}

git pull --rebase -X ours origin main
if ($LASTEXITCODE -ne 0) {
    git rebase --abort 2>$null
    git pull --no-edit -X ours origin main
}

$hashAntes = (git rev-parse HEAD).Trim()

git push origin main
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n❌ Erro ao enviar para o GitHub. Verifique sua conexao." -ForegroundColor Red
    exit 1
}

Write-Host "`n[3/4] Compilando o novo ELOS_Caixa.apk na nuvem (aguarde ~60 a 90 segundos)..." -ForegroundColor Yellow
$tempoDecorrido = 0
$compilou = $false

while ($tempoDecorrido -lt 180) {
    Start-Sleep -Seconds 10
    $tempoDecorrido += 10
    git fetch origin main 2>$null
    $hashRemoto = (git rev-parse origin/main).Trim()
    if ($hashRemoto -ne $hashAntes) {
        $compilou = $true
        break
    }
    Write-Host "   Compilando APK... ($tempoDecorrido s)" -ForegroundColor DarkGray
}

Write-Host "`n[4/4] Baixando o novo ELOS_Caixa.apk pronto para a sua pasta..." -ForegroundColor Cyan
git pull --rebase origin main

$infoApk = Get-Item "app_mobile\ELOS_Caixa.apk" -ErrorAction SilentlyContinue
if ($infoApk) {
    $tamMB = [math]::Round($infoApk.Length / 1MB, 2)
    Write-Host "`n✅ PRONTO! APK gerado e atualizado com sucesso!" -ForegroundColor Green
    Write-Host "   Arquivo : app_mobile\ELOS_Caixa.apk ($tamMB MB)" -ForegroundColor White
    Write-Host "   Data    : $($infoApk.LastWriteTime)" -ForegroundColor White
}
