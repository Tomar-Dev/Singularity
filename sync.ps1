# Singularity Smart Recovery & Sync Script - v1.5

$remote_url = "https://github.com/Tomar-Dev/Singularity.git"

# 1. .git klasörü yoksa tamir et ve GitHub geçmişini çek
if (!(Test-Path ".git")) {
    Write-Host ">>> .git folder is missing! Recovering history from GitHub..." -ForegroundColor Cyan
    git init -q
    git remote add origin $remote_url
    git fetch origin main -q
    # Yerel dalı oluştur ve GitHub geçmişine bağla
    git checkout -B main origin/main -q
    git reset --mixed origin/main -q
    Write-Host ">>> History recovered and branch set to 'main'." -ForegroundColor Green
}

# 2. Yerel dal isminin 'main' olduğundan emin ol (master/main uyuşmazlığı için)
git branch -M main

# 3. Tüm değişiklikleri sepete ekle
git add .

# 4. Değişiklik var mı kontrol et ve tarihli yorumu oluştur
$status = git status --porcelain
if ($status) {
    # Tarih formatını ayarla (Gün.Ay.Yıl)
    $dateStr = Get-Date -Format "dd.MM.yyyy"
    $commitMsg = "Update $dateStr"
    
    git commit -m "$commitMsg" -q
    Write-Host "Changes committed: $commitMsg" -ForegroundColor Gray
} else {
    Write-Host "No changes detected since last sync." -ForegroundColor Gray
}

# 5. GitHub'a gönder (Push)
Write-Host "Updating GitHub..." -ForegroundColor Cyan
$pushError = git push origin main 2>&1

# Eğer geçmiş uyuşmazlığı varsa (yedeğe dönüldüğünde olur), kullanıcıya sor
if ($LASTEXITCODE -ne 0) {
    Write-Host "Normal push failed (History mismatch detected)." -ForegroundColor Red
    $choice = Read-Host "Do you want to FORCE update GitHub with your local files? (y/n)"
    if ($choice -eq 'y') {
        Write-Host "Force pushing to GitHub..." -ForegroundColor Yellow
        git push origin main --force
    } else {
        Write-Host "Push cancelled." -ForegroundColor White
    }
}

Write-Host ">>> Sync process finished!" -ForegroundColor Green
pause